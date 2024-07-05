#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#ifndef _EXFAT_COMPAT_H
#define _EXFAT_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
#include <linux/iversion.h>
#else
#define inode_inc_iversion(inode) (inode->i_version++)
#define inode_query_iversion(inode) (inode->i_version)
#define inode_eq_iversion(inode, version) (inode->i_version == version)
#define inode_peek_iversion_raw(inode) (inode->i_version)
#define inode_set_iversion(inode, val) (inode->i_version = val)
#endif

#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,11,0)
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void * kvmalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kmalloc(size, flags | __GFP_NOWARN);
	if (!ret) {
		ret = __vmalloc(size, flags, PAGE_KERNEL);
	}
	return ret;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,3,0)
static inline void kvfree(void *ptr)
{
	if (is_vmalloc_addr(ptr)) {
		vfree(ptr);
	} else {
		kfree(ptr);
	}
}
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(3,3,0) */
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(3,11,0) */
#endif /* MY_ABC_HERE */

/* MS flags were renamed to SB on v4.15 */
#ifndef SB_NODIRATIME
#define SB_NODIRATIME MS_NODIRATIME
#endif

#ifndef SB_RDONLY
#define SB_RDONLY MS_RDONLY
#endif

#ifndef SB_SYNCHRONOUS
#define SB_SYNCHRONOUS MS_SYNCHRONOUS
#endif

#ifndef sb_rdonly
#define sb_rdonly(sb) ((sb)->s_flags & SB_RDONLY)
#endif

#endif /* _EXFAT_COMPAT_H */
