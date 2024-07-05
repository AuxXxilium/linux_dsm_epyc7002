#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2021 synology
 */
#ifndef _LINUX_SYNOTIFY_H
#define _LINUX_SYNOTIFY_H

#ifdef MY_ABC_HERE
#include <linux/sysctl.h>
#include <uapi/linux/synotify.h>

extern struct ctl_table synotify_table[]; /* for sysctl */
#endif /* MY_ABC_HERE */

#endif /* _LINUX_SYNOTIFY_H */
