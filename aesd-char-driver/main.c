#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0;
int aesd_minor = 0;

MODULE_AUTHOR("Soha18");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    filp->f_pos = 0;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t entry_offset = 0;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &entry_offset);
    if (!entry || !entry->buffptr) {
        retval = 0;
        goto unlock;
    }

    size_t available = entry->size - entry_offset;
    if (available > count)
        available = count;

    if (copy_to_user(buf, entry->buffptr + entry_offset, available)) {
        retval = -EFAULT;
        goto unlock;
    }

    *f_pos += available;
    retval = available;

unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *kern_buf;

    kern_buf = kmalloc(count, GFP_KERNEL);
    if (!kern_buf) {
        retval = -ENOMEM;
        goto out;
    }

    if (copy_from_user(kern_buf, buf, count)) {
        kfree(kern_buf);
        retval = -EFAULT;
        goto out;
    }

    char *newline = memchr(kern_buf, '\n', count);
    size_t num_copy = newline ? (newline - kern_buf + 1) : count;

    if (mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        kfree(kern_buf);
        goto out;
    }

    char *tmp = krealloc(dev->working_entry.buffptr, dev->working_entry.size + num_copy, GFP_KERNEL);
    if (!tmp) {
        kfree(dev->working_entry.buffptr);
        retval = -ENOMEM;
        goto unlock;
    }

    dev->working_entry.buffptr = tmp;
    memcpy(dev->working_entry.buffptr + dev->working_entry.size, kern_buf, num_copy);
    dev->working_entry.size += num_copy;
    retval = num_copy;

    if (newline) {
        char *old_entry = aesd_circular_buffer_add_entry(&dev->circ_buf, &dev->working_entry);
        if (old_entry)
            kfree(old_entry);
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }

unlock:
    mutex_unlock(&dev->lock);
out:
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    loff_t total_size = aesd_circular_buffer_get_total_bytes(&dev->circ_buf);

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = total_size + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > total_size)
        return -EINVAL;

    filp->f_pos = new_pos;
    return new_pos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t new_pos = 0;
    size_t i;

    if (cmd != AESDCHAR_IOCSEEKTO)
        return -ENOTTY;

    if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
        return -EFAULT;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    if (seekto.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ||
        !dev->circ_buf.entry[seekto.write_cmd].buffptr ||
        seekto.write_cmd_offset >= dev->circ_buf.entry[seekto.write_cmd].size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    for (i = 0; i < seekto.write_cmd; i++)
        new_pos += dev->circ_buf.entry[i].size;

    new_pos += seekto.write_cmd_offset;
    filp->f_pos = new_pos;

    mutex_unlock(&dev->lock);
    return 0;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_ERR "Error %d adding aesd cdev", err);

    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
        return result;

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.circ_buf);
    aesd_device.working_entry.buffptr = NULL;
    aesd_device.working_entry.size = 0;

    result = aesd_setup_cdev(&aesd_device);
    if (result)
        unregister_chrdev_region(dev, 1);

    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct aesd_buffer_entry *entry;
    int i;

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circ_buf, i) {
        if (entry->buffptr)
            kfree(entry->buffptr);
    }

    if (aesd_device.working_entry.buffptr)
        kfree(aesd_device.working_entry.buffptr);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

