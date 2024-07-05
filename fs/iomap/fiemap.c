#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018 Christoph Hellwig.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/fiemap.h>

struct fiemap_ctx {
	struct fiemap_extent_info *fi;
	struct iomap prev;
#ifdef MY_ABC_HERE
	struct syno_rbd_meta_ioctl_args *rbd_meta;
#endif /* MY_ABC_HERE */
};

#ifdef MY_ABC_HERE
#define VALID_FIEMAP_FLAG_ON_RBD_META	(FIEMAP_EXTENT_UNWRITTEN | \
					 FIEMAP_EXTENT_LAST)

static int rbd_meta_fill_next_extent(struct syno_rbd_meta_ioctl_args *args,
				     u64 next_start, u64 phys, u64 len,
				     u32 flags)
{
	unsigned int idx = args->cnt;
	unsigned int max_cnt;

	if (flags & ~(VALID_FIEMAP_FLAG_ON_RBD_META)) {
		printk(KERN_WARNING
		       "rbd meta with invalid fiemap flag %u\n", flags);
		return -EINVAL;
	}

	if (args->act == SYNO_RBD_META_MAPPING) {
		max_cnt = (args->size - sizeof(struct syno_rbd_meta_ioctl_args)) /
			  sizeof(struct syno_rbd_meta_file_mapping);
		if ((idx + 1) > max_cnt)
			return 1;
		args->mappings[idx].length = len;
		args->mappings[idx].dev_offset = phys;
	}

	if (flags & FIEMAP_EXTENT_LAST)
		args->start = (u64) -1;
	else
		args->start = next_start;
	args->cnt++;
	return 0;
}
#endif /* MY_ABC_HERE */

static int iomap_to_fiemap(struct fiemap_extent_info *fi,
		struct iomap *iomap, u32 flags
#ifdef MY_ABC_HERE
		, struct syno_rbd_meta_ioctl_args *rbd_meta
#endif /* MY_ABC_HERE */
		)
{
	switch (iomap->type) {
	case IOMAP_HOLE:
		/* skip holes */
		return 0;
	case IOMAP_DELALLOC:
		flags |= FIEMAP_EXTENT_DELALLOC | FIEMAP_EXTENT_UNKNOWN;
		break;
	case IOMAP_MAPPED:
		break;
	case IOMAP_UNWRITTEN:
		flags |= FIEMAP_EXTENT_UNWRITTEN;
		break;
	case IOMAP_INLINE:
		flags |= FIEMAP_EXTENT_DATA_INLINE;
		break;
	}

	if (iomap->flags & IOMAP_F_MERGED)
		flags |= FIEMAP_EXTENT_MERGED;
	if (iomap->flags & IOMAP_F_SHARED)
		flags |= FIEMAP_EXTENT_SHARED;

#ifdef MY_ABC_HERE
	if (!fi && !rbd_meta)
		return -EINVAL;
	else if (rbd_meta)
		return rbd_meta_fill_next_extent(rbd_meta,
				iomap->offset + iomap->length,
				iomap->addr != IOMAP_NULL_ADDR ? iomap->addr : 0,
				iomap->length, flags);
#endif /* MY_ABC_HERE */
	return fiemap_fill_next_extent(fi, iomap->offset,
			iomap->addr != IOMAP_NULL_ADDR ? iomap->addr : 0,
			iomap->length, flags);
}

static loff_t
iomap_fiemap_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap, struct iomap *srcmap)
{
	struct fiemap_ctx *ctx = data;
	loff_t ret = length;

	if (iomap->type == IOMAP_HOLE)
		return length;

	ret = iomap_to_fiemap(ctx->fi, &ctx->prev, 0
#ifdef MY_ABC_HERE
			, ctx->rbd_meta
#endif /* MY_ABC_HERE */
			);
	ctx->prev = *iomap;
	switch (ret) {
	case 0:		/* success */
		return length;
	case 1:		/* extent array full */
		return 0;
	default:
		return ret;
	}
}

int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fi,
		u64 start, u64 len, const struct iomap_ops *ops)
{
	struct fiemap_ctx ctx;
	loff_t ret;

	memset(&ctx, 0, sizeof(ctx));
	ctx.fi = fi;
	ctx.prev.type = IOMAP_HOLE;
#ifdef MY_ABC_HERE
	ctx.rbd_meta = NULL;
#endif /* MY_ABC_HERE */

	ret = fiemap_prep(inode, fi, start, &len, 0);
	if (ret)
		return ret;

	while (len > 0) {
		ret = iomap_apply(inode, start, len, IOMAP_REPORT, ops, &ctx,
				iomap_fiemap_actor);
		/* inode with no (attribute) mapping will give ENOENT */
		if (ret == -ENOENT)
			break;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		start += ret;
		len -= ret;
	}

	if (ctx.prev.type != IOMAP_HOLE) {
		ret = iomap_to_fiemap(fi, &ctx.prev, FIEMAP_EXTENT_LAST
#ifdef MY_ABC_HERE
				      , NULL
#endif /* MY_ABC_HERE */
				);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iomap_fiemap);

static loff_t
iomap_bmap_actor(struct inode *inode, loff_t pos, loff_t length,
		void *data, struct iomap *iomap, struct iomap *srcmap)
{
	sector_t *bno = data, addr;

	if (iomap->type == IOMAP_MAPPED) {
		addr = (pos - iomap->offset + iomap->addr) >> inode->i_blkbits;
		*bno = addr;
	}
	return 0;
}

/* legacy ->bmap interface.  0 is the error return (!) */
sector_t
iomap_bmap(struct address_space *mapping, sector_t bno,
		const struct iomap_ops *ops)
{
	struct inode *inode = mapping->host;
	loff_t pos = bno << inode->i_blkbits;
	unsigned blocksize = i_blocksize(inode);
	int ret;

	if (filemap_write_and_wait(mapping))
		return 0;

	bno = 0;
	ret = iomap_apply(inode, pos, blocksize, 0, ops, &bno,
			  iomap_bmap_actor);
	if (ret)
		return 0;
	return bno;
}
EXPORT_SYMBOL_GPL(iomap_bmap);

#ifdef MY_ABC_HERE
static int rbd_meta_map_prep(struct inode *inode,
			     struct syno_rbd_meta_ioctl_args *rbd_meta,
			     u64 start, u64 *len)
{
	u64 maxbytes = inode->i_sb->s_maxbytes;

	if (*len == 0 || start == (u64)-1)
		return -EINVAL;
	if (start > maxbytes)
		return -EFBIG;
	if (*len > maxbytes || (maxbytes - *len) < start)
		*len = maxbytes - start;

	return filemap_write_and_wait(inode->i_mapping);
}

int iomap_rbd_meta_map(struct inode *inode, struct syno_rbd_meta_ioctl_args *rbd_meta,
		       u64 start, u64 len, const struct iomap_ops *ops)
{
	struct fiemap_ctx ctx;
	loff_t ret;

	memset(&ctx, 0, sizeof(ctx));
	ctx.fi = NULL;
	ctx.rbd_meta = rbd_meta;
	ctx.prev.type = IOMAP_HOLE;

	ret = rbd_meta_map_prep(inode, rbd_meta, start, &len);
	if (ret)
		return ret;

	while (len > 0) {
		ret = iomap_apply(inode, start, len, IOMAP_REPORT, ops, &ctx,
				iomap_fiemap_actor);
		/* inode with no (attribute) mapping will give ENOENT */
		if (ret == -ENOENT)
			break;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		start += ret;
		len -= ret;
	}

	if (ctx.prev.type != IOMAP_HOLE) {
		ret = iomap_to_fiemap(NULL, &ctx.prev, FIEMAP_EXTENT_LAST, rbd_meta);
		if (ret < 0)
			return ret;
	}

	return 0;
}
#endif /* MY_ABC_HERE */

