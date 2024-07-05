/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/syno_acl.h>
#include <linux/syno_acl_xattr.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syno.h>

#include "syno_acl.h"

struct syno_acl *syno_acl_alloc(int count, gfp_t flags)
{
	size_t size = sizeof(struct syno_acl) + count * sizeof(struct syno_acl_entry);
	struct syno_acl *acl = kmalloc(size, flags);

	if (acl) {
		refcount_set(&acl->a_refcount, 1);
		acl->a_count = count;
	}

	return acl;
}
EXPORT_SYMBOL(syno_acl_alloc);

struct syno_acl *syno_acl_clone(const struct syno_acl *acl, gfp_t flags)
{
	struct syno_acl *clone = NULL;

	if (acl) {
		size_t size = sizeof(struct syno_acl) +
			   acl->a_count * sizeof(struct syno_acl_entry);
		clone = kmemdup(acl, size, flags);
		if (clone)
			refcount_set(&clone->a_refcount, 1);
	}
	return clone;
}
EXPORT_SYMBOL(syno_acl_clone);

/*
 * Check if an ACL is valid. Returns 0 if it is, or -ERRNO for otherwise.
 */
int syno_acl_valid(const struct syno_acl *acl)
{
	const struct syno_acl_entry *pa, *pe;

	if (!acl)
		return -EINVAL;

	FOREACH_SYNOACL_ENTRY(pa, acl, pe) {
		if (pa->e_perm & ~(SYNO_PERM_FULL_CONTROL))
			return -EINVAL;
		if (pa->e_tag & ~(SYNO_ACL_TAG_ALL))
			return -EINVAL;
		if (SYNO_ACL_ALLOW != pa->e_allow && SYNO_ACL_DENY != pa->e_allow)
			return -EINVAL;
		if (pa->e_inherit & ~(SYNO_ACL_INHERIT_ALL))
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(syno_acl_valid);

/*
 * Re-allocate a new ACL with the specified number of entries.
 *
 * The caller must ensure the acl is only referenced once.
 */
struct syno_acl *syno_acl_realloc(struct syno_acl *acl, unsigned int counts,
				  gfp_t flags)
{
	struct syno_acl *acl_re;
	size_t size = sizeof(struct syno_acl) + counts * sizeof(struct syno_acl_entry);

	if (!acl)
		return NULL;

	if (refcount_read(&acl->a_refcount) != 1) {
		printk(KERN_ERR " acl reference count: %d \n ", refcount_read(&acl->a_refcount));
		return NULL;
	}

	/* assert(refcount_read(acl->a_refcount) == 1); */

	acl_re = krealloc(acl, size, flags);
	if (acl_re)
		acl_re->a_count = counts;

	return acl_re;
}
EXPORT_SYMBOL(syno_acl_realloc);

static inline int ace_syno_from_xattr(struct syno_acl_entry *pAce,
				      syno_acl_xattr_entry *pEntry)
{
	unsigned short tag = le16_to_cpu(pEntry->e_tag);

	// ID: user/group/everyone
	if (SYNO_ACL_XATTR_TAG_ID_GROUP & tag) {
		pAce->e_tag = SYNO_ACL_GROUP;
		pAce->e_id = le32_to_cpu(pEntry->e_id);
	} else if (SYNO_ACL_XATTR_TAG_ID_EVERYONE & tag) {
		pAce->e_tag = SYNO_ACL_EVERYONE;
		pAce->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_USER & tag) {
		pAce->e_tag = SYNO_ACL_USER;
		pAce->e_id = le32_to_cpu(pEntry->e_id);
	} else if (SYNO_ACL_XATTR_TAG_ID_OWNER & tag) {
		pAce->e_tag = SYNO_ACL_OWNER;
		pAce->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_AUTHENTICATEDUSER & tag) {
		pAce->e_tag = SYNO_ACL_AUTHENTICATEDUSER;
		pAce->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_SYSTEM & tag) {
		pAce->e_tag = SYNO_ACL_SYSTEM;
		pAce->e_id = SYNO_ACL_UNDEFINED_ID;
	} else {
		return -1;
	}

	// Allow/Deny
	if (SYNO_ACL_XATTR_TAG_IS_DENY & tag)
		pAce->e_allow = SYNO_ACL_DENY;
	else if (SYNO_ACL_XATTR_TAG_IS_ALLOW & tag)
		pAce->e_allow = SYNO_ACL_ALLOW;
	else
		return -1;

	pAce->e_perm    = le32_to_cpu(pEntry->e_perm);
	pAce->e_inherit = le16_to_cpu(pEntry->e_inherit);
	pAce->e_level   = le32_to_cpu(pEntry->e_level);

	return 0;
}

static inline int ace_syno_to_xattr(const struct syno_acl_entry *pAce,
				    syno_acl_xattr_entry *pEntry)
{
	int ret = 0;
	unsigned short tag = 0;

	// ID: user/group/everyone
	switch (pAce->e_tag) {
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
		ret = -EINVAL;
		goto Err;
	}

	// Allow/Deny
	switch (pAce->e_allow) {
	case SYNO_ACL_DENY:
		tag |= SYNO_ACL_XATTR_TAG_IS_DENY;
		break;
	case SYNO_ACL_ALLOW:
		tag |= SYNO_ACL_XATTR_TAG_IS_ALLOW;
		break;
	default:
		ret = -EINVAL;
		goto Err;
	}

	pEntry->e_tag       = cpu_to_le16(tag);
	pEntry->e_inherit   = cpu_to_le16(pAce->e_inherit);
	pEntry->e_perm      = cpu_to_le32(pAce->e_perm);
	pEntry->e_id        = cpu_to_le32(pAce->e_id);
	pEntry->e_level     = cpu_to_le32(pAce->e_level);

Err:
	return ret;
}

/*
 * Convert from extended attribute to in-memory representation.
 */
struct syno_acl *syno_acl_from_xattr(const void *value, size_t size)
{
	syno_acl_xattr_header *header;
	syno_acl_xattr_entry *entry, *end;
	int count;
	struct syno_acl *acl;
	struct syno_acl_entry *acl_e;

	if (!value)
		return NULL;

	if (size < sizeof(syno_acl_xattr_header))
		return ERR_PTR(-EINVAL);

	header = (syno_acl_xattr_header *)value;
	entry = (syno_acl_xattr_entry *)(header + 1);

	if (header->a_version != cpu_to_le16(SYNO_ACL_XATTR_VERSION))
		return ERR_PTR(-EOPNOTSUPP);

	count = syno_acl_xattr_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	acl = syno_acl_alloc(count, GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	acl_e = acl->a_entries;
	end = entry + count;
	for (; entry != end; acl_e++, entry++) {
		if (0 > ace_syno_from_xattr(acl_e, entry))
			goto fail;
	}
	return acl;

fail:
	syno_acl_release(acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL(syno_acl_from_xattr);

/*
 * Convert from in-memory to extended attribute representation.
 */
int syno_acl_to_xattr(const struct syno_acl *acl, void *buffer, size_t size)
{
	syno_acl_xattr_header *ext_acl = NULL;
	syno_acl_xattr_entry *ext_entry = NULL;
	int real_size, i, ret;

	if (!acl)
		return 0;

	real_size = syno_acl_xattr_size(acl->a_count);
	if (!buffer)
		return real_size;
	if (real_size > size)
		return -ERANGE;

	ext_acl = (syno_acl_xattr_header *)buffer;
	ext_entry = ext_acl->a_entries;
	ext_acl->a_version = cpu_to_le16(SYNO_ACL_XATTR_VERSION);

	for (i = 0; i < acl->a_count; i++, ext_entry++) {
		ret = ace_syno_to_xattr(&(acl->a_entries[i]), ext_entry);
		if (0 > ret)
			return ret;
	}
	return real_size;
}
EXPORT_SYMBOL(syno_acl_to_xattr);

/*
 * Inode operations of SynoACL
 */
int synoacl_op_permission(struct dentry *dentry, int perm)
{
	int ret;
	struct inode *inode = d_inode(dentry);

	if (perm & MAY_NOT_BLOCK)
		return -ECHILD;

	if (inode->i_op->syno_permission)
		ret = inode->i_op->syno_permission(dentry, perm);
	else
		ret = synoacl_mod_permission(dentry, perm);

	trace_synoacl_permission(dentry, perm, ret);

	return ret;
}
EXPORT_SYMBOL(synoacl_op_permission);

int synoacl_op_exec_permission(struct dentry *dentry, struct inode *inode)
{
	int ret;

	if (inode->i_op->syno_exec_permission)
		ret = inode->i_op->syno_exec_permission(dentry);
	else
		ret = synoacl_mod_exec_permission(dentry);

	trace_synoacl_exec_permission(dentry, ret);

	return ret;
}
EXPORT_SYMBOL(synoacl_op_exec_permission);

int synoacl_op_archive_bit_change_ok(struct dentry *dentry,
				     unsigned int cmd, int tag, int mask)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_archive_bit_change_ok)
		return inode->i_op->syno_archive_bit_change_ok(dentry, cmd, tag, mask);

	return synoacl_mod_archive_bit_change_ok(dentry, cmd, tag, mask);
}
EXPORT_SYMBOL(synoacl_op_archive_bit_change_ok);

/**
 * synoacl_op_setattr_prepare - check if attribute changes to a dentry are allowed
 * @dentry:	dentry to check
 * @attr:	attributes to change
 *
 * this is corresponding to setattr_prepare(), which was named as inode_change_ok()
 * before v4.9
 */
int synoacl_op_setattr_prepare(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_setattr_prepare)
		return inode->i_op->syno_setattr_prepare(dentry, attr);

	return synoacl_mod_setattr_prepare(dentry, attr);
}
EXPORT_SYMBOL(synoacl_op_setattr_prepare);

int synoacl_op_setattr_post(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_setattr_post)
		return inode->i_op->syno_setattr_post(dentry, attr);

	return synoacl_mod_setattr_post(dentry, attr);
}
EXPORT_SYMBOL(synoacl_op_setattr_post);

int synoacl_op_may_delete(struct dentry *victim, struct inode *dir)
{
	int ret;

	if (dir->i_op->syno_may_delete)
		ret = dir->i_op->syno_may_delete(victim, dir);
	else
		ret = synoacl_mod_may_delete(victim, dir);

	trace_synoacl_may_delete(victim, dir, ret);

	return ret;
}
EXPORT_SYMBOL(synoacl_op_may_delete);

int synoacl_op_may_access(struct dentry *dentry, int mode)
{
	int ret;
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_may_access)
		ret = inode->i_op->syno_may_access(dentry, mode);
	else
		ret = synoacl_mod_may_access(dentry, mode);

	trace_synoacl_may_access(dentry, mode, ret);

	return ret;
}
EXPORT_SYMBOL(synoacl_op_may_access);

int synoacl_op_acl_xattr_get(struct dentry *dentry, int cmd, void *value, size_t size)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_acl_xattr_get)
		return inode->i_op->syno_acl_xattr_get(dentry, cmd, value, size);

	return synoacl_mod_acl_xattr_get(dentry, cmd, value, size);
}
EXPORT_SYMBOL(synoacl_op_acl_xattr_get);

void synoacl_op_to_mode(struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_acl_to_mode)
		inode->i_op->syno_acl_to_mode(dentry, stat);
	else
		synoacl_mod_to_mode(dentry, stat);
}
EXPORT_SYMBOL(synoacl_op_to_mode);

int synoacl_op_init(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_op->syno_acl_init)
		return inode->i_op->syno_acl_init(dentry, inode);

	return synoacl_mod_init(dentry, inode);
}
EXPORT_SYMBOL(synoacl_op_init);

int synoacl_op_xattr_permission(const char *name, struct dentry *dentry, unsigned int perm)
{
	int error = 0;

	if (!name || strcmp(name, SYNO_ACL_XATTR_ACCESS))
		return 0; // skip xattr except ACL.

	switch (perm) {
	case MAY_READ_PERMISSION:
		if (!IS_SYNOACL(dentry))
			return -EOPNOTSUPP;
		break;

	case MAY_WRITE_PERMISSION:
		if (!IS_FS_SYNOACL(dentry->d_inode))
			return -EOPNOTSUPP;
		break;

	default:
		return 0; // invalid parameters, just skip it.
	}

	error = synoacl_op_permission(dentry, perm);
	if (error)
		return error;

	return 0;
}

static inline int
syno_acl_entry_from_disk(struct syno_acl_entry *sae, syno_acl_entry_t *bsae)
{
	unsigned short tag = le16_to_cpu(bsae->e_tag);

	// ID: user/group/everyone
	if (SYNO_ACL_XATTR_TAG_ID_GROUP & tag) {
		sae->e_tag = SYNO_ACL_GROUP;
		sae->e_id = le32_to_cpu(bsae->e_id);
	} else if (SYNO_ACL_XATTR_TAG_ID_EVERYONE & tag) {
		sae->e_tag = SYNO_ACL_EVERYONE;
		sae->e_id = SYNO_ACL_UNDEFINED_ID;
	} else if (SYNO_ACL_XATTR_TAG_ID_USER & tag) {
		sae->e_tag = SYNO_ACL_USER;
		sae->e_id = le32_to_cpu(bsae->e_id);
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

	sae->e_perm = le32_to_cpu(bsae->e_perm);
	sae->e_inherit = le16_to_cpu(bsae->e_inherit);
	sae->e_level = 0;

	return 0;
}

static inline int
syno_acl_entry_to_disk(const struct syno_acl_entry *sae, syno_acl_entry_t *bsae)
{
	unsigned short tag = 0;

	//ID: user/group/everyone
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

	bsae->e_tag     = cpu_to_le16(tag);
	bsae->e_inherit = cpu_to_le16(sae->e_inherit);
	bsae->e_perm    = cpu_to_le32(sae->e_perm);
	bsae->e_id      = cpu_to_le32(sae->e_id);

	return 0;
}

/*
 * Convert from filesystem to in-memory representation.
 */
struct syno_acl *syno_acl_from_disk(const void *value, size_t size)
{
	int i, count;
	struct syno_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(syno_acl_header_t))
		return ERR_PTR(-EINVAL);
	if ((size - sizeof(syno_acl_header_t)) % sizeof(syno_acl_entry_t))
		return ERR_PTR(-EINVAL);

	count = (size - sizeof(syno_acl_header_t)) / sizeof(syno_acl_entry_t);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	if (((syno_acl_header_t *)value)->a_version != cpu_to_le16(SYNO_ACL_VERSION))
		return ERR_PTR(-EINVAL);

	acl = syno_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	value = (char *)value + sizeof(syno_acl_header_t);
	for (i = 0; i < count; i++) {
		if (syno_acl_entry_from_disk(&(acl->a_entries[i]), (syno_acl_entry_t *)value))
			goto fail;
		value = (char *)value + sizeof(syno_acl_entry_t);
	}
	return acl;

fail:
	syno_acl_release(acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL(syno_acl_from_disk);

/*
 * Convert from in-memory to filesystem representation.
 */
void * syno_acl_to_disk(const struct syno_acl *acl, size_t *size)
{
	char *ent;
	size_t i;
	syno_acl_header_t *b_acl;

	*size = sizeof(syno_acl_header_t) + acl->a_count * sizeof(syno_acl_entry_t);
	b_acl = kmalloc(*size, GFP_NOFS);
	if (!b_acl)
		return ERR_PTR(-ENOMEM);

	b_acl->a_version = cpu_to_le16(SYNO_ACL_VERSION);
	ent = (char *)b_acl + sizeof(syno_acl_header_t);

	for (i = 0; i < acl->a_count; i++) {
		if (0 > syno_acl_entry_to_disk(&(acl->a_entries[i]), (syno_acl_entry_t *)ent))
			goto fail;
		ent += sizeof(syno_acl_entry_t);
	}

	return (char *)b_acl;

fail:
	kfree(b_acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL(syno_acl_to_disk);

static void __forget_cached_syno_acl(struct syno_acl **p)
{
	struct syno_acl *old;

	old = xchg(p, ACL_NOT_CACHED);
	if (!is_uncached_syno_acl(old))
		syno_acl_release(old);
}

void forget_cached_syno_acl(struct inode *inode)
{
	__forget_cached_syno_acl(&inode->i_syno_acl);
}
EXPORT_SYMBOL(forget_cached_syno_acl);
