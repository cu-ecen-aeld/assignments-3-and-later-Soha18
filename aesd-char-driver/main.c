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
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("abhirathkoushik-cub");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    filp->f_pos = 0;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t entry_offset = 0;
    struct aesd_buffer_entry *entry;
    size_t bytes_read = 0;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    /* Lock device to protect our data */
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &entry_offset);
    if (!entry || entry->buffptr == NULL) 
    {
        retval = 0;
        goto mutex_unlock;
    }
    
    size_t available = entry->size - entry_offset;
    if (available > count)
        available = count;
    

    size_t not_copied = copy_to_user(buf, entry->buffptr + entry_offset, available);

    if (not_copied)
    {
        retval = -EFAULT;
        goto mutex_unlock;
    }

    *f_pos += available;
    retval  = available;

    mutex_unlock:
        mutex_unlock(&dev->lock);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *kern_buf;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    /* Allocate kernel memory for the incoming data */
    kern_buf = (char *)kmalloc(count, GFP_KERNEL);
    if(kern_buf == NULL) {
        retval = -ENOMEM;
        goto out;
    }

    size_t bytes_not_copied = copy_from_user(kern_buf, buf, count);
    if (bytes_not_copied) 
    {
        kfree(kern_buf);
        retval = -EFAULT;
        goto out;
    }

    /* Searching for new line */
    char *new_line_pos = memchr(kern_buf, '\n', count);
    size_t num_copy = count;
    if (new_line_pos != NULL)
    {
        num_copy = new_line_pos - kern_buf + 1;
    }
    
    if(mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        kfree(kern_buf);
        goto out;
    }
    
    /* Append the new data to the working entry */
    char *tmp = (char *)krealloc(dev->working_entry.buffptr, dev->working_entry.size + num_copy, GFP_KERNEL);
    if (tmp == NULL) {
        kfree(dev->working_entry.buffptr);
        retval = -ENOMEM;
        goto out;
    }
    dev->working_entry.buffptr = tmp;
    memcpy(dev->working_entry.buffptr + dev->working_entry.size, kern_buf, num_copy); 
    dev->working_entry.size += num_copy;
    retval = num_copy;

    /* Process newline if encountered */
    if (new_line_pos != NULL)
    {
        char *temp_ptr = aesd_circular_buffer_add_entry(&dev->circ_buf, &dev->working_entry);
        if (temp_ptr != NULL)
        {
            kfree(temp_ptr);
        }
        // Clear the buffer entry 
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }

    out:
        mutex_unlock(&dev->lock);
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
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.circ_buf);

     /* Initialize working entry to an empty state */
     aesd_device.working_entry.buffptr = NULL;
     aesd_device.working_entry.size = 0;

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

    int i = 0;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circ_buf, i) {
        if (entry->buffptr)
            kfree((void*)entry->buffptr);
    }

    /* Free any data remaining in the working entry */
    if (aesd_device.working_entry.buffptr)
        kfree(aesd_device.working_entry.buffptr);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
