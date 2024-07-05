#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2019 Synology Inc.  All rights reserved.
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
#include <linux/uuid.h>
#include "ctree.h"
#include "disk-io.h"
#include "locking.h"
#include "space-info.h"
#include "transaction.h"

static inline int syno_usage_need_stop(struct btrfs_fs_info *fs_info)
{
	return sb_rdonly(fs_info->sb) || btrfs_fs_closing(fs_info);
}

static int btrfs_clean_tree_by_root_throttle(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	int ret;
	int nr = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.offset = 0;
	key.type = 0;

	while (1) {
		trans = btrfs_start_transaction_fallback_global_rsv(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
		nr = btrfs_header_nritems(path->nodes[0]);
		if (!nr)
			break;
		/*
		 * delete the leaf one by one
		 * since the whole tree is going
		 * to be deleted.
		 */
		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto out;

		if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_DISABLE) {
			fs_info->syno_usage_status.cur_full_rescan_size += nr;
			if (fs_info->syno_usage_status.cur_full_rescan_size > fs_info->syno_usage_status.total_full_rescan_size)
				fs_info->syno_usage_status.total_full_rescan_size = fs_info->syno_usage_status.cur_full_rescan_size;
		}

		btrfs_release_path(path);
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
		if (syno_usage_need_stop(fs_info)) {
			btrfs_debug(fs_info, "drop tree early exit for syno usage");
			ret = -EAGAIN;
			goto out;
		}
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();
	}
	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	return ret;
}

static int usage_update_helper(struct btrfs_trans_handle *trans,
						   struct btrfs_root *root,
						   struct btrfs_path *path,
						   struct btrfs_key *key,
						   u32 extra_size)
{
	int ret;
	int orig_search_for_extension = path->search_for_extension;

	path->search_for_extension = 1;
	ret = btrfs_search_slot(trans, root, key, path, extra_size, 1);
	path->search_for_extension = orig_search_for_extension;

	if (ret > 0) {
		btrfs_release_path(path);
		// insert new item
		ret = btrfs_insert_empty_item(trans, root, path, key, extra_size);
	}
	return ret;
}

int btrfs_syno_usage_status_update(struct btrfs_trans_handle *trans)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int extra_size;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_status_item *ei;
	struct btrfs_disk_key rescan_progress_disk_key;

	if (!root)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
	    fs_info->syno_usage_status.state != SYNO_USAGE_STATE_DISABLE)
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->leave_spinning = 1;

	key.objectid = 0;
	key.type = SYNO_BTRFS_USAGE_STATUS_KEY;
	key.offset = 0;

	extra_size = sizeof(struct btrfs_syno_usage_status_item);
	ret = usage_update_helper(trans, root, path, &key, extra_size);

	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_status_item);
	btrfs_set_syno_usage_status_version(leaf, ei, fs_info->syno_usage_status.version);
	btrfs_set_syno_usage_status_state(leaf, ei, fs_info->syno_usage_status.state);
	btrfs_set_syno_usage_status_flags(leaf, ei, fs_info->syno_usage_status.flags);
	btrfs_set_syno_usage_status_generation(leaf, ei, trans->transid);

	btrfs_cpu_key_to_disk(&rescan_progress_disk_key, &fs_info->syno_usage_status.extent_rescan_progress);
	btrfs_set_syno_usage_status_extent_rescan_progress_key(leaf, ei, &rescan_progress_disk_key);

	btrfs_set_syno_usage_status_cur_full_rescan_size(leaf, ei, fs_info->syno_usage_status.cur_full_rescan_size);
	btrfs_set_syno_usage_status_total_full_rescan_size(leaf, ei, fs_info->syno_usage_status.total_full_rescan_size);
	btrfs_set_syno_usage_status_extent_tree_cur_rescan_size(leaf, ei, fs_info->syno_usage_status.extent_tree_cur_rescan_size);
	btrfs_set_syno_usage_status_extent_tree_total_rescan_size(leaf, ei, fs_info->syno_usage_status.extent_tree_total_rescan_size);
	btrfs_set_syno_usage_status_total_syno_extent_tree_items(leaf, ei, fs_info->syno_usage_status.total_syno_extent_tree_items);
	btrfs_set_syno_usage_status_total_syno_subvol_usage_items(leaf, ei, fs_info->syno_usage_status.total_syno_subvol_usage_items);

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to update syno usage status");
	}
	return ret;
}

static int __btrfs_syno_usage_global_type_update(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info, int type, u64 num_bytes)
{
	int ret;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int extra_size;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_global_type_item *ei;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;
	if (type >= SYNO_USAGE_TYPE_MAX)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->leave_spinning = 1;

	key.objectid = 0;
	key.type = SYNO_BTRFS_USAGE_GLOBAL_TYPE_KEY;
	key.offset = type;

	extra_size = sizeof(struct btrfs_syno_usage_global_type_item);
	ret = usage_update_helper(trans, root, path, &key, extra_size);

	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_global_type_item);
	btrfs_set_syno_usage_global_type_num_bytes(leaf, ei, num_bytes);

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to update syno usage global type with type:%d", type);
	}
	return ret;
}

int btrfs_syno_usage_global_type_update(struct btrfs_trans_handle *trans)
{
	int ret = 0;
	int i;
	u64 num_bytes;
	struct btrfs_fs_info *fs_info = trans->fs_info;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	for (i = SYNO_USAGE_TYPE_RO_SNAPSHOT; i < SYNO_USAGE_TYPE_MAX; i++) {
		spin_lock(&fs_info->syno_usage_lock);
		num_bytes = fs_info->syno_usage_status.syno_usage_type_num_bytes[i];
		spin_unlock(&fs_info->syno_usage_lock);
		if (num_bytes)
			fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[i] = true;
		if (fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[i]) {
			ret = __btrfs_syno_usage_global_type_update(trans, fs_info, i, num_bytes);
			if (ret)
				goto out;
		}
		if (!num_bytes)
			fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[i] = false;
	}
out:
	return ret;
}

int btrfs_read_syno_usage_config(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_status_item *syno_usage_status_item;
	struct btrfs_syno_usage_global_type_item *syno_usage_global_type_item;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;
	struct btrfs_disk_key rescan_progress_disk_key;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * read all syno usage status/types
	 */
	key.objectid = 0;
	key.type = 0;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) { /* tree empty */
		clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);
		ret = 0;
		goto out;
	}

	while (1) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid != 0 || (found_key.type != SYNO_BTRFS_USAGE_GLOBAL_TYPE_KEY && found_key.type != SYNO_BTRFS_USAGE_STATUS_KEY))
			break;

		if (found_key.type == SYNO_BTRFS_USAGE_STATUS_KEY) {
			syno_usage_status_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_status_item);
			fs_info->syno_usage_status.version = btrfs_syno_usage_status_version(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.state = btrfs_syno_usage_status_state(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.flags = btrfs_syno_usage_status_flags(leaf, syno_usage_status_item);
			btrfs_syno_usage_status_extent_rescan_progress_key(leaf, syno_usage_status_item, &rescan_progress_disk_key);
			btrfs_disk_key_to_cpu(&fs_info->syno_usage_status.extent_rescan_progress, &rescan_progress_disk_key);
			fs_info->syno_usage_status.cur_full_rescan_size = btrfs_syno_usage_status_cur_full_rescan_size(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.total_full_rescan_size = btrfs_syno_usage_status_total_full_rescan_size(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.extent_tree_cur_rescan_size = btrfs_syno_usage_status_extent_tree_cur_rescan_size(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.extent_tree_total_rescan_size = btrfs_syno_usage_status_extent_tree_total_rescan_size(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.total_syno_extent_tree_items = btrfs_syno_usage_status_total_syno_extent_tree_items(leaf, syno_usage_status_item);
			fs_info->syno_usage_status.total_syno_subvol_usage_items = btrfs_syno_usage_status_total_syno_subvol_usage_items(leaf, syno_usage_status_item);
			if (btrfs_syno_usage_status_generation(leaf, syno_usage_status_item) != fs_info->generation) {
				fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
				btrfs_warn(fs_info, "Failed to generation mismatch for syno usage");
			}
			if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_NONE || fs_info->syno_usage_status.state == SYNO_USAGE_STATE_DISABLE)
				break;
			else if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR)
				fs_info->syno_usage_status.state = SYNO_USAGE_STATE_RESCAN;
		} else if (found_key.type == SYNO_BTRFS_USAGE_GLOBAL_TYPE_KEY) {
			if (found_key.offset >= SYNO_USAGE_TYPE_MAX) {
				fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
				btrfs_warn(fs_info, "Failed to type [%llu] overflow for syno usage", found_key.offset);
				fs_info->syno_usage_status.state = SYNO_USAGE_STATE_NONE;
				clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);
				break;
			}
			syno_usage_global_type_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_global_type_item);
			fs_info->syno_usage_status.syno_usage_type_num_bytes[found_key.offset] = btrfs_syno_usage_global_type_num_bytes(leaf, syno_usage_global_type_item);
			fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[found_key.offset] = true;
		}

		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
		cond_resched();
	}
	if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_NONE || fs_info->syno_usage_status.state == SYNO_USAGE_STATE_DISABLE)
		clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

void btrfs_syno_usage_root_status_init(struct btrfs_syno_usage_root_status *status,
		struct btrfs_syno_usage_root_status *src, bool readonly, bool init)
{
	memset(status, 0, sizeof(*status));
	status->type = SYNO_USAGE_TYPE_NONE;
	status->new_type = SYNO_USAGE_TYPE_NONE;
	status->state = SYNO_USAGE_ROOT_STATE_RESCAN;
	/* drop_progress key is set to {0, 0, 0} */
	/* fast_rescan_progres key is set to {0, 0, 0} */
	if (src) {
		status->num_bytes = src->num_bytes;
		status->full_rescan_progress = src->full_rescan_progress;
		status->cur_full_rescan_size = src->cur_full_rescan_size;
		status->total_full_rescan_size = src->total_full_rescan_size;
		status->total_syno_subvol_usage_items = src->total_syno_subvol_usage_items;
		status->flags = src->flags & ~BTRFS_SYNO_USAGE_ROOT_FLAG_RESET_MASK;
	} else if (init) {
		status->num_bytes = 0;
		/* full_rescan_progress key is set to {0, 0, 0} */
		status->cur_full_rescan_size = 0;
		status->total_full_rescan_size = 0;
		status->total_syno_subvol_usage_items = 0;
		status->flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_FULL_RESCAN;
	} else {
		status->num_bytes = 0;
		status->full_rescan_progress.objectid = -1;
		status->full_rescan_progress.type = -1;
		status->full_rescan_progress.offset = -1;
		status->cur_full_rescan_size = 0;
		status->total_full_rescan_size = 0;
		status->total_syno_subvol_usage_items = 0;
	}
	if (readonly)
		status->flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY;
	status->flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_FAST_RESCAN;
}

int btrfs_syno_usage_root_status_update(struct btrfs_trans_handle *trans,
				u64 root_objectid, struct btrfs_syno_usage_root_status *syno_usage_root_status)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int extra_size;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_root_status_item *ei;
	struct btrfs_disk_key progress_disk_key;

	if (!root)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
	    fs_info->syno_usage_status.state != SYNO_USAGE_STATE_DISABLE)
		return 0;

	if (syno_usage_root_status->type >= SYNO_USAGE_TYPE_MAX)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->leave_spinning = 1;

	key.objectid = root_objectid;
	key.type = SYNO_BTRFS_USAGE_ROOT_STATUS_KEY;
	key.offset = 0;

	extra_size = sizeof(struct btrfs_syno_usage_root_status_item);
	ret = usage_update_helper(trans, root, path, &key, extra_size);

	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_root_status_item);
	btrfs_set_syno_usage_root_status_type(leaf, ei, syno_usage_root_status->type);
	btrfs_set_syno_usage_root_status_new_type(leaf, ei, syno_usage_root_status->new_type);
	btrfs_set_syno_usage_root_status_state(leaf, ei, syno_usage_root_status->state);
	btrfs_set_syno_usage_root_status_flags(leaf, ei, syno_usage_root_status->flags);
	btrfs_set_syno_usage_root_status_num_bytes(leaf, ei, syno_usage_root_status->num_bytes);

	btrfs_cpu_key_to_disk(&progress_disk_key, &syno_usage_root_status->drop_progress);
	btrfs_set_syno_usage_root_status_drop_progress_key(leaf, ei, &progress_disk_key);
	btrfs_cpu_key_to_disk(&progress_disk_key, &syno_usage_root_status->fast_rescan_progress);
	btrfs_set_syno_usage_root_status_fast_rescan_progress_key(leaf, ei, &progress_disk_key);
	btrfs_cpu_key_to_disk(&progress_disk_key, &syno_usage_root_status->full_rescan_progress);
	btrfs_set_syno_usage_root_status_full_rescan_progress_key(leaf, ei, &progress_disk_key);

	btrfs_set_syno_usage_root_status_cur_full_rescan_size(leaf, ei, syno_usage_root_status->cur_full_rescan_size);
	btrfs_set_syno_usage_root_status_total_full_rescan_size(leaf, ei, syno_usage_root_status->total_full_rescan_size);
	btrfs_set_syno_usage_root_status_total_syno_subvol_usage_items(leaf, ei, syno_usage_root_status->total_syno_subvol_usage_items);

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to update syno usage root status with root:%llu", root_objectid);
	}
	return ret;
}

int btrfs_syno_usage_root_status_remove(struct btrfs_trans_handle *trans, u64 root_objectid)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;

	if (!root)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
	    fs_info->syno_usage_status.state != SYNO_USAGE_STATE_DISABLE)
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = root_objectid;
	key.type = SYNO_BTRFS_USAGE_ROOT_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = 0;
		goto out;
	}
	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to remove syno usage root status with root:%llu", root_objectid);
	}
	return ret;
}

int btrfs_syno_usage_root_status_lookup(struct btrfs_fs_info *fs_info, u64 root_objectid,
		        struct btrfs_syno_usage_root_status *ret_syno_usage_root_status)
{
	int ret;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_root_status_item *ei;
	struct btrfs_disk_key progress_disk_key;

	if (!root)
		return 1;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
	    fs_info->syno_usage_status.state != SYNO_USAGE_STATE_DISABLE)
		return 1;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = root_objectid;
	key.type = SYNO_BTRFS_USAGE_ROOT_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret != 0)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_root_status_item);

	ret_syno_usage_root_status->type = btrfs_syno_usage_root_status_type(leaf, ei);
	ret_syno_usage_root_status->new_type = btrfs_syno_usage_root_status_new_type(leaf, ei);
	ret_syno_usage_root_status->state = btrfs_syno_usage_root_status_state(leaf, ei);
	ret_syno_usage_root_status->flags = btrfs_syno_usage_root_status_flags(leaf, ei);
	ret_syno_usage_root_status->num_bytes = btrfs_syno_usage_root_status_num_bytes(leaf, ei);
	btrfs_syno_usage_root_status_drop_progress_key(leaf, ei, &progress_disk_key);
	btrfs_disk_key_to_cpu(&ret_syno_usage_root_status->drop_progress, &progress_disk_key);
	btrfs_syno_usage_root_status_fast_rescan_progress_key(leaf, ei, &progress_disk_key);
	btrfs_disk_key_to_cpu(&ret_syno_usage_root_status->fast_rescan_progress, &progress_disk_key);
	btrfs_syno_usage_root_status_full_rescan_progress_key(leaf, ei, &progress_disk_key);
	btrfs_disk_key_to_cpu(&ret_syno_usage_root_status->full_rescan_progress, &progress_disk_key);
	ret_syno_usage_root_status->cur_full_rescan_size = btrfs_syno_usage_root_status_cur_full_rescan_size(leaf, ei);
	ret_syno_usage_root_status->total_full_rescan_size = btrfs_syno_usage_root_status_total_full_rescan_size(leaf, ei);
	ret_syno_usage_root_status->total_syno_subvol_usage_items = btrfs_syno_usage_root_status_total_syno_subvol_usage_items(leaf, ei);
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_create_syno_usage_tree(struct btrfs_trans_handle *trans)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *syno_usage_root;

	syno_usage_root = btrfs_create_tree(trans, BTRFS_SYNO_USAGE_TREE_OBJECTID);
	if (IS_ERR(syno_usage_root)) {
		ret = PTR_ERR(syno_usage_root);
		goto out;
	}
	fs_info->syno_usage_root = syno_usage_root;
	fs_info->syno_usage_root->block_rsv = &fs_info->delayed_refs_rsv;

	ret = 0;
out:
	return ret;
}

static int syno_usage_clear_tree(struct btrfs_fs_info *fs_info, struct btrfs_root **ptr_root)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root = *ptr_root;
	int ret;

	if (!root) {
		ret = 0;
		goto out;
	}

	ret = btrfs_clean_tree_by_root_throttle(root);
	if (ret)
		goto out;

	trans = btrfs_start_transaction_fallback_global_rsv(tree_root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	*ptr_root = NULL;

	ret = btrfs_del_root(trans, &root->root_key);
	if (ret)
		goto abort;

	list_del(&root->dirty_list);

	btrfs_tree_lock(root->node);
	btrfs_clean_tree_block(root->node);
	btrfs_tree_unlock(root->node);
	btrfs_free_tree_block(trans, root, root->node, 0, 1);

	btrfs_put_root(root);

	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out;

	ret = 0;
out:
	return ret;

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	trans = NULL;
	goto out;
}

int btrfs_clear_syno_usage_tree(struct btrfs_fs_info *fs_info)
{
	return syno_usage_clear_tree(fs_info, &fs_info->syno_usage_root);
}

static int syno_extent_usage_add_entry(struct btrfs_trans_handle *trans,
						struct btrfs_root *root, struct btrfs_path *path,
						int want, struct btrfs_key *ins, int refs_mod)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_syno_extent_usage_item *ei;
	struct btrfs_syno_extent_usage_inline_ref *iref;
	struct extent_buffer *leaf;
	u32 size;
	int ret;
	bool update_inline_ref;

	if (want == SYNO_USAGE_TYPE_RO_SNAPSHOT)
		update_inline_ref = false;
	else
		update_inline_ref = true;

	size = sizeof(struct btrfs_syno_extent_usage_item);
	if (update_inline_ref)
		size += sizeof(struct btrfs_syno_extent_usage_inline_ref);

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, root, path, ins, size);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);

	spin_lock(&fs_info->syno_usage_lock);
	fs_info->syno_usage_status.syno_usage_type_num_bytes[want] += ins->offset;
	fs_info->syno_usage_status.total_syno_extent_tree_items++;
	spin_unlock(&fs_info->syno_usage_lock);
	btrfs_set_syno_extent_usage_type(leaf, ei, want);

	if (update_inline_ref) {
		iref = (struct btrfs_syno_extent_usage_inline_ref *)(ei + 1);
		btrfs_set_syno_extent_usage_inline_ref_type(leaf, iref, want);
		btrfs_set_syno_extent_usage_inline_ref_count(leaf, iref, refs_mod);
	}

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(path);
	return ret;
}

/*
 * helper to update inline back ref
 */
static noinline_for_stack
void syno_extent_usage_update_inline_ref_count(struct btrfs_root *root,
				  struct btrfs_path *path,
				  struct btrfs_syno_extent_usage_inline_ref *iref,
				  int refs_to_mod)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_syno_extent_usage_item *ei;
	struct btrfs_syno_extent_usage_inline_ref *tmp_iref;
	unsigned long ptr;
	unsigned long end;
	u64 item_size;
	long long refs;
	int type, want, inline_ref_type;
	int size;
	struct btrfs_key key;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);
	type = btrfs_syno_extent_usage_type(leaf, ei);

	refs = btrfs_syno_extent_usage_inline_ref_count(leaf, iref);
	if (refs_to_mod < 0 && refs < refs_to_mod) {
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		btrfs_warn_rl(fs_info, "Failed to syno usage refs underflow for extent bytenr %llu refs %lld refs_to_mod %d", key.objectid, refs, refs_to_mod);
		refs = 0;
	} else {
		refs += refs_to_mod;
	}
	btrfs_set_syno_extent_usage_inline_ref_count(leaf, iref, refs);

	if (refs == 0) {
		inline_ref_type = btrfs_syno_extent_usage_inline_ref_type(leaf, iref);
		/* modify type */
		if (type == inline_ref_type) {
			item_size = btrfs_item_size_nr(leaf, path->slots[0]);
			ptr = (unsigned long)(ei + 1);
			end = (unsigned long)ei + item_size;
			want = SYNO_USAGE_TYPE_RO_SNAPSHOT;
			while (1) {
				if (ptr >= end) {
					WARN_ON(ptr > end);
					break;
				}
				tmp_iref = (struct btrfs_syno_extent_usage_inline_ref *)ptr;
				refs = btrfs_syno_extent_usage_inline_ref_count(leaf, tmp_iref);
				if (refs > 0) {
					want = btrfs_syno_extent_usage_inline_ref_type(leaf, tmp_iref);
					break;
				}
				ptr += sizeof(struct btrfs_syno_extent_usage_inline_ref);
			}
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
			spin_lock(&fs_info->syno_usage_lock);
			if (fs_info->syno_usage_status.syno_usage_type_num_bytes[type] < key.offset) {
				btrfs_warn_rl(fs_info, "Failed to syno usage underflow for type %u size %llu key [%llu %u %llu]",
								type, fs_info->syno_usage_status.syno_usage_type_num_bytes[type], key.objectid, key.type, key.offset);
				fs_info->syno_usage_status.syno_usage_type_num_bytes[type] = 0;
			} else {
				fs_info->syno_usage_status.syno_usage_type_num_bytes[type] -= key.offset;
			}
			fs_info->syno_usage_status.syno_usage_type_num_bytes[want] += key.offset;
			spin_unlock(&fs_info->syno_usage_lock);
			btrfs_set_syno_extent_usage_type(leaf, ei, want);
		}

		/* remove unused inline ref */
		size = sizeof(struct btrfs_syno_extent_usage_inline_ref);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size, end - ptr - size);
		item_size -= size;
		btrfs_truncate_item(path, item_size, 1);
	}

	btrfs_mark_buffer_dirty(leaf);
}

/*
 * helper to add new inline back ref
 */
static noinline_for_stack
void syno_extent_usage_setup_inline_ref(struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_syno_extent_usage_inline_ref *iref,
				 int want, int refs_mod)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_syno_extent_usage_item *ei;
	unsigned long ptr;
	unsigned long end;
	unsigned long item_offset;
	int size;
	int type;
	struct btrfs_key key;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);
	item_offset = (unsigned long)iref - (unsigned long)ei;

	size = sizeof(struct btrfs_syno_extent_usage_inline_ref);

	btrfs_extend_item(path, size);

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);
	type = btrfs_syno_extent_usage_type(leaf, ei);
	if (type == SYNO_USAGE_TYPE_RO_SNAPSHOT || want < type) {
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		spin_lock(&fs_info->syno_usage_lock);
		if (fs_info->syno_usage_status.syno_usage_type_num_bytes[type] < key.offset) {
			btrfs_warn_rl(fs_info, "Failed to syno usage underflow for type %u size %llu key [%llu %u %llu]",
							type, fs_info->syno_usage_status.syno_usage_type_num_bytes[type],
							key.objectid, key.type, key.offset);
			fs_info->syno_usage_status.syno_usage_type_num_bytes[type] = 0;
		} else {
			fs_info->syno_usage_status.syno_usage_type_num_bytes[type] -= key.offset;
		}
		fs_info->syno_usage_status.syno_usage_type_num_bytes[want] += key.offset;
		spin_unlock(&fs_info->syno_usage_lock);
		btrfs_set_syno_extent_usage_type(leaf, ei, want);
	}

	ptr = (unsigned long)ei + item_offset;
	end = (unsigned long)ei + btrfs_item_size_nr(leaf, path->slots[0]);
	if (ptr < end - size)
		memmove_extent_buffer(leaf, ptr + size, ptr, end - size - ptr);

	iref = (struct btrfs_syno_extent_usage_inline_ref *)ptr;
	btrfs_set_syno_extent_usage_inline_ref_type(leaf, iref, want);
	btrfs_set_syno_extent_usage_inline_ref_count(leaf, iref, refs_mod);

	btrfs_mark_buffer_dirty(leaf);
}

/*
 * look for inline ref. if ref is found, *ref_ret is set
 * to the address of inline ref, and 0 is returned.
 *
 * if ref isn't found, *ref_ret is set to the address where it
 * should be inserted, and -ENOENT is returned.
 *
 * NOTE: inline refs are ordered in the same way that ref
 *	 items in the tree are ordered.
 */
static noinline_for_stack
int syno_extent_usage_lookup_inline_ref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_syno_extent_usage_inline_ref **ref_ret,
				 int want, int insert)
{
	int ret;
	struct extent_buffer *leaf;
	struct btrfs_syno_extent_usage_item *ei;
	struct btrfs_syno_extent_usage_inline_ref *iref;
	u64 item_size;
	unsigned long ptr;
	unsigned long end;
	int type;

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	BUG_ON(item_size < sizeof(struct btrfs_syno_extent_usage_item));

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);

	ptr = (unsigned long)(ei + 1);
	end = (unsigned long)ei + item_size;

	ret = -ENOENT;
	while (1) {
		if (ptr >= end) {
			WARN_ON(ptr > end);
			break;
		}
		iref = (struct btrfs_syno_extent_usage_inline_ref *)ptr;
		type = btrfs_syno_extent_usage_inline_ref_type(leaf, iref);
		if (want == type) {
			ret = 0;
			break;
		}
		if (type > want)
			break;

		ptr += sizeof(struct btrfs_syno_extent_usage_inline_ref);
	}
	if (ret == -ENOENT && insert) {
		if (sizeof(struct btrfs_item) + item_size +
		    sizeof(struct btrfs_syno_extent_usage_inline_ref) >
		    BTRFS_LEAF_DATA_SIZE(root->fs_info)) {
			ret = -EOVERFLOW;
			goto out;
		}
	}
	*ref_ret = (struct btrfs_syno_extent_usage_inline_ref *)ptr;

out:
	return ret;
}

int btrfs_syno_extent_usage_add(struct btrfs_trans_handle *trans,
				int want, u64 bytenr, u64 num_bytes, int refs_to_add)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->syno_extent_usage_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_syno_extent_usage_inline_ref *iref;
	int extra_size;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	if (want == SYNO_USAGE_TYPE_NONE)
		return 0;

	if (want >= SYNO_USAGE_TYPE_MAX)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = bytenr;
	key.type = SYNO_BTRFS_EXTENT_USAGE_KEY;
	key.offset = num_bytes;
	/*
	 * if btrfs_search_slot return 0, item already exists. In this case we don't need
	 * space for struct btrfs_syno_extent_usage_item, we may only need to insert
	 * struct btrfs_syno_extent_usage_inline_ref if type doesn't exist.
	 */
	extra_size = sizeof(struct btrfs_syno_extent_usage_inline_ref);

again:
	path->search_for_extension = 1;
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	path->search_for_extension = 0;
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		btrfs_release_path(path);
		// insert new item
		ret = syno_extent_usage_add_entry(trans, root, path, want, &key, refs_to_add);
		if (ret == -EEXIST)
			goto again;
		goto out;
	}

	if (want == SYNO_USAGE_TYPE_RO_SNAPSHOT)
		goto out;

	ret = syno_extent_usage_lookup_inline_ref(trans, root, path, &iref, want, 1);
	if (ret == 0) { /* update */
		syno_extent_usage_update_inline_ref_count(root, path, iref, refs_to_add);
	} else if (ret == -ENOENT) { /* insert new ref */
		syno_extent_usage_setup_inline_ref(root, path, iref, want, refs_to_add);
		ret = 0;
	}

out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to add syno usage extent item with bytenr:%llu", bytenr);
	}
	return ret;
}

int btrfs_syno_extent_usage_free(struct btrfs_trans_handle *trans,
				int want, u64 bytenr, u64 num_bytes, int refs_to_drop, bool remove)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->syno_extent_usage_root;
	struct btrfs_path *path;
	struct btrfs_syno_extent_usage_inline_ref *iref;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_syno_extent_usage_item *ei;
	int type;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	if (!remove && (want == SYNO_USAGE_TYPE_NONE || want == SYNO_USAGE_TYPE_RO_SNAPSHOT))
		return 0;

	if (want >= SYNO_USAGE_TYPE_MAX)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = bytenr;
	key.type = SYNO_BTRFS_EXTENT_USAGE_KEY;
	key.offset = num_bytes;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	if (remove) {
		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_extent_usage_item);
		type = btrfs_syno_extent_usage_type(leaf, ei);
		spin_lock(&fs_info->syno_usage_lock);
		if (fs_info->syno_usage_status.syno_usage_type_num_bytes[type] < key.offset) {
			btrfs_warn_rl(fs_info, "Failed to syno usage underflow for type %u size %llu key [%llu %u %llu]",
							type, fs_info->syno_usage_status.syno_usage_type_num_bytes[type],
							key.objectid, key.type, key.offset);
			fs_info->syno_usage_status.syno_usage_type_num_bytes[type] = 0;
		} else {
			fs_info->syno_usage_status.syno_usage_type_num_bytes[type] -= num_bytes;
		}
		if (fs_info->syno_usage_status.total_syno_extent_tree_items > 0)
			fs_info->syno_usage_status.total_syno_extent_tree_items--;
		spin_unlock(&fs_info->syno_usage_lock);
		ret = btrfs_del_item(trans, root, path);
		goto out;
	}

	ret = syno_extent_usage_lookup_inline_ref(trans, root, path, &iref, want, 0);
	if (ret == 0) /* update */
		syno_extent_usage_update_inline_ref_count(root, path, iref, -refs_to_drop);
	else if (ret == -ENOENT)
		ret = 0;
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to free syno usage extent item with bytenr:%llu", bytenr);
	}
	return ret;
}

static int btrfs_create_syno_extent_usage_tree(struct btrfs_trans_handle *trans)
{
	int ret;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *syno_extent_usage_root;

	syno_extent_usage_root = btrfs_create_tree(trans, BTRFS_SYNO_EXTENT_USAGE_TREE_OBJECTID);
	if (IS_ERR(syno_extent_usage_root)) {
		ret = PTR_ERR(syno_extent_usage_root);
		goto out;
	}
	fs_info->syno_extent_usage_root = syno_extent_usage_root;
	fs_info->syno_extent_usage_root->block_rsv = &fs_info->delayed_refs_rsv;

	ret = 0;
out:
	return ret;
}

int btrfs_clear_extent_usage_tree(struct btrfs_fs_info *fs_info)
{
	return syno_usage_clear_tree(fs_info, &fs_info->syno_extent_usage_root);
}

static int syno_subvol_usage_add_entry(struct btrfs_trans_handle *trans,
						struct btrfs_root *root, struct btrfs_path *path,
						struct btrfs_key *ins, u64 num_bytes, int refs_mod, u8 *type)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_syno_subvol_usage_item *ei;
	struct extent_buffer *leaf;
	u32 size;
	int ret;

	size = sizeof(struct btrfs_syno_subvol_usage_item);

	ret = btrfs_insert_empty_item(trans, root, path, ins, size);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_subvol_usage_item);
	btrfs_set_syno_subvol_usage_ref_count(leaf, ei, refs_mod);
	btrfs_set_syno_subvol_usage_num_bytes(leaf, ei, num_bytes);

	spin_lock(&root->syno_usage_lock);
	root->syno_usage_root_status.num_bytes += num_bytes;
	root->syno_usage_root_status.total_syno_subvol_usage_items++;
	spin_lock(&fs_info->syno_usage_lock);
	fs_info->syno_usage_status.total_syno_subvol_usage_items++;
	spin_unlock(&fs_info->syno_usage_lock);
	if (type) {
		if (root->syno_usage_root_status.state == SYNO_USAGE_ROOT_STATE_RESCAN &&
			btrfs_comp_cpu_keys(ins, &root->syno_usage_root_status.fast_rescan_progress) >= 0)
			*type = root->syno_usage_root_status.type;
		else
			*type = root->syno_usage_root_status.new_type;
		if (*type == SYNO_USAGE_TYPE_NONE) {
			if ((root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_FORCE_EXTENT) ||
				btrfs_root_readonly(root))
				*type = SYNO_USAGE_TYPE_RO_SNAPSHOT;
		}
	}
	spin_unlock(&root->syno_usage_lock);
	btrfs_record_root_in_trans(trans, root);

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(path);
	return ret;
}

static int syno_subvol_usage_update_ref_count(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root, struct btrfs_path *path,
				  struct btrfs_key *ins, int refs_to_mod, int *last_ref, u8 *type)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_syno_subvol_usage_item *ei;
	long long refs;
	int ret = 0;
	u64 num_bytes;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_subvol_usage_item);

	num_bytes = btrfs_syno_subvol_usage_num_bytes(leaf, ei);
	refs = btrfs_syno_subvol_usage_ref_count(leaf, ei);
	if (refs_to_mod < 0 && refs < refs_to_mod) {
		btrfs_warn_rl(fs_info, "Failed to syno usage refs underflow for subvol usage bytenr %llu refs %lld refs_to_mod %d", ins->offset, refs, refs_to_mod);
		refs = 0;
	} else {
		refs += refs_to_mod;
	}
	btrfs_set_syno_subvol_usage_ref_count(leaf, ei, refs);
	btrfs_mark_buffer_dirty(leaf);

	if (refs == 0) {
		ret = btrfs_del_item(trans, root, path);
		if (ret)
			goto out;

		spin_lock(&root->syno_usage_lock);
		root->syno_usage_root_status.num_bytes -= num_bytes;
		if (root->syno_usage_root_status.total_syno_subvol_usage_items > 0)
			root->syno_usage_root_status.total_syno_subvol_usage_items--;
		spin_lock(&fs_info->syno_usage_lock);
		if (fs_info->syno_usage_status.total_syno_subvol_usage_items > 0)
			fs_info->syno_usage_status.total_syno_subvol_usage_items--;
		spin_unlock(&fs_info->syno_usage_lock);
		if (last_ref)
			*last_ref = 1;
		if (type) {
			if (root->syno_usage_root_status.state == SYNO_USAGE_ROOT_STATE_RESCAN &&
			    btrfs_comp_cpu_keys(ins, &root->syno_usage_root_status.fast_rescan_progress) >= 0)
				*type = root->syno_usage_root_status.type;
			else
				*type = root->syno_usage_root_status.new_type;
		}
		spin_unlock(&root->syno_usage_lock);
		btrfs_record_root_in_trans(trans, root);
	}
out:
	return ret;
}

static int __btrfs_syno_subvol_usage_add(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 bytenr, u64 num_bytes, int refs_to_add)
{
	int ret = 0;
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	int extra_size;
	u8 type;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    !test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = bytenr;

	extra_size = sizeof(struct btrfs_syno_subvol_usage_item);

again:
	path->search_for_extension = 1;
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	path->search_for_extension = 0;
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		btrfs_release_path(path);
		// insert new item
		ret = syno_subvol_usage_add_entry(trans, root, path, &key, num_bytes, refs_to_add, &type);
		if (ret == -EEXIST)
			goto again;
		if (ret)
			goto out;
		ret = btrfs_syno_extent_usage_add(trans, type, bytenr, num_bytes, 1);
		goto out;
	}

	ret = syno_subvol_usage_update_ref_count(trans, root, path, &key, refs_to_add, NULL, NULL);
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to add syno usage subvol item with bytenr:%llu", bytenr);
	}
	return ret;
}

int btrfs_syno_subvol_usage_add(struct btrfs_trans_handle *trans,
				u64 root_objectid, u64 bytenr, u64 num_bytes, int refs_to_add)
{
	int ret = 0;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	root = btrfs_get_fs_root(fs_info, root_objectid, true);
	if (IS_ERR(root)) {
		if (PTR_ERR(root) != -ENOENT) {
			ret = PTR_ERR(root);
			fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
			btrfs_warn_rl(fs_info, "Failed to read root with objectid:%llu, err:%d", root_objectid, ret);
		}
		goto out;
	}

	trans->syno_usage = true;
	ret = __btrfs_syno_subvol_usage_add(trans, root, bytenr, num_bytes, refs_to_add);
	trans->syno_usage = false;
	btrfs_put_root(root);
out:
	return ret;
}

static int __btrfs_syno_subvol_usage_free(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 bytenr, u64 num_bytes, int refs_to_drop)
{
	int ret = 0;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	int last_ref = 0;
	u8 type;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    !test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = bytenr;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	ret = syno_subvol_usage_update_ref_count(trans, root, path, &key, -refs_to_drop, &last_ref, &type);
	if (ret)
		goto out;

	if (last_ref) {
		btrfs_release_path(path);
		ret = btrfs_syno_extent_usage_free(trans, type, bytenr, num_bytes, 1, 0);
	}
out:
	btrfs_free_path(path);
	if (ret) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to free syno usage subvol item with bytenr:%llu", bytenr);
	}
	return ret;
}

int btrfs_syno_subvol_usage_free(struct btrfs_trans_handle *trans,
				u64 root_objectid, u64 bytenr, u64 num_bytes, int refs_to_drop)
{
	int ret = 0;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		return 0;

	root = btrfs_get_fs_root(fs_info, root_objectid, true);
	if (IS_ERR(root)) {
		if (PTR_ERR(root) != -ENOENT) {
			ret = PTR_ERR(root);
			fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
			btrfs_warn_rl(fs_info, "Failed to read root with objectid:%llu, err:%d", root_objectid, ret);
		}
		goto out;
	}

	trans->syno_usage = true;
	ret = __btrfs_syno_subvol_usage_free(trans, root, bytenr, num_bytes, refs_to_drop);
	trans->syno_usage = false;
	btrfs_put_root(root);
out:
	return ret;
}

static void syno_usage_wait_on_rescan(struct btrfs_root *root)
{
	wait_on_bit(&root->syno_usage_runtime_flags,
				SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN,
				TASK_UNINTERRUPTIBLE);
}

static void syno_usage_rescan_start(struct btrfs_root *root)
{
	set_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN, &root->syno_usage_runtime_flags);
}

static void syno_usage_rescan_end(struct btrfs_root *root)
{
	clear_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN, &root->syno_usage_runtime_flags);
	smp_mb__after_atomic();
	wake_up_bit(&root->syno_usage_runtime_flags, SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN);
}

static int syno_subvol_dummy_insert(struct btrfs_root *root,
						struct btrfs_path *path, struct btrfs_key *ins)
{
	int ret;
	struct btrfs_trans_handle *trans = NULL;

	trans = btrfs_start_transaction_fallback_global_rsv(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	ret = btrfs_insert_empty_item(trans, root, path, ins, root->fs_info->nodesize / 2);
	if (ret)
		goto out;

out:
	btrfs_release_path(path);
	if (trans)
		btrfs_end_transaction(trans);
	return ret;
}

static int syno_subvol_dummy_add(struct btrfs_root *root, u64 offset)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    !test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_DUMMY_KEY;
	key.offset = offset;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		btrfs_release_path(path);
		// insert new item
		ret = syno_subvol_dummy_insert(root, path, &key);
		goto out;
	}
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_syno_clear_subvol_usage_item_prepare(struct btrfs_root *root)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int i;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    !test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		return 0;

	if (!root->syno_usage_root_status.total_syno_subvol_usage_items)
		return 0;

	for (i = 0; i < 3; i++) {
		ret = syno_subvol_dummy_add(root, i);
		if (ret)
			goto out;
	}

	ret = 0;
out:
	if (ret)
		btrfs_warn_rl(fs_info, "Failed to add subvol usage dummy item with root %llu err %d", root->root_key.objectid, ret);
	return ret;
}

int btrfs_syno_clear_subvol_usage_item_doing(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	struct btrfs_syno_subvol_usage_item *ei;
	u64 bytenr, num_bytes;
	int ret;
	u8 type;
	bool bl_free_fs_root = false;

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    !test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		return 0;

	if (!list_empty(&root->syno_usage_rescan_list)) {
		spin_lock(&fs_info->syno_usage_full_rescan_lock);
		spin_lock(&fs_info->syno_usage_fast_rescan_lock);
		if (!list_empty(&root->syno_usage_rescan_list)) {
			list_del_init(&root->syno_usage_rescan_list);
			bl_free_fs_root = true;
		}
		spin_unlock(&fs_info->syno_usage_fast_rescan_lock);
		spin_unlock(&fs_info->syno_usage_full_rescan_lock);
	}
	if (bl_free_fs_root)
		btrfs_put_root(root);
	syno_usage_wait_on_rescan(root);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = root->syno_usage_root_status.drop_progress.offset;

	if ((root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_NONE || root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_RO_SNAPSHOT) &&
		btrfs_comp_cpu_keys(&key, &root->syno_usage_root_status.fast_rescan_progress) < 0) {
		key.offset = root->syno_usage_root_status.fast_rescan_progress.offset;
	}

	while (1) {
		trans = btrfs_start_transaction_fallback_global_rsv(fs_info->tree_root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key, path->slots[0]);

		spin_lock(&root->syno_usage_lock);
		root->syno_usage_root_status.drop_progress = found_key;
		root->syno_usage_root_status.drop_progress.offset = found_key.offset + 1;
		key.offset = root->syno_usage_root_status.drop_progress.offset;
		spin_unlock(&root->syno_usage_lock);

		if (found_key.objectid != BTRFS_SYNO_SUBVOL_USAGE_OBJECTID || found_key.type != SYNO_BTRFS_SUBVOL_USAGE_KEY)
			break;

		/* if new_type is NONE , old type always is NONE, so we can skip it */
		if (root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_NONE)
			break;

		if (btrfs_comp_cpu_keys(&found_key, &root->syno_usage_root_status.fast_rescan_progress) >= 0) {
			if (root->syno_usage_root_status.type == SYNO_USAGE_TYPE_NONE || root->syno_usage_root_status.type == SYNO_USAGE_TYPE_RO_SNAPSHOT)
				break;
			type = root->syno_usage_root_status.type;
		} else {
			type = root->syno_usage_root_status.new_type;
		}

		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_subvol_usage_item);
		bytenr = found_key.offset;
		num_bytes = btrfs_syno_subvol_usage_num_bytes(leaf, ei);
		btrfs_release_path(path);

		ret = btrfs_syno_extent_usage_free(trans, type, bytenr, num_bytes, 1, 0);
		if (ret)
			goto out;

		ret = btrfs_syno_usage_root_status_update(trans, root->root_key.objectid, &root->syno_usage_root_status);
		if (ret)
			goto out;

		btrfs_end_transaction_throttle(trans);
		trans = NULL;

		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags)
#ifdef MY_ABC_HERE
			|| !fs_info->snapshot_cleaner
#endif /* MY_ABC_HERE */
			) {
			btrfs_debug(fs_info, "drop subvol usage early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}
	btrfs_release_path(path);
	ret = btrfs_syno_usage_root_status_remove(trans, root->root_key.objectid);
	if (ret)
		goto out;
	spin_lock(&root->syno_usage_lock);
	spin_lock(&fs_info->syno_usage_lock);
	if (!(root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY)) {
		if (fs_info->syno_usage_status.total_syno_subvol_usage_items >= root->syno_usage_root_status.total_syno_subvol_usage_items)
			fs_info->syno_usage_status.total_syno_subvol_usage_items -= root->syno_usage_root_status.total_syno_subvol_usage_items;
		else
			fs_info->syno_usage_status.total_syno_subvol_usage_items = 0;
	}
	spin_unlock(&fs_info->syno_usage_lock);
	spin_unlock(&root->syno_usage_lock);
	clear_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state);
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	if (ret && ret != -EAGAIN) {
		fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
		btrfs_warn_rl(fs_info, "Failed to clear subvol usage item with root %llu err %d", root->root_key.objectid, ret);
	}
	return ret;
}

int btrfs_syno_usage_ref_check(struct btrfs_root *root, u64 objectid, u64 offset)
{
	struct btrfs_key syno_usage_key;
	int syno_usage = 0;

	if (test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
		syno_usage = 1;

	if (syno_usage && root->syno_usage_root_status.state == SYNO_USAGE_ROOT_STATE_RESCAN) {
		syno_usage_key.objectid = objectid;
		syno_usage_key.type = BTRFS_EXTENT_DATA_KEY;
		syno_usage_key.offset = offset;
		read_lock(&root->syno_usage_rwlock);
		if (btrfs_comp_cpu_keys(&syno_usage_key, &root->syno_usage_root_status.full_rescan_progress) >= 0)
			syno_usage = 0;
		read_unlock(&root->syno_usage_rwlock);

		if (!syno_usage) {
			if ((root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_FORCE_EXTENT) ||
				btrfs_root_readonly(root))
				syno_usage = 2;
		}
	}

	return syno_usage;
}

static int syno_usage_full_rescan_root_clear_unused_item(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	struct btrfs_key key;
	int del_nr = 0;
	int del_slot = 0;
	int ret;
	int recow;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = 0;

	while (1) {
		if (btrfs_root_readonly(root) || btrfs_root_dead(root)) {
			ret = 1;
			goto out;
		}

		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		recow = 0;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			BUG_ON(del_nr > 0);
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0) {
				ret = 0;
				break;
			}
			recow = 1;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.objectid != BTRFS_SYNO_SUBVOL_USAGE_OBJECTID ||
		    key.type != SYNO_BTRFS_SUBVOL_USAGE_KEY)
			break;

		if (recow) {
			btrfs_release_path(path);
			goto next;
		}

		if (del_nr == 0) {
			del_slot = path->slots[0];
			del_nr = 1;
		} else {
			BUG_ON(del_slot + del_nr != path->slots[0]);
			del_nr++;
		}

		if (path->slots[0] + 1 < btrfs_header_nritems(path->nodes[0])) {
			path->slots[0]++;
			goto next_slot;
		}

		ret = btrfs_del_items(trans, root, path, del_slot, del_nr);
		if (ret)
			goto out;

		if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING) {
			spin_lock(&fs_info->syno_usage_lock);
			if (root->syno_usage_root_status.cur_full_rescan_size + fs_info->nodesize > root->syno_usage_root_status.total_full_rescan_size) {
				root->syno_usage_root_status.total_full_rescan_size += fs_info->nodesize;
				fs_info->syno_usage_status.total_full_rescan_size += fs_info->nodesize;
			}
			root->syno_usage_root_status.cur_full_rescan_size += fs_info->nodesize;
			fs_info->syno_usage_status.cur_full_rescan_size += fs_info->nodesize;
			spin_unlock(&fs_info->syno_usage_lock);
		}

		del_nr = 0;
		del_slot = 0;
		btrfs_release_path(path);

next:
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			btrfs_debug(fs_info, "full rescan clear unused subvol usage early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}
	if (del_nr > 0) {
		ret = btrfs_del_items(trans, root, path, del_slot, del_nr);
		if (ret)
			goto out;
	}
	btrfs_release_path(path);

	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	if (ret < 0 && ret != -EAGAIN)
		btrfs_warn_rl(fs_info, "Failed to clear unused subvol usage item with root %llu err %d", root->root_key.objectid, ret);
	return ret;
}

static int syno_usage_fast_rescan_root(struct btrfs_root *root)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_syno_subvol_usage_item *ei;
	u64 bytenr, num_bytes;
	struct btrfs_key first_key;

	first_key.objectid = 0;
	first_key.type = 0;
	first_key.offset = 0;

	set_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_FAST_RESCAN, &root->syno_usage_runtime_flags);

	if (btrfs_comp_cpu_keys(&first_key, &root->syno_usage_root_status.full_rescan_progress) == 0) {
		ret = syno_usage_full_rescan_root_clear_unused_item(root);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = 0;
			goto out;
		}
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = root->syno_usage_root_status.fast_rescan_progress.offset;

	while(1) {
		if (btrfs_root_readonly(root) || btrfs_root_dead(root))
			goto success;

		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.objectid != BTRFS_SYNO_SUBVOL_USAGE_OBJECTID ||
		    key.type != SYNO_BTRFS_SUBVOL_USAGE_KEY)
			break;

		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_subvol_usage_item);
		bytenr = key.offset;
		key.offset++;
		num_bytes = btrfs_syno_subvol_usage_num_bytes(leaf, ei);

		spin_lock(&root->syno_usage_lock);
		root->syno_usage_root_status.fast_rescan_progress = key;
		spin_unlock(&root->syno_usage_lock);

		ret = btrfs_syno_extent_usage_add(trans, root->syno_usage_root_status.new_type, bytenr, num_bytes, 1);
		if (ret)
			goto inconsistent;
		ret = btrfs_syno_extent_usage_free(trans, root->syno_usage_root_status.type, bytenr, num_bytes, 1, 0);
		if (ret)
			goto inconsistent;

		btrfs_release_path(path);
		btrfs_end_transaction_throttle(trans);
		trans = NULL;

		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			btrfs_debug(fs_info, "fast rescan subvol usage early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}

	spin_lock(&root->syno_usage_lock);
	root->syno_usage_root_status.fast_rescan_progress.objectid = -1;
	root->syno_usage_root_status.fast_rescan_progress.type = -1;
	root->syno_usage_root_status.fast_rescan_progress.offset = -1;
	root->syno_usage_root_status.type = root->syno_usage_root_status.new_type;
	spin_unlock(&root->syno_usage_lock);

success:
	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	clear_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_FAST_RESCAN, &root->syno_usage_runtime_flags);
	return ret;

inconsistent:
	fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
	btrfs_warn_rl(fs_info, "Failed to fast rescan for syno usage with root %llu err:%d", root->root_key.objectid, ret);
	goto out;
}

static int syno_usage_full_rescan_root(struct btrfs_root *root)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	int extent_type;
	u64 disk_bytenr;
	u64 disk_num_bytes;
	int processed_count;
	struct btrfs_key first_key;

	first_key.objectid = 0;
	first_key.type = 0;
	first_key.offset = 0;

	set_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_FULL_RESCAN, &root->syno_usage_runtime_flags);

	if (btrfs_comp_cpu_keys(&first_key, &root->syno_usage_root_status.full_rescan_progress) == 0) {
		ret = syno_usage_full_rescan_root_clear_unused_item(root);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = 0;
			goto out;
		}
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key = root->syno_usage_root_status.full_rescan_progress;

	while(1) {
		if (btrfs_root_readonly(root) || btrfs_root_dead(root))
			goto success;

		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		processed_count = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			if (processed_count == 0) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
				if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING) {
					spin_lock(&fs_info->syno_usage_lock);
					if (root->syno_usage_root_status.cur_full_rescan_size + root->fs_info->nodesize > root->syno_usage_root_status.total_full_rescan_size) {
						root->syno_usage_root_status.total_full_rescan_size += root->fs_info->nodesize;
						fs_info->syno_usage_status.total_full_rescan_size += root->fs_info->nodesize;
					}
					root->syno_usage_root_status.cur_full_rescan_size += root->fs_info->nodesize;
					fs_info->syno_usage_status.cur_full_rescan_size += root->fs_info->nodesize;
					spin_unlock(&fs_info->syno_usage_lock);
				}
			} else {
				btrfs_release_path(path);
				goto next;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		processed_count++;

		key.offset++;
		write_lock(&root->syno_usage_rwlock);
		root->syno_usage_root_status.full_rescan_progress = key;
		write_unlock(&root->syno_usage_rwlock);
		key = root->syno_usage_root_status.full_rescan_progress;
		if (key.objectid > BTRFS_LAST_FREE_OBJECTID)
			break;
		if (key.type != BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			goto next_slot;
		}

		leaf = path->nodes[0];
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);

		if (extent_type == BTRFS_FILE_EXTENT_REG ||
		    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
			disk_num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		} else {
			path->slots[0]++;
			goto next_slot;
		}
		btrfs_release_path(path);

		ret = __btrfs_syno_subvol_usage_add(trans, root, disk_bytenr, disk_num_bytes, 1);
		if (ret)
			goto inconsistent;

next:
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			btrfs_debug(fs_info, "full rescan subvol usage early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}

	write_lock(&root->syno_usage_rwlock);
	root->syno_usage_root_status.full_rescan_progress.objectid = -1;
	root->syno_usage_root_status.full_rescan_progress.type = -1;
	root->syno_usage_root_status.full_rescan_progress.offset = -1;
	write_unlock(&root->syno_usage_rwlock);

success:
	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	clear_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_FULL_RESCAN, &root->syno_usage_runtime_flags);
	return ret;

inconsistent:
	fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
	btrfs_warn_rl(fs_info, "Failed to full rescan for syno usage with root %llu err:%d", root->root_key.objectid, ret);
	goto out;
}

#define SYNO_BTRFS_IOPRIO_FAST_RESCAN_USAGE (IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7))
static void __btrfs_syno_usage_fast_rescan(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	int ret = 0, err;
	struct btrfs_key rescan_finish_key;
	struct btrfs_trans_handle *trans = NULL;
	int old_ioprio;

	fs_info = container_of(work, struct btrfs_fs_info, syno_usage_fast_rescan_work);

	fs_info->syno_usage_fast_rescan_pid = current->pid;

	old_ioprio = IOPRIO_PRIO_VALUE(task_nice_ioclass(current),
				       task_nice_ioprio(current));
	set_task_ioprio(current, SYNO_BTRFS_IOPRIO_FAST_RESCAN_USAGE);

	if (syno_usage_need_stop(fs_info) ||
	    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_INITIAL ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
		ret = -EAGAIN;
	}

	rescan_finish_key.objectid = -1;
	rescan_finish_key.type = -1;
	rescan_finish_key.offset = -1;

	spin_lock(&fs_info->syno_usage_fast_rescan_lock);
	while (!list_empty(&fs_info->syno_usage_pending_fast_rescan_roots)) {
		root = list_first_entry(&fs_info->syno_usage_pending_fast_rescan_roots, struct btrfs_root, syno_usage_rescan_list);
		list_del_init(&root->syno_usage_rescan_list);
		if (ret ||
			root->syno_usage_root_status.state != SYNO_USAGE_ROOT_STATE_RESCAN ||
			root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_NONE ||
			root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_RO_SNAPSHOT ||
			btrfs_root_readonly(root) ||
			btrfs_root_dead(root)) {
			spin_unlock(&fs_info->syno_usage_fast_rescan_lock);
			goto next;
		}
		syno_usage_rescan_start(root);
		spin_unlock(&fs_info->syno_usage_fast_rescan_lock);

		err = syno_usage_fast_rescan_root(root);
		if (err) {
			if (err != -EAGAIN)
				btrfs_warn_rl(fs_info, "Failed to rescan syno suage with objectid:%llu, err:%d", root->root_key.objectid, err);
			goto next;
		}
		err = syno_usage_full_rescan_root(root);
		if (err) {
			if (err != -EAGAIN)
				btrfs_warn_rl(fs_info, "Failed to rescan syno suage with objectid:%llu, err:%d", root->root_key.objectid, err);
			goto next;
		}
		if (!ret && btrfs_comp_cpu_keys(&rescan_finish_key, &root->syno_usage_root_status.fast_rescan_progress) == 0 &&
			btrfs_comp_cpu_keys(&rescan_finish_key, &root->syno_usage_root_status.full_rescan_progress) == 0) {
			trans = btrfs_start_transaction(root, 0);
			if (!IS_ERR(trans)) {
				spin_lock(&root->syno_usage_lock);
				if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING) {
					spin_lock(&fs_info->syno_usage_lock);
					fs_info->syno_usage_status.cur_full_rescan_size += root->syno_usage_root_status.total_full_rescan_size - root->syno_usage_root_status.cur_full_rescan_size;
					root->syno_usage_root_status.cur_full_rescan_size = 0;
					root->syno_usage_root_status.total_full_rescan_size = 0;
					spin_unlock(&fs_info->syno_usage_lock);
				}
				root->syno_usage_root_status.state = SYNO_USAGE_ROOT_STATE_NORMAL;
				root->syno_usage_root_status.flags &= ~(BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_MASK);
				spin_unlock(&root->syno_usage_lock);
				btrfs_end_transaction(trans);
			}
		}
next:
		atomic_dec(&fs_info->syno_usage_pending_fast_rescan_count);
		syno_usage_rescan_end(root);
		btrfs_put_root(root);
		cond_resched();
		spin_lock(&fs_info->syno_usage_fast_rescan_lock);
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE)
			ret = -EAGAIN;
	}
	spin_unlock(&fs_info->syno_usage_fast_rescan_lock);

	set_task_ioprio(current, old_ioprio);

	fs_info->syno_usage_fast_rescan_pid = 0;
}

void btrfs_init_syno_usage_fast_rescan_work(struct work_struct *work)
{
	INIT_WORK(work, __btrfs_syno_usage_fast_rescan);
}

static int syno_usage_extent_rescan(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_root *syno_extent_usage_root = fs_info->syno_extent_usage_root;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int processed_count;
	u64 bytenr;
	u64 num_bytes;
	u64 root_new_total_size;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key = fs_info->syno_usage_status.extent_rescan_progress;

	while(1) {
		trans = btrfs_start_transaction(syno_extent_usage_root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		processed_count = 0;
		ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			if (processed_count == 0) {
				ret = btrfs_next_leaf(extent_root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
				spin_lock(&fs_info->syno_usage_lock);
				if (fs_info->syno_usage_status.extent_tree_cur_rescan_size + extent_root->fs_info->nodesize > fs_info->syno_usage_status.extent_tree_total_rescan_size) {
					root_new_total_size = btrfs_root_used(&extent_root->root_item);
					if (root_new_total_size > fs_info->syno_usage_status.extent_tree_total_rescan_size) {
						fs_info->syno_usage_status.total_full_rescan_size += root_new_total_size - fs_info->syno_usage_status.extent_tree_total_rescan_size;
						fs_info->syno_usage_status.extent_tree_total_rescan_size = root_new_total_size;
					}
				}
				if (fs_info->syno_usage_status.extent_tree_cur_rescan_size + extent_root->fs_info->nodesize > fs_info->syno_usage_status.extent_tree_total_rescan_size) {
					fs_info->syno_usage_status.extent_tree_total_rescan_size += extent_root->fs_info->nodesize;
					fs_info->syno_usage_status.total_full_rescan_size += extent_root->fs_info->nodesize;
				}
				fs_info->syno_usage_status.extent_tree_cur_rescan_size += extent_root->fs_info->nodesize;
				fs_info->syno_usage_status.cur_full_rescan_size += extent_root->fs_info->nodesize;
				spin_unlock(&fs_info->syno_usage_lock);
			} else {
				btrfs_release_path(path);
				btrfs_end_transaction_throttle(trans);
				trans = NULL;
				goto next;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key, path->slots[0]);
		processed_count++;

		fs_info->syno_usage_status.extent_rescan_progress = found_key;
		fs_info->syno_usage_status.extent_rescan_progress.offset = found_key.offset + 1;
		key = fs_info->syno_usage_status.extent_rescan_progress;
		if (found_key.type != BTRFS_EXTENT_ITEM_KEY) {
			path->slots[0]++;
			goto next_slot;
		}

		bytenr = found_key.objectid;
		num_bytes = found_key.offset;

		ret = btrfs_syno_extent_usage_add(trans, SYNO_USAGE_TYPE_RO_SNAPSHOT, bytenr, num_bytes, 0);
		if (ret)
			goto inconsistent;

		btrfs_release_path(path);
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
next:
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			btrfs_debug(fs_info, "syno usage extent rescan early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}

	fs_info->syno_usage_status.extent_rescan_progress.objectid = -1;
	fs_info->syno_usage_status.extent_rescan_progress.type = -1;
	fs_info->syno_usage_status.extent_rescan_progress.offset = -1;
	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	return ret;

inconsistent:
	fs_info->syno_usage_status.flags |= BTRFS_SYNO_USAGE_FLAG_INCONSISTENT;
	btrfs_warn_rl(fs_info, "Failed to extent rescan for syno usage, err:%d", ret);
	goto out;
}

static void __btrfs_syno_usage_full_rescan(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	int ret = 0, err;
	struct btrfs_key rescan_finish_key;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_syno_usage_status *usage_status;

	fs_info = container_of(work, struct btrfs_fs_info, syno_usage_full_rescan_work);
	usage_status = &fs_info->syno_usage_status;

	fs_info->syno_usage_full_rescan_pid = current->pid;

	if (syno_usage_need_stop(fs_info) ||
	    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		usage_status->state == SYNO_USAGE_STATE_INITIAL ||
		usage_status->state == SYNO_USAGE_STATE_RESCAN_ERROR ||
		usage_status->state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
		ret = -EAGAIN;
	}

	rescan_finish_key.objectid = -1;
	rescan_finish_key.type = -1;
	rescan_finish_key.offset = -1;

	spin_lock(&fs_info->syno_usage_full_rescan_lock);
	while (!list_empty(&fs_info->syno_usage_pending_full_rescan_roots)) {
		root = list_first_entry(&fs_info->syno_usage_pending_full_rescan_roots, struct btrfs_root, syno_usage_rescan_list);
		list_del_init(&root->syno_usage_rescan_list);
		if (ret ||
			root->syno_usage_root_status.state != SYNO_USAGE_ROOT_STATE_RESCAN ||
			root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_NONE ||
			root->syno_usage_root_status.new_type == SYNO_USAGE_TYPE_RO_SNAPSHOT ||
			btrfs_root_readonly(root) ||
			btrfs_root_dead(root)) {
			spin_unlock(&fs_info->syno_usage_full_rescan_lock);
			goto next;
		}
		syno_usage_rescan_start(root);
		spin_unlock(&fs_info->syno_usage_full_rescan_lock);

		err = syno_usage_fast_rescan_root(root);
		if (err) {
			if (err != -EAGAIN) {
				ret = err;
				btrfs_warn_rl(fs_info, "Failed to rescan syno suage with objectid:%llu, err:%d", root->root_key.objectid, err);
			}
			goto next;
		}
		err = syno_usage_full_rescan_root(root);
		if (err) {
			if (err != -EAGAIN) {
				ret = err;
				btrfs_warn_rl(fs_info, "Failed to rescan syno suage with objectid:%llu, err:%d", root->root_key.objectid, err);
			}
			goto next;
		}
		if (!ret && btrfs_comp_cpu_keys(&rescan_finish_key, &root->syno_usage_root_status.fast_rescan_progress) == 0 &&
			btrfs_comp_cpu_keys(&rescan_finish_key, &root->syno_usage_root_status.full_rescan_progress) == 0) {
			trans = btrfs_start_transaction(root, 0);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				trans = NULL;
				goto next;
			}
			spin_lock(&root->syno_usage_lock);
			if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING) {
				spin_lock(&fs_info->syno_usage_lock);
				usage_status->cur_full_rescan_size += root->syno_usage_root_status.total_full_rescan_size - root->syno_usage_root_status.cur_full_rescan_size;
				root->syno_usage_root_status.cur_full_rescan_size = 0;
				root->syno_usage_root_status.total_full_rescan_size = 0;
				spin_unlock(&fs_info->syno_usage_lock);
			}
			root->syno_usage_root_status.state = SYNO_USAGE_ROOT_STATE_NORMAL;
			root->syno_usage_root_status.flags &= ~(BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_MASK);
			spin_unlock(&root->syno_usage_lock);
			btrfs_end_transaction(trans);
		}
next:
		atomic_dec(&fs_info->syno_usage_pending_full_rescan_count);
		syno_usage_rescan_end(root);
		btrfs_put_root(root);
		cond_resched();
		spin_lock(&fs_info->syno_usage_full_rescan_lock);
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
			usage_status->state == SYNO_USAGE_STATE_RESCAN_PAUSE)
			ret = -EAGAIN;
	}
	spin_unlock(&fs_info->syno_usage_full_rescan_lock);

	if (!ret &&
		btrfs_comp_cpu_keys(&rescan_finish_key, &usage_status->extent_rescan_progress) != 0)
		ret = syno_usage_extent_rescan(fs_info);

	if (!ret &&
	    (usage_status->state == SYNO_USAGE_STATE_RESCAN ||
	     usage_status->state == SYNO_USAGE_STATE_RESCAN_PAUSE) &&
	    btrfs_comp_cpu_keys(&rescan_finish_key, &usage_status->extent_rescan_progress) == 0) {
		usage_status->cur_full_rescan_size += usage_status->extent_tree_total_rescan_size - usage_status->extent_tree_cur_rescan_size;
		usage_status->extent_tree_cur_rescan_size = 0;
		usage_status->extent_tree_total_rescan_size = 0;
		usage_status->cur_full_rescan_size = usage_status->total_full_rescan_size;
		usage_status->state = SYNO_USAGE_STATE_ENABLE;
	}

	if (ret && ret != -EAGAIN && (usage_status->state == SYNO_USAGE_STATE_RESCAN)) {
		usage_status->state = SYNO_USAGE_STATE_RESCAN_ERROR;
		usage_status->error_code = ret;
	}

	fs_info->syno_usage_full_rescan_pid = 0;
}

void btrfs_init_syno_usage_full_rescan_work(struct work_struct *work)
{
	INIT_WORK(work, __btrfs_syno_usage_full_rescan);
}

static void __syno_usage_rescan_resume(struct btrfs_fs_info *fs_info)
{
	u64 root_objectid = 0;
	struct btrfs_root *gang[8];
	int i = 0;
	unsigned int ret = 0;
	struct btrfs_root *subvol_root;

	while (1) {
		spin_lock(&fs_info->fs_roots_radix_lock);
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang));
		if (!ret) {
			spin_unlock(&fs_info->fs_roots_radix_lock);
			break;
		}
		root_objectid = gang[ret - 1]->root_key.objectid + 1;

		for (i = 0; i < ret; i++) {
			/* Avoid to grab roots in dead_roots */
			if (btrfs_root_refs(&gang[i]->root_item) == 0) {
				gang[i] = NULL;
				continue;
			}
			/* grab all the search result for later use */
			gang[i] = btrfs_grab_root(gang[i]);
		}
		spin_unlock(&fs_info->fs_roots_radix_lock);

		for (i = 0; i < ret; i++) {
			if (!gang[i])
				continue;
			if (btrfs_root_readonly(gang[i]))
				continue;

			subvol_root = gang[i];
			spin_lock(&subvol_root->syno_usage_lock);
			spin_lock(&fs_info->syno_usage_full_rescan_lock);
			spin_lock(&fs_info->syno_usage_fast_rescan_lock);
			if (test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &subvol_root->state) &&
				subvol_root->syno_usage_root_status.state == SYNO_USAGE_ROOT_STATE_RESCAN &&
				subvol_root->syno_usage_root_status.new_type != SYNO_USAGE_TYPE_NONE &&
				subvol_root->syno_usage_root_status.new_type != SYNO_USAGE_TYPE_RO_SNAPSHOT &&
				!test_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN, &subvol_root->syno_usage_runtime_flags) &&
				list_empty(&subvol_root->syno_usage_rescan_list)) {
				btrfs_grab_root(subvol_root);
				if (subvol_root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_FULL_RESCAN) {
					list_move_tail(&subvol_root->syno_usage_rescan_list, &fs_info->syno_usage_pending_full_rescan_roots);
					atomic_inc(&fs_info->syno_usage_pending_full_rescan_count);
				} else {
					list_move_tail(&subvol_root->syno_usage_rescan_list, &fs_info->syno_usage_pending_fast_rescan_roots);
					atomic_inc(&fs_info->syno_usage_pending_fast_rescan_count);
				}
			}
			spin_unlock(&fs_info->syno_usage_fast_rescan_lock);
			spin_unlock(&fs_info->syno_usage_full_rescan_lock);
			spin_unlock(&subvol_root->syno_usage_lock);
		}

		for (i = 0; i < ret; i++) {
			if (gang[i])
				btrfs_put_root(gang[i]);
		}
	}
}

static int __syno_usage_rescan_preload(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_syno_usage_root_status_item *ei;
	struct btrfs_key key;
	int ret;
	struct btrfs_root *subvol_root;
	int processed_count;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * read all syno usage root
	 */
	key.objectid = 0;
	key.type = SYNO_BTRFS_USAGE_ROOT_STATUS_KEY;
	key.offset = 0;

	while(1) {
		processed_count = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			if (processed_count == 0) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
			} else {
				goto next;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		processed_count++;

		key.offset++;
		if (key.type != SYNO_BTRFS_USAGE_ROOT_STATUS_KEY) {
			path->slots[0]++;
			goto next_slot;
		}

		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_usage_root_status_item);
		if (btrfs_syno_usage_root_status_state(leaf, ei) != SYNO_USAGE_ROOT_STATE_RESCAN) {
			path->slots[0]++;
			goto next_slot;
		}
		if ((btrfs_syno_usage_root_status_flags(leaf, ei) & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY)) {
			path->slots[0]++;
			goto next_slot;
		}
		if (btrfs_syno_usage_root_status_new_type(leaf, ei) == SYNO_USAGE_TYPE_NONE ||
		    btrfs_syno_usage_root_status_new_type(leaf, ei) == SYNO_USAGE_TYPE_RO_SNAPSHOT) {
			path->slots[0]++;
			goto next_slot;
		}
		btrfs_release_path(path);

		subvol_root = btrfs_get_fs_root(fs_info, key.objectid, true);
		if (IS_ERR(subvol_root)) {
			ret = PTR_ERR(subvol_root);
			if (ret != -ENOENT)
				btrfs_warn_rl(fs_info, "Failed to syno usage rescan, read fs root with objectid:%llu, err:%d", key.objectid, ret);
			goto next;
		}
		btrfs_put_root(subvol_root);
next:
		btrfs_release_path(path);
		if (syno_usage_need_stop(fs_info) ||
		    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			btrfs_debug(fs_info, "syno usage rescan resume early exit");
			ret = -EAGAIN;
			goto out;
		}
		cond_resched();
	}

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static void __btrfs_syno_usage_rescan(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	int ret = 0;

	fs_info = container_of(work, struct btrfs_fs_info, syno_usage_rescan_work);

	if (syno_usage_need_stop(fs_info) ||
	    !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_INITIAL ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR ||
	    fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE)
		ret = -EAGAIN;

	if (!ret && !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_PRELOAD, &fs_info->flags)) {
		ret = __syno_usage_rescan_preload(fs_info);
		if (!ret)
			set_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_PRELOAD, &fs_info->flags);
	}
	if (!ret && !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_CHECK_ALL, &fs_info->flags)) {
		__syno_usage_rescan_resume(fs_info);
		set_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_CHECK_ALL, &fs_info->flags);
	}

	if (!list_empty(&fs_info->syno_usage_pending_fast_rescan_roots) &&
	    !work_busy(&fs_info->syno_usage_fast_rescan_work))
		queue_work(system_unbound_wq, &fs_info->syno_usage_fast_rescan_work);
	if (!list_empty(&fs_info->syno_usage_pending_full_rescan_roots) &&
	    !work_busy(&fs_info->syno_usage_full_rescan_work))
		queue_work(system_unbound_wq, &fs_info->syno_usage_full_rescan_work);
	if (!ret && (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN) &&
		!work_busy(&fs_info->syno_usage_full_rescan_work))
		queue_work(system_unbound_wq, &fs_info->syno_usage_full_rescan_work);
}

void btrfs_init_syno_usage_rescan_work(struct work_struct *work)
{
	INIT_WORK(work, __btrfs_syno_usage_rescan);
}

void btrfs_syno_usage_rescan_resume(struct btrfs_fs_info *fs_info)
{
	if ((syno_usage_need_stop(fs_info) ||
	     !test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
	     fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR ||
	     fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) &&
	    list_empty(&fs_info->syno_usage_pending_fast_rescan_roots) &&
	    list_empty(&fs_info->syno_usage_pending_full_rescan_roots))
		return;
	if (work_busy(&fs_info->syno_usage_rescan_work))
		return;
	queue_work(system_unbound_wq, &fs_info->syno_usage_rescan_work);
}

void btrfs_syno_usage_root_initialize(struct btrfs_root *subvol_root)
{
	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &subvol_root->fs_info->flags) ||
	    !is_fstree(subvol_root->root_key.objectid))
		return;

	spin_lock(&subvol_root->syno_usage_lock);
	if (!test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &subvol_root->state)) {
		btrfs_syno_usage_root_status_init(&subvol_root->syno_usage_root_status,
		                NULL, btrfs_root_readonly(subvol_root), true);
		set_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &subvol_root->state);
	}
	spin_unlock(&subvol_root->syno_usage_lock);
}

int syno_usage_clear_subvol_usage_item(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path;
	struct btrfs_key key;
	int del_nr = 0;
	int del_slot = 0;
	int ret;
	int recow;
	bool bl_free_fs_root = false;

	if (!list_empty(&root->syno_usage_rescan_list)) {
		spin_lock(&fs_info->syno_usage_full_rescan_lock);
		spin_lock(&fs_info->syno_usage_fast_rescan_lock);
		if (!list_empty(&root->syno_usage_rescan_list)) {
			list_del_init(&root->syno_usage_rescan_list);
			bl_free_fs_root = true;
		}
		spin_unlock(&fs_info->syno_usage_fast_rescan_lock);
		spin_unlock(&fs_info->syno_usage_full_rescan_lock);
	}
	if (bl_free_fs_root)
		btrfs_put_root(root);
	syno_usage_wait_on_rescan(root);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_SUBVOL_USAGE_OBJECTID;
	key.type = SYNO_BTRFS_SUBVOL_USAGE_KEY;
	key.offset = 0;

	while (1) {
		if (btrfs_root_readonly(root) || btrfs_root_dead(root)) {
			trans = btrfs_start_transaction_fallback_global_rsv(fs_info->tree_root, 2);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				trans = NULL;
				goto out;
			}
			break;
		}

		trans = btrfs_start_transaction_fallback_global_rsv(root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}

		recow = 0;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			BUG_ON(del_nr > 0);
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0) {
				ret = 0;
				break;
			}
			recow = 1;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.objectid != BTRFS_SYNO_SUBVOL_USAGE_OBJECTID ||
		    key.type != SYNO_BTRFS_SUBVOL_USAGE_KEY)
			break;

		if (recow) {
			btrfs_release_path(path);
			goto next;
		}

		if (del_nr == 0) {
			del_slot = path->slots[0];
			del_nr = 1;
		} else {
			BUG_ON(del_slot + del_nr != path->slots[0]);
			del_nr++;
		}

		if (path->slots[0] + 1 < btrfs_header_nritems(path->nodes[0])) {
			path->slots[0]++;
			goto next_slot;
		}

		ret = btrfs_del_items(trans, root, path, del_slot, del_nr);
		if (ret)
			goto out;

		spin_lock(&root->syno_usage_lock);
		spin_lock(&fs_info->syno_usage_lock);
		if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY) {
			fs_info->syno_usage_status.total_syno_subvol_usage_items += root->syno_usage_root_status.total_syno_subvol_usage_items;
			fs_info->syno_usage_status.total_full_rescan_size += root->syno_usage_root_status.total_syno_subvol_usage_items;
			root->syno_usage_root_status.flags &= ~BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY;
		}

		/* remove subvol syno subvol usage item count */
		if (root->syno_usage_root_status.total_syno_subvol_usage_items >= del_nr)
			root->syno_usage_root_status.total_syno_subvol_usage_items -= del_nr;
		else
			root->syno_usage_root_status.total_syno_subvol_usage_items = 0;

		/* remove volume syno subvol usage item count */
		if (fs_info->syno_usage_status.total_syno_subvol_usage_items > del_nr)
			fs_info->syno_usage_status.total_syno_subvol_usage_items -= del_nr;
		else
			fs_info->syno_usage_status.total_syno_subvol_usage_items = 0;

		/* add processed count for disable progress */
		fs_info->syno_usage_status.cur_full_rescan_size += del_nr;
		if (fs_info->syno_usage_status.cur_full_rescan_size > fs_info->syno_usage_status.total_full_rescan_size)
			fs_info->syno_usage_status.total_full_rescan_size = fs_info->syno_usage_status.cur_full_rescan_size;
		spin_unlock(&fs_info->syno_usage_lock);
		spin_unlock(&root->syno_usage_lock);

		del_nr = 0;
		del_slot = 0;
		btrfs_release_path(path);

next:
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
		if (syno_usage_need_stop(fs_info)) {
			btrfs_debug(fs_info, "drop subvol usage early exit");
			ret = -EAGAIN;
			goto out;
		}
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();
	}
	if (del_nr > 0) {
		ret = btrfs_del_items(trans, root, path, del_slot, del_nr);
		if (ret)
			goto out;
	}
	btrfs_release_path(path);
	ret = btrfs_syno_usage_root_status_remove(trans, root->root_key.objectid);
	if (ret)
		goto out;
	spin_lock(&root->syno_usage_lock);
	spin_lock(&fs_info->syno_usage_lock);
	if (!(root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY)) {
		if (fs_info->syno_usage_status.total_syno_subvol_usage_items >= root->syno_usage_root_status.total_syno_subvol_usage_items)
			fs_info->syno_usage_status.total_syno_subvol_usage_items -= root->syno_usage_root_status.total_syno_subvol_usage_items;
		else
			fs_info->syno_usage_status.total_syno_subvol_usage_items = 0;

		fs_info->syno_usage_status.cur_full_rescan_size += root->syno_usage_root_status.total_syno_subvol_usage_items;
		if (fs_info->syno_usage_status.cur_full_rescan_size > fs_info->syno_usage_status.total_full_rescan_size)
			fs_info->syno_usage_status.total_full_rescan_size = fs_info->syno_usage_status.cur_full_rescan_size;
	}
	spin_unlock(&fs_info->syno_usage_lock);
	spin_unlock(&root->syno_usage_lock);
	clear_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state);

	ret = 0;
out:
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction_throttle(trans);
	if (ret && ret != -EAGAIN && ret != -EINTR)
		btrfs_info(fs_info, "Failed to clear syno usage subvol item when disabling");
	return ret;
}

static int syno_usage_clear_all_subvol_usage_item(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *root = fs_info->syno_usage_root;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_root *subvol_root;
	int processed_count;

	if (!root) {
		ret = 0;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * read all syno usage root
	 */
	key.objectid = 0;
	key.type = SYNO_BTRFS_USAGE_ROOT_STATUS_KEY;
	key.offset = 0;

	while (1) {
		processed_count = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			if (processed_count == 0) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					goto out;
				else if (ret > 0)
					break;
			} else {
				btrfs_release_path(path);
				goto next;
			}
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		processed_count++;

		key.offset++;
		if (key.type != SYNO_BTRFS_USAGE_ROOT_STATUS_KEY) {
			path->slots[0]++;
			goto next_slot;
		}
		btrfs_release_path(path);

		subvol_root = btrfs_get_fs_root(fs_info, key.objectid, true);
		if (IS_ERR(subvol_root)) {
			ret = PTR_ERR(subvol_root);
			if (ret != -ENOENT)
				btrfs_warn_rl(fs_info, "Failed to disable syno usage, read fs root with objectid:%llu, err:%d", key.objectid, ret);
			goto next;
		}

		ret = syno_usage_clear_subvol_usage_item(subvol_root);
		btrfs_put_root(subvol_root);
		if (ret)
			goto out;
next:
		if (syno_usage_need_stop(fs_info)) {
			btrfs_debug(fs_info, "syno usage disable early exit");
			ret = -EAGAIN;
			goto out;
		}
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();
	}

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_syno_usage_disable(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_trans_handle *trans = NULL;
	int i;

	if (!fs_info->syno_usage_root && !fs_info->syno_extent_usage_root) {
		ret = 0;
		goto out;
	}

	trans = btrfs_start_transaction(fs_info->tree_root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	/* disable syno usage */
	clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);

	/* initialize disable progress */
	if (fs_info->syno_usage_status.state != SYNO_USAGE_STATE_DISABLE) {
		fs_info->syno_usage_status.state = SYNO_USAGE_STATE_DISABLE;
		fs_info->syno_usage_status.cur_full_rescan_size = 0;
		fs_info->syno_usage_status.total_full_rescan_size = fs_info->syno_usage_status.total_syno_extent_tree_items + fs_info->syno_usage_status.total_syno_subvol_usage_items;
	}

	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out;

	ret = syno_usage_clear_all_subvol_usage_item(fs_info);
	if (ret)
		goto out;

	ret = btrfs_clear_extent_usage_tree(fs_info);
	if (ret)
		goto out;

	/* clear syno usage tree */
	fs_info->syno_usage_status.state = SYNO_USAGE_STATE_NONE;

	trans = btrfs_start_transaction(fs_info->tree_root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}
	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out;

	ret = btrfs_clear_syno_usage_tree(fs_info);
	if (ret)
		goto out;

	spin_lock(&fs_info->syno_usage_lock);
	for (i = SYNO_USAGE_TYPE_RO_SNAPSHOT; i < SYNO_USAGE_TYPE_MAX; i++) {
		fs_info->syno_usage_status.syno_usage_type_num_bytes[i] = 0;
		fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[i] = false;
	}
	spin_unlock(&fs_info->syno_usage_lock);

out:
	return ret;
}

static int syno_usage_enable_precheck(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	u64 extent_tree_size;
	struct btrfs_space_info *metadata_space_info = NULL, *tmp;
	u64 used, total_used = 0, total_free = 0;
	int metadata_ratio = 1;
	int c;

	extent_tree_size = btrfs_root_used(&fs_info->extent_root->root_item);

	list_for_each_entry(tmp, &fs_info->space_info, list) {
		total_used += tmp->disk_total;
		if (tmp->flags == BTRFS_BLOCK_GROUP_METADATA)
			metadata_space_info = tmp;
	}

	if (!metadata_space_info)
		goto success;

	down_read(&metadata_space_info->groups_sem);
	for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
		if (!list_empty(&metadata_space_info->block_groups[c])) {
			if (c == BTRFS_RAID_DUP)
				metadata_ratio = 2;
			break;
		}
	}
	up_read(&metadata_space_info->groups_sem);

	used = metadata_space_info->bytes_used + metadata_space_info->bytes_reserved +
			metadata_space_info->bytes_pinned + metadata_space_info->bytes_readonly +
			metadata_space_info->bytes_may_use;
	total_free += (metadata_space_info->total_bytes - used) * metadata_ratio;

	total_free += btrfs_super_total_bytes(disk_super) - total_used;
	do_div(total_free, metadata_ratio);

	/*
	 * Space Required :
	 * 1. syno extent usage item (Similar to extent tree)
	 * 2. syno subvol usage item (Similar to extent tree)
	 * 3. global reserve : 2G (maximum)
	 */

	if (total_free < (extent_tree_size * 2 + SZ_2G)) {
		ret = -ENOSPC;
		goto out;
	}

success:
	ret = 0;
out:
	return ret;
}

int btrfs_syno_usage_enable(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct btrfs_trans_handle *trans = NULL;
	int i;

	if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		goto out;

	// clear old tree;
	if (fs_info->syno_usage_root || fs_info->syno_extent_usage_root) {
		ret = btrfs_syno_usage_disable(fs_info);
		if (ret)
			goto out;
	}

	ret = syno_usage_enable_precheck(fs_info);
	if (ret)
		goto out;

	/*
	 * 2 - root node
	 * 2 - root item
	 */
	trans = btrfs_start_transaction(fs_info->tree_root, 4);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	ret = btrfs_create_syno_usage_tree(trans);
	if (ret) {
		btrfs_warn_rl(fs_info, "Failed to create syno usage tree, err:%d", ret);
		goto out_abort;
	}

	ret = btrfs_create_syno_extent_usage_tree(trans);
	if (ret) {
		btrfs_warn_rl(fs_info, "Failed to create syno extent usage tree, err:%d", ret);
		goto out_abort;
	}

	fs_info->syno_usage_status.version = BTRFS_SYNO_USAGE_STATUS_VERSION;
	fs_info->syno_usage_status.state = SYNO_USAGE_STATE_INITIAL;
	fs_info->syno_usage_status.flags = 0;
	fs_info->syno_usage_status.total_syno_subvol_usage_items = 0;
	fs_info->syno_usage_status.total_syno_extent_tree_items = 0;
	fs_info->syno_usage_status.extent_rescan_progress.objectid = 0;
	fs_info->syno_usage_status.extent_rescan_progress.type = 0;
	fs_info->syno_usage_status.extent_rescan_progress.offset = 0;
	fs_info->syno_usage_status.extent_tree_cur_rescan_size = 0;
	fs_info->syno_usage_status.extent_tree_total_rescan_size = btrfs_root_used(&fs_info->extent_root->root_item);
	fs_info->syno_usage_status.cur_full_rescan_size = 0;
	fs_info->syno_usage_status.total_full_rescan_size = fs_info->syno_usage_status.extent_tree_total_rescan_size;
	clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_PRELOAD, &fs_info->flags);
	clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_CHECK_ALL, &fs_info->flags);

	spin_lock(&fs_info->syno_usage_lock);
	for (i = SYNO_USAGE_TYPE_RO_SNAPSHOT; i < SYNO_USAGE_TYPE_MAX; i++) {
		fs_info->syno_usage_status.syno_usage_type_num_bytes[i] = 0;
		fs_info->syno_usage_status.syno_usage_type_num_bytes_valid[i] = false;
	}
	spin_unlock(&fs_info->syno_usage_lock);

	set_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);
	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out_recovery;

out:
	return ret;

out_abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	goto out;

out_recovery:
	fs_info->syno_usage_status.state = SYNO_USAGE_STATE_NONE;
	clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags);
	goto out;
}

