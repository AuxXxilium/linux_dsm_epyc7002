// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017,2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/debugfs.h>

struct dentry *rtk_info_debugfs_root;

static int __init rtk_info_debugfs_init(void)
{
	rtk_info_debugfs_root = debugfs_create_dir("info", NULL);
	return 0;
}
arch_initcall(rtk_info_debugfs_init);

