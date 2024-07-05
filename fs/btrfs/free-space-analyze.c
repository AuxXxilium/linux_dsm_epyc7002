/*
 * Copyright (C) 2020 Synology Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#include <linux/kernel.h>
#include <linux/bits.h>
#include "ctree.h"
#include "space-info.h"
#include "block-group.h"
#include "free-space-cache.h"
#include "free-space-tree.h"

#define BITS_PER_BITMAP (PAGE_SIZE * BITS_PER_BYTE)
#define PAGE_SHIFT_4K  12

static inline void
add_to_results(struct btrfs_ioctl_free_space_analyze_args *args,
	       u64 total_size, u32 count)
{
	u64 interval_size = div_u64(total_size, count);
	u64 tmp_size = interval_size >> PAGE_SHIFT_4K;
	int index = 0;

	if (0 == tmp_size)
		return;

	if (interval_size >= args->min_continuous_size) {
		args->total_continuous_size += total_size;
		args->continuous_cnts += count;
	} else {
		args->total_frag_size += total_size;
		args->frag_cnts += count;
	}

	index = fls64(tmp_size) - 1;
	ASSERT(index >= 0);

	if (index >= BTRFS_FREE_SPACE_ANALYZE_NR_INTERVAL)
		return;

	args->interval_cnts[index] += count;
}

int btrfs_free_space_analyze(struct btrfs_fs_info *fs_info,
			     struct btrfs_ioctl_free_space_analyze_args *args)
{
	int ret = -1;
	int index, count, cached;
	u64 total_free;
	struct btrfs_space_info *sinfo = fs_info->data_sinfo;
	struct btrfs_block_group *bg = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_free_space_info *info = NULL;
	struct btrfs_key searching_key;
	struct btrfs_root *free_space_root = fs_info->free_space_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	memset(args->interval_cnts, 0, sizeof(args->interval_cnts));

	down_read(&sinfo->groups_sem);
	for (index = 0; index < BTRFS_NR_RAID_TYPES; ++index) {
		list_for_each_entry(bg, &sinfo->block_groups[index], list) {
			// Get total_free and count from cache or on-disk structure
			spin_lock(&bg->lock);
			cached = bg->cached;

			if (!(bg->flags & BTRFS_BLOCK_GROUP_DATA)) {
				spin_unlock(&bg->lock);
				cond_resched();
				continue;
			}
			// For total free space size
			total_free = bg->length - bg->used - bg->bytes_super;
			spin_unlock(&bg->lock);

			// For extent count
			if (cached == BTRFS_CACHE_FINISHED) {
				spin_lock(&bg->free_space_ctl->tree_lock);
				total_free = bg->free_space_ctl->free_space;
				if (bg->free_space_ctl->total_bitmaps == 0)
					count = bg->free_space_ctl->free_extents;
				spin_unlock(&bg->free_space_ctl->tree_lock);
				goto update;
			}

			searching_key.objectid = bg->start;
			searching_key.type = BTRFS_FREE_SPACE_INFO_KEY;
			searching_key.offset = bg->length;

			ret = btrfs_search_slot(NULL, free_space_root,
						&searching_key, path, 0, 0);
			if (ret < 0) {
				up_read(&sinfo->groups_sem);
				goto out;
			} else if (ret > 0) {
				btrfs_release_path(path);
				cond_resched();
				continue;
			}

			info = btrfs_item_ptr(path->nodes[0], path->slots[0],
					      struct btrfs_free_space_info);

			count = btrfs_free_space_extent_count(path->nodes[0], info);

			btrfs_release_path(path);
update:
			cond_resched();
			// Calculate averge_size and add it to results
			if (total_free < 0 || count == 0)
				continue;
			add_to_results(args, total_free, count);
		}
	}
	up_read(&sinfo->groups_sem);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static void
add_free_space_to_result(struct btrfs_free_space *free_space,
			 int unit,
			 struct btrfs_ioctl_free_space_analyze_args *args)
{
	unsigned long i = 0;
	unsigned long next_zero;
	u64 bytes;

	if (free_space->bitmap) {
		for_each_set_bit_from(i, free_space->bitmap, BITS_PER_BITMAP) {
			next_zero = find_next_zero_bit(free_space->bitmap,
						       BITS_PER_BITMAP, i);
			bytes = (next_zero - i) * unit;
			add_to_results(args, bytes, 1);
			i = next_zero;
		}
	} else {
		add_to_results(args, free_space->bytes, 1);
	}
}

static void
scan_block_group_free_space_on_cache(struct btrfs_block_group *bg,
				     struct btrfs_ioctl_free_space_analyze_args *args)
{
	struct btrfs_free_space_ctl *ctl = bg->free_space_ctl;
	struct btrfs_free_cluster *cluster = NULL;
	struct btrfs_free_space *free_space;
	struct rb_node *n;

	/*
	 * We should hold the tree lock until we finished scanning cluster.
	 * Otherwise, the cluster may have returned free space before scanned.
	 */
	spin_lock(&ctl->tree_lock);
	for (n = rb_first(&ctl->free_space_offset); n; n = rb_next(n)) {
		free_space = rb_entry(n, struct btrfs_free_space, offset_index);
		add_free_space_to_result(free_space, ctl->unit, args);
	}

	/* Get the cluster for this block_group if it exists */
	if (list_empty(&bg->cluster_list))
		goto out;

	cluster = list_entry(bg->cluster_list.next,
			     struct btrfs_free_cluster,
			     block_group_list);
	if (unlikely(!cluster))
		goto out;

	spin_lock(&cluster->lock);
	for (n = rb_first(&cluster->root); n; n = rb_next(n)) {
		free_space = rb_entry(n, struct btrfs_free_space, offset_index);
		add_free_space_to_result(free_space, bg->free_space_ctl->unit, args);
	}
	spin_unlock(&cluster->lock);
out:
	spin_unlock(&ctl->tree_lock);
}

static void
scan_free_space_bitmap_on_disk(struct btrfs_key *key,
			       struct btrfs_path *path,
			       u32 sectorsize,
			       struct btrfs_ioctl_free_space_analyze_args *args)
{
	int prev_bit = 0, bit;
	u64 extent_start = 0;
	u64 offset = key->objectid;
	u64 end = key->objectid + key->offset;
	unsigned long ptr;
	unsigned long i = 0;

	ptr = btrfs_item_ptr_offset(path->nodes[0], path->slots[0]);

	while (offset < end) {
		bit = !!extent_buffer_test_bit(path->nodes[0], ptr, i);

		if (prev_bit == 0 && bit == 1)
			extent_start = offset;
		else if (prev_bit == 1 && bit == 0)
			add_to_results(args, offset - extent_start, 1);

		++i;
		prev_bit = bit;
		offset += sectorsize;
	}

	if (prev_bit == 1)
		add_to_results(args, end - extent_start, 1);
}

static int
scan_block_group_free_space_on_disk(struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group *bg,
				    struct btrfs_ioctl_free_space_analyze_args *args)
{
	int ret = -1;
	u64 end;
	struct btrfs_path *path = NULL;
	struct btrfs_key searching_key;
	struct btrfs_key item_key;
	struct btrfs_root *free_space_root = fs_info->free_space_root;

	if (!free_space_root)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	searching_key.objectid = bg->start;
	searching_key.type = BTRFS_FREE_SPACE_INFO_KEY;
	searching_key.offset = bg->length;

	ret = btrfs_search_slot(NULL, free_space_root, &searching_key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = 0;
		goto out;
	}

	end = searching_key.objectid + searching_key.offset;
	while (1) {
		ret = btrfs_next_item(free_space_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;

		btrfs_item_key_to_cpu(path->nodes[0], &item_key, path->slots[0]);
		if (item_key.type == BTRFS_FREE_SPACE_INFO_KEY ||
		    item_key.objectid >= end ||
		    (item_key.objectid + item_key.offset) > end)
			break;

		if (BTRFS_FREE_SPACE_EXTENT_KEY == item_key.type)
			add_to_results(args, item_key.offset, 1);
		else if (BTRFS_FREE_SPACE_BITMAP_KEY == item_key.type)
			scan_free_space_bitmap_on_disk(&item_key, path,
						       fs_info->sectorsize,
						       args);
		else {
			btrfs_warn(fs_info,
				  "invalid key type %u "
				  "on block group offset %llu, length %llu",
				  item_key.type, bg->start, bg->length);
			goto out;
		}
	}

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_free_space_analyze_full(struct btrfs_fs_info *fs_info,
				  struct btrfs_ioctl_free_space_analyze_args *args)
{
	int ret = -1;
	int index;
	struct btrfs_space_info *sinfo = fs_info->data_sinfo;
	struct btrfs_block_group *bg = NULL;

	memset(args->interval_cnts, 0, sizeof(args->interval_cnts));

	down_read(&sinfo->groups_sem);
	for (index = 0; index < BTRFS_NR_RAID_TYPES; ++index) {
		list_for_each_entry(bg, &sinfo->block_groups[index], list) {
			int cached;

			spin_lock(&bg->lock);
			cached = bg->cached;

			if (!(bg->flags & BTRFS_BLOCK_GROUP_DATA)) {
				spin_unlock(&bg->lock);
				cond_resched();
				continue;
			}
			spin_unlock(&bg->lock);

			if (cached == BTRFS_CACHE_FINISHED) {
				scan_block_group_free_space_on_cache(bg, args);
			} else {
				ret = scan_block_group_free_space_on_disk(
						fs_info, bg, args);
				if (ret < 0) {
					up_read(&sinfo->groups_sem);
					goto out;
				}
			}
			cond_resched();
		}
	}
	up_read(&sinfo->groups_sem);

	ret = 0;
out:
	return ret;
}

