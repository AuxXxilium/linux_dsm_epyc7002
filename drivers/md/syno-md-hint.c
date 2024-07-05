/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#include <linux/slab.h>
#include <linux/rbtree.h>

#include "syno-md-hint.h"

static struct kmem_cache *syno_md_hint_cache;

int syno_hint_add(struct syno_hint_tree *tree, sector_t h_start,
		   sector_t h_end, gfp_t gfp_mask)
{
	struct rb_root_cached *root = &tree->root;
	struct rb_node **link = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct syno_hint *new;
	bool leftmost = true;

	while (*link) {
		struct syno_hint *curr;

		parent = *link;
		curr = rb_entry(parent, struct syno_hint, node);

		if (h_start < curr->h_start) {
			link = &(*link)->rb_left;
		} else if (h_start > curr->h_start) {
			if (h_start <= curr->h_end) {
				curr->h_end = max(curr->h_end, h_end);
				return 0;
			}
			link = &(*link)->rb_right;
			leftmost = false;
		} else {
			curr->h_end = max(curr->h_end, h_end);
			return 0;
		}
	}
	if (tree->count == tree->max_count)
		return -ENOMEM;
	new = kmem_cache_zalloc(syno_md_hint_cache, gfp_mask);
	if (!new)
		return -ENOMEM;
	new->h_start = h_start;
	new->h_end = h_end;
	tree->count++;

	/* Add new node and rebalance tree. */
	rb_link_node(&new->node, parent, link);
	rb_insert_color_cached(&new->node, root, leftmost);

	return 0;
}

void syno_hint_remove(struct syno_hint_tree *tree, struct syno_hint *hint)
{
	rb_erase_cached(&hint->node, &tree->root);
	tree->count--;
}

void syno_hint_free(struct syno_hint *hint)
{
	kmem_cache_free(syno_md_hint_cache, hint);
}

struct syno_hint *syno_hint_first(struct syno_hint_tree *tree)
{
	struct rb_node *first = rb_first_cached(&tree->root);

	return first ? rb_entry(first, struct syno_hint, node) : NULL;
}

void syno_hint_tree_init(struct syno_hint_tree *tree)
{
	tree->root = RB_ROOT_CACHED;
	tree->count = 0;
	tree->max_count = SYNO_HINT_DEFAULT_MAX_COUNT;
}

void syno_hint_tree_clear(struct syno_hint_tree *tree)
{
	struct rb_root *rbroot = &tree->root.rb_root;
	struct syno_hint *hint, *next;

	rbtree_postorder_for_each_entry_safe(hint, next, rbroot, node)
		syno_hint_free(hint);
	tree->root = RB_ROOT_CACHED;
	tree->count = 0;
}

int __init syno_md_hint_init(void)
{
	syno_md_hint_cache = kmem_cache_create("syno_md_hint",
					       sizeof(struct syno_hint),
					       0, 0, NULL);
	if (!syno_md_hint_cache)
		return -ENOMEM;
	return 0;
}

void syno_md_hint_exit(void)
{
	kmem_cache_destroy(syno_md_hint_cache);
}
