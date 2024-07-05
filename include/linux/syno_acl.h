/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#ifndef _LINUX_SYNO_ACL_H
#define _LINUX_SYNO_ACL_H

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <uapi/linux/syno_acl.h>

/* e_tag entry in struct syno_acl_entry */
#define SYNO_ACL_USER               0x01
#define SYNO_ACL_GROUP              0x02
#define SYNO_ACL_EVERYONE           0x04
#define SYNO_ACL_OWNER              0x08
#define SYNO_ACL_AUTHENTICATEDUSER  0x09
#define SYNO_ACL_SYSTEM             0x0A
#define SYNO_ACL_TAG_ALL                                                       \
	(SYNO_ACL_USER | SYNO_ACL_GROUP | SYNO_ACL_OWNER | SYNO_ACL_EVERYONE)

/* e_allow */
#define SYNO_ACL_ALLOW              0x01
#define SYNO_ACL_DENY               0x02

struct syno_acl_entry {
	unsigned short          e_tag;
	unsigned int            e_id;
	unsigned int            e_perm;
	unsigned short          e_inherit;
	unsigned short          e_allow;
	unsigned int            e_level;
};

struct syno_acl {
	refcount_t              a_refcount;
	struct rcu_head         a_rcu;
	unsigned int            a_count;
	struct syno_acl_entry   a_entries[0];
};

#define FOREACH_SYNOACL_ENTRY(pa, acl, pe)                                     \
	for (pa = (acl)->a_entries, pe = pa + (acl)->a_count; pa < pe; pa++)

/*
 * Duplicate an ACL handle.
 */
static inline struct syno_acl *syno_acl_dup(struct syno_acl *acl)
{
	if (acl)
		refcount_inc(&acl->a_refcount);
	return acl;
}

/*
 * Free an ACL handle.
 */
static inline void syno_acl_release(struct syno_acl *acl)
{
	if (acl && refcount_dec_and_test(&acl->a_refcount))
		kfree_rcu(acl, a_rcu);
}

extern struct syno_acl *syno_acl_alloc(int count, gfp_t flags);
extern int syno_acl_valid(const struct syno_acl *);
extern struct syno_acl *syno_acl_realloc(struct syno_acl *acl, unsigned int counts, gfp_t flags);
extern struct syno_acl *syno_acl_clone(const struct syno_acl *acl, gfp_t flags);

extern int syno_acl_to_xattr(const struct syno_acl *acl, void *buffer, size_t size);
extern struct syno_acl *syno_acl_from_xattr(const void *value, size_t size);

static inline struct syno_acl *get_cached_syno_acl(struct inode *inode)
{
	struct syno_acl **p = &inode->i_syno_acl;
	struct syno_acl *acl;

	for (;;) {
		rcu_read_lock();
		acl = rcu_dereference(*p);
		if (!acl || is_uncached_syno_acl(acl) ||
		    refcount_inc_not_zero(&acl->a_refcount))
			break;
		rcu_read_unlock();
		cpu_relax();
	}
	rcu_read_unlock();
	return acl;
}

static inline void set_cached_syno_acl(struct inode *inode, struct syno_acl *acl)
{
	struct syno_acl **p = &inode->i_syno_acl;
	struct syno_acl *old = NULL;

	old = xchg(p, syno_acl_dup(acl));
	if (!is_uncached_syno_acl(old))
		syno_acl_release(old);
}

extern void forget_cached_syno_acl(struct inode *inode);
extern bool syno_acl_module_get(void);
extern void syno_acl_module_put(void);

extern int synoacl_op_permission(struct dentry *dentry, int perm);
extern int synoacl_op_exec_permission(struct dentry *dentry, struct inode *inode);
extern int synoacl_op_archive_bit_change_ok(struct dentry *dentry, unsigned int cmd, int tag, int mask);
extern int synoacl_op_setattr_prepare(struct dentry *dentry, struct iattr *attr);
extern int synoacl_op_setattr_post(struct dentry *dentry, struct iattr *attr);
extern int synoacl_op_may_delete(struct dentry *victim, struct inode *dir);
extern int synoacl_op_may_access(struct dentry *dentry, int mode);
extern int synoacl_op_acl_xattr_get(struct dentry *dentry, int cmd, void *value, size_t size);
extern void synoacl_op_to_mode(struct dentry *dentry, struct kstat *stat);
extern int synoacl_op_init(struct dentry *dentry);
extern int synoacl_op_xattr_permission(const char *name, struct dentry *dentry, unsigned int perm);

extern int synoacl_mod_permission(struct dentry *, int);
extern int synoacl_mod_exec_permission(struct dentry *);
extern int synoacl_mod_archive_bit_change_ok(struct dentry *, unsigned int, int, int);
extern int synoacl_mod_setattr_prepare(struct dentry *, struct iattr *);
extern int synoacl_mod_setattr_post(struct dentry *, struct iattr *);
extern int synoacl_mod_may_delete(struct dentry *, struct inode *);
extern int synoacl_mod_may_access(struct dentry *, int);
extern int synoacl_mod_acl_xattr_get(struct dentry *, int, void *, size_t);
extern void synoacl_mod_to_mode(struct dentry *, struct kstat *);
extern int synoacl_mod_init(struct dentry *, struct inode *);

extern struct syno_acl *syno_acl_from_disk(const void *value, size_t size);
extern void * syno_acl_to_disk(const struct syno_acl *acl, size_t *size);
#endif  /* _LINUX_SYNO_ACL_H */
