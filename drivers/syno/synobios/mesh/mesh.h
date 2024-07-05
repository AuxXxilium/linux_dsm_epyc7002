#ifndef _MESH_SYSTEM_H
#define _MESH_SYSTEM_H
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h> /* printk() */
#include <linux/synobios.h>
#define SZ_MESH_IS_RE_NAME "syno_mesh_is_re"
#define SZ_MESH_IS_JOINING_NAME "syno_mesh_is_joining"
#define SZ_MESH_SIGNAL_QUALITY_NAME "syno_mesh_signal_quality"
#define SZ_MESH_HEARTBEAT_ALIVE_NAME "syno_mesh_heartbeat_alive"
#define SZ_MESH_BACKHAUL_TYPE_NAME "syno_mesh_backhaul_type"
int start_syno_mesh_proc(void);
int remove_syno_mesh_proc(void);
typedef enum {
	MESH_BACKHAUL_TYPE_NONE,
	MESH_BACKHAUL_TYPE_WIRED,
	MESH_BACKHAUL_TYPE_WIRELESS,
} SYNO_MESH_BACKHAUL_TYPE;

#endif /* _MESH_SYSTEM_H */
