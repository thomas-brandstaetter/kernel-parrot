#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>

#include "parrot.h"

/* Module information */
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");

/** \defgroup Internal device variables */
/** @{ */
static struct class* parrot_class = NULL;
static struct device* parrot_device = NULL;
static int parrot_major;
volatile static bool message_read;
/** @} */

/** Flag used with the one_shot mode */
static DEFINE_MUTEX(parrot_device_mutex);

/** Use Kernel FIFO for read operations */
static DECLARE_KFIFO(parrot_msg_fifo, char, PARROT_MSG_FIFO_SIZE);

/** \defgroup Kernel fifo housekeeping */
/** @{ */

/** This table keeps track of each message length in the FIFO */
static unsigned int parrot_msg_len[PARROT_MSG_FIFO_MAX];
static int parrot_msg_idx_read;     /**< Read index for the table parrot_msg_len */
static int parrot_msg_idx_write;    /**< Write index for the table parrot_msg_len */

/** @} */


/** \defgroup Module parameters that can be provided on insmod */
/** @{ */

/** Flag print extra debug info */
static bool debug = false;
module_param(debug, bool, S_IRUGO | S_IWUSR);
//MODULE_PARAM_DESC(debug, "enable debug info (default = false)");

/** @} */


/** \defgroup Flag for read only a sinlge message after open() */
/** @{ */

/** Flag for one_shot mode */
static bool one_shot = true;
module_param(one_shot, bool, S_IRUGO | S_IWUSR);
//MODULE_PARAM_DESC(one_shot, "disable the readout of multiple messages at once (default = true)");

/** @} */


/** \defgroup File operations */
/** @{ */

/**
 * Enusre no write access and mutex for single proccess access
 * @param inode
 * @param file
 * @return -EACCES if write access is requested, -EBUSY if another process using this device.
 */
static int
parrot_device_open(struct inode* inode, struct file* file)
{
    dbg("");

    /* prohibit write access */
    if (file->f_mode & FMODE_CAN_WRITE)
    {
        warn("write acces is prohibited\n");
        return -EACCES;
    }

    /* try to lock the mutex to ensure only one process has access */
    if (!mutex_trylock(&parrot_device_mutex))
    {
        warn("another process is accessing the device\n");
        return -EBUSY;
    }

    message_read = false;
    return 0;
}


/**
 * Cleanup module
 * @details Unlock mutex
 * @param inode
 * @param file
 * @return
 */
static int
parrot_device_release(struct inode* inode, struct file* file)
{
    dbg("");

    mutex_unlock(&parrot_device_mutex);
    return 0;
}

/**
 *
 * @param file
 * @param __user buffer
 * @param length
 * @param offset
 * @return
 */
static ssize_t
parrot_device_read(struct file* file, char __user *buffer, size_t length, loff_t* offset)
{
    int retval;
    unsigned int copied;

    /* prevent multiple reads until. otherwise the multiple reads deplete the FIFO. */
    if (one_shot && message_read) return 0;
    dbg("");

    if (kfifo_is_empty(&parrot_msg_fifo)) {
        dbg("no message in kfifo\n");
        return 0;
    }

    if (offset == NULL) {
        dbg("offset is NULL");
        return -1;
    }

    retval = kfifo_to_user(&parrot_msg_fifo, buffer, parrot_msg_len[parrot_msg_idx_read], &copied);

    /* warn about short reads */
    if (parrot_msg_len[parrot_msg_idx_read] != copied) {
        warn("short read detected");
    }

    /* update read count */
    parrot_msg_idx_read = (parrot_msg_idx_read+1) % PARROT_MSG_FIFO_MAX;
    message_read = true;

    return retval ? retval : copied;
}


static struct file_operations fops = {
        .read = parrot_device_read,
        .open = parrot_device_open,
        .release = parrot_device_release
};

/** @} */


/** \defgroup sysfs entries  */

/**
 * Placing data into the FIFO is done through sysfs
 * @param dev
 * @param attr
 * @param buffer
 * @param count
 * @return -ENOSPCE if either (count > fifo slots available) or counter write == counter read
 */
static size_t
sys_add_to_fifo(struct device *dev, struct device_attribute *attr, const char *buffer, size_t count)
{
    unsigned int copied;

    dbg("");

    if (kfifo_avail(&parrot_msg_fifo) < count) {
        warn("not enough space left on fifo\n");
        return -ENOSPC;
    }

    if ((parrot_msg_idx_write + 1) % PARROT_MSG_FIFO_MAX == parrot_msg_idx_read) {
        warn("message length table is full\n");
        return -ENOSPC;
    }

    copied = kfifo_in(&parrot_msg_fifo, buffer, count);
    parrot_msg_len[parrot_msg_idx_write] = copied;
    if (copied != count) {
        warn("short write detected\n");
    }
    parrot_msg_idx_write = (parrot_msg_idx_write+1) % PARROT_MSG_FIFO_MAX;

    return copied;
}

/**
 * @details Ideally, we would have a mutex around the FIFO, to ensure that we don't reset while in use.
 * @param dev
 * @param attr
 * @param buffer
 * @param count
 * @return count
 */
static size_t
sys_reset(struct device *dev, struct device_attribute *attr, const char *buffer, size_t count)
{
    dbg("");

    kfifo_reset(&parrot_msg_fifo);
    parrot_msg_idx_read = parrot_msg_idx_write = 0;

    return count;
}

static DEVICE_ATTR(fifo, S_IWUSR, NULL, sys_add_to_fifo);
static DEVICE_ATTR(reset, S_IWUSR, NULL, sys_reset);

/** @} */

/** \defgroup Kernel init exit calls  */
/** @{ */

static int __init parrot_module_init(void)
{
    int retval;
    dbg("");

    parrot_major = register_chrdev(0, DEVICE_NAME, &fops);
    if (parrot_major < 0) {
        err("failed to register device: error %d\n", parrot_major);
        retval = parrot_major;
        goto failed_chrdevreg;
    }
    dbg("parrot device has major: %d\n", parrot_major);

    parrot_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(parrot_class)) {
        err("failed to register device class '%s'\n", CLASS_NAME);
        retval = PTR_ERR(parrot_class);
        goto failed_classreg;
    }

    parrot_device = device_create(parrot_class, NULL, MKDEV(parrot_major, 0), NULL, CLASS_NAME "_" DEVICE_NAME);
    if (IS_ERR(parrot_device)) {
        err("failed to register device '%s_%s'\n", CLASS_NAME, DEVICE_NAME);
        retval = PTR_ERR(parrot_device);
        goto failed_devreg;
    }

    retval = device_create_file(parrot_device, &dev_attr_fifo);
    if (retval < 0) {
        warn("failed to create write /sys endpoint - continue w/o\n");
    }

    retval = device_create_file(parrot_device, &dev_attr_reset);
    if (retval < 0) {
        warn("failed to create reset /sys endpoint - continue w/o\n");
    }

    mutex_init(&parrot_device_mutex);
    INIT_KFIFO(parrot_msg_fifo);
    parrot_msg_idx_read = parrot_msg_idx_write = 0;

    return 0;

failed_devreg:
    class_destroy(parrot_class);
failed_classreg:
    unregister_chrdev(parrot_major, DEVICE_NAME);
failed_chrdevreg:
    return -1;
}

static void __exit parrot_module_exit(void)
{
    dbg("");

    device_remove_file(parrot_device, &dev_attr_fifo);
    device_remove_file(parrot_device, &dev_attr_reset);
    class_destroy(parrot_class);
    unregister_chrdev(parrot_major, DEVICE_NAME);
}

module_init(parrot_module_init);
module_exit(parrot_module_exit);

/** @} */
