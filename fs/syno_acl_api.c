#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/file.h>

#include "syno_acl.h"


#define SYSCALL_OPS             synoacl_mod_info->syscall_ops
#define VFS_OPS                 synoacl_mod_info->vfs_ops

#define IS_MOD_INFO_READY       (synoacl_mod_info && SYSCALL_OPS && VFS_OPS)
#define IS_SYSCALL_ACL_READY(x) (IS_MOD_INFO_READY && SYSCALL_OPS->x)
#define IS_VFS_ACL_READY(x)     (IS_MOD_INFO_READY && VFS_OPS->x)

#define DO_SYSCALL(x, ...)      SYSCALL_OPS->x(__VA_ARGS__)
#define DO_VFS(x, ...)          VFS_OPS->x(__VA_ARGS__)

static DEFINE_MUTEX(synoacl_mod_mutex);
struct synoacl_mod_info *synoacl_mod_info = NULL;
EXPORT_SYMBOL(synoacl_mod_info);

bool syno_acl_module_get(void)
{
	int ret = -1;
	/* If synoacl_vfs.ko wasn't loaded earlier then load it now.
	 * When synoacl_vfs is built into vmlinux the module's __init
	 * function will populate synoacl_mod_info.
	 */
	mutex_lock(&synoacl_mod_mutex);
	if (!synoacl_mod_info) {
		ret = request_module("synoacl_vfs");
		if (!synoacl_mod_info) {
			mutex_unlock(&synoacl_mod_mutex);
			pr_err("synoacl_vfs request_module failed. err code:%d\n", ret);
			return false;
		}
	}
	mutex_unlock(&synoacl_mod_mutex);

	/* And grab the reference, so the module doesn't disappear while the
	 * kernel is interacting with the kernel module.
	 */
	if (!try_module_get(synoacl_mod_info->owner)) {
		pr_err("synoacl_vfs try_module_get failed.\n");
		return false;
	}

	return true;
}
EXPORT_SYMBOL(syno_acl_module_get);

void syno_acl_module_put(void)
{
	if (synoacl_mod_info)
		module_put(synoacl_mod_info->owner);
}
EXPORT_SYMBOL(syno_acl_module_put);

/*
 * VFS API
 */
int synoacl_mod_permission(struct dentry *d, int mask)
{
	if (IS_VFS_ACL_READY(syno_acl_permission))
		return DO_VFS(syno_acl_permission, d, mask);

	return 0;
}
EXPORT_SYMBOL(synoacl_mod_permission);

int synoacl_mod_exec_permission(struct dentry *d)
{
	if (IS_VFS_ACL_READY(syno_acl_exec_permission))
		return DO_VFS(syno_acl_exec_permission, d);

	return 0;
}
EXPORT_SYMBOL(synoacl_mod_exec_permission);

int synoacl_mod_archive_bit_change_ok(struct dentry *d, unsigned int cmd, int tag,
				      int mask)
{
	if (IS_VFS_ACL_READY(archive_change_ok))
		return DO_VFS(archive_change_ok, d, cmd, tag, mask);

	return 0; // is settable
}
EXPORT_SYMBOL(synoacl_mod_archive_bit_change_ok);

int synoacl_mod_setattr_prepare(struct dentry *d, struct iattr *attr)
{
	if (IS_VFS_ACL_READY(syno_inode_change_ok))
		return DO_VFS(syno_inode_change_ok, d, attr);

	return setattr_prepare(d, attr);
}
EXPORT_SYMBOL(synoacl_mod_setattr_prepare);

int synoacl_mod_setattr_post(struct dentry *dentry, struct iattr *attr)
{
	if (IS_VFS_ACL_READY(syno_acl_setattr_post))
		return DO_VFS(syno_acl_setattr_post, dentry, attr);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(synoacl_mod_setattr_post);

int synoacl_mod_may_delete(struct dentry *d, struct inode *dir)
{
	if (IS_VFS_ACL_READY(syno_acl_may_delete))
		return DO_VFS(syno_acl_may_delete, d, dir, 1);

	return inode_permission(dir, MAY_WRITE | MAY_EXEC);
}
EXPORT_SYMBOL(synoacl_mod_may_delete);

int synoacl_mod_may_access(struct dentry *d, int mask)
{
	if (IS_VFS_ACL_READY(syno_acl_access))
		return DO_VFS(syno_acl_access, d, mask);

	return inode_permission(d->d_inode, mask | MAY_ACCESS);
}
EXPORT_SYMBOL(synoacl_mod_may_access);

int synoacl_mod_acl_xattr_get(struct dentry *d, int cmd, void *value, size_t size)
{
	if (IS_VFS_ACL_READY(syno_acl_xattr_get))
		return DO_VFS(syno_acl_xattr_get, d, cmd, value, size);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(synoacl_mod_acl_xattr_get);

void synoacl_mod_to_mode(struct dentry *d, struct kstat *stat)
{
	if (IS_VFS_ACL_READY(syno_acl_to_mode))
		DO_VFS(syno_acl_to_mode, d, stat);
}
EXPORT_SYMBOL(synoacl_mod_to_mode);

int synoacl_mod_init(struct dentry *dentry, struct inode *inode)
{
	if (IS_VFS_ACL_READY(syno_acl_init))
		return DO_VFS(syno_acl_init, dentry, inode);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(synoacl_mod_init);

#ifdef MY_ABC_HERE
#include <linux/syscalls.h>

SYSCALL_DEFINE2(syno_acl_check_perm, const char __user *, filename, int, mask)
{
#ifdef MY_ABC_HERE
	struct path path;
	int error;

	error = user_path_at(AT_FDCWD, filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;

	if (IS_SYSCALL_ACL_READY(check_perm))
		error = DO_SYSCALL(check_perm, path.dentry, mask);
	else
		error = -EOPNOTSUPP;

	path_put(&path);
	return error;
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE3(syno_acl_is_support, const char __user *, filename, int, fd, int, tag)
{
#ifdef MY_ABC_HERE
	int is_path_get = 0;
	struct path path;
	struct file *fp = NULL;
	struct dentry *dentry;
	int error = -EINVAL;

	if (filename) {
		error = user_path_at(AT_FDCWD, filename, LOOKUP_FOLLOW, &path);
		if (error)
			goto out;

		is_path_get = 1;

		if (!path.dentry || !path.dentry->d_inode)
			goto out;

		dentry = path.dentry;
	} else if (fd >= 0) {
		fp = fget(fd);
		if (!fp || !fp->f_path.dentry) {
			error = -EBADF;
			goto out;
		}
		dentry = fp->f_path.dentry;
	} else {
		goto out;
	}

	if (IS_SYSCALL_ACL_READY(is_acl_support))
		error = DO_SYSCALL(is_acl_support, dentry, tag);
	else
		error = -EOPNOTSUPP;

out:
	if (is_path_get)
		path_put(&path);
	if (fp)
		fput(fp);

	return error;
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE2(syno_acl_get_perm, const char __user *, filename, int __user *, out_perm)
{
#ifdef MY_ABC_HERE
	unsigned int perm_allow = 0;
	int error;
	struct path path;

	error = user_path_at(AT_FDCWD, filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;

	if (IS_SYNOACL_SUPERUSER()) {
		perm_allow = SYNO_PERM_FULL_CONTROL;
		error = 0;
		goto end;
	}

	if (IS_SYSCALL_ACL_READY(get_perm))
		error = DO_SYSCALL(get_perm, path.dentry, &perm_allow);
	else
		error = -EOPNOTSUPP;

end:
	if (copy_to_user(out_perm, &perm_allow, sizeof(perm_allow))) {
		error = -EFAULT;
		goto err;
	}

err:
	path_put(&path);
	return error;
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}
#endif /* MY_ABC_HERE */
