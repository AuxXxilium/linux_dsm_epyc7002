/*
 * Copyright (C) 2000-2021 Synology Inc. All rights reserved.
 */

#ifndef __BTRFS_SYNO_RBD_META_H_
#define __BTRFS_SYNO_RBD_META_H_
#include <linux/fs.h>

#include "ctree.h"
#include "volumes.h"

int btrfs_rbd_meta_file_activate(struct inode *inode);
int btrfs_rbd_meta_file_deactivate(struct inode *inode);
int btrfs_rbd_meta_file_mapping(struct inode *inode,
			struct syno_rbd_meta_ioctl_args *args);

int btrfs_delete_all_rbd_meta_file_records(struct inode *inode);
int btrfs_activate_all_rbd_meta_files(struct btrfs_fs_info *fs_info);
void btrfs_unpin_rbd_meta_file(struct inode *inode);

#endif /* __BTRFS_SYNO_RBD_META_H_ */
