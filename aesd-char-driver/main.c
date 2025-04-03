/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h> // kmalloc

#include "aesdchar.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Florian Depraz");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * Return the content (or partial content) related to the most recent 10 write commands,
     * in the order they were received, on any read attempt.
     *
     * 1. You should use the position specified in the read to determine the location and number of bytes to return.
     * 2. You should honor the count argument by sending only up to the first “count” bytes back of
     *  the available bytes remaining.
     * Perform appropriate locking to ensure safe multi-thread and multi-process access and ensure a full
     *  write file operation from a thread completes before accepting a new write file operation.
     */

    // lock mutex
    PDEBUG("Lock read");
    retval = mutex_lock_interruptible(&aesd_device.mutex);

    if (retval == 0)
    {


        size_t entry_offset_byte_rtn = 0;
        struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &aesd_device.buffer, *f_pos, &entry_offset_byte_rtn);

        if (entry == NULL)
        {
            PDEBUG("No entry found");
            retval = -ENODATA;
            goto end;
        }

        size_t j = 0;
        for (size_t i = 0; i < count; i++)
        {
            if ((entry_offset_byte_rtn + j) >= entry->size)
            {
                PDEBUG("End of entry %d %d/%d %d", i, j, entry->size, retval);
                // Get the next one
                entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                    &aesd_device.buffer, *f_pos + i, &entry_offset_byte_rtn);
                if (entry == NULL)
                {
                    PDEBUG("No new entry found, exiting");
                    break;
                }

                // There is an entry, but does it work ?
                j = 0;
            }

            if (copy_to_user(buf + i, entry->buffptr + entry_offset_byte_rtn + j, 1))
            {
                PDEBUG("Error copying to user");
                retval = -EFAULT;
                goto end;
            }

            j++;
            retval++;
        }
        *f_pos = retval;


    }


end:

    if (retval != -EINTR)
    {
        PDEBUG("Unlock read");
        mutex_unlock(&aesd_device.mutex);
    }
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    static bool start = true;
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    PDEBUG("Lock write");
    retval = mutex_lock_interruptible(&aesd_device.mutex);

    if (retval == 0)
    {
        if (start)
        {
            start = false;
            PDEBUG("First write");

            aesd_device.allocatedBuffer = kmalloc(AESD_CIRCULAR_BUFFER_SIZE, GFP_KERNEL);
            if(aesd_device.allocatedBuffer == NULL){
                PDEBUG("Error allocating memory");
                retval = -ENOMEM;
                goto end;
            }

            aesd_device.current_entry_c.buffptr = aesd_device.allocatedBuffer;
            aesd_device.current_entry_c.size = 0;
        }

        PDEBUG("Copy to %p %d", aesd_device.allocatedBuffer, count);

        if(copy_from_user(aesd_device.allocatedBuffer + aesd_device.current_entry_c.size, buf, count)){
            PDEBUG("Error copying from user");
            retval = -EFAULT;
            goto error;
        }

        if (aesd_device.current_entry_c.size + count > AESD_CIRCULAR_BUFFER_SIZE)
        {
            PDEBUG("Buffer full");
            goto error;
        }
        aesd_device.current_entry_c.size += count;
        retval = count;

        if (buf[count-1] == '\n')
        {
            PDEBUG("End of line");
            void* bufferToFree = aesd_circular_buffer_add_entry(&aesd_device.buffer, &aesd_device.current_entry_c);
            if (bufferToFree != NULL)
            {
                kfree(bufferToFree);
            }

            start = true;
        }




        goto end;

    }


error:
    kfree(aesd_device.allocatedBuffer);
    start = true;

end:
    if (retval != -EINTR)
    {
        PDEBUG("Unlock write");
        mutex_unlock(&aesd_device.mutex);
    }

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    // init mutex
    mutex_init(&aesd_device.mutex);

    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.buffer,index) {
        kfree(entry->buffptr);
    }

    // Cleanup mutex
    mutex_destroy(&aesd_device.mutex);

    aesd_device.buffer.in_offs = 0;
    aesd_device.buffer.out_offs = 0;
    aesd_device.buffer.full = false;

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
