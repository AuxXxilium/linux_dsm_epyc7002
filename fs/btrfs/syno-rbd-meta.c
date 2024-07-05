/*
 * Copyright (C) 2000-2021 Synology Inc. All rights reserved.
 */
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/srcu.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/btrfs.h>

#include "ctree.h"
#include "async-thread.h"
#include "btrfs_inode.h"
#include "disk-io.h"
#include "extent_io.h"
#include "transaction.h"
#include "ordered-data.h"
#include "block-group.h"

#include "syno-feat-tree.h"
#include "syno-rbd-meta.h"

static int btrfs_pin_rbd_meta_file(struct inode *inode);
static int insert_rbd_meta_file_record(struct inode *inode);
static int delete_rbd_meta_file_record(struct inode *inode);
static int lookup_rbd_meta_file_extent(struct btrfs_fs_info *fs_info,
				       struct inode *inode,
				       struct btrfs_device **device,
				       u64 start,
				       u64 *logical_block_start,
				       u64 *physical_block_start,
				       u64 *len_out);

int btrfs_rbd_meta_file_activate(struct inode *inode)
{
	int ret;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;

	ret = btrfs_pin_rbd_meta_file(inode);
	if (ret) {
		btrfs_info(fs_info, "failed to pin rbd meta file");
		goto out;
	}

	ret = insert_rbd_meta_file_record(inode);
	if (ret) {
		btrfs_info(fs_info, "failed to insert rbd meta file record");
		goto out;
	}
out:
	if (ret)
		btrfs_unpin_rbd_meta_file(inode);
	return ret;
}

int btrfs_rbd_meta_file_deactivate(struct inode *inode)
{
	int ret;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;

	ret = delete_rbd_meta_file_record(inode);
	if (ret) {
		btrfs_info(fs_info, "failed to delete rbd meta file record");
		return ret;
	}

	btrfs_unpin_rbd_meta_file(inode);
	return 0;
}

int btrfs_rbd_meta_file_mapping(struct inode *inode,
			struct syno_rbd_meta_ioctl_args *args)
{
	int ret;
	unsigned long long i;
	unsigned long long max_cnt;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_state *cached_state = NULL;
	u64 isize;
	u64 len;

	if (args->start == (u64)-1)
		return -EINVAL;

	if (args->act == SYNO_RBD_META_MAPPING)
		max_cnt = (args->size - sizeof(struct syno_rbd_meta_ioctl_args)) /
			sizeof(struct syno_rbd_meta_file_mapping);
	else
		max_cnt = U64_MAX;

	isize = ALIGN_DOWN(inode->i_size, fs_info->sectorsize);

	args->cnt = 0;
	for (i = 0; i < max_cnt; i++) {
		u64 logical_block_start;
		u64 physical_block_start;

		if (args->start >= isize) {
			args->start = (u64)-1;
			break;
		}
		len = isize - args->start;

		lock_extent_bits(io_tree, args->start, isize - 1, &cached_state);
		ret = lookup_rbd_meta_file_extent(fs_info, inode, NULL,
						  args->start, &logical_block_start,
						  &physical_block_start, &len);
		unlock_extent_cached(io_tree, args->start, isize - 1, &cached_state);
		if (ret)
			goto out;
		if (args->act == SYNO_RBD_META_MAPPING) {
			args->mappings[i].length = len;
			args->mappings[i].dev_offset = physical_block_start;
		}
		args->start += len;
		args->cnt++;
	}

	if (args->start >= isize)
		args->start = (u64)-1;

	ret = 0;
out:
	return ret;
}

static int iterate_all_file_records(struct btrfs_fs_info *fs_info,
		int (*handler)(struct btrfs_fs_info *fs_info,
			       const u64 subvol_id,
			       const u64 i_ino,
			       const u64 generation));
static int pin_file_helper(struct btrfs_fs_info *fs_info,
			   const u64 subvol_id,
			   const u64 i_ino,
			   const u64 generation);

int btrfs_activate_all_rbd_meta_files(struct btrfs_fs_info *fs_info)
{
	return iterate_all_file_records(fs_info, pin_file_helper);
}

static struct inode *get_file_inode(struct btrfs_fs_info *fs_info,
				    const u64 subvol_id,
				    const u64 i_ino,
				    const u64 generation)
{
	struct btrfs_root *root;
	struct super_block *sb = fs_info->sb;
	struct inode *inode = NULL;

	root = btrfs_get_fs_root(fs_info, subvol_id, true);
	if (IS_ERR(root))
		return ERR_PTR(PTR_ERR(root));

	inode = btrfs_iget(sb, i_ino, root);
	if (IS_ERR(inode))
		goto out;

	if (generation != BTRFS_I(inode)->generation) {
		btrfs_warn(fs_info,
	"generation %llu of inode <%llu, %llu> record is not equal to %llu",
			   generation, subvol_id, i_ino,
			   BTRFS_I(inode)->generation);
		iput(inode);
		inode = ERR_PTR(-EINVAL);
	}
out:
	btrfs_put_root(root);
	return inode;
}

static int pin_file_helper(struct btrfs_fs_info *fs_info,
			   const u64 subvol_id,
			   const u64 i_ino,
			   const u64 generation)
{
	int ret;
	struct inode *inode;

	inode = get_file_inode(fs_info, subvol_id,
			       i_ino, generation);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	ret = btrfs_pin_rbd_meta_file(inode);
	if (ret) {
		btrfs_unpin_rbd_meta_file(inode);
		goto out;
	}
out:
	// for get_file_inode.
	iput(inode);
	return ret;
}

static int iterate_inode_records(struct btrfs_fs_info *fs_info,
				 struct btrfs_root *feat_root,
				 const u64 subvol_id,
			int (*handler)(struct btrfs_fs_info *fs_info,
				       const u64 subvol_id,
				       const u64 i_ino,
				       const u64 generation))
{
	int ret;
	int cnt;
	int slot;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_rbd_meta_file_inode_record_item *item;
	struct extent_buffer *leaf;
	u32 nritems;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = subvol_id;
	key.type = SYNO_BTRFS_RBD_META_FILE_INODE_RECORD;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, feat_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	cnt = 0;
	while (1) {
		u64 generation;
		leaf = path->nodes[0];
		slot = path->slots[0];
		nritems = btrfs_header_nritems(leaf);
		if (slot >= nritems) {
			ret = btrfs_next_leaf(feat_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			continue;
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);

		if (key.objectid != subvol_id ||
		    key.type != SYNO_BTRFS_RBD_META_FILE_INODE_RECORD)
			break;

		item = btrfs_item_ptr(leaf, slot,
			struct btrfs_rbd_meta_file_inode_record_item);

		generation = btrfs_syno_rbd_meta_file_inode_record_generation(
				leaf, item);

		ret = handler(fs_info, subvol_id, key.offset,
			      generation);
		if (ret)
			goto out;

		cnt++;
		path->slots[0]++;
	}
	ret = cnt;
out:
	btrfs_free_path(path);
	return ret;
}

static int iterate_all_file_records(struct btrfs_fs_info *fs_info,
		int (*handler)(struct btrfs_fs_info *fs_info,
			       const u64 subvol_id,
			       const u64 i_ino,
			       const u64 generation))
{
	int ret;
	int slot;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *feat_root = fs_info->syno_feat_root;
	struct btrfs_rbd_meta_file_subvol_record_item *item;
	struct extent_buffer *leaf;
	u64 subvol_id;
	u32 nritems;
	u32 inode_cnt;

	if (!btrfs_syno_check_feat_tree_enable(fs_info))
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = SYNO_BTRFS_RBD_META_FILE_SUBVOL_RECORD;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, feat_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		nritems = btrfs_header_nritems(leaf);
		if (slot >= nritems) {
			ret = btrfs_next_leaf(feat_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			continue;
		}
		btrfs_item_key_to_cpu(leaf, &key, slot);

		if (key.objectid != 0 ||
		    key.type != SYNO_BTRFS_RBD_META_FILE_SUBVOL_RECORD)
			break;

		subvol_id = key.offset;
		item = btrfs_item_ptr(leaf, slot,
			struct btrfs_rbd_meta_file_subvol_record_item);

		inode_cnt = btrfs_syno_rbd_meta_file_subvol_record_inode_cnt(
				leaf, item);
		ret = iterate_inode_records(fs_info, feat_root,
					    subvol_id, handler);
		if (0 > ret)
			goto out;
		if (ret != inode_cnt)
			btrfs_warn(fs_info,
		"the count (%d) of inode is not match subvol %llu record (%u)",
				   ret, subvol_id, inode_cnt);

		path->slots[0]++;
	}
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int delete_all_inode_records(struct btrfs_fs_info *fs_info,
				    struct btrfs_root *feat_root,
				    struct btrfs_path *path,
				    u64 subvol_id)
{
	int ret;
	int slot;
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u32 nritems;
	int del_nr;

	while (1) {
		trans = btrfs_start_transaction_fallback_global_rsv(feat_root, 1);
		if (IS_ERR(trans))
			goto out;

		key.objectid = subvol_id;
		key.type = SYNO_BTRFS_RBD_META_FILE_INODE_RECORD;
		key.offset = 0;

		ret = btrfs_search_slot(trans, feat_root, &key, path, -1, 1);
		if (ret < 0)
			goto end_trans;

		leaf = path->nodes[0];
		slot = path->slots[0];
		nritems = btrfs_header_nritems(leaf);

		for (del_nr = 0; slot < nritems; del_nr++, slot++) {
			btrfs_item_key_to_cpu(leaf, &key, slot);
			if (key.objectid != subvol_id ||
			    key.type != SYNO_BTRFS_RBD_META_FILE_INODE_RECORD)
				break;
		}

		if (!del_nr)
			break;

		ret = btrfs_del_items(trans, feat_root, path, path->slots[0], del_nr);
		if (ret)
			goto end_trans;
		btrfs_release_path(path);

		ret = btrfs_end_transaction_throttle(trans);
		if (ret)
			goto out;
		trans = NULL;
	}

	ret = 0;
end_trans:
	if (trans)
		btrfs_end_transaction_throttle(trans);
out:
	btrfs_release_path(path);
	return ret;
}

int btrfs_delete_all_rbd_meta_file_records(struct inode *inode)
{
	int ret;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *feat_root = fs_info->syno_feat_root;
	u64 subvol_id;

	if (!btrfs_syno_check_feat_tree_enable(fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		trans = btrfs_start_transaction_fallback_global_rsv(feat_root, 1);
		if (IS_ERR(trans))
			goto out;

		key.objectid = 0;
		key.type = SYNO_BTRFS_RBD_META_FILE_SUBVOL_RECORD;
		key.offset = 0;
		ret = btrfs_search_slot(trans, feat_root, &key, path, -1, 1);
		if (ret < 0)
			goto end_trans;

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid != 0 ||
		    key.type != SYNO_BTRFS_RBD_META_FILE_SUBVOL_RECORD)
			break;

		subvol_id = key.offset;
		ret = btrfs_del_item(trans, feat_root, path);
		if (ret)
			goto end_trans;
		btrfs_release_path(path);
		ret = btrfs_end_transaction_throttle(trans);
		if (ret)
			goto out;
		trans = NULL;

		ret = delete_all_inode_records(fs_info, feat_root,
					       path, subvol_id);
		if (ret)
			goto out;
	}
	ret = 0;
end_trans:
	if (trans)
		btrfs_end_transaction_throttle(trans);
out:
	btrfs_free_path(path);
	return ret;
}

static int delete_subvol_record(struct btrfs_fs_info *fs_info,
				struct btrfs_trans_handle *trans,
				struct btrfs_root *feat_root,
				struct btrfs_path *path,
				struct btrfs_key key)
{
	int ret;

	ret = btrfs_search_slot(trans, feat_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (ret) {
		ret = 0;
		goto out;
	}

	ret = btrfs_del_item(trans, feat_root, path);
	if (ret)
		goto out;

	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

static int insert_subvol_record(struct btrfs_fs_info *fs_info,
				struct btrfs_trans_handle *trans,
				struct btrfs_root *feat_root,
				struct btrfs_path *path,
				struct btrfs_key key)
{
	int ret;
	struct extent_buffer *leaf;
	struct btrfs_rbd_meta_file_subvol_record_item *item;

	ret = btrfs_insert_empty_item(trans, feat_root, path, &key,
		sizeof(struct btrfs_rbd_meta_file_subvol_record_item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_rbd_meta_file_subvol_record_item);

	btrfs_set_syno_rbd_meta_file_subvol_record_inode_cnt(leaf, item, 1);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

static int update_subvol_record(struct btrfs_fs_info *fs_info,
				struct btrfs_trans_handle *trans,
				struct btrfs_root *feat_root,
				struct btrfs_path *path,
				const u64 subvol_id,
				const int insert_cnt)
{
	int ret;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_rbd_meta_file_subvol_record_item *item;
	u32 inode_cnt = 0;

	key.objectid = 0;
	key.type = SYNO_BTRFS_RBD_META_FILE_SUBVOL_RECORD;
	key.offset = subvol_id;

	ret = btrfs_search_slot(trans, feat_root, &key, path, 0, 1);
	if (ret < 0)
		goto out;
	else if (ret) {
		if (insert_cnt < 0) {
			ret = 0;
			goto out;
		}
		goto insert;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_rbd_meta_file_subvol_record_item);

	inode_cnt = btrfs_syno_rbd_meta_file_subvol_record_inode_cnt(leaf, item);
	if (inode_cnt <= 1 && insert_cnt < 0)
		goto delete;

	btrfs_set_syno_rbd_meta_file_subvol_record_inode_cnt(leaf, item,
			inode_cnt + insert_cnt);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
insert:
	btrfs_release_path(path);
	return insert_subvol_record(fs_info, trans,
				    feat_root, path, key);
delete:
	btrfs_release_path(path);
	return delete_subvol_record(fs_info, trans,
				    feat_root, path, key);
}

/*
 * return 1: delete success
 *        0: record is not exist
 *       -1: error
 */
static int delete_inode_record(struct btrfs_fs_info *fs_info,
			       struct btrfs_trans_handle *trans,
			       struct btrfs_root *feat_root,
			       struct btrfs_path *path,
			       u64 subvol_id,
			       u64 i_ino)
{
	int ret;
	struct btrfs_key key;

	key.objectid = subvol_id;
	key.type = SYNO_BTRFS_RBD_META_FILE_INODE_RECORD;
	key.offset = i_ino;

	ret = btrfs_search_slot(trans, feat_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (ret) {
		ret = 0;
		goto out;
	}

	ret = btrfs_del_item(trans, feat_root, path);
	if (ret)
		goto out;

	ret = 1;
out:
	btrfs_release_path(path);
	return ret;
}

static int insert_inode_record(struct btrfs_fs_info *fs_info,
			       struct btrfs_trans_handle *trans,
			       struct btrfs_root *feat_root,
			       struct btrfs_path *path,
			       struct inode *inode,
			       u64 subvol_id,
			       u64 i_ino)
{
	int ret;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_rbd_meta_file_inode_record_item *item;

	key.objectid = subvol_id;
	key.type = SYNO_BTRFS_RBD_META_FILE_INODE_RECORD;
	key.offset = i_ino;

	ret = btrfs_insert_empty_item(trans, feat_root, path, &key,
			sizeof(struct btrfs_rbd_meta_file_inode_record_item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_rbd_meta_file_inode_record_item);

	btrfs_set_syno_rbd_meta_file_inode_record_generation(leaf, item,
			BTRFS_I(inode)->generation);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

static int delete_rbd_meta_file_record(struct inode *inode)
{
	int ret;
	int func;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *feat_root = fs_info->syno_feat_root;
	struct btrfs_path *path;
	u64 subvol_id = BTRFS_I(inode)->root->root_key.objectid;
	u64 i_ino = btrfs_ino(BTRFS_I(inode));

	if (!btrfs_syno_check_feat_tree_enable(fs_info))
		return 0;

	// 1 for inode record item, 1 for subvol record item.
	trans = btrfs_start_transaction(feat_root, 2);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto err;
	}

	ret = delete_inode_record(fs_info, trans, feat_root,
				  path, subvol_id, i_ino);
	if (ret < 0)
		goto err;
	else if (!ret)
		goto out;

	ret = update_subvol_record(fs_info, trans, feat_root,
				   path, subvol_id, -1);
	if (ret)
		goto rollback;
out:
	btrfs_free_path(path);
	return btrfs_commit_transaction(trans);

rollback:
	func = insert_inode_record(fs_info, trans, feat_root,
				   path, inode, subvol_id, i_ino);
	if (func)
		btrfs_warn(fs_info,
	"failed to rollback rbd meta file inode record item with [%llu,%llu]",
			   subvol_id, i_ino);
err:
	btrfs_free_path(path);
	btrfs_end_transaction(trans);
	return ret;
}

static int insert_rbd_meta_file_record(struct inode *inode)
{
	int ret;
	int func;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *feat_root = fs_info->syno_feat_root;
	struct btrfs_path *path;
	u64 subvol_id;
	u64 i_ino;

	if (!btrfs_syno_check_feat_tree_enable(fs_info))
		return -EINVAL;

	// 1 for inode record item, 1 for subvol record item.
	trans = btrfs_start_transaction(feat_root, 2);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto err;
	}

	subvol_id = BTRFS_I(inode)->root->root_key.objectid;
	i_ino = btrfs_ino(BTRFS_I(inode));

	ret = insert_inode_record(fs_info, trans, feat_root,
				  path, inode, subvol_id, i_ino);
	if (ret)
		goto err;

	ret = update_subvol_record(fs_info, trans, feat_root,
				   path, subvol_id, 1);
	if (ret)
		goto rollback;

	btrfs_free_path(path);
	return btrfs_commit_transaction(trans);

rollback:
	func = delete_inode_record(fs_info, trans, feat_root,
				   path, subvol_id, i_ino);
	if (func < 0)
		btrfs_warn(fs_info,
	"failed to rollback rbd meta file inode record with [%llu,%llu]",
			   subvol_id, i_ino);
err:
	btrfs_free_path(path);
	btrfs_end_transaction(trans);
	return ret;
}

static int lookup_rbd_meta_file_extent(struct btrfs_fs_info *fs_info,
				       struct inode *inode,
				       struct btrfs_device **device,
				       u64 start,
				       u64 *logical_block_start,
				       u64 *physical_block_start,
				       u64 *len_out)
{
	int ret;
	struct extent_map *em = NULL;
	u64 len = *len_out;

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, start, len);
	if (IS_ERR(em))
		return PTR_ERR(em);

	if (em->block_start == EXTENT_MAP_HOLE) {
		btrfs_info(fs_info, "rbd meta file must not have holes");
		ret = -EINVAL;
		goto out;
	}
	if (em->block_start == EXTENT_MAP_INLINE) {
		btrfs_info(fs_info, "rbd meta file must not be inline");
		ret = -EINVAL;
		goto out;
	}
	if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
		btrfs_info(fs_info, "rbd meta file must not be compressed");
		ret = -EINVAL;
		goto out;
	}

	*logical_block_start = em->block_start + (start - em->start);
	len = min(len, em->len - (start - em->start));
	free_extent_map(em);
	em = NULL;

	ret = can_nocow_extent(inode, start, &len, NULL, NULL, NULL, true);
	if (ret < 0)
		goto out;
	else if (!ret) {
		btrfs_info(fs_info,
			   "rbd meta file must not be copy-on-write");
		ret = -EAGAIN;
		goto out;
	}

	em = btrfs_get_chunk_map(fs_info, *logical_block_start, len);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out;
	}

	if (em->map_lookup->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
		btrfs_info(fs_info,
			   "rbd meta file must have single data profile");
		ret = -EINVAL;
		goto out;
	}

	if (device) {
		if (*device == NULL)
			*device = em->map_lookup->stripes[0].dev;
		else if (*device != em->map_lookup->stripes[0].dev) {
			btrfs_info(fs_info, "swapfile must be on one device");
			ret = -EINVAL;
			goto out;
		}
	}

	*physical_block_start = (em->map_lookup->stripes[0].physical +
				(*logical_block_start - em->start));
	len = min(len, em->len - (*logical_block_start - em->start));

	*len_out = len;
	ret = 0;
out:
	if (!IS_ERR_OR_NULL(em))
		free_extent_map(em);
	return ret;
}

void btrfs_unpin_rbd_meta_file(struct inode *inode)
{
	struct btrfs_fs_info *fs_info;

	if (!inode || list_empty(&(BTRFS_I(inode)->syno_rbd_meta_file)))
		goto out;

	fs_info = BTRFS_I(inode)->root->fs_info;
	spin_lock(&fs_info->syno_rbd.lock);
	if (!list_empty(&(BTRFS_I(inode)->syno_rbd_meta_file))) {
		list_del_init(&(BTRFS_I(inode)->syno_rbd_meta_file));
	} else {
		spin_unlock(&fs_info->syno_rbd.lock);
		goto out;
	}
	spin_unlock(&fs_info->syno_rbd.lock);
	btrfs_free_swapfile_pins(inode);
	atomic_dec(&BTRFS_I(inode)->root->nr_swapfiles);

	inode_lock(inode);
	inode->i_flags &= ~S_SWAPFILE;
	inode_unlock(inode);

	iput(inode);
out:
	return;
}

static int btrfs_pin_rbd_meta_file(struct inode *inode)
{
	bool is_dev_pinned = false;
	int ret;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_state *cached_state = NULL;
	struct btrfs_device *device = NULL;
	u64 start;
	u64 isize;

	inode_lock(inode);
	if (IS_SWAPFILE(inode)) {
		ret = -EBUSY;
		goto unlock_inode;
	}
	/*
	 * If the meta file file was just created, make sure delalloc is done. If the
	 * file changes again after this, the user is doing something stupid and
	 * we don't really care.
	 */
	ret = btrfs_wait_ordered_range(inode, 0, (u64)-1);
	if (ret)
		goto unlock_inode;

	if (BTRFS_I(inode)->flags & BTRFS_INODE_COMPRESS) {
		btrfs_info(fs_info, "rbd meta file must not be compressed");
		ret = -EINVAL;
		goto unlock_inode;
	}
	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW)) {
		btrfs_info(fs_info, "rbd meta file must not be copy-on-write");
		ret = -EINVAL;
		goto unlock_inode;
	}
	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)) {
		btrfs_info(fs_info, "rbd meta file must not be checksummed");
		ret = -EINVAL;
		goto unlock_inode;
	}
	if (!IS_IMMUTABLE(inode)) {
		btrfs_info(fs_info, "rbd meta file must be immutable");
		ret = -EINVAL;
		goto unlock_inode;
	}
	isize = ALIGN_DOWN(inode->i_size, fs_info->sectorsize);
	if (!isize) {
		ret = -EINVAL;
		goto unlock_inode;
	}
	/*
	 * Balance or device remove/replace/resize can move stuff around from
	 * under us. The EXCL_OP flag makes sure they aren't running/won't run
	 * concurrently while we are mapping the swap extents, and
	 * fs_info->swapfile_pins prevents them from running while the rbd meta file
	 * is active and moving the extents. Note that this also prevents a
	 * concurrent device add which isn't actually necessary, but it's not
	 * really worth the trouble to allow it.
	 */
	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_SWAP_ACTIVATE)) {
		btrfs_info(fs_info,
	"cannot activate rbd-meta while exclusive operation is running");
		ret = -EBUSY;
		goto unlock_inode;
	}

	/*
	 * Prevent snapshot creation while we are activating the swap file.
	 * We do not want to race with snapshot creation. If snapshot creation
	 * already started before we bumped nr_swapfiles from 0 to 1 and
	 * completes before the first write into the swap file after it is
	 * activated, than that write would fallback to COW.
	 */
	if (!btrfs_drew_try_write_lock(&root->snapshot_lock)) {
		btrfs_warn(fs_info,
	   "cannot activate rbd-meta because snapshot creation is in progress");
		ret = -EINVAL;
		goto finish_op;
	}
	/*
	 * Snapshots can create extents which require COW even if NODATACOW is
	 * set. We use this counter to prevent snapshots. We must increment it
	 * before walking the extents because we don't want a concurrent
	 * snapshot to run after we've already checked the extents.
	 */
	atomic_inc(&BTRFS_I(inode)->root->nr_swapfiles);
	spin_lock(&fs_info->syno_rbd.lock);
	list_add(&(BTRFS_I(inode)->syno_rbd_meta_file),
		 &fs_info->syno_rbd.pinned_meta_files);
	spin_unlock(&fs_info->syno_rbd.lock);
	// We need to hold inode until this inode been deactivated.
	ihold(inode);

	lock_extent_bits(io_tree, 0, isize - 1, &cached_state);
	start = 0;
	while (start < isize) {
		u64 logical_block_start, physical_block_start;
		struct btrfs_block_group *bg;
		u64 len = isize - start;
		ret = lookup_rbd_meta_file_extent(fs_info, inode,
						  &device, start,
						  &logical_block_start,
						  &physical_block_start,
						  &len);
		if (ret) {
			btrfs_info(fs_info,
		"could not lookup rbd meta file extent, with start %llu",
				   start);
			goto unlock_extent;
		}

		if (device && !is_dev_pinned) {
			ret = btrfs_add_swapfile_pin(inode, device, false);
			if (0 > ret) {
				btrfs_info(fs_info, "could not pin device");
				goto unlock_extent;
			}
			is_dev_pinned = true;
		}

		bg = btrfs_lookup_block_group(fs_info, logical_block_start);
		if (!bg) {
			btrfs_info(fs_info,
			"could not find block group containing rbd meta file");
			ret = -EINVAL;
			goto unlock_extent;
		}

		if (!btrfs_inc_block_group_swap_extents(bg)) {
			btrfs_warn(fs_info,
			   "block group for rbd-meta at %llu is read-only%s",
			   bg->start,
			   atomic_read(&fs_info->scrubs_running) ?
				       " (scrub running)" : "");
			btrfs_put_block_group(bg);
			ret = -EINVAL;
			goto unlock_extent;
		}
		ret = btrfs_add_swapfile_pin(inode, bg, true);
		if (ret) {
			btrfs_put_block_group(bg);
			if (ret == 1)
				ret = 0;
			else {
				btrfs_info(fs_info, "could not pin block group");
				goto unlock_extent;
			}
		}
		start += len;
	}
	inode->i_flags |= S_SWAPFILE;
	ret = 0;

unlock_extent:
	unlock_extent_cached(io_tree, 0, isize - 1, &cached_state);
	btrfs_drew_write_unlock(&root->snapshot_lock);
finish_op:
	btrfs_exclop_finish(fs_info);
unlock_inode:
	inode_unlock(inode);
	return ret;
}
