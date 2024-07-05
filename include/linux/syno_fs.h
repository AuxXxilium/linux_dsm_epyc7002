#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */
#ifndef _LINUX_SYNO_FS_H
#define _LINUX_SYNO_FS_H

#ifdef MY_ABC_HERE
static inline int syno_op_get_archive_bit(struct dentry *dentry, unsigned int *archive_bit)
{
	int err = 0;
	struct inode *inode = dentry->d_inode;

	if (inode->i_op->syno_get_archive_bit) {
		err = inode->i_op->syno_get_archive_bit(dentry, archive_bit, 0);
		if (-ENODATA == err) {
			err = 0;
			*archive_bit= 0;
		}
	} else {
		*archive_bit = inode->i_archive_bit;
	}

	return err;
}

static inline int syno_op_set_archive_bit_nolock(struct dentry *dentry, unsigned int archive_bit)
{
	int err = 0;
	struct inode *inode = dentry->d_inode;

	if (inode->i_op->syno_set_archive_bit) {
		err = inode->i_op->syno_set_archive_bit(dentry, archive_bit);
	} else {
		return -EOPNOTSUPP;
	}

	return err;
}

static inline int syno_op_set_archive_bit(struct dentry *dentry, unsigned int archive_bit)
{
	int err = 0;
	struct inode *inode = dentry->d_inode;

	mutex_lock(&inode->i_archive_bit_mutex);
	err = syno_op_set_archive_bit_nolock(dentry, archive_bit);
	mutex_unlock(&inode->i_archive_bit_mutex);
	return err;
}

long syno_archive_bit_set(struct dentry *dentry, unsigned int cmd);
long syno_archive_bit_overwrite(struct dentry *dentry, unsigned int flags);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline int syno_op_get_crtime(struct inode *inode, struct timespec64 *time)
{
	int error = 0;

	if (!inode->i_op->syno_get_crtime)
		return -EOPNOTSUPP;

	inode_lock(inode);
	error = inode->i_op->syno_get_crtime(inode, time);
	inode_unlock(inode);

	return error;
}

static inline int syno_op_set_crtime(struct inode *inode, struct timespec64 *time)
{
	int error = 0;

	if (!inode->i_op->syno_set_crtime)
		return -EOPNOTSUPP;

	inode_lock(inode);
	error = inode->i_op->syno_set_crtime(inode, time);
	inode_unlock(inode);

	return error;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline int syno_op_get_sb_archive_version(struct super_block *sb,
						 u32 *version)
{
	int ret = 0;

	if (!sb->s_op->syno_get_sb_archive_version)
		return -EOPNOTSUPP;
	down_read(&sb->s_archive_version_rwsem);
	ret = sb->s_op->syno_get_sb_archive_version(sb, version);
	up_read(&sb->s_archive_version_rwsem);
	return ret;
}
static inline int syno_op_set_sb_archive_version(struct super_block *sb,
						 u32 version)
{
	int ret = 0;

	if (!sb->s_op->syno_set_sb_archive_version)
		return -EOPNOTSUPP;
	down_write(&sb->s_archive_version_rwsem);
	ret = sb->s_op->syno_set_sb_archive_version(sb, version);
	up_write(&sb->s_archive_version_rwsem);
	return ret;
}
static inline int syno_op_get_inode_archive_version(struct dentry *dentry,
						    u32 *version)
{
	int ret = 0;
	struct inode *inode = d_inode(dentry);

	if (!inode->i_op->syno_get_archive_version)
		return -EOPNOTSUPP;
	mutex_lock(&inode->i_archive_version_mutex);
	ret = inode->i_op->syno_get_archive_version(dentry, version);
	mutex_unlock(&inode->i_archive_version_mutex);
	return ret;
}
static inline int syno_op_set_inode_archive_version(struct dentry *dentry,
						    u32 version)
{
	int ret = 0;
	struct inode *inode = d_inode(dentry);
	u32 old_version;

	if (!inode->i_op->syno_set_archive_version)
		return -EOPNOTSUPP;
	mutex_lock(&inode->i_archive_version_mutex);
	if (inode->i_op->syno_get_archive_version) {
		ret = inode->i_op->syno_get_archive_version(dentry, &old_version);
		if (!ret && old_version == version)
			goto out_lock;
	}
	ret = inode->i_op->syno_set_archive_version(dentry, version);
out_lock:
	mutex_unlock(&inode->i_archive_version_mutex);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline int syno_op_locker_mode_get(struct inode *inode, enum locker_mode *mode)
{
	if (!inode->i_op->syno_locker_mode_get)
		return -EOPNOTSUPP;

	return inode->i_op->syno_locker_mode_get(inode, mode);
}

static inline int syno_op_locker_state_get(struct inode *inode, enum locker_state *state)
{
	if (!inode->i_op->syno_locker_state_get)
		return -EOPNOTSUPP;

	return inode->i_op->syno_locker_state_get(inode, state);
}

static inline int syno_op_locker_state_set(struct inode *inode, enum locker_state state)
{
	if (!inode->i_op->syno_locker_state_set)
		return -EOPNOTSUPP;

	return inode->i_op->syno_locker_state_set(inode, state);
}

static inline int syno_op_locker_period_end_set(struct inode *inode, struct timespec64 *time)
{
	if (!inode->i_op->syno_locker_period_end_set)
		return -EOPNOTSUPP;

	return inode->i_op->syno_locker_period_end_set(inode, time);
}

#define LOCKER_CHUNK_SIZE               SZ_64K
#define LOCKER_DEFAULT_WAITTIME         TIME64_MAX
#define LOCKER_DEFAULT_DURATION         TIME64_MAX
#define LOCKER_DEFAULT_UPDATE_TIME      TIME64_MAX
#define LOCKER_DEFAULT_PERIOD_BEGIN     TIME64_MAX
#define LOCKER_DEFAULT_PERIOD_END       TIME64_MIN

#define IS_LOCKER_STATE_APPENDABLE(state) \
	((state) == LS_APPENDABLE || (state) == LS_EXPIRED_A || (state) == LS_W_APPENDABLE)
#define IS_LOCKER_STATE_IMMUTABLE(state) \
	((state) == LS_IMMUTABLE || (state) == LS_EXPIRED_I || (state) == LS_W_IMMUTABLE)
#define IS_LOCKER_STATE_EXPIRED(state) \
	((state) == LS_EXPIRED_I || (state) == LS_EXPIRED_A)

static inline bool syno_op_locker_is_open(struct inode *inode)
{
	int ret;
	enum locker_state state;

	ret = syno_op_locker_state_get(inode, &state);
	if (ret)
		return true;

	return state == LS_OPEN;
}

static inline bool syno_op_locker_is_appendable(struct inode *inode)
{
	int ret;
	enum locker_state state;

	ret = syno_op_locker_state_get(inode, &state);
	if (ret)
		return false;

	return IS_LOCKER_STATE_APPENDABLE(state);
}

static inline bool syno_op_locker_is_immutable(struct inode *inode)
{
	int ret;
	enum locker_state state;

	ret = syno_op_locker_state_get(inode, &state);
	if (ret)
		return false;

	return IS_LOCKER_STATE_IMMUTABLE(state);
}

static inline bool syno_op_locker_is_expired(struct inode *inode)
{
	int ret;
	enum locker_state state;

	ret = syno_op_locker_state_get(inode, &state);
	if (ret)
		return false;

	return IS_LOCKER_STATE_EXPIRED(state);
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline int syno_op_getattr(struct dentry *dentry,
				  struct kstat *stat,
				  unsigned int syno_flags)
{
	struct inode *inode = d_inode(dentry);

	if (!syno_flags)
		return 0;

	/* all requested fields will be assigned a default value in VFS, and
	 * be overwritten later in each filesystem or not.
	 */
	if (syno_flags & SYNOST_IS_INLINE)
		stat->is_inline = false;
	if (syno_flags & SYNOST_COMPRESSION)
		stat->syno_compressed = 0;
#ifdef MY_ABC_HERE
	if (syno_flags & SYNOST_ARCHIVE_BIT)
		stat->syno_archive_bit = 0;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (syno_flags & SYNOST_ARCHIVE_VER)
		stat->syno_archive_version = 0;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (syno_flags & SYNOST_CREATE_TIME) {
		stat->syno_create_time.tv_sec = 0;
		stat->syno_create_time.tv_nsec = 0;
	}
#endif /* MY_ABC_HERE */

	if (inode->i_op->syno_getattr)
		return inode->i_op->syno_getattr(dentry, stat, syno_flags);

	return 0;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define IS_SYNOACL_SUPERUSER()      (uid_eq(KUIDT_INIT(0), current_fsuid()))

/*
 * may_not_block indicates that caller doesn't expect this function to block.
 * If this function might block, return -ECHILD so that caller can handle
 * it appropriately. For example in ceph or fuse, getting archive bit may need
 * to send request to remote and wait for the reply.
 */
static inline int
is_syno_archive_bit_enable(struct inode *inode, struct dentry * dentry,
                             unsigned int archive_bit, int may_not_block)
{
	if (inode->i_op->syno_get_archive_bit) {
		unsigned int tmp = 0;
		int err = inode->i_op->syno_get_archive_bit(dentry, &tmp, may_not_block);
		if (-ECHILD == err)  // the only error seen by caller is ECHILD
			return err;

		if (!err && (archive_bit & tmp))
			return 1;

		if (-EOPNOTSUPP != err) // err or archive_bit not enabled
			return 0;
	}

	if (inode->i_archive_bit & archive_bit)
		return 1;

	return 0;
}

/*
 * IS_INODE_SYNOACL_NOBLOCK will return 1  if inode acl is supported
 *                                      0  if inode acl is NOT supported, or error other than ECHILD happens
 *                                      -ECHILD checking inode acl support may block
 */
#define IS_INODE_SYNOACL_NOBLOCK(inode, dentry) is_syno_archive_bit_enable(inode, dentry, S2_SYNO_ACL_SUPPORT, 1)
#define IS_INODE_SYNOACL(inode, dentry)         is_syno_archive_bit_enable(inode, dentry, S2_SYNO_ACL_SUPPORT, 0)
#define IS_SMB_READONLY(dentry)                 is_syno_archive_bit_enable(dentry->d_inode, dentry, S2_SMB_READONLY, 0)
#define IS_SYNOACL_INHERIT(dentry)              is_syno_archive_bit_enable(dentry->d_inode, dentry, S2_SYNO_ACL_INHERIT, 0)
#define IS_SYNOACL_EXIST(dentry)                is_syno_archive_bit_enable(dentry->d_inode, dentry, S2_SYNO_ACL_EXIST, 0)
#define HAS_SYNOACL(dentry)                     is_syno_archive_bit_enable(dentry->d_inode, dentry, (S2_SYNO_ACL_EXIST | S2_SYNO_ACL_INHERIT), 0)
#define IS_SYNOACL_OWNER_IS_GROUP(dentry)       is_syno_archive_bit_enable(dentry->d_inode, dentry, S2_SYNO_ACL_IS_OWNER_GROUP, 0)

#define IS_FS_SYNOACL(inode)                    __IS_FLG(inode, SB_SYNOACL)
#define IS_SYNOACL(dentry)                      (IS_FS_SYNOACL(dentry->d_inode) && IS_INODE_SYNOACL(dentry->d_inode, dentry))
#define IS_SYNOACL_INODE(inode, dentry)         (IS_FS_SYNOACL(inode) && IS_INODE_SYNOACL(inode, dentry))

#define is_synoacl_owner(dentry)                IS_SYNOACL_OWNER_IS_GROUP(dentry)?in_group_p(dentry->d_inode->i_gid):(uid_eq(dentry->d_inode->i_uid, current_fsuid()))
#define is_synoacl_owner_or_capable(dentry)     (is_synoacl_owner(dentry) || capable(CAP_FOWNER))
#endif /* MY_ABC_HERE */

#endif /* _LINUX_SYNO_FS_H */
