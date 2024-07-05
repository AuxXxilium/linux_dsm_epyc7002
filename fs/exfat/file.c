#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

#include "exfat_fs.h"
#ifdef MY_ABC_HERE
#include <linux/syno.h>
#include <linux/stat.h>
#endif /* MY_ABC_HERE */

static int exfat_cont_expand(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t start = i_size_read(inode), count = size - i_size_read(inode);
	int err, err2;

	err = generic_cont_expand_simple(inode, size);
	if (err)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	inode->i_ctime = inode->i_mtime = current_time(inode);
#else
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
#endif
	mark_inode_dirty(inode);

	if (!IS_SYNC(inode))
		return 0;

	err = filemap_fdatawrite_range(mapping, start, start + count - 1);
	err2 = sync_mapping_buffers(mapping);
	if (!err)
		err = err2;
	err2 = write_inode_now(inode, 1);
	if (!err)
		err = err2;
	if (err)
		return err;

	return filemap_fdatawait_range(mapping, start, start + count - 1);
}

static bool exfat_allow_set_time(struct exfat_sb_info *sbi, struct inode *inode)
{
	mode_t allow_utime = sbi->options.allow_utime;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	if (!uid_eq(current_fsuid(), inode->i_uid)) {
#else
	if (current_fsuid() != inode->i_uid) {
#endif
		if (in_group_p(inode->i_gid))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return true;
	}

	/* use a default check */
	return false;
}

static int exfat_sanitize_mode(const struct exfat_sb_info *sbi,
		struct inode *inode, umode_t *mode_ptr)
{
	mode_t i_mode, mask, perm;

	i_mode = inode->i_mode;

	mask = (S_ISREG(i_mode) || S_ISLNK(i_mode)) ?
		sbi->options.fs_fmask : sbi->options.fs_dmask;
	perm = *mode_ptr & ~(S_IFMT | mask);

	/* Of the r and x bits, all (subject to umask) must be present.*/
	if ((perm & 0555) != (i_mode & 0555))
		return -EPERM;

	if (exfat_mode_can_hold_ro(inode)) {
		/*
		 * Of the w bits, either all (subject to umask) or none must
		 * be present.
		 */
		if ((perm & 0222) && ((perm & 0222) != (0222 & ~mask)))
			return -EPERM;
	} else {
		/*
		 * If exfat_mode_can_hold_ro(inode) is false, can't change
		 * w bits.
		 */
		if ((perm & 0222) != (0222 & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

/* resize the file length */
int __exfat_truncate(struct inode *inode, loff_t new_size)
{
	unsigned int num_clusters_new, num_clusters_phys;
	unsigned int last_clu = EXFAT_FREE_CLUSTER;
	struct exfat_chain clu;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	int evict = (ei->dir.dir == DIR_DELETED) ? 1 : 0;

	/* check if the given file ID is opened */
	if (ei->type != TYPE_FILE && ei->type != TYPE_DIR)
		return -EPERM;

	exfat_set_volume_dirty(sb);

	num_clusters_new = EXFAT_B_TO_CLU_ROUND_UP(i_size_read(inode), sbi);
	num_clusters_phys =
		EXFAT_B_TO_CLU_ROUND_UP(EXFAT_I(inode)->i_size_ondisk, sbi);

	exfat_chain_set(&clu, ei->start_clu, num_clusters_phys, ei->flags);

	if (new_size > 0) {
		/*
		 * Truncate FAT chain num_clusters after the first cluster
		 * num_clusters = min(new, phys);
		 */
		unsigned int num_clusters =
			min(num_clusters_new, num_clusters_phys);

		/*
		 * Follow FAT chain
		 * (defensive coding - works fine even with corrupted FAT table
		 */
		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			clu.dir += num_clusters;
			clu.size -= num_clusters;
		} else {
			while (num_clusters > 0) {
				last_clu = clu.dir;
				if (exfat_get_next_cluster(sb, &(clu.dir)))
					return -EIO;

				num_clusters--;
				clu.size--;
			}
		}
	} else {
		ei->flags = ALLOC_NO_FAT_CHAIN;
		ei->start_clu = EXFAT_EOF_CLUSTER;
	}

	i_size_write(inode, new_size);

	if (ei->type == TYPE_FILE)
		ei->attr |= ATTR_ARCHIVE;

	/* update the directory entry */
	if (!evict) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
		struct timespec64 ts;
#else
		struct timespec ts;
#endif
		struct exfat_dentry *ep, *ep2;
		struct exfat_entry_set_cache *es;
		int err;

		es = exfat_get_dentry_set(sb, &(ei->dir), ei->entry,
				ES_ALL_ENTRIES);
		if (!es)
			return -EIO;
		ep = exfat_get_dentry_cached(es, 0);
		ep2 = exfat_get_dentry_cached(es, 1);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
		ts = current_time(inode);
#else
		ts = CURRENT_TIME_SEC;
#endif
		exfat_set_entry_time(sbi, &ts,
				&ep->dentry.file.modify_tz,
				&ep->dentry.file.modify_time,
				&ep->dentry.file.modify_date,
				&ep->dentry.file.modify_time_cs);
		ep->dentry.file.attr = cpu_to_le16(ei->attr);

		/* File size should be zero if there is no cluster allocated */
		if (ei->start_clu == EXFAT_EOF_CLUSTER) {
			ep2->dentry.stream.valid_size = 0;
			ep2->dentry.stream.size = 0;
		} else {
			ep2->dentry.stream.valid_size = cpu_to_le64(new_size);
			ep2->dentry.stream.size = ep2->dentry.stream.valid_size;
		}

		if (new_size == 0) {
			/* Any directory can not be truncated to zero */
			WARN_ON(ei->type != TYPE_FILE);

			ep2->dentry.stream.flags = ALLOC_FAT_CHAIN;
			ep2->dentry.stream.start_clu = EXFAT_FREE_CLUSTER;
		}

		exfat_update_dir_chksum_with_entry_set(es);
		err = exfat_free_dentry_set(es, inode_needs_sync(inode));
		if (err)
			return err;
	}

	/* cut off from the FAT chain */
	if (ei->flags == ALLOC_FAT_CHAIN && last_clu != EXFAT_FREE_CLUSTER &&
			last_clu != EXFAT_EOF_CLUSTER) {
		if (exfat_ent_set(sb, last_clu, EXFAT_EOF_CLUSTER))
			return -EIO;
	}

	/* invalidate cache and free the clusters */
	/* clear exfat cache */
	exfat_cache_inval_inode(inode);

	/* hint information */
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->hint_bmap.clu = EXFAT_EOF_CLUSTER;
	if (ei->rwoffset > new_size)
		ei->rwoffset = new_size;

	/* hint_stat will be used if this is directory. */
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = ei->start_clu;
	ei->hint_femp.eidx = EXFAT_HINT_NONE;

	/* free the clusters */
	if (exfat_free_cluster(inode, &clu))
		return -EIO;
#ifdef MY_ABC_HERE
	if (sbi->vol_clean_on_mount)
#endif /* MY_ABC_HERE */
	exfat_clear_volume_dirty(sb);

	return 0;
}

void exfat_truncate(struct inode *inode, loff_t size)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned int blocksize = 1 << inode->i_blkbits;
	loff_t aligned_size;
	int err;

	__lock_super(sb);
	if (EXFAT_I(inode)->start_clu == 0) {
		/*
		 * Empty start_clu != ~0 (not allocated)
		 */
		exfat_fs_error(sb, "tried to truncate zeroed cluster.");
		goto write_size;
	}

	err = __exfat_truncate(inode, i_size_read(inode));
	if (err)
		goto write_size;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	inode->i_ctime = inode->i_mtime = current_time(inode);
#else
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
#endif
	if (IS_DIRSYNC(inode))
		exfat_sync_inode(inode);
	else
		mark_inode_dirty(inode);

	inode->i_blocks = ((i_size_read(inode) + (sbi->cluster_size - 1)) &
			~(sbi->cluster_size - 1)) >> inode->i_blkbits;
write_size:
	aligned_size = i_size_read(inode);
	if (aligned_size & (blocksize - 1)) {
		aligned_size |= (blocksize - 1);
		aligned_size++;
	}

	if (EXFAT_I(inode)->i_size_ondisk > i_size_read(inode))
		EXFAT_I(inode)->i_size_ondisk = aligned_size;

	if (EXFAT_I(inode)->i_size_aligned > i_size_read(inode))
		EXFAT_I(inode)->i_size_aligned = aligned_size;
	__unlock_super(sb);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
int exfat_getattr(const struct path *path, struct kstat *stat,
		unsigned int request_mask, unsigned int query_flags)
{
	struct inode *inode = d_backing_inode(path->dentry);
	struct exfat_inode_info *ei = EXFAT_I(inode);

	generic_fillattr(inode, stat);
	exfat_truncate_atime(&stat->atime);
	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = ei->i_crtime.tv_sec;
	stat->btime.tv_nsec = ei->i_crtime.tv_nsec;
	stat->blksize = EXFAT_SB(inode->i_sb)->cluster_size;
	return 0;
}
#else
int exfat_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;

	generic_fillattr(inode, stat);
	exfat_truncate_atime(&stat->atime);
	stat->blksize = EXFAT_SB(inode->i_sb)->cluster_size;

	return 0;
}
#endif

int exfat_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct exfat_sb_info *sbi = EXFAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid;
	int error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > i_size_read(inode)) {
		error = exfat_cont_expand(inode, attr->ia_size);
		if (error || attr->ia_valid == ATTR_SIZE)
			goto out;
		attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Check for setting the inode time. */
	ia_valid = attr->ia_valid;
	if ((ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) &&
	    exfat_allow_set_time(sbi, inode)) {
		attr->ia_valid &= ~(ATTR_MTIME_SET | ATTR_ATIME_SET |
				ATTR_TIMES_SET);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)
	error = setattr_prepare(dentry, attr);
#else
	error = inode_change_ok(inode, attr);
#endif
	attr->ia_valid = ia_valid;
	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

	if (((attr->ia_valid & ATTR_UID) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	     !uid_eq(attr->ia_uid, sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     !gid_eq(attr->ia_gid, sbi->options.fs_gid)) ||
#else
		 (attr->ia_uid != sbi->options.fs_uid)) ||
		((attr->ia_valid & ATTR_GID) &&
		 (attr->ia_gid != sbi->options.fs_gid)) ||
#endif
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~(S_IFREG | S_IFLNK | S_IFDIR | 0777)))) {
		error = -EPERM;
#ifdef MY_ABC_HERE
		if (sbi->options.quiet)
			error = 0;
#endif /* MY_ABC_HERE */
		goto out;
	}

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

#ifdef MY_ABC_HERE
	if (attr->ia_valid & (ATTR_MTIME_SET | ATTR_MTIME)) {
		attr->ia_valid |= ATTR_CTIME;
		attr->ia_ctime = attr->ia_mtime;
	}
#endif /* MY_ABC_HERE */

	/*
	 * We don't return -EPERM here. Yes, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (exfat_sanitize_mode(sbi, inode, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	if (attr->ia_valid & ATTR_SIZE) {
		error = exfat_block_truncate_page(inode, attr->ia_size);
		if (error)
			goto out;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
		down_write(&EXFAT_I(inode)->truncate_lock);
		truncate_setsize(inode, attr->ia_size);
		exfat_truncate(inode, attr->ia_size);
		up_write(&EXFAT_I(inode)->truncate_lock);
#else
		truncate_setsize(inode, attr->ia_size);
		exfat_truncate(inode, attr->ia_size);
#endif
	}

	setattr_copy(inode, attr);
#ifdef MY_ABC_HERE
	EXFAT_I(inode)->attr = exfat_make_attr(inode);
#endif
	exfat_truncate_atime(&inode->i_atime);
	mark_inode_dirty(inode);

out:
	return error;
}

int exfat_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	int err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	err = __generic_file_fsync(filp, start, end, datasync);
#else
	err = generic_file_fsync(filp, start, end, datasync);
#endif
	if (err)
		return err;

	err = sync_blockdev(inode->i_sb->s_bdev);
	if (err)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL);
#else
	return blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
#endif
}

#ifdef MY_ABC_HERE
void exfat_attr_to_syno_archive_bit(unsigned int *archive_bit,
			u32 exfat_attr)
{
	*archive_bit = 0;

	if (exfat_attr & ATTR_READONLY)
		*archive_bit |= S2_SMB_READONLY;
	if (exfat_attr & ATTR_HIDDEN)
		*archive_bit |= S2_SMB_HIDDEN;
	if (exfat_attr & ATTR_SYSTEM)
		*archive_bit |= S2_SMB_SYSTEM;
	if (exfat_attr & ATTR_ARCHIVE)
		*archive_bit |= S2_SMB_ARCHIVE;

	return;
}

void syno_archive_bit_to_exfat_attr(unsigned int archive_bit,
			u32 *exfat_attr)
{
	u32 final_exfat_arbit = 0;

	if (archive_bit & S2_SMB_HIDDEN)
		final_exfat_arbit |= ATTR_HIDDEN;
	if (archive_bit & S2_SMB_SYSTEM)
		final_exfat_arbit |= ATTR_SYSTEM;
	if (archive_bit & S2_SMB_ARCHIVE)
		final_exfat_arbit |= ATTR_ARCHIVE;
	if (archive_bit & S2_SMB_READONLY)
		final_exfat_arbit |= ATTR_READONLY;

	*exfat_attr &= ~(ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE | ATTR_READONLY);
	*exfat_attr |= final_exfat_arbit;

	return;
}

static int exfat_syno_setattr(struct inode *inode, u32 new_attr)
{
	u32 type;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_entry_set_cache *es = NULL;
	struct exfat_dentry *ep = NULL;

	if (new_attr == exfat_make_attr(inode)) {
		return 0;
	}

	if (ei->type == TYPE_DIR &&
			ei->dir.dir == sbi->root_dir && ei->entry == -1) {
		return 0;
	}

	/* get the directory entry of given file */
	es = exfat_get_dentry_set(sb, &(ei->dir), ei->entry, ES_ALL_ENTRIES);
	if (!es)
		return -EIO;
	ep = exfat_get_dentry_cached(es, 0);
	type = exfat_get_entry_type(ep);

	if (((type == TYPE_FILE) && (new_attr & ATTR_SUBDIR)) ||
		((type == TYPE_DIR) && (!(new_attr & ATTR_SUBDIR)))) {
		exfat_free_dentry_set(es, false);
		return -EINVAL;
	}

	exfat_set_volume_dirty(sb);

	/* set the file attribute */
	exfat_save_attr(inode, new_attr);

	exfat_update_dir_chksum_with_entry_set(es);
	exfat_free_dentry_set(es, inode_needs_sync(inode));

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
int exfat_syno_get_archive_bit(struct dentry *dentry,
			unsigned int *archive_bit, int may_not_block)
#else
int exfat_syno_get_archive_bit(struct dentry *dentry,
			unsigned int *archive_bit)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
{
	if (!dentry || !dentry->d_inode)
		return -EINVAL;

	exfat_attr_to_syno_archive_bit(archive_bit, exfat_make_attr(dentry->d_inode));

	return 0;
}

int exfat_syno_set_archive_bit(struct dentry *dentry,
			unsigned int archive_bit)
{
	int ret = 0;
	u32 exfat_attr = 0;
	u32 old_exfat_attr = 0;
	struct super_block *sb = NULL;
	struct inode *inode = NULL;
	struct exfat_sb_info *sbi = NULL;

	if (!dentry || !dentry->d_inode)
		return -EINVAL;

	inode = dentry->d_inode;
	sb = inode->i_sb;
	sbi = EXFAT_SB(sb);

	__lock_super(sb);

	old_exfat_attr = exfat_attr = exfat_make_attr(inode);
	syno_archive_bit_to_exfat_attr(archive_bit, &exfat_attr);

	if (old_exfat_attr == exfat_attr)
		goto out;

	if (exfat_syno_setattr(inode, exfat_attr))
		ret = -EIO;
	inode->i_mode = exfat_make_mode(sbi, exfat_attr, S_IRWXUGO);

	if (IS_DIRSYNC(inode))
		exfat_sync_inode(inode);
	else
		mark_inode_dirty(inode);
out:
	if (sb)
		__unlock_super(sb);

	return ret;
}

#if defined(CONFIG_SYNO_FS_WINACL) || defined(SYNO_FS_SYNO_ACL)
int exfat_syno_arbit_chg_ok(struct dentry *dentry,
				    unsigned int cmd, int tag, int mask)
{
	if (cmd != F_SETSMB_ARCHIVE && cmd != F_CLRSMB_ARCHIVE &&
	    cmd != F_SETSMB_HIDDEN && cmd != F_CLRSMB_HIDDEN &&
	    cmd != F_SETSMB_READONLY && cmd != F_CLRSMB_READONLY &&
	    cmd != F_SETSMB_SYSTEM && cmd != F_CLRSMB_SYSTEM)
		return -EPERM;

	return 0;
}
#endif /* CONFIG_SYNO_FS_WINACL || SYNO_FS_SYNO_ACL */
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
int exfat_syno_getattr(struct dentry *dentry, struct kstat *kstat, unsigned int syno_flags)
#else
int exfat_syno_getattr(struct dentry *dentry, struct kstat *kst, int flags)
#endif
{
	int ret = 0;
	struct inode *inode;

	if (!dentry || !dentry->d_inode)
		return -EINVAL;

	inode = dentry->d_inode;
#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	if (syno_flags & SYNOST_ARCHIVE_BIT)
		exfat_attr_to_syno_archive_bit(&kstat->syno_archive_bit, exfat_make_attr(inode));
#else
	if (flags & SYNOST_ARCHIVE_BIT)
		exfat_attr_to_syno_archive_bit(&kst->syno_archive_bit, exfat_make_attr(inode));
#endif
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	if (syno_flags & SYNOST_CREATE_TIME)
		kstat->syno_create_time = EXFAT_I(dentry->d_inode)->i_crtime;
#else
	if (flags & SYNOST_CREATE_TIME)
		kst->syno_create_time = dentry->d_inode->i_create_time;
#endif
#endif /* MY_ABC_HERE */
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
int exfat_syno_get_create_time(struct inode *inode, struct timespec64* time)
{
	*time = EXFAT_I(inode)->i_crtime;

	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
int exfat_syno_set_create_time(struct inode *inode, struct timespec64* time) {
#else
int exfat_syno_set_create_time(struct dentry *dentry, struct timespec* time) {
#endif
	struct super_block *sb = NULL;
	struct exfat_inode_info *ei = NULL;
	struct exfat_sb_info *sbi = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	if (!inode)
		return -EINVAL;
#else
	struct inode *inode = NULL;

	if (!dentry || !dentry->d_inode)
		return -EINVAL;
	inode = dentry->d_inode;
#endif

	sb = inode->i_sb;
	ei = EXFAT_I(inode);
	sbi = EXFAT_SB(sb);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	ei->i_crtime = timestamp_truncate(*time, inode);
	inode->i_ctime = current_time(inode);
#else
	inode->i_create_time = ei->i_crtime = timespec_trunc(*time, inode->i_sb->s_time_gran);
	inode->i_ctime = CURRENT_TIME;
#endif
	mark_inode_dirty(inode);

	return 0;
}
#endif /* MY_ABC_HERE */

const struct file_operations exfat_file_operations = {
	.llseek		= generic_file_llseek,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
	.read		= new_sync_read,
	.write		= new_sync_write,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
#endif
	.mmap		= generic_file_mmap,
	.fsync		= exfat_file_fsync,
	.splice_read	= generic_file_splice_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	.splice_write	= iter_file_splice_write,
#endif
};

const struct inode_operations exfat_file_inode_operations = {
	.setattr     = exfat_setattr,
	.getattr     = exfat_getattr,
#ifdef CONFIG_EXFAT_VIRTUAL_XATTR
	.listxattr      = exfat_listxattr,
#endif
#ifdef MY_ABC_HERE
	.syno_getattr = exfat_syno_getattr,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	.syno_get_archive_bit = exfat_syno_get_archive_bit,
	.syno_set_archive_bit = exfat_syno_set_archive_bit,
#if defined(CONFIG_SYNO_FS_WINACL) || defined(SYNO_FS_SYNO_ACL)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	.syno_archive_bit_change_ok = exfat_syno_arbit_chg_ok,
#else
	.syno_arbit_chg_ok = exfat_syno_arbit_chg_ok,
#endif
#endif /* CONFIG_SYNO_FS_WINACL || SYNO_FS_SYNO_ACL */
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	.syno_get_crtime = exfat_syno_get_create_time,
#endif
	.syno_set_crtime = exfat_syno_set_create_time,
#endif /* MY_ABC_HERE */
};
