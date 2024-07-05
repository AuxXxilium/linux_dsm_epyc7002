/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#ifndef _BTRFS_SYNO_ACL_H
#define _BTRFS_SYNO_ACL_H

#include <linux/syno_acl.h>

int btrfs_set_syno_acl(struct inode *inode, struct syno_acl *acl);
struct syno_acl *btrfs_get_syno_acl(struct inode *inode);

#endif /* _BTRFS_SYNO_ACL_H */
