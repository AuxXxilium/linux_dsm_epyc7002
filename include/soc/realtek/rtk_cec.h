// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */

#ifndef __RTK_CEC__H__
#define __RTK_CEC__H__

struct ipc_shm_cec {
	unsigned long  standby_config;
	unsigned char  standby_logical_addr;
	unsigned short standby_physical_addr;
	unsigned char  standby_cec_version;
	unsigned long  standby_vendor_id;
	unsigned short standby_rx_mask;
	unsigned char  standby_cec_wakeup_off;
};

#endif
