/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */
#include <linux/ceph/ceph_debug.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/slab.h>

#include "super.h"
#include "syno_acl.h"

static inline void ceph_set_cached_syno_acl(struct inode *inode,
					struct syno_acl *acl)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	spin_lock(&ci->i_ceph_lock);
	if (__ceph_caps_issued_mask_metric(ci, CEPH_CAP_XATTR_SHARED, 0))
		set_cached_syno_acl(inode, acl);
	else
		forget_cached_syno_acl(inode);
	spin_unlock(&ci->i_ceph_lock);
}

/*
 * Inode operation syno_acl_get().
 */
struct syno_acl *ceph_get_syno_acl(struct inode *inode)
{
	int size;
	char *value = NULL;
	struct syno_acl *acl;

	acl = get_cached_syno_acl(inode);
	if (!is_uncached_syno_acl(acl))
		return acl;

	size = __ceph_getxattr(inode, SYNO_ACL_XATTR_ACCESS, "", 0);
	if (size > 0) {
		value = kzalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __ceph_getxattr(inode, SYNO_ACL_XATTR_ACCESS, value, size);
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
static int __ceph_set_syno_acl(struct inode *inode, struct syno_acl *acl)
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

	ret = __ceph_setxattr(inode, SYNO_ACL_XATTR_ACCESS, value, size, 0);

	kfree(value);
	if (!ret)
		set_cached_syno_acl(inode, acl);

	return ret;
}

/*
 * Inode operation syno_acl_set().
 */
int ceph_set_syno_acl(struct inode *inode, struct syno_acl *acl)
{
	int ret;

	if (!inode || !acl)
		return -EINVAL;

	ret = __ceph_set_syno_acl(inode, acl);

	return ret;
}

static int
ceph_xattr_syno_acl_get(const struct xattr_handler *handler,
			 struct dentry *dentry, struct inode *inode,
			 const char *name, void *value, size_t size)
{
	int ret = 0;
	struct syno_acl *acl;

	acl = ceph_get_syno_acl(dentry->d_inode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	ret = syno_acl_to_xattr(acl, value, size);
	syno_acl_release(acl);

	return ret;
}

static int
ceph_xattr_syno_acl_set(const struct xattr_handler *handler,
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

	ret = __ceph_set_syno_acl(dentry->d_inode, acl);
out:
	syno_acl_release(acl);
	return ret;
}

const struct xattr_handler ceph_xattr_synoacl_access_handler = {
	.name   = SYNO_ACL_XATTR_ACCESS,
	.get    = ceph_xattr_syno_acl_get,
	.set    = ceph_xattr_syno_acl_set,
};
