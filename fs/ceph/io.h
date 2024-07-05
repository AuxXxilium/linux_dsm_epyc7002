/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_IO_H
#define _FS_CEPH_IO_H

void ceph_start_io_read(struct inode *inode);
void ceph_end_io_read(struct inode *inode);
void ceph_start_io_write(struct inode *inode);
void ceph_end_io_write(struct inode *inode);
#ifdef CONFIG_SYNO_CEPH_RECVFILE
void ceph_start_io_write_nolock(struct inode *inode);
void ceph_end_io_write_nolock(struct inode *inode);
#endif /* CONFIG_SYNO_CEPH_RECVFILE */
void ceph_start_io_direct(struct inode *inode);
void ceph_end_io_direct(struct inode *inode);

#endif /* FS_CEPH_IO_H */
