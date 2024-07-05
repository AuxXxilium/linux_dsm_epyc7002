// SPDX-License-Identifier: GPL-2.0+
/*
 * Gadget Function Driver for PTP
 *
 * Copyright (C) 2014 Google, Inc.
 * Author: Badhri Jagan Sridharan <badhri@android.com>
 *
 */

#include <linux/module.h>
#include <linux/types.h>

#include <linux/configfs.h>
#include <linux/usb/composite.h>

#include "f_mtp.h"

static struct usb_function_instance *ptp_alloc_inst(void)
{
	return alloc_inst_mtp_ptp(false);
}

static struct usb_function *ptp_alloc(struct usb_function_instance *fi)
{
	return function_alloc_mtp_ptp(fi, false);
}

DECLARE_USB_FUNCTION_INIT(ptp, ptp_alloc_inst, ptp_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Badhri Jagan Sridharan");
