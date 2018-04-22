/**
 *
 *
 */

#define DEVICE_NAME "device"
#define CLASS_NAME "parrot"
#define PARROT_MSG_FIFO_SIZE 1024
#define PARROT_MSG_FIFO_MAX  128

#define AUTHOR "Thomas Brandstätter"
#define DESCRIPTION "'parrot' device driver"
#define VERSION "1.0"

#define dbg(format, arg...) do { if (debug) pr_info(CLASS_NAME ": %s: " format, __FUNCTION__, ## arg); } while (0)
#define err(format, arg...) pr_err(CLASS_NAME ": " format, ## arg)
#define info(format, arg...) pr_info(CLASS_NAME ": " format, ## arg)
#define warn(format, arg...) pr_warn(CLASS_NAME ": " format, ## arg)
