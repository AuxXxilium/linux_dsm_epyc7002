/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/slab.h>

#include "ctree.h"
#include "btrfs_inode.h"
#include "xattr.h"

#include <linux/syno_acl_xattr.h>
#include "syno_acl.h"

/*
 * Inode operation syno_acl_get().
 */
struct syno_acl *btrfs_get_syno_acl(struct inode *inode)
{
	int size;
	char *value = NULL;
	struct syno_acl *acl;

	acl = get_cached_syno_acl(inode);
	if (!is_uncached_syno_acl(acl))
		return acl;

	size = btrfs_getxattr(inode, SYNO_ACL_XATTR_ACCESS, "", 0);
	if (size > 0) {
		value = kzalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = btrfs_getxattr(inode, SYNO_ACL_XATTR_ACCESS, value, size);
	}

	if (size > 0)
		acl = syno_acl_from_disk(value, size);
	else if (size == -ENOENT || size == -ENODATA || size == 0)
		/* FIXME, who returns -ENOENT?  I think nobody */
		acl = NULL;
	else
		acl = ERR_PTR(size);

	kfree(value);

	if (!IS_ERR(acl))
		set_cached_syno_acl(inode, acl);

	return acl;
}

/*
 * Needs to be called with fs_mutex held
 */
static int __btrfs_set_syno_acl(struct btrfs_trans_handle *trans,
				struct inode *inode, struct syno_acl *acl)
{
	int ret;
	size_t size = 0;
	char *value = NULL;

	if (acl) {
		ret = syno_acl_valid(acl);
		if (ret < 0)
			return ret;

		value = syno_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return PTR_ERR(value);
	}

	if (trans)
		ret = btrfs_setxattr(trans, inode, SYNO_ACL_XATTR_ACCESS, value, size, 0);
	else
		ret = btrfs_setxattr_trans(inode, SYNO_ACL_XATTR_ACCESS, value, size, 0);

	kfree(value);
	if (!ret)
		set_cached_syno_acl(inode, acl);

	return ret;
}

/*
 * Inode operation syno_acl_set().
 */
int btrfs_set_syno_acl(struct inode *inode, struct syno_acl *acl)
{
	int ret;

	if (!inode || !acl)
		return -EINVAL;

	ret = __btrfs_set_syno_acl(NULL, inode, acl);

	return ret;
}

static int
btrfs_xattr_syno_acl_get(const struct xattr_handler *handler,
			 struct dentry *dentry, struct inode *inode,
			 const char *name, void *value, size_t size)
{
	int ret = 0;
	struct syno_acl *acl;

	acl = btrfs_get_syno_acl(dentry->d_inode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	ret = syno_acl_to_xattr(acl, value, size);
	syno_acl_release(acl);

	return ret;
}

static int
btrfs_xattr_syno_acl_set(const struct xattr_handler *handler,
			 struct dentry *dentry, struct inode *inode,
			 const char *name, const void *value, size_t size, int flags)
{
	int ret;
	struct syno_acl *acl = NULL;

	if (value) {
		acl = syno_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (acl) {
			ret = syno_acl_valid(acl);
			if (ret)
				goto out;
		}
	}

	ret = __btrfs_set_syno_acl(NULL, dentry->d_inode, acl);
out:
	syno_acl_release(acl);
	return ret;
}

const struct xattr_handler btrfs_xattr_synoacl_access_handler = {
	.name   = SYNO_ACL_XATTR_ACCESS,
	.get    = btrfs_xattr_syno_acl_get,
	.set    = btrfs_xattr_syno_acl_set,
};
