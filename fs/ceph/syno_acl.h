/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */

#ifndef _CEPH_SYNO_ACL_H
#define _CEPH_SYNO_ACL_H

#include <linux/syno_acl.h>
#include <linux/syno_acl_xattr.h>

int ceph_set_syno_acl(struct inode *inode, struct syno_acl *acl);
struct syno_acl *ceph_get_syno_acl(struct inode *inode);
struct syno_acl *ceph_syno_acl_from_disk(const void *value, size_t size);

#endif /* _CEPH_SYNO_ACL_H */
