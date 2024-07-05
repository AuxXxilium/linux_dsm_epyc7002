#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 symlink handling code
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include "ext4.h"
#include "xattr.h"

static const char *ext4_encrypted_get_link(struct dentry *dentry,
					   struct inode *inode,
					   struct delayed_call *done)
{
	struct page *cpage = NULL;
	const void *caddr;
	unsigned int max_size;
	const char *paddr;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	if (ext4_inode_is_fast_symlink(inode)) {
		caddr = EXT4_I(inode)->i_data;
		max_size = sizeof(EXT4_I(inode)->i_data);
	} else {
		cpage = read_mapping_page(inode->i_mapping, 0, NULL);
		if (IS_ERR(cpage))
			return ERR_CAST(cpage);
		caddr = page_address(cpage);
		max_size = inode->i_sb->s_blocksize;
	}

	paddr = fscrypt_get_symlink(inode, caddr, max_size, done);
	if (cpage)
		put_page(cpage);
	return paddr;
}

const struct inode_operations ext4_encrypted_symlink_inode_operations = {
#ifdef MY_ABC_HERE
	.syno_getattr	= ext4_syno_getattr,
#endif /* MY_ABC_HERE */
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	.syno_set_archive_bit = ext4_syno_set_archive_bit,
#endif /* MY_ABC_HERE || MY_ABC_HERE */
#ifdef MY_ABC_HERE
	.syno_get_archive_version = ext4_syno_get_inode_archive_version,
	.syno_set_archive_version = ext4_syno_set_inode_archive_version,
#endif /* MY_ABC_HERE */
	.get_link	= ext4_encrypted_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
#ifdef MY_ABC_HERE
	.syno_get_crtime = ext4_syno_get_crtime,
	.syno_set_crtime = ext4_syno_set_crtime,
#endif /* MY_ABC_HERE */
};

const struct inode_operations ext4_symlink_inode_operations = {
#ifdef MY_ABC_HERE
	.syno_getattr	= ext4_syno_getattr,
#endif /* MY_ABC_HERE */
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	.syno_set_archive_bit = ext4_syno_set_archive_bit,
#endif /* MY_ABC_HERE || MY_ABC_HERE */
#ifdef MY_ABC_HERE
	.syno_get_archive_version = ext4_syno_get_inode_archive_version,
	.syno_set_archive_version = ext4_syno_set_inode_archive_version,
#endif /* MY_ABC_HERE */
	.get_link	= page_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
#ifdef MY_ABC_HERE
	.syno_get_crtime = ext4_syno_get_crtime,
	.syno_set_crtime = ext4_syno_set_crtime,
#endif /* MY_ABC_HERE */
};

const struct inode_operations ext4_fast_symlink_inode_operations = {
#ifdef MY_ABC_HERE
	.syno_getattr	= ext4_syno_getattr,
#endif /* MY_ABC_HERE */
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	.syno_set_archive_bit = ext4_syno_set_archive_bit,
#endif /* MY_ABC_HERE || MY_ABC_HERE */
#ifdef MY_ABC_HERE
	.syno_get_archive_version = ext4_syno_get_inode_archive_version,
	.syno_set_archive_version = ext4_syno_set_inode_archive_version,
#endif /* MY_ABC_HERE */
	.get_link	= simple_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
#ifdef MY_ABC_HERE
	.syno_get_crtime = ext4_syno_get_crtime,
	.syno_set_crtime = ext4_syno_set_crtime,
#endif /* MY_ABC_HERE */
};
#ifdef MY_ABC_HERE
const struct file_operations ext4_symlink_file_operations = {
	.unlocked_ioctl = ext4_symlink_ioctl,
};
#endif /* MY_ABC_HERE */
