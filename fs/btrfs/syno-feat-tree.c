#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2020 Synology Inc.  All rights reserved.
 */

#include "ctree.h"
#include "xattr.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "syno-feat-tree.h"
#include "disk-io.h"
#include "locking.h"

static inline int syno_feat_tree_update_helper(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *key,
					u32 data_size)
{
	int ret;

	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret > 0) {
		btrfs_release_path(path);
		// insert new item
		ret = btrfs_insert_empty_item(trans, root, path, key, data_size);
	}
	return ret;
}

static int __btrfs_create_syno_feat_tree(struct btrfs_trans_handle *trans, struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *syno_feat_root;

	syno_feat_root = btrfs_create_tree(trans, BTRFS_SYNO_FEATURE_TREE_OBJECTID);
	if (IS_ERR(syno_feat_root)) {
		ret = PTR_ERR(syno_feat_root);
		goto out;
	}
	fs_info->syno_feat_root = syno_feat_root;

	ret = 0;
out:
	return ret;
}

static int btrfs_syno_feat_tree_status_update(struct btrfs_trans_handle *trans, struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *root = fs_info->syno_feat_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int data_size;
	struct extent_buffer *leaf;
	struct btrfs_syno_feat_tree_status_item *ei;

	if (!btrfs_syno_check_feat_tree_enable(fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_FEAT_TREE_STATUS_OBJECTID;
	key.type = SYNO_BTRFS_FEAT_TREE_STATUS_KEY;
	key.offset = 0;

	data_size = sizeof(struct btrfs_syno_feat_tree_status_item);

	ret = syno_feat_tree_update_helper(trans, root, path, &key, data_size);
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_feat_tree_status_item);
	btrfs_set_syno_feat_tree_status_version(leaf, ei, fs_info->syno_feat_tree_status.version);
	btrfs_set_syno_feat_tree_status_status(leaf, ei, fs_info->syno_feat_tree_status.status);

	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	if (ret)
		btrfs_abort_transaction(trans, ret);
	btrfs_free_path(path);
	return ret;
}

static int btrfs_create_syno_feat_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *tree_root = fs_info->tree_root;
	int ret;

	trans = btrfs_start_transaction(tree_root, 3);
	if (IS_ERR(trans)) {
		return PTR_ERR(trans);
	}

	/* root item x 1 + root node x 1 */
	ret = __btrfs_create_syno_feat_tree(trans, fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to create syno feature tree, ret=[%d]", ret);
		goto out;
	}

	btrfs_syno_set_feat_tree_enable(fs_info);

	/* tree item x 1 */
	ret = btrfs_syno_feat_tree_status_update(trans, fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to update status of syno feature tree at the first time, ret=[%d]", ret);
		btrfs_syno_set_feat_tree_disable(fs_info);
		goto out;
	}

	return btrfs_commit_transaction(trans);

out:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_syno_feat_tree_enable(struct btrfs_fs_info *fs_info)
{
	int ret = -1;

	if (btrfs_syno_check_feat_tree_enable(fs_info)) {
		ret = 0;
		goto out;
	}

#ifdef MY_ABC_HERE
	/*
	 * Don't clean up existing feature-tree
	 */
#else
	btrfs_syno_set_feat_tree_disable(fs_info);

	// clean up old tree
	if (fs_info->syno_feat_root) {
		btrfs_err(fs_info, "we are going to clean up syno feature tree because the status is not enabled");
		ret = btrfs_syno_feat_tree_disable(fs_info);
		if (ret) {
			btrfs_err(fs_info, "failed to disable and clean up syno feature tree, ret: [%d].", ret);
			goto out;
		}
	}
#endif /* MY_ABC_HERE */

	ret = btrfs_create_syno_feat_tree(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to create syno feature tree, ret: [%d].", ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static inline int syno_feat_tree_need_stop(struct btrfs_fs_info *fs_info)
{
	return sb_rdonly(fs_info->sb) || btrfs_fs_closing(fs_info);
}

#ifdef MY_ABC_HERE
	/*
	 * locker will store information in feature-tree, and we cannot provide any
	 * abilities to remove feature-tree.
	 */
#else
static int btrfs_clear_syno_feat_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = fs_info->syno_feat_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int nr;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = 0;
	key.offset = 0;

	while (1) {
		trans = btrfs_start_transaction_fallback_global_rsv(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto end_trans;

		nr = btrfs_header_nritems(path->nodes[0]);
		if (!nr)
			break;

		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret) {
			btrfs_err(root->fs_info, "failed to delete item of syno feature tree, ret: [%d].", ret);
			goto end_trans;
		}

		btrfs_release_path(path);
		ret = btrfs_end_transaction_throttle(trans);
		if (ret)
			goto out;
		trans = NULL;

		if (syno_feat_tree_need_stop(root->fs_info)) {
			ret = -EAGAIN;
			btrfs_debug(root->fs_info, "drop tree early exit, ret: [%d].", ret);
			goto out;
		}
		if (fatal_signal_pending(current)) {
			btrfs_debug(root->fs_info, "catch interrupt singal as cleaning up syno feature tree.");
			ret = -EINTR;
			goto out;
		}
	}

	ret = 0;
end_trans:
	if (trans)
		btrfs_end_transaction_throttle(trans);
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_delete_syno_feat_tree_root(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *syno_feat_root = fs_info->syno_feat_root;
	int ret;

	trans = btrfs_start_transaction(tree_root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	fs_info->syno_feat_root = NULL;

	ret = btrfs_del_root(trans, &syno_feat_root->root_key);
	if (ret) {
		btrfs_err(fs_info, "failed to delete the root item about syno feature tree, ret: [%d].", ret);
		goto abort;
	}

	list_del(&syno_feat_root->dirty_list);

	btrfs_tree_lock(syno_feat_root->node);
	btrfs_clean_tree_block(syno_feat_root->node);
	btrfs_tree_unlock(syno_feat_root->node);
	btrfs_free_tree_block(trans, syno_feat_root, syno_feat_root->node, 0, 1);

	btrfs_put_root(syno_feat_root);

	return btrfs_commit_transaction(trans);

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_syno_feat_tree_disable(struct btrfs_fs_info *fs_info)
{
	int ret;

	if (!fs_info->syno_feat_root) {
		ret = 0;
		goto out;
	}

	btrfs_syno_set_feat_tree_disable(fs_info);

	ret = btrfs_clear_syno_feat_tree(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to clean up syno feature tree, ret: [%d].", ret);
		goto out;
	}

	ret = btrfs_delete_syno_feat_tree_root(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to delete syno feature tree root, ret: [%d].", ret);
		goto out;
	}
	btrfs_info(fs_info, "have finished to clean up syno feature tree, ret: [%d].", ret);

	ret = 0;
out:
	return ret;
}
#endif /* MY_ABC_HERE */

int btrfs_syno_feat_tree_load_status_from_disk(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->syno_feat_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_syno_feat_tree_status_item *ei;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;

	if (!fs_info->syno_feat_root) {
		btrfs_info(fs_info, "root of syno feature tree is null");
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_err(fs_info, "failed to allocate path, err: [%ld]", PTR_ERR(path));
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_SYNO_FEAT_TREE_STATUS_OBJECTID;
	key.type = SYNO_BTRFS_FEAT_TREE_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		btrfs_err(fs_info, "failed to search slot on syno feature tree, ret: [%d].", ret);
		goto out;
	} else if (ret > 0) { /* tree empty */
		btrfs_warn(fs_info, "feature tree is empty.");
		ret = 0;
		goto out;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_syno_feat_tree_status_item);
	fs_info->syno_feat_tree_status.version = btrfs_syno_feat_tree_status_version(leaf, ei);
	fs_info->syno_feat_tree_status.status = btrfs_syno_feat_tree_status_status(leaf, ei);

	switch(fs_info->syno_feat_tree_status.status) {
	case SYNO_FEAT_TREE_ST_ENABLE:
		btrfs_info(fs_info, "syno feature tree is enabled");
		break;
	case SYNO_FEAT_TREE_ST_DISABLE:
		btrfs_warn(fs_info, "disable status [%llx] has detected. We are going to clean up feautre tree.", fs_info->syno_feat_tree_status.status);
		break;
	default:
		btrfs_err(fs_info, "invalid status [%llx] of syno feature tree has detected.", fs_info->syno_feat_tree_status.status);
		break;
	}

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}
