#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2021 Synology Inc.  All rights reserved.
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
#include <linux/sched.h>
#include <linux/rbtree.h>
#include "ctree.h"
#include "disk-io.h"
#include "backref.h"
#include "btrfs_inode.h"

/* send.c */
extern int write_buf(struct file *filp, const void *buf, u32 len,
		     loff_t *off);
extern int tree_advance_with_mode(struct btrfs_path *path, int *level,
				  int root_level, int mode,
				  struct btrfs_key *key);

static int snap_entry_cmp(struct btrfs_snapshot_size_entry *e1,
			  struct btrfs_snapshot_size_entry *e2);
static int snap_entry_insert(struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry **insert,
			     int replace);
static inline int
snap_entry_check_node_shared(struct btrfs_fs_info *fs_info,
			     struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry *entry);
static inline bool
snap_entry_check_file_extent(struct btrfs_snapshot_size_entry *entry);
static inline int
snap_entry_find_extent_owner(struct btrfs_fs_info *fs_info,
			     struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry *entry,
			     u64 *owner_id);
static inline void
snap_entry_update_usage(struct btrfs_snapshot_size_ctx *ctx,
			struct btrfs_snapshot_size_entry *entry,
			u64 owner_id,
			struct btrfs_ioctl_snapshot_size_query_args *args);

static inline void free_snapshot_size_ctx(struct btrfs_snapshot_size_ctx *ctx,
					  u64 snap_count);
static inline struct btrfs_snapshot_size_ctx*
prepare_snapshot_size_ctx(struct btrfs_fs_info *fs_info,
			  struct btrfs_ioctl_snapshot_size_query_args *args);

static int
show_calculate_progress(struct btrfs_snapshot_size_ctx *ctx,
			struct btrfs_ioctl_snapshot_size_query_args *args,
			u64 skip_bytes);

int
btrfs_snapshot_size_query(struct file *file,
			  struct btrfs_ioctl_snapshot_size_query_args *args)
{
	int i;
	int ret = 0;
	u64 snap_count = args->snap_count;
	struct btrfs_fs_info *fs_info;
	struct btrfs_snapshot_size_ctx *ctx;
	struct rb_node *node;
	struct btrfs_snapshot_size_entry *entry;

	fs_info = BTRFS_I(file_inode(file))->root->fs_info;
	ctx = prepare_snapshot_size_ctx(fs_info, args);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		ctx = NULL;
		goto out;
	}

	/*
	 * The loop are composed of 3 parts:
	 * 1> Check whether the given node is shared or not
	 * 2> Check shared EXTENT_ITEM and make statistic about the usage
	 * 3> Travel deeper node
	 */
	while (!RB_EMPTY_ROOT(&ctx->root)) {
		int nritems;
		int advance = ADVANCE;
		struct extent_buffer *eb;
		struct btrfs_snapshot_size_entry *next_entry;
		struct rb_node *next_node;

		// DSM#108202: avoid soft lockup
		// DSM#111569: check signal and exit if user cancels
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		cond_resched();

		/*
		 * 1st part:
		 * Check if the given node is shared or not, that is to say,
		 * we check whether the node is pointed by subvolumes
		 * (snapshots) that are not given by ioctl(). The node is
		 * shared if the callback function returns postive value (>0)
		 * and we make sure all descendants are shared and it has no
		 * need of deeper traveling of the tree.
		 */
		ret = 0;
		node = rb_first(&ctx->root);
		entry = rb_entry(node, struct btrfs_snapshot_size_entry,
				 node);
		if (entry->level != entry->root_level &&
		    entry->path->slots[entry->level] == 0) {
			ret = snap_entry_check_node_shared(fs_info, ctx,
							   entry);
			args->processed_size +=
				entry->root->fs_info->nodesize;
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				advance = ADVANCE_ONLY_UPNEXT;
				goto advance;
			}
		}

		/*
		 * 2nd part:
		 * All slots in the leaf node are processed at the same time.
		 * If the slot is not a hole(bytenr ==0) and EXTENT_ITEM is
		 * only referred by the given subvolumes, the usage of
		 * subvolume with the largest ID that pointer to it should be
		 * accumulated.
		 */
		eb = entry->path->nodes[entry->level];
		nritems = btrfs_header_nritems(eb);
		while (entry->level == 0 &&
		       entry->path->slots[entry->level] < nritems) {
			u64 owner_id;

			// DSM#108202: avoid soft lockup
			// DSM#111569: check signal and exit if user cancels
			if (signal_pending(current)) {
				ret = -EINTR;
				break;
			}
			cond_resched();

			btrfs_item_key_to_cpu(entry->path->nodes[0],
					      &entry->key,
					      entry->path->slots[0]);

			if (!snap_entry_check_file_extent(entry))
				goto next;

			ret = snap_entry_find_extent_owner(fs_info, ctx,
							   entry, &owner_id);
			if (ret < 0)
				goto out;
			if (ret == 0) {
				snap_entry_update_usage(ctx, entry, owner_id,
							args);
			}
next:
			entry->path->slots[0]++;
		}
		/* do verbose display */
		if (show_calculate_progress(ctx, args, 100 * 1024 * 1024)) {
			ret = -EBADF;
			goto out;
		}
advance:
		/*
		 * 3rd part:
		 * Now we've done processing this node, advance the tree node.
		 * There's a subtlety here: After tree advacne, if there's a
		 * subvolume whose next node to be processed is the same as
		 * this node. We'll keep only one subolume to handle this
		 * node. The other one needs to keep advancing until the next
		 * node to be processed for that subvolume is not overlapped
		 * with the existing one.
		 */
		next_node = rb_next(node);
		if (tree_advance_with_mode(entry->path, &entry->level,
					   entry->root_level, advance,
					   &entry->key) < 0) {
			rb_erase(&entry->node, &ctx->root);
			continue;
		}

		if (!next_node)
			continue;

		next_entry = rb_entry(next_node,
				      struct btrfs_snapshot_size_entry, node);
		/*
		 * After advance if this entry is still the lowest key in the
		 * tree, don't move it out and insert again. Is's just waste
		 * of time.
		 */
		if (snap_entry_cmp(entry, next_entry) < 0)
			continue;

		rb_erase(&entry->node, &ctx->root);
		RB_CLEAR_NODE(&entry->node);
		while (snap_entry_insert(ctx, &entry, 1)) {
			if (tree_advance_with_mode(entry->path, &entry->level,
						   entry->root_level,
						   ADVANCE_ONLY_NEXT,
						   &entry->key) < 0) {
				/*
				 * This node is not in the tree anymore, so
				 * don't call rb_erase on it.
				 */
				break;
			}
		}
	}

	// store the result
	i = 0;
	node = rb_first(&ctx->snap_roots->root);
	while (node) {
		struct ulist_node *ulist_node = rb_entry(node,
							 struct ulist_node,
							 rb_node);
		entry = (struct btrfs_snapshot_size_entry *) ulist_node->aux;
		args->id_maps[i].snap_id = ulist_node->val;
		args->id_maps[i++].marginal_size = entry->snap_exclusive_size;
		node = rb_next(node);
	}
out:
	free_snapshot_size_ctx(ctx, snap_count);
	return ret;
}

static int snap_entry_cmp(struct btrfs_snapshot_size_entry *e1,
			  struct btrfs_snapshot_size_entry *e2)
{
	int cmp;
	u64 e1_blockptr;
	u64 e2_blockptr;
	u64 e1_gen;
	u64 e2_gen;

	/*
	 * We process the in the order
	 * 1. lower key first
	 * 2. "higher" level first
	 * 3. lower block bytenr first
	 */
	cmp = btrfs_comp_cpu_keys(&e1->key, &e2->key);
	if (cmp != 0)
		return cmp;
	if (e1->level > e2->level)
		return -1;
	if (e1->level < e2->level)
		return 1;
	if (e1->level != 0) {
		e1_blockptr = btrfs_node_blockptr(
				e1->path->nodes[e1->level],
				e1->path->slots[e1->level]);
		e2_blockptr = btrfs_node_blockptr(
				e2->path->nodes[e2->level],
				e2->path->slots[e2->level]);
		e1_gen = btrfs_node_ptr_generation(
				e1->path->nodes[e1->level],
				e1->path->slots[e1->level]);
		e2_gen = btrfs_node_ptr_generation(
				e2->path->nodes[e2->level],
				e2->path->slots[e2->level]);
		if (e1_blockptr == e2_blockptr &&
		    e1_gen == e2_gen)
			return 0;
		if (e1_blockptr < e2_blockptr)
			return -1;
		if (e1_blockptr > e2_blockptr)
			return 1;
		WARN_ON(1);
	} else {
		e1_blockptr = e1->path->nodes[e1->level]->start;
		e2_blockptr = e2->path->nodes[e2->level]->start;
		if (e1_blockptr < e2_blockptr)
			return -1;
		if (e1_blockptr > e2_blockptr)
			return 1;
	}
	return 0;
}

static int snap_entry_insert(struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry **insert,
			     int replace)
{
	struct rb_node **p = &ctx->root.rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_snapshot_size_entry *entry;
	int cmp = 0;

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node,
				 struct btrfs_snapshot_size_entry, node);

		cmp = snap_entry_cmp(*insert, entry);
		if (cmp < 0) {
			p = &(*p)->rb_left;
		} else if (cmp > 0) {
			p = &(*p)->rb_right;
		} else {
			/*
			 * If the newly added entry shares the same key with
			 * the existing node in rbtree, and the added entry
			 * has larger subvolume id. We need to keep that
			 * entry, and advance the exsiting node in rbtree. If
			 * this behavior changes, make sure to change all the
			 * highest_root_id under btrfs_find_shared_root in
			 * backref.c.
			 */
			if (replace && (*insert)->root_id > entry->root_id) {
				rb_replace_node(parent_node, &(*insert)->node,
						&ctx->root);
				RB_CLEAR_NODE(parent_node);
				*insert = entry;
			}
			return 1;
		}
	}

	rb_link_node(&(*insert)->node, parent_node, p);
	rb_insert_color(&(*insert)->node, &ctx->root);
	return 0;
}

static inline bool
snap_entry_check_file_extent(struct btrfs_snapshot_size_entry *entry)
{
	bool ret = false;
	struct btrfs_file_extent_item *ei;
	u8 type;
	u64 bytenr;

	if (entry->key.type != BTRFS_EXTENT_DATA_KEY)
		goto out;

	ei = btrfs_item_ptr(entry->path->nodes[0], entry->path->slots[0],
			    struct btrfs_file_extent_item);

	type = btrfs_file_extent_type(entry->path->nodes[0], ei);
	if (type != BTRFS_FILE_EXTENT_PREALLOC &&
	    type != BTRFS_FILE_EXTENT_REG)
		goto out;

	bytenr = btrfs_file_extent_disk_bytenr(entry->path->nodes[0], ei);
	if (bytenr == 0)
		goto out;

	ret = true;
out:
	return ret;
}

static inline int
snap_entry_check_node_shared(struct btrfs_fs_info *fs_info,
			     struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry *entry)
{
	u64 bytenr = entry->path->nodes[entry->level]->start;
	u64 parent_bytenr = entry->path->nodes[entry->level+1]->start;
	u64 root_objectid = entry->root_id;
	u64 inum = entry->key.objectid;
	u64 file_offset = entry->key.offset;
	u64 datao = 0;

	return btrfs_check_shared_inlist(fs_info, root_objectid, inum,
					 file_offset, datao, bytenr,
					 ctx->snap_roots, parent_bytenr,
					 NULL);
}

static inline int
snap_entry_find_extent_owner(struct btrfs_fs_info *fs_info,
			     struct btrfs_snapshot_size_ctx *ctx,
			     struct btrfs_snapshot_size_entry *entry,
			     u64 *owner_id)
{
	struct btrfs_file_extent_item *ei =
		btrfs_item_ptr(entry->path->nodes[0],
			       entry->path->slots[0],
			       struct btrfs_file_extent_item);
	u64 bytenr = btrfs_file_extent_disk_bytenr(entry->path->nodes[0], ei);
	u64 parent_bytenr = entry->path->nodes[0]->start;
	u64 root_objectid = entry->root_id;
	u64 inum = entry->key.objectid;
	u64 file_offset = entry->key.offset;
	u64 datao = btrfs_file_extent_offset(entry->path->nodes[0], ei);
	*owner_id = entry->root_id;

	return btrfs_check_shared_inlist(fs_info, root_objectid, inum,
					 file_offset, datao, bytenr,
					 ctx->snap_roots, parent_bytenr,
					 owner_id);
}

static inline void
snap_entry_update_usage(struct btrfs_snapshot_size_ctx *ctx,
			struct btrfs_snapshot_size_entry *entry,
			u64 owner_id,
			struct btrfs_ioctl_snapshot_size_query_args *args)
{
	struct btrfs_file_extent_item *ei =
		btrfs_item_ptr(entry->path->nodes[0],
			       entry->path->slots[0],
			       struct btrfs_file_extent_item);
	u64 bytes = btrfs_file_extent_disk_num_bytes(entry->path->nodes[0],
						     ei);

	if (owner_id == entry->root_id)
		entry->snap_exclusive_size += bytes;
	else {
		struct ulist_node *node;
		struct btrfs_snapshot_size_entry *counted_entry;

		node = ulist_search(ctx->snap_roots, owner_id);
		counted_entry =
			(struct btrfs_snapshot_size_entry *) node->aux;
		counted_entry->snap_exclusive_size += bytes;
	}

	args->calc_size += bytes;
}

static inline void free_snapshot_size_ctx(struct btrfs_snapshot_size_ctx *ctx,
					  u64 snap_count)
{
	int i;

	if (!ctx)
		return;

	for (i = 0; i < snap_count; i++) {
		if (ctx->snaps[i].path)
			btrfs_free_path(ctx->snaps[i].path);
		if (ctx->snaps[i].root) {
			btrfs_put_root(ctx->snaps[i].root);
			spin_lock(&ctx->snaps[i].root->root_item_lock);
			ctx->snaps[i].root->send_in_progress--;
			spin_unlock(&ctx->snaps[i].root->root_item_lock);
		}
	}
	if (ctx->out_filp)
		fput(ctx->out_filp);
	if (ctx->snap_roots)
		ulist_free(ctx->snap_roots);
	kvfree(ctx);
}

static inline struct btrfs_snapshot_size_ctx*
prepare_snapshot_size_ctx(struct btrfs_fs_info *fs_info,
			  struct btrfs_ioctl_snapshot_size_query_args *args)
{
	int i;
	int ret = 0;
	u64 snap_count = args->snap_count;
	struct btrfs_snapshot_size_ctx *ctx;
	struct btrfs_root *snap_root;

	ctx = kvzalloc(sizeof(*ctx) +
		      sizeof(struct btrfs_snapshot_size_entry) * snap_count,
		      GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->snap_roots = ulist_alloc(GFP_NOFS);
	if (!ctx->snap_roots) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->root = RB_ROOT;
	ctx->flags = args->flags;
	ctx->out_filp = fget(args->fd);
	if (!ctx->out_filp) {
		ret = -EBADF;
		goto out;
	}

	for (i = 0; i < snap_count; ++i) {
		int level;
		struct btrfs_snapshot_size_entry *entry;
		struct extent_buffer *eb;

		ret = ulist_add(ctx->snap_roots,
				args->id_maps[i].snap_id,
				(u64)(&ctx->snaps[i]), GFP_KERNEL);
		if (ret <= 0) {
			if (ret == 0)
				ret = -EINVAL;
			goto out;
		}
		ret = 0;

		entry = &ctx->snaps[i];
		entry->root_id = args->id_maps[i].snap_id;

		entry->path = btrfs_alloc_path();
		if (!entry->path) {
			ret = -ENOMEM;
			goto out;
		}

		snap_root = btrfs_get_fs_root(fs_info, entry->root_id, true);
		if (IS_ERR(snap_root)) {
			ret = PTR_ERR(snap_root);
			goto out;
		}

		spin_lock(&snap_root->root_item_lock);
		if (btrfs_root_dead(snap_root) ||
		    !btrfs_root_readonly(snap_root)) {
			spin_unlock(&snap_root->root_item_lock);
			btrfs_put_root(snap_root);
			ret = -EPERM;
			goto out;
		}
		if (snap_root->dedupe_in_progress) {
			spin_unlock(&snap_root->root_item_lock);
			btrfs_put_root(snap_root);
			ret = -EAGAIN;
			goto out;
		}
#ifdef MY_ABC_HERE
		if (snap_root->syno_orphan_cleanup.cleanup_in_progress) {
			spin_unlock(&snap_root->root_item_lock);
			btrfs_put_root(snap_root);
			ret = -EAGAIN;
			goto out;
		}
#endif /* MY_ABC_HERE */
		snap_root->send_in_progress++;
		spin_unlock(&snap_root->root_item_lock);

		down_read(&snap_root->fs_info->commit_root_sem);
		eb = btrfs_clone_extent_buffer(snap_root->commit_root);
		up_read(&snap_root->fs_info->commit_root_sem);
		if (!eb) {
			ret = -ENOMEM;
			btrfs_put_root(snap_root);
			goto out;
		}

		level = btrfs_header_level(eb);
		entry->root = snap_root;
		entry->path->search_commit_root = 1;
		entry->path->skip_locking = 1;
		entry->root_level = entry->level = level;
		entry->path->nodes[level] = eb;

		if (level == 0)
			btrfs_item_key_to_cpu(entry->path->nodes[level],
			&entry->key, entry->path->slots[level]);
		else
			btrfs_node_key_to_cpu(entry->path->nodes[level],
			&entry->key, entry->path->slots[level]);

		while (snap_entry_insert(ctx, &entry, 0)) {
			if (tree_advance_with_mode(entry->path, &entry->level,
						   entry->root_level,
						   ADVANCE_ONLY_NEXT,
						   &entry->key) < 0) {
				break;
			}
		}
	}

out:
	if (ret) {
		free_snapshot_size_ctx(ctx, snap_count);
		return ERR_PTR(ret);
	}
	return ctx;
}

static int
show_calculate_progress(struct btrfs_snapshot_size_ctx *ctx,
			struct btrfs_ioctl_snapshot_size_query_args *args,
			u64 skip_bytes)
{
	int ret = 0;
	int len;
	char buf[256];
	u64 flags = ctx->flags;

	if (args->calc_size - ctx->last_calc_size < skip_bytes)
		goto out;

	if (get_seconds() - ctx->last_show < 2)
		goto out;

	if (!(flags & (BTRFS_SNAP_SIZE_SHOW_MARGINAL_SIZE|
		       BTRFS_SNAP_SIZE_SHOW_EXCL_SIZE|
		       BTRFS_SNAP_SIZE_SHOW_PROCESSED_SIZE)))
		goto out;

	if (flags & BTRFS_SNAP_SIZE_SHOW_MARGINAL_SIZE) {
		struct rb_node *node = rb_first(&ctx->snap_roots->root);

		while (node) {
			struct ulist_node *lnode = rb_entry(node,
							    struct ulist_node,
							    rb_node);
			struct btrfs_snapshot_size_entry *entry =
			       (struct btrfs_snapshot_size_entry *)lnode->aux;
			u64 snap_id = lnode->val;
			u64 marginal_size = entry->snap_exclusive_size;

			len = snprintf(buf, sizeof(buf),
				       "subvol(%llu) %llu bytes\n", snap_id,
				       marginal_size);
			ret = write_buf(ctx->out_filp, buf, len, &ctx->off);
			if (ret)
				goto out;
			node = rb_next(node);
		}
	}
	if (flags & BTRFS_SNAP_SIZE_SHOW_EXCL_SIZE) {
		/* show exclusize size by each entry*/
		len = snprintf(buf, sizeof(buf), "exclusive %llu bytes\n",
			       args->calc_size);
		ret = write_buf(ctx->out_filp, buf, len, &ctx->off);
		if (ret)
			goto out;
		ctx->last_calc_size = args->calc_size;
	}
	if (flags & BTRFS_SNAP_SIZE_SHOW_PROCESSED_SIZE) {
		len = snprintf(buf, sizeof(buf), "processed %llu bytes\n",
			       args->processed_size);
		ret = write_buf(ctx->out_filp, buf, len, &ctx->off);
		if (ret)
			goto out;
	}
	ret = write_buf(ctx->out_filp, "\n", 1, &ctx->off);
	if (ret)
		goto out;

	ctx->last_show = get_seconds();
out:
	return ret;
}
