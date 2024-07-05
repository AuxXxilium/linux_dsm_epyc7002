/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/quotaops.h>

#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"
#include "syno_acl.h"

static inline int
ext4_sae_from_disk(struct syno_acl_entry *sae, ext4_syno_acl_entry *esae)
{
	unsigned short tag = le16_to_cpu(esae->e_tag);

	// ID: user/group/everyone
	if (SYNO_ACL_XATTR_TAG_ID_GROUP & tag) {
		sae->e_tag = SYNO_ACL_GROUP;
		sae->e_id = le32_to_cpu(esae->e_id);
	} else if (SYNO_ACL_XATTR_TAG_ID_EVERYONE & tag) {
		sae->e_tag = SYNO_ACL_EVERYONE;
		sae->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_USER & tag) {
		sae->e_tag = SYNO_ACL_USER;
		sae->e_id = le32_to_cpu(esae->e_id);
	} else if (SYNO_ACL_XATTR_TAG_ID_OWNER & tag) {
		sae->e_tag = SYNO_ACL_OWNER;
		sae->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_AUTHENTICATEDUSER & tag) {
		sae->e_tag = SYNO_ACL_AUTHENTICATEDUSER;
		sae->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_SYSTEM & tag) {
		sae->e_tag = SYNO_ACL_SYSTEM;
		sae->e_id = SYNO_ACL_UNDEFINED_ID;
	} else {
		return -EINVAL;
	}

	// Allow/Deny
	if (SYNO_ACL_XATTR_TAG_IS_DENY & tag) {
		sae->e_allow = SYNO_ACL_DENY;
	} else if (SYNO_ACL_XATTR_TAG_IS_ALLOW & tag){
		sae->e_allow = SYNO_ACL_ALLOW;
	} else {
		return -EINVAL;
	}

	// Permission
	sae->e_perm = le32_to_cpu(esae->e_perm);
	// Inherit
	sae->e_inherit = le16_to_cpu(esae->e_inherit);
	// Inherit level
	sae->e_level = 0;

	return 0;
}

static inline int
ext4_sae_to_disk(const struct syno_acl_entry *sae, ext4_syno_acl_entry *esae)
{
	unsigned short tag = 0;

	// ID: user/group/everyone
	switch(sae->e_tag){
	case SYNO_ACL_GROUP:
		tag |= SYNO_ACL_XATTR_TAG_ID_GROUP;
		break;
	case SYNO_ACL_EVERYONE:
		tag |= SYNO_ACL_XATTR_TAG_ID_EVERYONE;
		break;
	case SYNO_ACL_USER:
		tag |= SYNO_ACL_XATTR_TAG_ID_USER;
		break;
	case SYNO_ACL_OWNER:
		tag |= SYNO_ACL_XATTR_TAG_ID_OWNER;
		break;
	case SYNO_ACL_AUTHENTICATEDUSER:
		tag |= SYNO_ACL_XATTR_TAG_ID_AUTHENTICATEDUSER;
		break;
	case SYNO_ACL_SYSTEM:
		tag |= SYNO_ACL_XATTR_TAG_ID_SYSTEM;
		break;
	default:
		return -EINVAL;
	}

	// Allow/Deny
	switch(sae->e_allow){
	case SYNO_ACL_DENY:
		tag |= SYNO_ACL_XATTR_TAG_IS_DENY;
		break;
	case SYNO_ACL_ALLOW:
		tag |= SYNO_ACL_XATTR_TAG_IS_ALLOW;
		break;
	default:
		return -EINVAL;
	}

	esae->e_tag     = cpu_to_le16(tag);
	esae->e_inherit = cpu_to_le16(sae->e_inherit);
	esae->e_perm    = cpu_to_le32(sae->e_perm);
	esae->e_id      = cpu_to_le32(sae->e_id);

	return 0;
}

/*
 * Convert from filesystem to in-memory representation.
 */
static struct syno_acl *
ext4_syno_acl_from_disk(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	size_t i, count;
	struct syno_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(ext4_syno_acl_header))
		return ERR_PTR(-EINVAL);

	count = ext4_syno_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	if (((ext4_syno_acl_header *)value)->a_version != cpu_to_le16(EXT4_SYNO_ACL_VERSION))
		return ERR_PTR(-EINVAL);

	acl = syno_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	value = (char *)value + sizeof(ext4_syno_acl_header);
	for (i = 0; i < count; i++) {
		ext4_syno_acl_entry *entry = (ext4_syno_acl_entry *)value;

		if ((char *)value + sizeof(ext4_syno_acl_entry) > end)
			goto fail;

		if (ext4_sae_from_disk(&(acl->a_entries[i]), entry))
			goto fail;

		value = (char *)value + sizeof(ext4_syno_acl_entry);
	}

	if (value != end)
		goto fail;
	return acl;

fail:
	syno_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Convert from in-memory to filesystem representation.
 */
static void *
ext4_syno_acl_to_disk(const struct syno_acl *acl, size_t *size)
{
	char *ent;
	size_t i;
	ext4_syno_acl_header *ext_acl;

	*size = ext4_syno_acl_size(acl->a_count);
	ext_acl = kmalloc(*size, GFP_NOFS);
	if (!ext_acl)
		return ERR_PTR(-ENOMEM);

	ext_acl->a_version = cpu_to_le16(EXT4_SYNO_ACL_VERSION);
	ent = (char *)ext_acl + sizeof(ext4_syno_acl_header);

	for (i = 0; i < acl->a_count; i++, ent += sizeof(ext4_syno_acl_entry)) {
		ext4_syno_acl_entry *entry = (ext4_syno_acl_entry *)ent;

		if (0 > ext4_sae_to_disk(&(acl->a_entries[i]), entry))
			goto fail;
	}

	return (char *)ext_acl;

fail:
	kfree(ext_acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Inode operation syno_acl_get().
 *
 * inode->i_mutex: don't care
 */
struct syno_acl * ext4_get_syno_acl(struct inode *inode)
{
	int size;
	char *value = NULL;
	struct syno_acl *acl;

	acl = get_cached_syno_acl(inode);
	if (!is_uncached_syno_acl(acl))
		return acl;

	size = ext4_xattr_get(inode, EXT4_XATTR_INDEX_SYNO_ACL_ACCESS, "", NULL, 0);
	if (size > 0) {
		value = kmalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = ext4_xattr_get(inode, EXT4_XATTR_INDEX_SYNO_ACL_ACCESS, "", value, size);
	}

	if (size > 0)
		acl = ext4_syno_acl_from_disk(value, size);
	else if (size == -ENODATA || size == -ENOSYS || size == 0)
		acl = NULL;
	else
		acl = ERR_PTR(size);

	kfree(value);

	if (!IS_ERR(acl))
		set_cached_syno_acl(inode, acl);

	return acl;
}

/*
 * Set the syno acl of an inode.
 *
 * inode->i_mutex: down unless called from ext4_new_inode
 */
static int
__ext4_set_syno_acl(handle_t *handle, struct inode *inode, struct syno_acl *acl)
{
	int ret;
	size_t size = 0;
	void *value = NULL;

	if (acl) {
		ret = syno_acl_valid(acl);
		if (ret < 0)
			return ret;

		value = ext4_syno_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return PTR_ERR(value);
	}

	ret = ext4_xattr_set_handle(handle, inode, EXT4_XATTR_INDEX_SYNO_ACL_ACCESS, "",
	                            value, size, 0);

	kfree(value);
	if (!ret)
		set_cached_syno_acl(inode, acl);

	return ret;
}


/*
 * Inode operation syno_acl_set().
 */
int ext4_set_syno_acl(struct inode *inode, struct syno_acl *acl)
{
	handle_t *handle;
	int error, retries = 0;

	if (!inode || !acl)
		return -EINVAL;

	error = dquot_initialize(inode);
	if (error)
		return error;
retry:
	handle = ext4_journal_start(inode, EXT4_HT_SYNO, EXT4_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	error = __ext4_set_syno_acl(handle, inode, acl);

	ext4_journal_stop(handle);
	if (error == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;

	return error;
}

static bool
ext4_xattr_syno_acl_list(struct dentry *dentry)
{
	return IS_EXT4_SYNOACL(d_inode(dentry));
}

static int
ext4_xattr_syno_acl_get(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, void *value, size_t size)
{
	int ret;
	struct syno_acl *acl;

	acl = ext4_get_syno_acl(inode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (NULL == acl)
		return -ENODATA;

	ret = syno_acl_to_xattr(acl, value, size);
	syno_acl_release(acl);

	return ret;
}

static int
ext4_xattr_syno_acl_set(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *value, size_t size, int flags)
{
	handle_t *handle;
	struct syno_acl *acl = NULL;
	int ret, retries = 0;

	if (value) {
		acl = syno_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (acl) {
			ret = syno_acl_valid(acl);
			if (ret)
				goto release_and_out;
		}
	}

	ret = dquot_initialize(inode);
	if (ret)
		return ret;

retry:
	handle = ext4_journal_start(inode, EXT4_HT_SYNO, EXT4_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto release_and_out;
	}

	ret = __ext4_set_syno_acl(handle, inode, acl);
	ext4_journal_stop(handle);
	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;

release_and_out:
	syno_acl_release(acl);
	return ret;
}

const struct xattr_handler ext4_xattr_synoacl_access_handler = {
	.name   = SYNO_ACL_XATTR_ACCESS,
	.list   = ext4_xattr_syno_acl_list,
	.get    = ext4_xattr_syno_acl_get,
	.set    = ext4_xattr_syno_acl_set,
};
