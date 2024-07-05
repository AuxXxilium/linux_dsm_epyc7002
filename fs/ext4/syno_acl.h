/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#ifndef _EXT4_SYNO_ACL_H
#define _EXT4_SYNO_ACL_H

#include <linux/syno_acl.h>

#define EXT4_SYNO_ACL_VERSION           SYNO_ACL_XATTR_VERSION

#define IS_EXT4_INODE_SYNOACL(inode)    ((inode)->i_archive_bit & S2_SYNO_ACL_SUPPORT)
#define IS_EXT4_SYNOACL(inode)          (IS_EXT4_INODE_SYNOACL(inode) && IS_FS_SYNOACL(inode))

typedef struct {
	__le16          e_tag;
	__le16          e_unused1;
	__le32          e_perm;
	__le16          e_inherit;
	__le16          e_unused2;
	__le32          e_id;
} __attribute__ ((__packed__)) ext4_syno_acl_entry;

typedef struct {
	__le16          a_version;
} __attribute__ ((__packed__)) ext4_syno_acl_header;

static inline size_t ext4_syno_acl_size(int count)
{
	return sizeof(ext4_syno_acl_header) + count * sizeof(ext4_syno_acl_entry);
}

static inline size_t ext4_syno_acl_count(size_t size)
{
	size -= sizeof(ext4_syno_acl_header);
	if (size % sizeof(ext4_syno_acl_entry))
		return -1;
	return size / sizeof(ext4_syno_acl_entry);
}

int ext4_set_syno_acl(struct inode *inode, struct syno_acl *acl);
struct syno_acl *ext4_get_syno_acl(struct inode *inode);

#endif /* _EXT4_SYNO_ACL_H */
