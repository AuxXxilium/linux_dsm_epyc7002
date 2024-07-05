/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM syno

#if !defined(_TRACE_SYNO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYNO_H

#include <linux/tracepoint.h>

#define show_permission_flags(flags) __print_flags(flags, "|",                 \
	{ MAY_EXEC,             "MAY_EXEC" },                                  \
	{ MAY_WRITE,            "MAY_WRITE" },                                 \
	{ MAY_READ,             "MAY_READ" },                                  \
	{ MAY_APPEND,           "MAY_APPEND" },                                \
	{ MAY_ACCESS,           "MAY_ACCESS" },                                \
	{ MAY_OPEN,             "MAY_OPEN" },                                  \
	{ MAY_READ_EXT_ATTR,    "MAY_READ_EXT_ATTR" },                         \
	{ MAY_READ_PERMISSION,  "MAY_READ_PERMISSION" },                       \
	{ MAY_READ_ATTR,        "MAY_READ_ATTR" },                             \
	{ MAY_WRITE_ATTR,       "MAY_WRITE_ATTR" },                            \
	{ MAY_WRITE_EXT_ATTR,   "MAY_WRITE_EXT_ATTR" },                        \
	{ MAY_WRITE_PERMISSION, "MAY_WRITE_PERMISSION" },                      \
	{ MAY_DEL,              "MAY_DEL" },                                   \
	{ MAY_DEL_CHILD,        "MAY_DEL_CHILD" },                             \
	{ MAY_GET_OWNER_SHIP,   "MAY_GET_OWNER_SHIP" },                        \
	{ MAY_CHDIR,            "MAY_CHDIR" },                                 \
	{ MAY_NOT_BLOCK,        "MAY_NOT_BLOCK" })

TRACE_EVENT(synoacl_permission,
	TP_PROTO(struct dentry *dentry, int perm, int error),

	TP_ARGS(dentry, perm, error),

	TP_STRUCT__entry(
		__string(name, dentry->d_name.name)
		__field(uid_t, cur_fsuid)
		__field(int, perm)
		__field(int, error)
	),

	TP_fast_assign(
		__assign_str(name,    dentry->d_name.name);
		__entry->cur_fsuid  = from_kuid(&init_user_ns, current_fsuid());
		__entry->perm       = perm;
		__entry->error      = error;
	),

	TP_printk("file:%s, fsuid:%u, perm:%s, error:%d",
		__get_str(name),
		__entry->cur_fsuid,
		show_permission_flags(__entry->perm),
		__entry->error
	)
);

TRACE_EVENT(synoacl_exec_permission,
	TP_PROTO(struct dentry *dentry, int error),

	TP_ARGS(dentry, error),

	TP_STRUCT__entry(
		__string(name, dentry->d_name.name)
		__field(uid_t, cur_fsuid)
		__field(int, error)
	),

	TP_fast_assign(
		__assign_str(name,    dentry->d_name.name);
		__entry->cur_fsuid  = from_kuid(&init_user_ns, current_fsuid());
		__entry->error      = error;
	),

	TP_printk("file:%s, fsuid:%u, perm:exec, error:%d",
		__get_str(name),
		__entry->cur_fsuid,
		__entry->error
	)
);

TRACE_EVENT(synoacl_may_delete,
	TP_PROTO(struct dentry *dentry, struct inode *parent, int error),

	TP_ARGS(dentry, parent, error),

	TP_STRUCT__entry(
		__string(name, dentry->d_name.name)
		__field(uid_t, cur_fsuid)
		__field(ino_t, parent_ino)
		__field(int, error)
	),

	TP_fast_assign(
		__assign_str(name,    dentry->d_name.name);
		__entry->cur_fsuid  = from_kuid(&init_user_ns, current_fsuid());
		__entry->parent_ino = parent->i_ino;
		__entry->error      = error;
	),

	TP_printk("file:%s, fsuid:%u, parent_ino:%lu, error:%d",
		__get_str(name),
		__entry->cur_fsuid,
		__entry->parent_ino,
		__entry->error
	)
);

TRACE_EVENT(synoacl_may_access,
	TP_PROTO(struct dentry *dentry, int mode, int error),

	TP_ARGS(dentry, mode, error),

	TP_STRUCT__entry(
		__string(name, dentry->d_name.name)
		__field(uid_t, cur_fsuid)
		__field(int, mode)
		__field(int, error)
	),

	TP_fast_assign(
		__assign_str(name,    dentry->d_name.name);
		__entry->cur_fsuid  = from_kuid(&init_user_ns, current_fsuid());
		__entry->mode       = mode;
		__entry->error      = error;
	),

	TP_printk("file:%s, fsuid:%u, mode:%d, error:%d",
		__get_str(name),
		__entry->cur_fsuid,
		__entry->mode,
		__entry->error
	)
);
#endif /* _TRACE_SYNO_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
