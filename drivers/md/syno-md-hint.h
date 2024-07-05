/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */

#ifndef _MD_HINT_H
#define _MD_HINT_H

#include <linux/slab.h>
#include <linux/rbtree.h>

#define SYNO_HINT_DEFAULT_MAX_COUNT 300000
#define SYNO_HINT_MIN_LEN  2 * 1024 /* 1MB (unit: sector) */

struct syno_hint {
	sector_t h_start;
	sector_t h_end;
	struct rb_node node;
};

struct syno_hint_tree {
	struct rb_root_cached root;
	unsigned long count;
	unsigned long max_count;
};

int syno_hint_add(struct syno_hint_tree *tree, sector_t h_start,
		   sector_t h_end, gfp_t gfp_mask);
void syno_hint_remove(struct syno_hint_tree *tree, struct syno_hint *hint);
void syno_hint_free(struct syno_hint *hint);
struct syno_hint* syno_hint_first(struct syno_hint_tree *tree);
void syno_hint_tree_init(struct syno_hint_tree *tree);
void syno_hint_tree_clear(struct syno_hint_tree *tree);
static inline unsigned long syno_hint_count(struct syno_hint_tree *tree)
{
	return tree->count;
}
static inline unsigned long syno_hint_max_count(struct syno_hint_tree *tree)
{
	return tree->max_count;
}

int syno_md_hint_init(void);
void syno_md_hint_exit(void);
#endif /* _MD_HINT_H */
