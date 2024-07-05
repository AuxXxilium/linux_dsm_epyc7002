#ifndef _LINUX_ION_RTK_IOCTL_H
#define _LINUX_ION_RTK_IOCTL_H

#include <linux/file.h>

long ion_rtk_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#endif /* _LINUX_ION_RTK_IOCTL_H */
