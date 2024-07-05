
#include "../mapping.h"
#include "mesh.h"
extern int router_exdisplay_handler(struct _SynoMsgPkt *pMsgPkt);
bool gMeshHeartBeatAlive = false;
bool gMeshIsRE = false;
bool gMeshIsJoining = false;
int gMeshSingalQuality = 0;
SYNO_MESH_BACKHAUL_TYPE gMeshBackhaulType = MESH_BACKHAUL_TYPE_NONE;
int syno_mesh_signal_quality_read_command(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", gMeshSingalQuality);
	return 0;
}

int syno_mesh_signal_quality_write_command(struct file *filp,const char *buf,size_t count,loff_t *offp)
{
	int val = -1;
	struct _SynoMsgPkt msgPkt;

	sscanf(buf, "%d", &val);

	if (0 > val || 100 < val) {
		goto END;
	}

	gMeshSingalQuality = val;

	msgPkt.usNum = SYNO_MESH_SIGNAL_QUALITY;
	msgPkt.usLen = 0;
	msgPkt.szMsg[0] = 0;

	router_exdisplay_handler(&msgPkt);
END:
	return count;
}

static int synobios_proc_mesh_signal_quality_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_mesh_signal_quality_read_command, NULL);
}

static const struct file_operations synobios_proc_mesh_signal_quality_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_proc_mesh_signal_quality_open,
	.write		= syno_mesh_signal_quality_write_command,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int syno_mesh_is_joining_read_command(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", gMeshIsJoining);
	return 0;
}

int syno_mesh_is_joining_write_command(struct file *filp,const char *buf,size_t count,loff_t *offp)
{
	int val = -1;
	struct _SynoMsgPkt msgPkt;
	module_t* pSynoModule = NULL;

	sscanf(buf, "%d", &val);

	if (0 > val || 1 < val) {
		goto END;
	}

	if (val) {
		gMeshIsJoining = true;
		msgPkt.usNum = SYNO_LED_NETWORK_SETTING;
	} else {
		gMeshIsJoining = false;
		msgPkt.usNum = SYNO_LED_NETWORK_SETTING_END;
	}
	pSynoModule = module_type_get();
	if (LED_RT == pSynoModule->led_type) {
		snprintf(msgPkt.szMsg, sizeof(msgPkt.szMsg), "findme");
		msgPkt.usLen = strlen(msgPkt.szMsg);

		router_exdisplay_handler(&msgPkt);
	}
END:
	return count;
}

static int synobios_proc_mesh_is_joining_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_mesh_is_joining_read_command, NULL);
}

static const struct file_operations synobios_proc_mesh_is_joining_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_proc_mesh_is_joining_open,
	.write		= syno_mesh_is_joining_write_command,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int syno_mesh_is_re_read_command(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", gMeshIsRE);
	return 0;
}

int syno_mesh_is_re_write_command(struct file *filp,const char *buf,size_t count,loff_t *offp)
{
	int val = -1;
	module_t* pSynoModule = NULL;
	struct _SynoMsgPkt msgPkt;

	sscanf(buf, "%d", &val);

	if (0 > val || 1 < val) {
		goto END;
	}

	if (!val) {
		gMeshIsRE = false;
	} else {
		gMeshIsRE = true;
		if (gMeshHeartBeatAlive) {
			msgPkt.usNum = SYNO_LED_CONNECT;
		} else {
			msgPkt.usNum = SYNO_LED_DISCONNECT;
		}
		msgPkt.usLen = 0;
		msgPkt.szMsg[0] = 0;

		pSynoModule = module_type_get();
		if (LED_RT == pSynoModule->led_type) {
			router_exdisplay_handler(&msgPkt);

		}
	}
END:
	return count;
}

static int synobios_proc_mesh_is_re_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_mesh_is_re_read_command, NULL);
}

static const struct file_operations synobios_proc_mesh_is_re_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_proc_mesh_is_re_open,
	.write		= syno_mesh_is_re_write_command,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int syno_mesh_heartbeat_alive_read_command(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", gMeshHeartBeatAlive);
	return 0;
}

int syno_mesh_heartbeat_alive_write_command(struct file *filp,const char *buf,size_t count,loff_t *offp)
{
	int val = -1;
	module_t* pSynoModule = NULL;
	struct _SynoMsgPkt msgPkt;

	sscanf(buf, "%d", &val);

	if (0 > val || 1 < val) {
		goto END;
	}

	if (0 == val) {
		gMeshHeartBeatAlive = false;
		msgPkt.usNum = SYNO_LED_DISCONNECT;
	} else {
		gMeshHeartBeatAlive = true;
		msgPkt.usNum = SYNO_LED_CONNECT;
	}
	msgPkt.usLen = 0;
	msgPkt.szMsg[0] = 0;

	pSynoModule = module_type_get();
	if (LED_RT == pSynoModule->led_type) {
		router_exdisplay_handler(&msgPkt);

	}
END:
	return count;
}

static int synobios_read_proc_mesh_heartbeat_alive_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_mesh_heartbeat_alive_read_command, NULL);
}

static const struct file_operations synobios_read_proc_mesh_heartbeat_alive_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_read_proc_mesh_heartbeat_alive_open,
	.write		= syno_mesh_heartbeat_alive_write_command,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int syno_mesh_eth_backhaul_type_read_command(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", gMeshBackhaulType);
	return 0;
}

int syno_mesh_backhaul_type_write_command(struct file *filp,const char *buf,size_t count,loff_t *offp)
{
	int val = -1;
	struct _SynoMsgPkt msgPkt;

	sscanf(buf, "%d", &val);

	if (0 > val || 2 < val) {
		goto END;
	}

	gMeshBackhaulType = val;

	msgPkt.usNum = SYNO_MESH_BACKHAUL_IFACE;
	msgPkt.usLen = 0;
	msgPkt.szMsg[0] = 0;

	router_exdisplay_handler(&msgPkt);
END:
	return count;
}

static int synobios_read_proc_mesh_backhaul_type_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_mesh_eth_backhaul_type_read_command, NULL);
}

static const struct file_operations synobios_proc_mesh_backhaul_type_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_read_proc_mesh_backhaul_type_open,
	.write		= syno_mesh_backhaul_type_write_command,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int start_syno_mesh_proc(void)
{
	static struct proc_dir_entry *syno_mesh_is_re;
	syno_mesh_is_re = proc_create_data(SZ_MESH_IS_RE_NAME, 0600, proc_synobios_root,
			&synobios_proc_mesh_is_re_fops, NULL);
	static struct proc_dir_entry *syno_mesh_is_joining;
	syno_mesh_is_joining = proc_create_data(SZ_MESH_IS_JOINING_NAME, 0600, proc_synobios_root,
			&synobios_proc_mesh_is_joining_fops, NULL);
	static struct proc_dir_entry *syno_mesh_signal_quality;
	syno_mesh_signal_quality = proc_create_data(SZ_MESH_SIGNAL_QUALITY_NAME, 0600, proc_synobios_root,
			&synobios_proc_mesh_signal_quality_fops, NULL);
	static struct proc_dir_entry *syno_mesh_heartbeat_alive;
	syno_mesh_heartbeat_alive = proc_create_data(SZ_MESH_HEARTBEAT_ALIVE_NAME, 0600, proc_synobios_root,
							&synobios_read_proc_mesh_heartbeat_alive_fops, NULL);
	static struct proc_dir_entry *syno_mesh_backhaul_type;
	syno_mesh_backhaul_type = proc_create_data(SZ_MESH_BACKHAUL_TYPE_NAME, 0600, proc_synobios_root,
							&synobios_proc_mesh_backhaul_type_fops, NULL);
	return 0;
}

int remove_syno_mesh_proc(void)
{
	remove_proc_entry(SZ_MESH_BACKHAUL_TYPE_NAME, proc_synobios_root);
	remove_proc_entry(SZ_MESH_HEARTBEAT_ALIVE_NAME, proc_synobios_root);
	remove_proc_entry(SZ_MESH_SIGNAL_QUALITY_NAME, proc_synobios_root);
	remove_proc_entry(SZ_MESH_IS_JOINING_NAME, proc_synobios_root);
	remove_proc_entry(SZ_MESH_IS_RE_NAME, proc_synobios_root);
	return 0;
}
