#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2020 Synology Inc.  All rights reserved.
 */

#ifndef __BTRFS_SYNO_FEAT_TREE_H_
#define __BTRFS_SYNO_FEAT_TREE_H_

#include "ctree.h"
#include "xattr.h"
#include "transaction.h"
#include "btrfs_inode.h"

#define __set_feat_tree_status(fs_info, feat_tree_st)  					\
do {                                                                   	\
	fs_info->syno_feat_tree_status.status = feat_tree_st;				\
} while(0)																\

#define btrfs_syno_set_feat_tree_enable(fs_info)		\
	__set_feat_tree_status(fs_info, SYNO_FEAT_TREE_ST_ENABLE)
#define btrfs_syno_set_feat_tree_disable(fs_info)		\
	__set_feat_tree_status(fs_info, SYNO_FEAT_TREE_ST_DISABLE)

#define btrfs_syno_check_feat_tree_enable(fs_info) 		\
	((fs_info->syno_feat_root) && 						\
	(SYNO_FEAT_TREE_ST_ENABLE == fs_info->syno_feat_tree_status.status))

int btrfs_syno_feat_tree_enable(struct btrfs_fs_info *fs_info);
#ifdef MY_ABC_HERE
#define btrfs_syno_feat_tree_disable(...)
#else
int btrfs_syno_feat_tree_disable(struct btrfs_fs_info *fs_info);
#endif /* MY_ABC_HERE */
int btrfs_syno_feat_tree_load_status_from_disk(struct btrfs_fs_info *fs_info);

#endif /* __BTRFS_SYNO_FEAT_TREE_H_ */
