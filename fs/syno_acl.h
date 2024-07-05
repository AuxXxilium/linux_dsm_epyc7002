/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#ifndef _FS_SYNO_ACL_H
#define _FS_SYNO_ACL_H

#include <linux/syno_acl.h>

#define PROTECT_BY_ACL          0x0001
#define NEED_INODE_ACL_SUPPORT  0x0004
#define NEED_FS_ACL_SUPPORT     0x0008

struct synoacl_syscall_operations {
	int (*get_perm)(struct dentry *dentry, int *allow_out);
	int (*is_acl_support)(struct dentry *dentry, int tag);
	int (*check_perm)(struct dentry *dentry, int mask);
};

struct synoacl_vfs_operations {
	int (*syno_acl_permission)(struct dentry *d, int mask);
	int (*syno_acl_exec_permission)(struct dentry *d);
	int (*archive_change_ok)(struct dentry *d, unsigned int cmd, int tag, int mask);
	int (*syno_inode_change_ok)(struct dentry *d, struct iattr *attr);
	int (*syno_acl_setattr_post)(struct dentry *dentry, struct iattr *);
	int (*syno_acl_may_delete)(struct dentry *, struct inode *, int);
	int (*syno_acl_access)(struct dentry *d, int mask);
	int (*syno_acl_xattr_get)(struct dentry *d, int cmd, void *value, size_t size);
	void (*syno_acl_to_mode)(struct dentry *d, struct kstat *stat);
	int (*syno_acl_init)(struct dentry *d, struct inode *inode);
};

struct synoacl_mod_info {
	struct synoacl_syscall_operations *syscall_ops;
	struct synoacl_vfs_operations *vfs_ops;
	struct module *owner;
};

#endif /* _FS_SYNO_ACL_H */
