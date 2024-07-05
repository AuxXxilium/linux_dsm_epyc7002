#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/****************************************************************************
 *  flashcache_conf.c
 *  FlashCache: Device mapper target for block-level disk caching
 *
 *  Copyright 2010 Facebook, Inc.
 *  Author: Mohan Srinivasan (mohan@fb.com)
 *
 *  Based on DM-Cache:
 *   Copyright (C) International Business Machines Corp., 2006
 *   Author: Ming Zhao (mingzhao@ufl.edu)
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include <asm/atomic.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/hardirq.h>
#include <linux/sysctl.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
#include "dm.h"
#include "dm-io.h"
#include "dm-bio-list.h"
#include "kcopyd.h"
#else
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27)
#include "dm.h"
#endif
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/dm-kcopyd.h>
#endif

#include "flashcache.h"
#include "flashcache_ioctl.h"
#ifdef MY_ABC_HERE
#include "syno_quickflush.h"
#endif

#ifdef MY_ABC_HERE
#include <linux/semaphore.h>
#endif

static int fallow_clean_speed_min = FALLOW_SPEED_MIN;
static int fallow_clean_speed_max = FALLOW_SPEED_MAX;

extern u_int64_t size_hist[];

#ifdef SYNO_FLASHCACHE_DEBUG
extern struct _syno_debug syno_debug;
#endif

static char *flashcache_cons_procfs_cachename(struct cache_c *dmc, char *path_component);
#ifdef MY_ABC_HERE
static char *syno_get_sysctl_dirname(struct cache_c *dmc);
#else
static char *flashcache_cons_sysctl_devname(struct cache_c *dmc);
#endif

#define FLASHCACHE_PROC_ROOTDIR_NAME	"flashcache"

#ifdef MY_ABC_HERE
extern unsigned long hash_block(struct cache_c *dmc, sector_t dbn);
#endif /* MY_ABC_HERE */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
typedef struct ctl_table ctl_table;
#endif

static char * entry_get_combined_name(const char *ssd_path, const char *disk_path);

static int
flashcache_io_latency_init(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
			   struct file *file,
#endif
			   void __user *buffer,
			   size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
	proc_dointvec(table, write, file, buffer, length, ppos);
#else
	proc_dointvec(table, write, buffer, length, ppos);
#endif
	if (write) {
		if (dmc->sysctl_io_latency_hist) {
			int i;
				
			for (i = 0 ; i < IO_LATENCY_BUCKETS ; i++)
				dmc->latency_hist[i] = 0;
			dmc->latency_hist_10ms = 0;
		}
	}
	return 0;
}

static int 
flashcache_sync_sysctl(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
		       struct file *file, 
#endif
		       void __user *buffer, 
		       size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
	proc_dointvec(table, write, file, buffer, length, ppos);
#else
	proc_dointvec(table, write, buffer, length, ppos);
#endif
	if (write) {
		if (dmc->sysctl_do_sync) {
			dmc->sysctl_stop_sync = 0;
#ifdef MY_ABC_HERE
#else
			cancel_delayed_work(&dmc->delayed_clean);
			flush_workqueue(cache_workqueue);
#endif
			/*
			 * SYNO:
			 * sync_all won't garantee that all dirty blocks would be SYNCED_BITS (It won't flush
			 * BLOCK_IN_PROG blocks)
			 * So in sync_for_remove(), we use do {} while (nr_dirty > 0) to operate it
			 */
			flashcache_sync_all(dmc);
		}
	}
	return 0;
}

#ifdef MY_ABC_HERE
static int
flashcache_stop_sync_sysctl(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
		       struct file *file,
#endif
		       void __user *buffer,
		       size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

	proc_dointvec(table, write, buffer, length, ppos);
	if (write) {
		if (dmc->sysctl_stop_sync) {
			dmc->start_sync_all = 0;
			dmc->init_nr_dirty = -1;
		}
	}
	return 0;
}
#endif /* MY_ABC_HERE */

static int 
flashcache_zerostats_sysctl(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
			    struct file *file, 
#endif
			    void __user *buffer, 
			    size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
	proc_dointvec(table, write, file, buffer, length, ppos);
#else
	proc_dointvec(table, write, buffer, length, ppos);
#endif
	if (write) {
		if (dmc->sysctl_zerostats) {
			int i;

			memset(&dmc->flashcache_stats, 0, sizeof(struct flashcache_stats));
			for (i = 0 ; i < IO_LATENCY_BUCKETS ; i++)
				dmc->latency_hist[i] = 0;
			dmc->latency_hist_10ms = 0;
#ifdef MY_ABC_HERE
			atomic_set(&dmc->num_flush_bio, 0);
#endif
		}
	}
	return 0;
}

static int
flashcache_fallow_clean_speed_sysctl(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
				     struct file *file, 
#endif
				     void __user *buffer, 
				     size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
	proc_dointvec(table, write, file, buffer, length, ppos);
#else
	proc_dointvec(table, write, buffer, length, ppos);
#endif
	if (write) {
		if (dmc->sysctl_fallow_clean_speed < fallow_clean_speed_min)
			dmc->sysctl_fallow_clean_speed = fallow_clean_speed_min;

		if (dmc->sysctl_fallow_clean_speed > fallow_clean_speed_max)
			dmc->sysctl_fallow_clean_speed = fallow_clean_speed_max;
	}
	return 0;
}

static int
flashcache_dirty_thresh_sysctl(ctl_table *table, int write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
			       struct file *file, 
#endif
			       void __user *buffer, 
			       size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
        proc_dointvec(table, write, file, buffer, length, ppos);
#else
        proc_dointvec(table, write, buffer, length, ppos);
#endif
	if (write) {
		if (dmc->sysctl_dirty_thresh > DIRTY_THRESH_MAX)
			dmc->sysctl_dirty_thresh = DIRTY_THRESH_MAX;

		if (dmc->sysctl_dirty_thresh < DIRTY_THRESH_MIN)
			dmc->sysctl_dirty_thresh = DIRTY_THRESH_MIN;

		dmc->dirty_thresh_set = 
			(dmc->assoc * dmc->sysctl_dirty_thresh) / 100;
	}
	return 0;
}

#ifdef MY_ABC_HERE
static int
flashcache_enable_wb_task_sysctl(ctl_table *table, int write,
			       void __user *buffer,
			       size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;

	proc_dointvec(table, write, buffer, length, ppos);


	if (write) {
		if (dmc->oqf.sysctl_enable_wb_work) {
			quickflush_schedule_wb_work(dmc);
		} else {
			cancel_delayed_work_sync(&dmc->oqf.wb_work);
		}
	}

	return 0;
}
#endif

static int
flashcache_syno_latency_sysctls(ctl_table *table, int write,
			       void __user *buffer,
			       size_t *length, loff_t *ppos)
{
	struct cache_c *dmc = (struct cache_c *)table->extra1;
	unsigned long itvl_jiffy = 0;
	unsigned long flags = 0;

	proc_dointvec(table, write, buffer, length, ppos);

	if (dmc->sysctl_latency_diagnose_itvl_ms < 100) {
		dmc->sysctl_latency_diagnose_itvl_ms = 100;
		serr("Invalid sysctl latency diagnose itvl");
		return -1;
	}

	if (dmc->sysctl_lat_perc_ten_thousandth < 0
		|| 10000 < dmc->sysctl_lat_perc_ten_thousandth) {
		dmc->sysctl_lat_perc_ten_thousandth = LAT_PERC_TEN_THOUSANDTH;
		serr("Invalid sysctl latency percentile ten thousandth");
		return -1;
	}

	if (write) {
		sprint("Got latency sysctl");
		del_timer_sync(&dmc->latency_timer);
		if (dmc->sysctl_syno_latency_diagnose) {

			spin_lock_irqsave(&dmc->lat_spin_lock, flags);

			dmc->lat_thresholds_jiffies[TYPE_SSD]=
				msecs_to_jiffies(dmc->sysctl_io_ssd_lat_thres_ms);
			dmc->lat_thresholds_jiffies[TYPE_DISK] =
				msecs_to_jiffies(dmc->sysctl_io_disk_lat_thres_ms);
			dmc->lat_thresholds_jiffies[TYPE_COULD_SSD] =
				msecs_to_jiffies(dmc->sysctl_io_disk_lat_thres_ms);

			memset(&dmc->last_itvl_lat_stats[dmc->lat_stats_idx],
				0, sizeof(dmc->last_itvl_lat_stats[0]));

			spin_unlock_irqrestore(&dmc->lat_spin_lock, flags);

			itvl_jiffy = msecs_to_jiffies(dmc->sysctl_latency_diagnose_itvl_ms);
			dmc->latency_timer.expires = jiffies + itvl_jiffy;
			add_timer(&dmc->latency_timer);
		}
	}
	return 0;
}

static int
flashcache_syno_cache_all_sysctl(ctl_table *table, int write,
				void __user *buffer,
				size_t *length, loff_t *ppos)
{
	int ret = 0;
	struct cache_c *dmc = (struct cache_c *)table->extra1;
	unsigned long flags = 0;

	proc_dointvec(table, write, buffer, length, ppos);

	if (write) {
		spin_lock_irqsave(&dmc->bypass_lock, flags);
		if (dmc->bypass_cache && dmc->sysctl_cache_all_input) {
			dmc->sysctl_cache_all_input = 0;
			ret = -1;
		} else {
			dmc->sysctl_cache_all = dmc->sysctl_cache_all_input;
		}
		spin_unlock_irqrestore(&dmc->bypass_lock, flags);
	}

	return ret;
}

static int
syno_wb_sysctl_handler(ctl_table *table, int write,
				void __user *buffer,
				size_t *length, loff_t *ppos)
{
	int ret = 0;
#ifdef MY_ABC_HERE
	struct cache_c *dmc = (struct cache_c *)table->extra1;
	unsigned long flags = 0;
	plug_wb_t *plug_wb = &dmc->plug_wb;

	/*
	 * Keep values aligned before proc_dointvec in case driver
	 * changes the values.
	 */
	plug_wb->sysctl_disk_write_ios_limit = plug_wb->disk_write_ios_limit;
	plug_wb->sysctl_ssd_read_ios_limit = plug_wb->ssd_read_ios_limit;
	plug_wb->sysctl_batch_size = plug_wb->batch_size;

	proc_dointvec(table, write, buffer, length, ppos);

	/*
	 * wb params are protected by lock, proc_dointvec copies from userspace
	 * so must not underlock, user sysctl_* as temp storage.
	 */
	if (write) {
		spin_lock_irqsave(&dmc->plug_wb.lock, flags);
		dmc->plug_wb.batch_size = dmc->plug_wb.sysctl_batch_size;
		dmc->plug_wb.disk_write_ios_limit = dmc->plug_wb.sysctl_disk_write_ios_limit;
		dmc->plug_wb.ssd_read_ios_limit = dmc->plug_wb.sysctl_ssd_read_ios_limit;
		spin_unlock_irqrestore(&dmc->plug_wb.lock, flags);
	}
#else
	proc_dointvec(table, write, buffer, length, ppos);
#endif

	return ret;
}

#ifdef MY_ABC_HERE
static int
syno_group_throtl_sysctl_handler(ctl_table *table, int write,
				void __user *buffer,
				size_t *length, loff_t *ppos)
{
	int ret = 0;
	struct cache_c *dmc = (struct cache_c *)table->extra1;
	extern struct mutex dmcg_mtx;
	int has_dmcg = 0;

	mutex_lock(&dmcg_mtx);

	has_dmcg = (NULL != dmc->dmcg);

	if (has_dmcg) {
		down_write(&dmc->dmcg->rw_sem);
		dmc->new_mu.group_throtl_thres = dmc->dmcg->md_io_throtl_thres;
	}
	proc_dointvec(table, write, buffer, length, ppos);
	if (has_dmcg) {
		dmc->dmcg->md_io_throtl_thres = dmc->new_mu.group_throtl_thres;
		up_write(&dmc->dmcg->rw_sem);
	}

	mutex_unlock(&dmcg_mtx);

	return ret;
}
#endif

static ctl_table wb_sysctl_vars[] = {
	{
		.procname	= "io_latency_hist",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_io_latency_init,
	},
	{
		.procname	= "do_sync",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_sync_sysctl,
	},
	{
		.procname	= "stop_sync",
		.maxlen		= sizeof(int),
		.mode		= 0644,
#ifdef MY_ABC_HERE
		.proc_handler	= &flashcache_stop_sync_sysctl,
#else
		.proc_handler	= &proc_dointvec,
#endif /* MY_ABC_HERE */
	},
	{
		.procname	= "dirty_thresh_pct",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_dirty_thresh_sysctl,
	},
	{
		.procname	= "max_clean_ios_total",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "max_clean_ios_set",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "do_pid_expiry",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "max_pids",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "pid_expiry_secs",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "reclaim_policy",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "zero_stats",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_zerostats_sysctl,
	},
// SYNO: Enable it
#if 1
	{
		.procname	= "error_inject",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.procname	= "fast_remove",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "cache_all",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_cache_all_sysctl,
	},
	{
		.procname	= "fallow_clean_speed",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_fallow_clean_speed_sysctl,
	},
	{
		.procname	= "fallow_delay",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "skip_seq_thresh_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "skip_seq_forward_gap_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "skip_seq_backward_gap_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		// Export to user space
		.procname	= "wb_ssd_read_ios_limit",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &syno_wb_sysctl_handler,
	},
#ifdef MY_ABC_HERE
	{
		.procname	= "wb_disk_write_ios_limit",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &syno_wb_sysctl_handler,
	},
	{
		.procname	= "wb_batch_size",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &syno_wb_sysctl_handler,
	},
	{
		.procname	= "enable_writeback_task",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_enable_wb_task_sysctl,
	},
#endif /* MY_ABC_HERE */
	{
		.procname	= "syno_latency_diagnose",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "latency_diagnose_itvl_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "io_disk_lat_thres_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "io_ssd_lat_thres_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "lat_percentile_ten_thousandth",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "new_error_inject",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef MY_ABC_HERE
	{
		.procname	= "md_ios_total",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "mu_delay_sec",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "mu_check_itvl_sec",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "mu_group_throtl_thres",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &syno_group_throtl_sysctl_handler,
	},
	{
		.procname	= "mu_flush_itvl_sec",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
};

/*
 * Each ctl_table array needs to be 1 more than the actual number of
 * entries - zero padded at the end ! Therefore the +1 in the size
 */
static struct flashcache_writeback_sysctl_table {
	struct ctl_table_header *sysctl_header;
	ctl_table		vars[sizeof(wb_sysctl_vars) / sizeof(wb_sysctl_vars[0]) + 1];
	ctl_table		dev[2];
	ctl_table		dir[2];
	ctl_table		root[2];
} flashcache_writeback_sysctl = {
#ifndef MY_ABC_HERE
	.dev = {
		{
			.procname	= "flashcache-dev",
			.maxlen		= 0,
			.mode		= S_IRUGO|S_IXUGO,
			.child		= flashcache_writeback_sysctl.vars,
		},
	},
#endif
	.dir = {
		{
			.procname	= FLASHCACHE_PROC_ROOTDIR_NAME,
			.maxlen		= 0,
			.mode		= S_IRUGO|S_IXUGO,
#ifdef MY_ABC_HERE
			.child		= flashcache_writeback_sysctl.vars,
#else
			.child		= flashcache_writeback_sysctl.dev,
#endif
		},
	},
	.root = {
		{
			.procname	= "dev",
			.maxlen		= 0,
			.mode		= 0555,
			.child		= flashcache_writeback_sysctl.dir,
		},
	},
};

static ctl_table wt_sysctl_vars[] = {
	{
		.procname	= "io_latency_hist",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_io_latency_init,
	},
	{
		.procname	= "do_pid_expiry",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "max_pids",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "pid_expiry_secs",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "reclaim_policy",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "zero_stats",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_zerostats_sysctl,
	},
// SYNO: Enable it
#if 1
	{
		.procname	= "error_inject",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.procname	= "cache_all",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_cache_all_sysctl,
	},
	{
		.procname	= "skip_seq_thresh_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "skip_seq_forward_gap_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "skip_seq_backward_gap_kb",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "syno_latency_diagnose",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "latency_diagnose_itvl_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "io_disk_lat_thres_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "io_ssd_lat_thres_ms",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
	{
		.procname	= "lat_percentile_ten_thousandth",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &flashcache_syno_latency_sysctls,
	},
};

/*
 * Each ctl_table array needs to be 1 more than the actual number of
 * entries - zero padded at the end ! Therefore the +1 in the size
 */
static struct flashcache_writethrough_sysctl_table {
	struct ctl_table_header *sysctl_header;
	ctl_table		vars[sizeof(wt_sysctl_vars) / sizeof(wt_sysctl_vars[0]) + 1];
	ctl_table		dev[2];
	ctl_table		dir[2];
	ctl_table		root[2];
} flashcache_writethrough_sysctl = {
#ifndef MY_ABC_HERE
	.dev = {
		{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			.ctl_name	= CTL_UNNUMBERED,
#endif
			.procname	= "flashcache-dev",
			.maxlen		= 0,
			.mode		= S_IRUGO|S_IXUGO,
			.child		= flashcache_writethrough_sysctl.vars,
		},
	},
#endif
	.dir = {
		{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			.ctl_name	= CTL_UNNUMBERED,
#endif
			.procname	= FLASHCACHE_PROC_ROOTDIR_NAME,
			.maxlen		= 0,
			.mode		= S_IRUGO|S_IXUGO,
#ifdef MY_ABC_HERE
			.child		= flashcache_writethrough_sysctl.vars,
#else
			.child		= flashcache_writethrough_sysctl.dev,
#endif
		},
	},
	.root = {
		{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			.ctl_name	= CTL_DEV,
#endif
			.procname	= "dev",
			.maxlen		= 0,
			.mode		= 0555,
			.child		= flashcache_writethrough_sysctl.dir,
		},
	},
};

/* TODO: original wb removed, max_clean_ios_total/set can be removed. Also in userspace */
int *
flashcache_find_sysctl_data(struct cache_c *dmc, ctl_table *vars)
{
	if (strcmp(vars->procname, "io_latency_hist") == 0)
		return &dmc->sysctl_io_latency_hist;
	else if (strcmp(vars->procname, "do_sync") == 0) 
		return &dmc->sysctl_do_sync;
	else if (strcmp(vars->procname, "stop_sync") == 0) 
		return &dmc->sysctl_stop_sync;
	else if (strcmp(vars->procname, "dirty_thresh_pct") == 0) 
		return &dmc->sysctl_dirty_thresh;
	else if (strcmp(vars->procname, "max_clean_ios_total") == 0) 
		return &dmc->max_clean_ios_total;
	else if (strcmp(vars->procname, "max_clean_ios_set") == 0) 
		return &dmc->max_clean_ios_set;
	else if (strcmp(vars->procname, "do_pid_expiry") == 0) 
		return &dmc->sysctl_pid_do_expiry;
	else if (strcmp(vars->procname, "max_pids") == 0) 
		return &dmc->sysctl_max_pids;
	else if (strcmp(vars->procname, "pid_expiry_secs") == 0) 
		return &dmc->sysctl_pid_expiry_secs;
	else if (strcmp(vars->procname, "reclaim_policy") == 0) 
		return &dmc->sysctl_reclaim_policy;
	else if (strcmp(vars->procname, "zero_stats") == 0) 
		return &dmc->sysctl_zerostats;
	else if (strcmp(vars->procname, "error_inject") == 0) 
		return &dmc->sysctl_error_inject;
	else if (strcmp(vars->procname, "fast_remove") == 0) 
		return &dmc->sysctl_fast_remove;
	else if (strcmp(vars->procname, "cache_all") == 0) 
		return &dmc->sysctl_cache_all_input;
	else if (strcmp(vars->procname, "fallow_clean_speed") == 0) 
		return &dmc->sysctl_fallow_clean_speed;
	else if (strcmp(vars->procname, "fallow_delay") == 0) 
		return &dmc->sysctl_fallow_delay;
	else if (strcmp(vars->procname, "skip_seq_thresh_kb") == 0) 
		return &dmc->sysctl_skip_seq_thresh_kb;
	else if (strcmp(vars->procname, "skip_seq_forward_gap_kb") == 0)
		return &dmc->sysctl_skip_seq_fgap_kb;
	else if (strcmp(vars->procname, "skip_seq_backward_gap_kb") == 0)
		return &dmc->sysctl_skip_seq_bgap_kb;
#ifdef MY_ABC_HERE
	else if (strcmp(vars->procname, "enable_writeback_task") == 0)
		return &dmc->oqf.sysctl_enable_wb_work;
	else if (strcmp(vars->procname, "wb_disk_write_ios_limit") == 0)
		return &dmc->plug_wb.sysctl_disk_write_ios_limit;
	else if (strcmp(vars->procname, "wb_batch_size") == 0)
		return &dmc->plug_wb.sysctl_batch_size;
	else if (strcmp(vars->procname, "wb_ssd_read_ios_limit") == 0)
		return &dmc->plug_wb.sysctl_ssd_read_ios_limit;
#else
	else if (strcmp(vars->procname, "wb_ssd_read_ios_limit") == 0)
		return &dmc->sysctl_ssd_read_ios_limit;
#endif
	else if (strcmp(vars->procname, "syno_latency_diagnose") == 0)
		return &dmc->sysctl_syno_latency_diagnose;
	else if (strcmp(vars->procname, "latency_diagnose_itvl_ms") == 0)
		return &dmc->sysctl_latency_diagnose_itvl_ms;
	else if (strcmp(vars->procname, "io_disk_lat_thres_ms") == 0)
		return &dmc->sysctl_io_disk_lat_thres_ms;
	else if (strcmp(vars->procname, "io_ssd_lat_thres_ms") == 0)
		return &dmc->sysctl_io_ssd_lat_thres_ms;
	else if (strcmp(vars->procname, "lat_percentile_ten_thousandth") == 0)
		return &dmc->sysctl_lat_perc_ten_thousandth;
	else if (strcmp(vars->procname, "new_error_inject") == 0)
		return &dmc->sysctl_new_error_inject;
#ifdef MY_ABC_HERE
	else if (strcmp(vars->procname, "md_ios_total") == 0)
		return &dmc->new_mu.sysctl_mu_ios_total;
	else if (strcmp(vars->procname, "mu_delay_sec") == 0)
		return &dmc->new_mu.sysctl_mu_delay_sec;
	else if (strcmp(vars->procname, "mu_check_itvl_sec") == 0)
		return &dmc->new_mu.sysctl_mu_check_itvl_sec;
	else if (strcmp(vars->procname, "mu_group_throtl_thres") == 0)
		return &dmc->new_mu.group_throtl_thres;
	else if (strcmp(vars->procname, "mu_flush_itvl_sec") == 0)
		return &dmc->new_mu.flush_itvl_sec;
#endif
	VERIFY_WARN(0);
	return NULL;
}

static void
flashcache_writeback_sysctl_register(struct cache_c *dmc)
{
	int i;
	struct flashcache_writeback_sysctl_table *t;

	t = kmemdup(&flashcache_writeback_sysctl, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;

	memcpy(t->vars, wb_sysctl_vars, sizeof(wb_sysctl_vars));

	for (i = 0 ; i < ARRAY_SIZE(t->vars) - 1 ; i++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		t->vars[i].de = NULL;
#endif
		t->vars[i].data = flashcache_find_sysctl_data(dmc, &t->vars[i]);
		t->vars[i].extra1 = dmc;
	}
	
#ifdef MY_ABC_HERE
	t->dir[0].child = t->vars;
#else
	t->dev[0].procname = flashcache_cons_sysctl_devname(dmc);
	t->dev[0].child = t->vars;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->dev[0].de = NULL;
#endif
	t->dir[0].child = t->dev;
#endif /* End of MY_ABC_HERE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->dir[0].de = NULL;
#endif
#ifdef MY_ABC_HERE
	t->dir[0].procname = syno_get_sysctl_dirname(dmc);
#endif

	t->root[0].child = t->dir;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->root[0].de = NULL;
#endif
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->sysctl_header = register_sysctl_table(t->root, 0);
#else
	t->sysctl_header = register_sysctl_table(t->root);
#endif
	if (t->sysctl_header == NULL)
		goto out;
	
	dmc->sysctl_handle = t;
	return;

out:
#ifdef MY_ABC_HERE
	kfree(t->dir[0].procname);
#else
	kfree(t->dev[0].procname);
#endif
	kfree(t);
}

static void
flashcache_writeback_sysctl_unregister(struct cache_c *dmc)
{
	struct flashcache_writeback_sysctl_table *t;

	t = dmc->sysctl_handle;
	if (t != NULL) {
		dmc->sysctl_handle = NULL;
		unregister_sysctl_table(t->sysctl_header);
#ifdef MY_ABC_HERE
		kfree(t->dir[0].procname);
#else
		kfree(t->dev[0].procname);
#endif

		kfree(t);		
	}
}

static void
flashcache_writethrough_sysctl_register(struct cache_c *dmc)
{
	int i;
	struct flashcache_writethrough_sysctl_table *t;

	t = kmemdup(&flashcache_writethrough_sysctl, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;

	memcpy(t->vars, wt_sysctl_vars, sizeof(wt_sysctl_vars));

	for (i = 0 ; i < ARRAY_SIZE(t->vars) - 1 ; i++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		t->vars[i].de = NULL;
#endif
		t->vars[i].data = flashcache_find_sysctl_data(dmc, &t->vars[i]);
		t->vars[i].extra1 = dmc;
	}
	
#ifdef MY_ABC_HERE
	t->dir[0].child = t->vars;
#else
	t->dev[0].procname = flashcache_cons_sysctl_devname(dmc);
	t->dev[0].child = t->vars;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->dev[0].de = NULL;
#endif
	t->dir[0].child = t->dev;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->dir[0].de = NULL;
#endif
#ifdef MY_ABC_HERE
	t->dir[0].procname = syno_get_sysctl_dirname(dmc);
#endif

	t->root[0].child = t->dir;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->root[0].de = NULL;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	t->sysctl_header = register_sysctl_table(t->root, 0);
#else
	t->sysctl_header = register_sysctl_table(t->root);
#endif
	if (t->sysctl_header == NULL)
		goto out;
	
	dmc->sysctl_handle = t;
	return;

out:
#ifdef MY_ABC_HERE
	kfree(t->dir[0].procname);
#else
	kfree(t->dev[0].procname);
#endif
	kfree(t);
}

static void
flashcache_writethrough_sysctl_unregister(struct cache_c *dmc)
{
	struct flashcache_writethrough_sysctl_table *t;

	t = dmc->sysctl_handle;
	if (t != NULL) {
		dmc->sysctl_handle = NULL;
		unregister_sysctl_table(t->sysctl_header);
#ifdef MY_ABC_HERE
		kfree(t->dir[0].procname);
#else
		kfree(t->dev[0].procname);
#endif
		kfree(t);		
	}
}


static int 
flashcache_stats_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	struct flashcache_stats *stats;
	int read_hit_pct, write_hit_pct, dirty_write_hit_pct;

	stats = &dmc->flashcache_stats;
	if (atomic64_read(&stats->reads) > 0)
		read_hit_pct = compatible_div(atomic64_read(&stats->read_hits) * 100, atomic64_read(&stats->reads));
	else
		read_hit_pct = 0;
	if (atomic64_read(&stats->writes) > 0) {
		write_hit_pct = compatible_div(atomic64_read(&stats->write_hits) * 100, atomic64_read(&stats->writes));
		dirty_write_hit_pct = compatible_div((long long)atomic64_read(&stats->dirty_write_hits) * 100, atomic64_read(&stats->writes));
	} else {
		write_hit_pct = 0;
		dirty_write_hit_pct = 0;
	}
	seq_printf(seq, "reads=%lld writes=%lld \n",
		   (long long)atomic64_read(&stats->reads),
		   (long long)atomic64_read(&stats->writes));
	seq_printf(seq, "read_hits=%lld read_hit_percent=%d ",
		   (long long)atomic64_read(&stats->read_hits), read_hit_pct);
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK || dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
		seq_printf(seq, "write_hits=%lld write_hit_percent=%d ",
			   (long long)atomic64_read(&stats->write_hits), write_hit_pct);
#ifdef MY_ABC_HERE
		seq_printf(seq, "write_miss_ssd=%lld ",
			   (long long)atomic64_read(&stats->wr_miss_ssd));
#endif
#ifdef MY_ABC_HERE
		seq_printf(seq, "dirty_writeback_kb=%lld dirty_writeback_sync_kb=%lld ",
			   compatible_div((long long)atomic64_read(&stats->dirty_writeback_sector), 2),
			   compatible_div((long long)atomic64_read(&stats->dirty_writeback_sync_sector), 2));
#endif
	}
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		seq_printf(seq, "dirty_write_hits=%lld dirty_write_hit_percent=%d ",
			   (long long)atomic64_read(&stats->dirty_write_hits), dirty_write_hit_pct);
	}
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK || dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
		seq_printf(seq, "replacement=%lld write_replacement=%lld ",
			   (long long)atomic64_read(&stats->replace), (long long)atomic64_read(&stats->wr_replace));
		seq_printf(seq,  "write_invalidates=%lld read_invalidates=%lld ",
			   (long long)atomic64_read(&stats->wr_invalidates), (long long)atomic64_read(&stats->rd_invalidates));
	} else {	/* WRITE_AROUND */
		seq_printf(seq, "replacement=%lld ",
			   (long long)atomic64_read(&stats->replace));
		seq_printf(seq, "read_invalidates=%lld ",
			   (long long)atomic64_read(&stats->rd_invalidates));
	}
#ifdef FLASHCACHE_DO_CHECKSUMS
	seq_printf(seq,  "checksum_store=%lld checksum_valid=%lld checksum_invalid=%lld ",
		(long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
	seq_printf(seq,  "pending_enqueues=%lld pending_inval=%lld ",
		   (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval));

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) { 
		seq_printf(seq, "metadata_dirties=%lld metadata_cleans=%lld ",
			   (long long)atomic64_read(&stats->md_write_dirty), (long long)atomic64_read(&stats->md_write_clean));
		seq_printf(seq, "metadata_batch=%lld metadata_ssd_writes=%lld ",
			   (long long)atomic64_read(&stats->md_write_batch), (long long)atomic64_read(&stats->md_ssd_writes));
		seq_printf(seq, "cleanings=%lld fallow_cleanings=%lld ",
			   (long long)atomic64_read(&stats->cleanings), (long long)atomic64_read(&stats->fallow_cleanings));
	}
	seq_printf(seq, "no_room=%lld ",
		   (long long)atomic64_read(&stats->noroom));

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
 		seq_printf(seq, "front_merge=%lld back_merge=%lld ",
			   (long long)atomic64_read(&stats->front_merge), (long long)atomic64_read(&stats->back_merge));
	}
	seq_printf(seq,  "disk_reads=%lld disk_writes=%lld ssd_reads=%lld ssd_writes=%lld ",
		   (long long)atomic64_read(&stats->disk_reads),
		   (long long)atomic64_read(&stats->disk_writes),
		   (long long)atomic64_read(&stats->ssd_reads), (long long)atomic64_read(&stats->ssd_writes));
	seq_printf(seq,  "uncached_reads=%lld uncached_writes=%lld uncached_IO_requeue=%lld ",
		   (long long)atomic64_read(&stats->uncached_reads), (long long)atomic64_read(&stats->uncached_writes), (long long)atomic64_read(&stats->uncached_io_requeue));
	seq_printf(seq,  "uncached_sequential_reads=%lld uncached_sequential_writes=%lld ",
		   (long long)atomic64_read(&stats->uncached_sequential_reads),
		   (long long)atomic64_read(&stats->uncached_sequential_writes));
	seq_printf(seq, "pid_adds=%lld pid_dels=%lld pid_drops=%lld pid_expiry=%lld\n",
		   (long long)atomic64_read(&stats->pid_adds), (long long)atomic64_read(&stats->pid_dels), (long long)atomic64_read(&stats->pid_drops), (long long)atomic64_read(&stats->expiry));
	return 0;
}

static int 
flashcache_stats_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_stats_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_stats_show, PDE_DATA(inode));
	#endif
}
DECLARE_PROC_OPS_OPEN(flashcache_stats_operations, flashcache_stats_open);

// cache_info_show
static int
cache_info_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;

	seq_printf(seq, "ssd_dev=%s ", dmc->cache_devname);
	seq_printf(seq, "disk_dev=%s ", dmc->disk_devname);
	seq_printf(seq, "mode=%s ", mode_to_str(dmc->cache_mode));
	seq_printf(seq, "capacity_byte=%llu ", (u64)dmc->size*dmc->block_size>>11);
	seq_printf(seq, "associativity=%u ", dmc->assoc);
	seq_printf(seq, "data_block_size_kb=%u ", dmc->block_size>>(10-SECTOR_SHIFT));
	seq_printf(seq, "metadata_block_size_byte=%u ", dmc->md_block_size * 512);
	seq_printf(seq, "total_blocks=%llu ", (u64)dmc->size);
	seq_printf(seq, "cached_blocks=%llu ", (u64)atomic64_read(&dmc->cached_blocks));
	seq_printf(seq, "dirty_blocks=%d ", dmc->nr_dirty);
	seq_printf(seq, "synced_blocks=%d ", dmc->nr_synced);
	seq_printf(seq, "occupied_blocks=%d ", dmc->nr_occupied);
#ifdef MY_ABC_HERE
	seq_printf(seq, "support_pin=%d ", dmc->cache_mode == FLASHCACHE_WRITE_BACK? 1 : 0);
#else
	seq_printf(seq, "support_pin=0 ");
#endif
	seq_printf(seq, "version=%d ", dmc->on_ssd_version);
	seq_printf(seq, "queued_sync_io_processed=%llu ", (u64)atomic64_read(&dmc->flashcache_stats.qf_queued_sync_io_processed));
	seq_printf(seq, "\n");
	return 0;
}

static int
cache_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, &cache_info_show, PDE_DATA(inode));
}
DECLARE_PROC_OPS_OPEN(cache_info_ops, cache_info_open);

/*
 * WARNING: This is only for internal test since scan all cacheblocks might
 * affect performance.
 * If you want get inforamtion, query 'cache_info' instead
 */

// occupied_blocks
static int
occupied_blocks_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	int i = 0;
	int nr_pin_only = 0;
	int nr_pin_synced = 0;
	int nr_pin_dirty = 0;
	int nr_synced_only = 0;
	int nr_dirty_only = 0;
	int nr_total = 0;
	u_int16_t state = 0;

	for (i = 0; i < dmc->size; i++) {

		state = dmc->cache[i].cache_state & (PIN_FILE | SYNCED_BITS | DIRTY);
		if (state) {
			nr_total++;
		}
		if (state == PIN_FILE) {
			nr_pin_only++;
		} else if (state == DIRTY) {
			nr_dirty_only++;
		} else if ((state & SYNCED_BITS) && (0 == (state & (PIN_FILE | DIRTY)))) {
			nr_synced_only++;
		} else if (state == (PIN_FILE | DIRTY)) {
			nr_pin_dirty++;
		} else if ((state & PIN_FILE) && (state & SYNCED_BITS) && (0 == (state & DIRTY))) {
			nr_pin_synced++;
		}
	}

	seq_printf(seq, "pin_only=%d dirty_only=%d synced_only=%d pin_dirty=%d "
			"pin_synced=%d total=%d\n", nr_pin_only, nr_dirty_only,
			nr_synced_only, nr_pin_dirty, nr_pin_synced, nr_total);
	return 0;
}

static int
occupied_blocks_open(struct inode *inode, struct file *file)
{
	return single_open(file, &occupied_blocks_show, PDE_DATA(inode));
}
DECLARE_PROC_OPS_OPEN(occupied_blocks_ops, occupied_blocks_open);


#ifdef MY_ABC_HERE
static int
flashcache_progress_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	int init_nr_dirty = 0;
	int cur_nr_dirty = dmc->nr_dirty;
	int remaining_dirty = 0;
	const char *status = NULL;

	if (0 == dmc->sysctl_cache_all && 0 == dmc->nr_dirty) {
		status = "uncacheable_flush_done";
		init_nr_dirty = 0;
		remaining_dirty = 0;
	} else if (dmc->start_sync_all && dmc->sysctl_cache_all && 0 == dmc->nr_dirty) {
		// hibernation flush
		status = "cacheable_flush_done";
		init_nr_dirty = 0;
		remaining_dirty = 0;
	} else if (dmc->start_sync_all && 0 <= dmc->init_nr_dirty) {
		status = (dmc->sysctl_cache_all == 1)? "cacheable_flushing" : "uncacheable_flushing";
		init_nr_dirty = dmc->init_nr_dirty;
		// when remove a cache with some I/O, remaining > init cause negative percentage in UI
		remaining_dirty = cur_nr_dirty > init_nr_dirty ? init_nr_dirty : cur_nr_dirty;
	} else {
		status = "not_flushing";
		init_nr_dirty = 0;
		remaining_dirty = 0;
	}

	seq_printf(seq, "status=%s initial_num_of_dirty=%d remaining_num_of_dirty=%d\n",
			status, init_nr_dirty, remaining_dirty);
	return 0;
}

static int
flashcache_progress_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	return single_open(file, &flashcache_progress_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	return single_open(file, &flashcache_progress_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_progress_operation, flashcache_progress_open);
#endif

#ifdef SYNO_FLASHCACHE_DEBUG

#define SZ_SYNO_DEBUG_INFO "entry=%d\n"
static int
flashcache_debug(struct seq_file *seq, void *v)
{
	struct _syno_debug *d = seq->private;

	seq_printf(seq, SZ_SYNO_DEBUG_INFO, d->entry);
	return 0;
}

static int
flashcache_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, &flashcache_debug, PDE(inode)->data);
}

DECLARE_PROC_OPS_OPEN(flashcache_debug_operation, flashcache_debug_open);

#endif
#ifdef MY_ABC_HERE
static int
flashcache_hash_mapping(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;

	// TODO: handle userspace?
	seq_printf(seq, "%d\n", dmc->hash_mapping);
	return 0;
}

static int
flashcache_hash_mapping_open(struct inode *inode, struct file *file)
{
	return single_open(file, &flashcache_hash_mapping, PDE_DATA(inode));
}

DECLARE_PROC_OPS_OPEN(flashcache_hash_mapping_operation, flashcache_hash_mapping_open);

#endif
#ifdef MY_ABC_HERE
static int
flashcache_writeback_bypass(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	int is_bypass = 0;

	/*
	 * This entry is read by scemd to check if the SSD can be removed safely
	 * Here we don't check dmc->bypass_cache due to it won't be set if no further IO on the cache happens
	 */
	if ((FLASHCACHE_WRITE_BACK == dmc->cache_mode) && (1 == dmc->sysctl_do_sync)
			&& (0 == dmc->sysctl_cache_all) && (0 == dmc->nr_dirty) && (0 == dmc->nr_synced)) {
		is_bypass = 1;
	}
	seq_printf(seq, "%d\n", is_bypass);
	return 0;
}

static int
flashcache_writeback_bypass_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	return single_open(file, &flashcache_writeback_bypass, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	return single_open(file, &flashcache_writeback_bypass, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_writeback_bypass_operation, flashcache_writeback_bypass_open);

#endif

static int 
flashcache_errors_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	struct flashcache_errors *errors = &dmc->flashcache_errors;

	seq_printf(seq, "disk_read_errors=%d disk_write_errors=%d ",
		atomic_read(&errors->disk_read_errors),
		atomic_read(&errors->disk_write_errors));
	seq_printf(seq, "ssd_read_errors=%d ssd_write_errors=%d ",
		atomic_read(&errors->ssd_read_errors),
		atomic_read(&errors->ssd_write_errors));
#ifdef MY_ABC_HERE
	seq_printf(seq, "memory_alloc_errors=%d ",
		atomic_read(&errors->memory_alloc_errors));
	seq_printf(seq, "write_disk_sync_errors=%d\n",
		atomic_read(&errors->write_disk_sync_errors));
#else
	seq_printf(seq, "memory_alloc_errors=%d\n",
		atomic_read(&errors->memory_alloc_errors));
#endif
	return 0;
}

static int 
flashcache_errors_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_errors_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_errors_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_errors_operations, flashcache_errors_open);

static int 
flashcache_iosize_hist_show(struct seq_file *seq, void *v)
{
	int i;
	
	for (i = 1 ; i <= 32 ; i++) {
		seq_printf(seq, "%d:%llu ", i*512, size_hist[i]);
	}
	seq_printf(seq, "\n");
	return 0;
}

static int 
flashcache_iosize_hist_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_iosize_hist_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_iosize_hist_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_iosize_hist_operations, flashcache_iosize_hist_open);

static int 
flashcache_pidlists_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	struct flashcache_cachectl_pid *pid_list;
 	unsigned long flags;
	
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	seq_printf(seq, "Blacklist: ");
	pid_list = dmc->blacklist_head;
	while (pid_list != NULL) {
		seq_printf(seq, "%u ", pid_list->pid);
		pid_list = pid_list->next;
	}
	seq_printf(seq, "\n");
	seq_printf(seq, "Whitelist: ");
	pid_list = dmc->whitelist_head;
	while (pid_list != NULL) {
		seq_printf(seq, "%u ", pid_list->pid);
		pid_list = pid_list->next;
	}
	seq_printf(seq, "\n");
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	return 0;
}

static int 
flashcache_pidlists_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_pidlists_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_pidlists_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_pidlists_operations, flashcache_pidlists_open);

#ifdef CONFIG_SYNO_DATA_CORRECTION
static int
flashcache_correction_list_show(struct seq_file *seq, void *v)
{
	struct cache_c *dmc = seq->private;
	struct correction_entry *entry = NULL;
 	unsigned long flags = 0;
	int is_correcting = 0;

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	list_for_each_entry(entry, &dmc->correction_list->link, link){
		is_correcting = 1;
		seq_printf(seq, "correction list: range:%llu - %llu , correcting_bitmap:0x%x\n",
				entry->start_sector,
				entry->start_sector + MAX_BIO_SIZE -1,
				entry->correcting_bitmap);
	}

	if (0 == is_correcting) {
		seq_printf(seq, "0");
	}

	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	return 0;
}
static int
flashcache_correction_list_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_correction_list_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_correction_list_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_correction_list_operations, flashcache_correction_list_open);

#endif /* CONFIG_SYNO_DATA_CORRECTION */

#ifdef MY_ABC_HERE
#ifdef MY_ABC_HERE
char * data_state_strings[] = {"Uncached", "Cached", "Dirty", "Cached_Pin", "Dirty_Pin"};
#else
char * data_state_strings[] = {"Uncached", "Cached", "Dirty"};
#endif
typedef enum _data_state {
	DATA_UNKNOWN = -1,
	DATA_UNCACHED,
	DATA_CACHED,
	DATA_DIRTY,
#ifdef MY_ABC_HERE
	DATA_CACHED_PIN,
	DATA_DIRTY_PIN,
#endif
} data_state;

typedef struct _ssd_location {
	u64 block_start_sector;
	u64 block_num_sectors;
	u64 cache_start_sector;
	data_state state;
} ssd_location;

typedef struct _data_range
{
	int first_sub_block;
	// Only set in the first data range
	u64 offset_in_first_sub_block;
	int last_sub_block;
	// Only set in the last data range
	u64 offset_in_last_sub_block;
	data_state state;
} data_range;

typedef struct _cache_block_info
{
	u64 block_start_sector;
	u64 ssd_start_sector;
} cache_block_info;

void ssd_location_init(ssd_location *location, const cache_block_info *info,
		const data_range *range)
{
	u64 range_start_sector = range->first_sub_block * SUB_BLOCK_SIZE + range->offset_in_first_sub_block;
	u64 range_last_sector = (range->last_sub_block * SUB_BLOCK_SIZE) + range->offset_in_last_sub_block;

	sdbg(DF_LOC, "range->first_sub_block=%d", range->first_sub_block);
	sdbg(DF_LOC, "range->offset_in_first_sub_block=%llu", range->offset_in_first_sub_block);
	sdbg(DF_LOC, "range->last_sub_block=%d", range->last_sub_block);
	sdbg(DF_LOC, "range->offset_in_last_sub_block=%llu", range->offset_in_last_sub_block);
	sdbg(DF_LOC, "range->state=%s", data_state_strings[range->state]);

	location->block_start_sector = info->block_start_sector + range_start_sector;
	location->block_num_sectors = range_last_sector - range_start_sector + 1;
	location->cache_start_sector = info->ssd_start_sector + range_start_sector;
	location->state = range->state;

	sdbg(DF_LOC, "block_start_sector=%llu block_num_sectors=%llu cache_start_sector=%llu state=%s",
			location->block_start_sector,
			location->block_num_sectors,
			location->cache_start_sector,
			data_state_strings[location->state]);
}

// Return num location
static int cache_get_locations(struct cache_c *dmc, u64 block_start_sector, u64 block_num_sectors, ssd_location *locations, int max_locations, int cache_index)
{
	struct cacheblock *pcacheblock = NULL;
	int next_sub_block = 0;
	int num_locations = 0;
	int offset_sectors_in_cache_block = compatible_mod(block_start_sector, CACHE_BLK_SIZE_SEC);
	u64 block_last_sector = block_start_sector + block_num_sectors - 1;
	int offset_in_last_sub_block = compatible_mod(block_last_sector, SUB_BLOCK_SIZE);
	int start_sub_block = compatible_div(offset_sectors_in_cache_block, SUB_BLOCK_SIZE);
	int last_sub_block = compatible_div(compatible_mod(block_last_sector, CACHE_BLK_SIZE_SEC), SUB_BLOCK_SIZE);
	data_range next_range = {0};
	cache_block_info cache_block_info = {0};
	data_state next_state = DATA_UNKNOWN;
	data_state last_state = DATA_UNKNOWN;
	bitmap_t next_sub_block_bit = 0;
	ssd_location *next_location = NULL;

	pcacheblock = &dmc->cache[cache_index];
	next_location = locations;

	cache_block_info.block_start_sector =  pcacheblock->dbn;
	cache_block_info.ssd_start_sector = INDEX_TO_CACHE_ADDR(dmc, cache_index);


	sdbg(DF_LOC, "start_sub_block=%d last_sub_block=%d", start_sub_block, last_sub_block);
	sdbg(DF_LOC, "cache block data_bitmap=%x  dirty_bitmap=%x", pcacheblock->data_bitmap, pcacheblock->dirty_bitmap);

	next_range.first_sub_block = start_sub_block;

	next_range.offset_in_first_sub_block = compatible_mod(block_start_sector, SUB_BLOCK_SIZE);
	next_range.state  = DATA_UNKNOWN;

	for (next_sub_block = start_sub_block; next_sub_block <= last_sub_block; next_sub_block++) {

		next_sub_block_bit = 1 << next_sub_block;

		// Find next data range state
		if (pcacheblock->data_bitmap & next_sub_block_bit) {
			if (pcacheblock->dirty_bitmap & next_sub_block_bit) {
#ifdef MY_ABC_HERE
				if (pcacheblock->cache_state & PIN_FILE) {
					next_state = DATA_DIRTY_PIN;
				} else
#endif
				next_state = DATA_DIRTY;
			} else {
#ifdef MY_ABC_HERE
				if (pcacheblock->cache_state & PIN_FILE) {
					next_state = DATA_CACHED_PIN;
				} else
#endif
				next_state = DATA_CACHED;
			}
		} else {
			next_state = DATA_UNCACHED;
		}

		if (DATA_UNKNOWN == last_state) {
			next_range.state = next_state;
		} else if (last_state == next_state) {
			// State is keep the same
		} else {
			// State change, handle the previous range of data

			if (num_locations > max_locations) {
				serr("Over the number of locations");
				VERIFY_WARN(0);
			}

			next_range.last_sub_block = next_sub_block - 1;
			// Data fills the lat sub block
			next_range.offset_in_last_sub_block = SUB_BLOCK_SIZE - 1;

			ssd_location_init(next_location, &cache_block_info, &next_range);

			memset(&next_range, 0, sizeof(next_range));

			// Set for next_range
			next_range.first_sub_block = next_sub_block;
			next_range.offset_in_first_sub_block = 0;
			next_range.state = next_state;

			num_locations++;
			next_location++;
		}

		last_state = next_state;
	} // End of for

	// Handle the the last range of data (contain the last sub block)
	sdbg(DF_LOC, "Handle the last group:");

	if (num_locations > max_locations) {
		serr("Over the number of locations");
		VERIFY_WARN(0);
	}

	next_range.last_sub_block = next_sub_block - 1;
	next_range.offset_in_last_sub_block = offset_in_last_sub_block;

	ssd_location_init(next_location, &cache_block_info, &next_range);
	num_locations++;

	sdbg(DF_LOC, "out num_locations=%d", num_locations);
	return num_locations;
}

static int cache_query_ssd_location(struct cache_c *dmc, u64 block_start_sector, u64 block_num_sectors, ssd_location *locations, int max_locations)
{
	unsigned long set_number = 0;
	int start_index = 0;
	int end_index = 0;
	int i = 0;
	int num_locations = 0;
	unsigned long flags;

	VERIFY_WARN(dmc && locations && max_locations);

	set_number = hash_block(dmc, block_start_sector);
	start_index = dmc->assoc * set_number;
	end_index = start_index + dmc->assoc;

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);

	for (i = start_index ; i < end_index ; i++) {
		/*
		 * Search each cache block and check if bio's start sector (dbn) match a range of an cacheblk
		 */
		if (block_start_sector >= dmc->cache[i].dbn &&
			(block_start_sector < (dmc->cache[i].dbn + dmc->block_size)) &&
			dmc->cache[i].cache_state & VALID) {

			if ((block_start_sector + block_num_sectors) > (dmc->cache[i].dbn + dmc->block_size)) {
				// Should not enter here
				sdbg(DF_LOC, "dbn + size is over the cache's range Bio [sector=%llu size=%llu] \
						cacheblk.dbn=%llu\n",
						(u64)block_start_sector, (u64)block_num_sectors, (u64)dmc->cache[i].dbn);
				VERIFY_WARN(0);
			} else {
				// Find match cache block
				num_locations = cache_get_locations(dmc, block_start_sector, block_num_sectors, locations, max_locations, i);
				break;
			}

		}
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	return num_locations;
}

/*
 * buf, count: request and its size from user
 * offp: the current access offset
 */
static ssize_t ssd_query_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	static char data[2048]= {0};
	static int len = 0;
	static int data_remaining = 0;
	int num_read = 0;
	ssize_t ret = -EFAULT;
	int num_locations = 0;
	int i = 0;
	char * str_block_start_sector = "<Block Device Start Sector>";
	char * str_num_sectors = "<Num of Sectors>";
	char * str_cache_start_sector = "<SSD Device Start Sector>";
	char * str_state = "<State>";
	int len_block_start_sector = strlen(str_block_start_sector);
	int len_num_sectors = strlen(str_num_sectors);
	int len_cache_start_sector = strlen(str_cache_start_sector);
	int len_state = strlen(str_state);
	char format[256] = {0};
	ssd_location ssd_locations[MAX_NUM_LOCATION] = {};
	ssd_location *next_location = NULL;

	dmc = (struct cache_c*)PDE_DATA(file_inode(filp));

	sdbg_in(DF_LOC, "in");

	if (0 == len) {
		// First time access, generate data
		num_locations = cache_query_ssd_location(dmc, dmc->query_start_sector, dmc->query_num_sectors,
				ssd_locations, sizeof(ssd_locations)/sizeof(ssd_locations[0]));

		sdbg(DF_LOC, "num_locations=%d", num_locations);

		if (0 == num_locations) {
			len += snprintf(data + len, sizeof(data) - len, "Query data are not in cache!\n");
		} else {
			len += snprintf(data + len, sizeof(data) - len, "%s %s %s %s\n", str_block_start_sector, str_num_sectors, str_cache_start_sector, str_state);

			snprintf(format, sizeof(format),"%%%dllu %%%dllu %%%dllu %%%ds\n", len_block_start_sector,
					len_num_sectors, len_cache_start_sector, len_state);

			for (i = 0; i < num_locations; i++) {
				next_location = &ssd_locations[i];
				len += snprintf(data + len, sizeof(data) - len, format, next_location->block_start_sector, next_location->block_num_sectors,
						next_location->cache_start_sector, data_state_strings[next_location->state]);
				if (len == sizeof(data)) {
					serr("Data array size is not enough");
					break;
				}
			}
		}
	} else {
		if (*offp == len) {
			// No more data, return EOF
			len = 0;
			*offp = 0;
			memset(data, 0, sizeof(data));
			ret = 0;
			sdbg(DF_LOC, "return EOF");
			goto end;
		} else if (*offp < len) {
			// Skill has data to read
		} else {
			serr("Should not enter here");
			goto end;
		}

	}

	data_remaining = len - *offp;

	if (count > data_remaining) {
		num_read = data_remaining;
	} else {
		num_read = count;
	}

	sdbg(DF_LOC, "copy to user: *offp=%lld num_read=%d data_len=%d ", *offp, num_read, len);
	if (copy_to_user(buf, data + *offp, num_read)) {
		serr("Copy to user error");
		goto end;
	}
	*offp += num_read;

	ret = num_read;
end:
	sdbg_out(DF_LOC, "out");

	return ret;
}

ssize_t ssd_query_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	ssize_t ret = -EFAULT;
	char tmp[512] = {0};
	u64 query_start_sector = 0;
	u64 query_num_sectors = 0;
	u64 query_last_sector = 0;
	u64 start_block_num = 0;
	u64 last_block_num = 0;

	dmc = (struct cache_c*)PDE_DATA(file_inode(file));

	if (copy_from_user(tmp, buffer, count)) {
		goto end;
	}

	if (2 == sscanf(tmp, "%llu %llu", &query_start_sector, &query_num_sectors)) {
		sdbg(DF_LOC, "Query start=%llu num=%llu", query_start_sector, query_num_sectors);
	} else {
		serr("Parse error");
		goto err;
	}

	start_block_num = compatible_div(query_start_sector, MAX_BIO_SIZE);
	query_last_sector = query_start_sector + query_num_sectors - 1;
	last_block_num = compatible_div(query_last_sector, MAX_BIO_SIZE);

	if (start_block_num != last_block_num) {
		serr("Across two cache block, doesn't support: Query start sector=%llu end sector=%llu", query_start_sector, query_last_sector);
		goto err;
	}
	dmc->query_start_sector = query_start_sector;
	dmc->query_num_sectors = query_num_sectors;
	ret = count;

end:
	return ret;
err:
	dmc->query_start_sector = 0;
	dmc->query_num_sectors = 0;

	return ret;
}

DECLARE_PROC_OPS_READ_WRITE(ssd_query_ops, ssd_query_read_proc, ssd_query_write_proc);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE

const char * option_string[] = {
	/*
	 * QUERY_NUM_SETS
	 * get the num of sets in this cache device
	 */
	"num-sets",
	/*
	 * QUERY_SET_NUM
	 * parameter: block_start_sector
	 * calculate the set num from the block device start sector
	 */
	"get-set-num",
	/*
	 * QUERY_DUMP_SET
	 * parameter: set_num
	 * dump cache blocks from 0 to 511
	 */
	"dump-set-info",
	/*
	 * QUERY_DUMP_SET_LRU_LIST
	 * parameter: set_num
	 * dump the cache blocks in the lru list of this set
	 */
	"dump-set-lru-list",
	/*
	 * QUERY_BITMAP
	 * Dump bits set in the bitmap table
	 */
	"query-bitmap",
};

query_type_t get_query_type(char *option)
{
	int i = 0;
	query_type_t query_type = QUERY_ERROR;

	for (i = 0; i < sizeof(option_string)/sizeof(option_string[0]); i++) {
		if (0 == strncmp(option, option_string[i], strlen(option_string[i]))) {
			query_type = i;
			break;
		}
	}
	return query_type;
}

int query_set_num_set(struct cache_c *dmc, char *buf)
{
	int ret = -1;
	char option[512] = {0};
	u64 block_start_sector = 0;
	internal_query_t *pinternal_query = &(dmc->internal_query);

	if (2 == sscanf(buf, "%s %llu", option, &block_start_sector)) {
		sdbg(DF_DETAIL, "Query option=%s block_start_sector=%llu", option, block_start_sector);
		pinternal_query->block_start_sector = block_start_sector;
	} else {
		serr("Parse error");
		goto err;
	}

	ret = 0;

err:
	return ret;
}

int query_set_num_get(struct cache_c *dmc, char *buf, int buf_len)
{
	int len = 0;
	u64 set_number = 0;
	internal_query_t *pinternal_query = &(dmc->internal_query);
	set_number = hash_block(dmc, pinternal_query->block_start_sector);

	len = snprintf(buf, buf_len, "%llu\n", set_number);

	return len;
}

int query_dump_set_set(struct cache_c *dmc, char *buf)
{
	int ret = -1;
	char option[512] = {0};
	unsigned int set_num = 0;
	internal_query_t *pinternal_query = &(dmc->internal_query);

	if (2 == sscanf(buf, "%s %d", option, &set_num)) {
		sdbg(DF_DETAIL, "Query option=%s set_num=%d", option, set_num);
		pinternal_query->set_num = set_num;
	} else {
		serr("Parse error");
		goto err;
	}

	ret = 0;

err:
	return ret;
}

void query_dump_set_get(struct cache_c *dmc)
{
	internal_query_t *pinternal_query = &(dmc->internal_query);
	unsigned int set_num = pinternal_query->set_num;
	int start_index = dmc->assoc * set_num;
	int end_index = start_index + dmc->assoc;
	int i = 0;

	for (i = start_index; i < end_index; i++) {
		serr("cacheblk %d start_sector=%llu pin=%d", i, (u64)dmc->cache[i].dbn,
				(dmc->cache[i].cache_state & PIN_FILE) ? 1: 0);
	}
}

void query_dump_set_lru_list_get(struct cache_c *dmc)
{
	internal_query_t *pinternal_query = &(dmc->internal_query);
	unsigned int set_num = pinternal_query->set_num;
	int start_index = set_num * dmc->assoc;
	struct cache_set *cache_set = &dmc->cache_sets[set_num];
	int next_set_index = 0;
	struct cacheblock *next_cacheblk = NULL;
	int lru_count = 0;
	unsigned long flags = 0;

	if (FLASHCACHE_LRU_NULL == cache_set->lru_head) {
		serr("set [%u]: LRU queue is NULL", set_num);
		return;
	}

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);

	next_set_index = cache_set->lru_head;
	next_cacheblk = &dmc->cache[start_index + next_set_index];

	while (FLASHCACHE_LRU_NULL != next_set_index) {
		serr("set [%u]'s lru [%d] cacheblk [%d] start_sector=%llu pin=%d state=0x%x",
				set_num, lru_count, next_set_index,
				(u64)next_cacheblk->dbn, (next_cacheblk->cache_state & PIN_FILE) ? 1: 0,
				next_cacheblk->cache_state);

		next_set_index = next_cacheblk->lru_next;
		next_cacheblk = &dmc->cache[start_index + next_set_index];
		lru_count++;
	}

	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
}

int query_check_pin_lru_order(struct cache_c *dmc)
{
	internal_query_t *pinternal_query = &(dmc->internal_query);
	unsigned int set_num = pinternal_query->set_num;
	int start_index = set_num * dmc->assoc;
	struct cache_set *cache_set = &dmc->cache_sets[set_num];
	int next_set_index = 0;
	struct cacheblock *next_cacheblk = NULL;
	int lru_count = 0;
	int start_pin_num = 0;
	int got_pin_block = 0;
	unsigned long flags = 0;
	int ret = 1;

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);

	next_set_index = cache_set->lru_head;
	next_cacheblk = &dmc->cache[start_index + next_set_index];

	while (FLASHCACHE_LRU_NULL != next_set_index) {
		// Check if LRU list is in Head -> Unpin blocks -> Pin blocks -> Tail order
		if (next_cacheblk->cache_state & PIN_FILE) {
			got_pin_block = 1;
			start_pin_num = lru_count;
		} else {
			// cache block is not pinned
			if (got_pin_block) {
				// Should not has unpin blocks after pin blocks
				serr("Error: Get unpin cache blocks after cache blocks (currnt lru num = %d start pin num =%d)",
						lru_count, start_pin_num);
				goto err;
			}
		}
		next_set_index = next_cacheblk->lru_next;
		next_cacheblk = &dmc->cache[start_index + next_set_index];
		lru_count++;
	}

	if (dmc->assoc != lru_count) {
		serr("Error: lru has wrong node (lru_count = %d disk->assoc = %d)", lru_count, dmc->assoc);
		goto err;
	}

	ret = 0;
err:
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	return ret;
}


void query_bitmap_get(struct cache_c *dmc)
{
	unsigned char *pbytes = NULL;
	unsigned char next_byte = 0;
	int i = 0;
	int next_bit = 0;
#ifdef MY_ABC_HERE

	bitmap_control_get(dmc);
#endif
	pbytes = (unsigned char *) dmc->pbitmap_ranges;

	for (i = 0; i < dmc->bitmap_size_byte; i++) {
		next_byte = pbytes[i];
		for (next_bit = 0; next_bit < 8; next_bit ++) {
			if (next_byte & (1 << next_bit)) {
				serr("Byte=%d bit=%d is set", i, next_bit);
			}
		}
	}
#ifdef MY_ABC_HERE
	bitmap_control_put(dmc);
#endif
}

static ssize_t internal_query_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	int len = 0;
	char tmp[512]= {0};
	ssize_t ret = -EFAULT;
	static int finished = 0;

	dmc = (struct cache_c*)PDE_DATA(file_inode(filp));

	sdbg_in(DF_DETAIL, "in");

	// Return 0 to indicate EOF (avoid endless loop to read)
	if (finished) {
		finished = 0;
		return 0;
	}
	finished = 1;

	switch (dmc->internal_query.query_type) {
	case QUERY_ERROR:
		len += snprintf(tmp + len, sizeof(tmp) - len, "Last query input is incorrect");
		break;
	case QUERY_NUM_SETS:
		len += snprintf(tmp + len, sizeof(tmp) - len, "%u\n", dmc->num_sets);
		break;
	case QUERY_SET_NUM:
		len += query_set_num_get(dmc, tmp, sizeof(tmp));
		break;
	case QUERY_DUMP_SET:
		query_dump_set_get(dmc);
		/*
		 * Add a workaround here. This function jsut print message and return 1
		 * (return 0 will be treated as EOF)
		 */
		len = 1;
		break;
	case QUERY_DUMP_SET_LRU_LIST:
		query_dump_set_lru_list_get(dmc);
		/*
		 * Add a workaround here. This function jsut print message and return 1
		 * (return 0 will be treated as EOF)
		 */
		len = 1;
		break;
	case QUERY_BITMAP:
		query_bitmap_get(dmc);
		len = 1;
		break;
	}

	if (count > len) {
		count = len;
	}

	if (copy_to_user(buf, tmp, count)) {
		serr("copy to user error");
		goto end;
	} else {
		ret = count;
	}

end:
	sdbg_out(DF_DETAIL, "out");

	return ret;
}


ssize_t internal_query_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	ssize_t ret = -EFAULT;
	char tmp[512] = {0};
	char option[512] = {0};

	dmc = (struct cache_c*)PDE_DATA(file_inode(file));

	if (copy_from_user(tmp, buffer, count)) {
		goto err;
	}

	// Get first sub string
	if (1 == sscanf(tmp, "%s", option)) {
		sdbg(DF_DETAIL, "Query option=%s", option);
	} else {
		serr("Parse error");
		goto err;
	}

	// Set query type
	dmc->internal_query.query_type = get_query_type(option);

	// Set extra information
	switch (dmc->internal_query.query_type) {
	case QUERY_ERROR:
		serr("Query error, option=%s", option);
		break;
	case QUERY_NUM_SETS:
		break;
	case QUERY_SET_NUM:
		if (query_set_num_set(dmc, tmp)) {
			serr("Query set number failed");
		}
		break;
	case QUERY_DUMP_SET:
		if (query_dump_set_set(dmc, tmp)) {
			serr("Query dump set failed");
		}
		break;
	case QUERY_DUMP_SET_LRU_LIST:
		if (query_dump_set_set(dmc, tmp)) {
			serr("Query dump set failed");
		}
		break;
	case QUERY_BITMAP:
		serr("Query bitmp is set");
		break;
	}
	ret = count;

err:
	return ret;
}

DECLARE_PROC_OPS_READ_WRITE(internal_query_ops, internal_query_read_proc,
		internal_query_write_proc);
#endif
#ifdef MY_ABC_HERE
static const char *attr_str(attribute_t attr)
{
	switch (attr) {
	case ATTR_NONE:
		return "none";
		break;
	case ATTR_FORCE_REMOVE:
		return "force-remove";
		break;
	default:
		serr("incorrect attribute");
		return "";
		break;
	}
}

static ssize_t attribute_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	int len = 0;
	char output[512]= {0};
	ssize_t ret = -EFAULT;
	static int finished = 0;

	dmc = (struct cache_c*)PDE_DATA(file_inode(filp));

	// Return 0 to indicate EOF (avoid endless loop to read)
	if (finished) {
		finished = 0;
		return 0;
	}
	finished = 1;

	if (ATTR_NONE == dmc->attribute) {
		len += snprintf(output + len, sizeof(output) - len, "%s\n", attr_str(ATTR_NONE));
	} else if (ATTR_FORCE_REMOVE == dmc->attribute) {
		len += snprintf(output + len, sizeof(output) - len, "%s\n", attr_str(ATTR_FORCE_REMOVE));
	} else {
		serr("Incorrect attribute = %d", dmc->attribute);
	}

	if (count > len) {
		count = len;
	}

	if (copy_to_user(buf, output, count)) {
		serr("copy to user error");
		goto end;
	} else {
		ret = count;
	}
end:
	return ret;
}


ssize_t attribute_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	ssize_t ret = -EFAULT;
	char input[512] = {0};
	char attr[512] = {0};

	dmc = (struct cache_c*)PDE_DATA(file_inode(file));

	if (copy_from_user(input, buffer, count)) {
		serr("failed to get data from user space");
		goto err;
	}

	if (1 != sscanf(input, "%s", attr)) {
		serr("Parse error");
		goto err;
	}
	if (0 == strcmp(attr_str(ATTR_NONE), attr)) {
		dmc->attribute = ATTR_NONE;
	} else if (0 == strcmp(attr_str(ATTR_FORCE_REMOVE), attr)) {
		dmc->attribute = ATTR_FORCE_REMOVE;
	} else {
		serr("Incorrect attribute = %s", attr);
		goto err;
	}

	ret = count;

err:
	return ret;
}

DECLARE_PROC_OPS_READ_WRITE(attribute_ops, attribute_read_proc, attribute_write_proc);

#endif
#ifdef MY_ABC_HERE
static ssize_t list_set_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	struct cache_c *dmc = NULL;
	int len = 0;
	static char output[8192]= {0};
	ssize_t ret = -EFAULT;

	static int finished = 0;
	static int proc_next_set = 0;
	static int total_valid_count = 0;

	int round_set_max = 20;
	int round_set_count = 0;
	int next_set = 0;
	int start_index = 0;
	int last_index = 0;
	int valid_count = 0;
	int i = 0;

	memset(output, 0, sizeof(output));
	dmc = (struct cache_c*)PDE_DATA(file_inode(filp));

	// Return 0 to indicate EOF (avoid endless loop to read)
	if (finished) {
		finished = 0;
		proc_next_set = 0;
		total_valid_count = 0;
		return 0;
	}

	if (!proc_next_set) {
		len += snprintf(output + len, sizeof(output) - len, "set number, num of data blocks\n");
	}

	for (next_set = proc_next_set; round_set_count < round_set_max; next_set++) {

		proc_next_set++;

		// Finish
		if (next_set == dmc->num_sets) {
			len += snprintf(output + len, sizeof(output) - len,"Total Valid: %d\n", total_valid_count);
			finished = 1;
			break;
		}

		start_index = next_set * dmc->assoc;
		last_index = start_index + dmc->assoc - 1;
		valid_count = 0;

		for (i = start_index; i <= last_index; i++) {
			if (dmc->cache[i].cache_state & VALID) {
				valid_count++;
			}
		}
		len += snprintf(output + len, sizeof(output) - len, "%d, %d\n", next_set, valid_count);
		round_set_count++;
		total_valid_count += valid_count;

	}

	if (count > len) {
		count = len;
	}

	if (copy_to_user(buf, output, count)) {
		serr("copy to user error");
		goto end;
	} else {
		ret = count;
	}
end:
	return ret;
}


ssize_t list_set_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	return 0;
}

DECLARE_PROC_OPS_READ_WRITE(list_set_valid_block_ops, list_set_read_proc, list_set_write_proc);

#endif

extern char *flashcache_sw_version;

static int 
flashcache_version_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "Flashcache Version : %s\n", flashcache_sw_version);
#ifdef COMMIT_REV
#ifdef MY_ABC_HERE
	seq_printf(seq, "git commit: %s, MAX_JOB_MEM_COUNT:%d \n", COMMIT_REV, MAX_JOB_MEM_COUNT);
#else
	seq_printf(seq, "git commit: %s\n", COMMIT_REV);
#endif
#endif
	return 0;
}

static int 
flashcache_version_open(struct inode *inode, struct file *file)
{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_version_show, PDE(inode)->data);
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		return single_open(file, &flashcache_version_show, PDE_DATA(inode));
	#endif
}

DECLARE_PROC_OPS_OPEN(flashcache_version_operations, flashcache_version_open);

#ifdef MY_ABC_HERE
void wrap_create_proc_entry_version(void)
{
	struct proc_dir_entry *entry;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	entry = create_proc_entry("flashcache/flashcache_version_syno", 0, NULL);
	if (entry)
		entry->proc_fops =  &flashcache_version_operations;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	entry = proc_create("flashcache/flashcache_version_syno", 0, NULL, &flashcache_version_operations);
#endif
}
#endif


#ifdef MY_ABC_HERE

long debug_flags;

// Return the count of read on success. Otherwise return -EFAULT
static ssize_t common_proc_read_long(char *buf, size_t count, long input)
{
	ssize_t len = 0;
	char tmp[512]= {0};
	ssize_t ret = -EFAULT;
	static int finished = 0;

	// Return 0 to indicate EOF (avoid endless loop to read)
	if (finished) {
		finished = 0;
		return 0;
	}

	finished = 1;

	len = snprintf(tmp, sizeof(tmp), "%ld\n", input);

	if (count > len) {
		count = len;
	}

	if (copy_to_user(buf, tmp, count)) {
		goto end;
	} else {
		ret = count;
	}

end:
	return ret;
}

// Return the count of write on success, otherwise return -EFAULT
static ssize_t common_proc_write_long(const char __user *buffer, size_t count, long *output)
{
	char tmp[512] = {0};
	int ret = -EFAULT;

	if (copy_from_user(tmp, buffer, count)) {
		goto err;
	}

	if (0 > kstrtol(tmp, 10, output)) {
		printk("kstrtol failed\n");
		goto err;
	} else {
		ret = count;
	}

err:
	return ret;
}

/*
 * /proc/flashcache/debug_flags
 */
static struct proc_dir_entry *debug_entry;
long debug_flags;

static ssize_t debug_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	return common_proc_read_long(buf, count, debug_flags);
}

ssize_t debug_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	return common_proc_write_long(buffer, count, &debug_flags);
}

DECLARE_PROC_OPS_READ_WRITE(debug_flags_ops, debug_read_proc, debug_write_proc);

static struct proc_dir_entry *debug_entry;

void create_entry_debug_flags(void)
{
	debug_entry = proc_create("flashcache/debug_flags", 0, NULL, &debug_flags_ops);
	if (NULL == debug_entry) {
		sprint("Can't create debug entry");
	}
}

void remove_entry_debug_flags(void)
{
	if (debug_entry) {
		remove_proc_entry("flashcache/debug_flags",0);
	}
}

/*
 * /proc/flashcache/global_tester
 */

static struct proc_dir_entry *global_tester_entry;
long global_tester;

static ssize_t global_tester_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	return common_proc_read_long(buf, count, global_tester);
}

ssize_t global_tester_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	return common_proc_write_long(buffer, count, &global_tester);
}

DECLARE_PROC_OPS_READ_WRITE(global_tester_ops, global_tester_read_proc,
		global_tester_write_proc);

void create_entry_global_tester(void)
{
	global_tester_entry = proc_create("flashcache/global_tester", 0, NULL, &global_tester_ops);
	if (NULL == global_tester_entry) {
		sprint("Can't create global_tester entry");
	}
}

void remove_entry_global_tester(void)
{
	if (global_tester_entry) {
		remove_proc_entry("flashcache/global_tester",0);
	}
}

#endif

#ifdef MY_ABC_HERE
unsigned long long data_rescue_flags;
static ssize_t data_rescue_read_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	ssize_t len = 0;
	char tmp[512]= {0};
	ssize_t ret = -EFAULT;
	static int finished = 0;

	// Return 0 to indicate EOF (avoid endless loop to read)
	if (finished) {
		finished = 0;
		return 0;
	}

	finished = 1;

	len = snprintf(tmp, sizeof(tmp), "%lld\n", data_rescue_flags);

	if (count > len) {
		count = len;
	}

	if (copy_to_user(buf, tmp, count)) {
		goto end;
	} else {
		ret = count;
	}

end:
	return ret;
}

ssize_t data_rescue_write_proc(struct file *file, const char __user *buffer, size_t count, loff_t *offp)
{
	ssize_t ret = -EFAULT;
	char tmp[512] = {0};

	if (copy_from_user(tmp, buffer, count)) {
		goto end;
	}

	if (0 > kstrtoll(tmp, 10, &data_rescue_flags)) {
		sprint("kstrtol failed");
		goto end;
	} else {
		sprint("Set data rescue flags to %lld", data_rescue_flags);
		ret = count;
	}

end:
	return ret;
}

DECLARE_PROC_OPS_READ_WRITE(data_rescue_ops, data_rescue_read_proc,data_rescue_write_proc);

static struct proc_dir_entry *data_rescue_entry;

void create_entry_data_rescue(void)
{
	data_rescue_entry = proc_create("flashcache/data_rescue_flags", 0, NULL, &data_rescue_ops);
	if (NULL == data_rescue_entry) {
		sprint("Can't create data_rescue_flags entry");
	}
}

void remove_entry_data_rescue(void)
{
	if (data_rescue_entry) {
		remove_proc_entry("flashcache/data_rescue_flags",0);
		data_rescue_entry = NULL;
	}
}
#endif /* MY_ABC_HERE */

void
flashcache_module_procfs_init(void)
{
#ifdef CONFIG_PROC_FS
#ifdef MY_ABC_HERE
	int ret = -1;

	ret = down_interruptible(&syno_flashcache_proc_mutex);
	if (ret) {
		serr("Get interrupt while getting mutex");
		return;
	}

	if (0 == syno_flashcache_proc_count) {
		if (proc_mkdir("flashcache", NULL)) {
			wrap_create_proc_entry_version();
		}
	} else {
		wrap_create_proc_entry_version();
	}
	syno_flashcache_proc_count++;

#ifdef MY_ABC_HERE
	create_entry_debug_flags();
	create_entry_global_tester();
#endif
	create_entry_data_rescue();
	up(&syno_flashcache_proc_mutex);
#else
	struct proc_dir_entry *entry;

	if (proc_mkdir("flashcache", NULL)) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry("flashcache/flashcache_version", 0, NULL);
			if (entry)
				entry->proc_fops =  &flashcache_version_operations;
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create("flashcache/flashcache_version", 0, NULL, &flashcache_version_operations);
		#endif

	}
#endif
#endif /* CONFIG_PROC_FS */
}

void
flashcache_module_procfs_releae(void)
{
#ifdef CONFIG_PROC_FS
#ifdef MY_ABC_HERE
	int ret = -1;

	ret = down_interruptible(&syno_flashcache_proc_mutex);
	if (ret) {
		serr("Get interrupt while getting mutex");
		return;
	}
	syno_flashcache_proc_count--;
#ifdef MY_ABC_HERE
	remove_entry_debug_flags();
	remove_entry_data_rescue();
	remove_entry_global_tester();
#endif

	if (0 == syno_flashcache_proc_count) {
		(void)remove_proc_entry("flashcache/flashcache_version_syno", NULL);
		(void)remove_proc_entry("flashcache", NULL);
	} else {
		(void)remove_proc_entry("flashcache/flashcache_version_syno", NULL);
		printk(KERN_INFO "flashcache_syno: /proc/flashcache is still in use, don't remove it\n");
	}

	up(&syno_flashcache_proc_mutex);
#else
	(void)remove_proc_entry("flashcache/flashcache_version_syno", NULL);
	(void)remove_proc_entry("flashcache", NULL);
#endif
#endif /* CONFIG_PROC_FS */
}

#ifdef MY_ABC_HERE
static char *
syno_get_sysctl_dirname(struct cache_c *dmc)
{
	char *path_name = NULL;
	int path_len = 0;
	char *combined_name = NULL;
	char *prefix = "flashcache_";
	int err = 1;

	combined_name = entry_get_combined_name(dmc->cache_devname, dmc->disk_devname);
	if (!combined_name) {
		serr("Failed to get sysctl dirname by %s + %s", dmc->cache_devname, dmc->disk_devname);
		goto err;
	}

	// e.g. flashcache_COMBINED_NAME
	path_len = strlen(prefix) + strlen(combined_name) + 1;
	path_name = kzalloc(path_len, GFP_KERNEL);
	if (!path_name) {
		serr("Failed to allocate for sysctl dirname");
		goto err;
	}

	snprintf(path_name, path_len, "%s%s", prefix, combined_name);

	err = 0;
err:
	if (combined_name) {
		kfree(combined_name);
	}

	if (err && path_name) {
		kfree(path_name);
		path_name = NULL;
	}
	return path_name;
}
#else
static char *
flashcache_cons_sysctl_devname(struct cache_c *dmc)
{
	char *pathname;

	// alloc_cache_1+volume_1
	pathname = kzalloc(strlen(dmc->cache_devname) + strlen(dmc->disk_devname) + 2,
			   GFP_KERNEL);
	if (!pathname) {
		serr("Failed to allocate for sysctl devname");
		return NULL;
	}
	strcpy(pathname, strrchr(dmc->cache_devname, '/') + 1);
	strcat(pathname, "+");
	strcat(pathname, strrchr(dmc->disk_devname, '/') + 1);
	return pathname;
}
#endif
/*
 * /dev/shared_cache_vg1/alloc_cache_1 => share_cache_vg1_alloc_cache_1
 * /dev/md4 => md4
 * XXX: SSD entry name must be unique to generate unique entry path
 */
static char *
entry_get_ssd_name(const char *ssd_path)
{
	int i = 0;
	char *name = NULL;
	int name_len = 0;
	int err = 1;

	name_len = strlen(ssd_path) + 1;
	name = kzalloc(name_len, GFP_NOIO);
	if (!name) {
		serr("Failed to allocate memory");
		goto err;
	}

	// remove dev
	if (1 != sscanf(ssd_path, "/dev/%s", name)) {
		serr("No /dev/ in path %s", ssd_path);
		goto err;
	}

	for (i = 0; i < strlen(name); i++) {
		if (name[i] == '/') {
			name[i] = '_';
		}
	}

	err = 0;
err:
	if (err && name) {
		kfree(name);
		name = NULL;
	}
	return name;
}

/*
 * Always get the last word
 * For example: /dev/vg1000/lv => lv
 */
static const char* 
entry_locate_disk_name(const char *disk_path)
{
	const char *s = NULL;

	s = strrchr(disk_path, '/');
	if (s) {
		s++;
	} else {
		s = disk_path;
	}
	return s;
}

/*
 * NOTE: combined name must be UNIQUE
 *
 * <ssd_path> <disk_path> <combined_name>
 * /dev/shared_cache_vg1/alloc_1 /dev/vg1000/lv shared_cache_vg1_alloc_1+lv
 * /dev/md4 /dev/vg1000/lv md4+lv (single cache combined name remains unchanged)
 */
static char * 
entry_get_combined_name(const char *ssd_path, const char *disk_path)
{
	char *ssd_name = NULL;
	const char *disk_name = NULL;
	char *comb_name = NULL;
	const char *comb_char = "+";
	int comb_len = 0;
	int err = 1;
	
	ssd_name = entry_get_ssd_name(ssd_path);
	if (!ssd_name) {
		goto err;
	}

	disk_name = entry_locate_disk_name(disk_path);

	comb_len = strlen(ssd_name) + strlen(comb_char) + strlen(disk_name) + 1;
	comb_name = kzalloc(comb_len, GFP_NOIO);
	if (!comb_name) {
		serr("Failed to allocate memory");
		goto err;
	}


	snprintf(comb_name, comb_len, "%s%s%s", ssd_name, comb_char, disk_name);

	err = 0;
err:
	if (ssd_name) {
		kfree(ssd_name);
	}

	if (err) {
		serr("Failed to get combined name from %s,%s", ssd_path, disk_path);
		if (comb_name) {
			kfree(comb_name);
			comb_name = NULL;
		}
	}

	return comb_name;
}

static char *
flashcache_cons_procfs_cachename(struct cache_c *dmc, char *path_component)
{
	char *path_name = NULL;
	char *combined_name = NULL;
	int err = 1;
	int path_len = 0;
	int path_comp_len = 0;

	combined_name = entry_get_combined_name(dmc->cache_devname, dmc->disk_devname);
	if (!combined_name) {
		goto err;
	}

	// e.g. flashcache/COMBINED_NAME/flashcache_internal_query
	path_comp_len = strlen(path_component);
	path_len = strlen(FLASHCACHE_PROC_ROOTDIR_NAME) + strlen(combined_name) + path_comp_len + 3;

	path_name = kzalloc(path_len, GFP_NOIO);
	if (!path_name) {
		serr("failed to allocate memory for proc name %s/%s",
			combined_name, path_component);
		goto err;
	}

	if (path_comp_len != 0) {
		snprintf(path_name, path_len, "%s/%s/%s", FLASHCACHE_PROC_ROOTDIR_NAME, combined_name, path_component);
	} else {
		snprintf(path_name, path_len, "%s/%s", FLASHCACHE_PROC_ROOTDIR_NAME, combined_name);
	}

	err = 0;
err:
	if (combined_name) {
		kfree(combined_name);
	}

	if (err && path_name) {
		kfree(path_name);
		path_name = NULL;
	}

	return path_name;
}


void 
flashcache_ctr_procfs(struct cache_c *dmc)
{
	char *s;
	struct proc_dir_entry *entry;

	/* We can not create proc entries if fail to create folder */
	if ((s =  flashcache_cons_procfs_cachename(dmc, ""))) {
		entry = proc_mkdir(s, NULL);
		kfree(s);
		if (entry == NULL)
			return;
	} else {
		return;
	}

#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_progress"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		entry = create_proc_entry(s, 0, NULL);
		if (entry) {
			entry->proc_fops =  &flashcache_progress_operation;
			entry->data = dmc;
		}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_progress_operation, dmc);
		#endif
		kfree(s);
	}
#endif
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "hash_mapping"))) {
		entry = proc_create_data(s, 0, NULL, &flashcache_hash_mapping_operation, dmc);
		kfree(s);
	}
#endif
#ifdef SYNO_FLASHCACHE_DEBUG
	if ((s = flashcache_cons_procfs_cachename(dmc, "debug"))) {
		entry = create_proc_entry(s, 0, NULL);
		if (entry) {
			entry->proc_fops =  &flashcache_debug_operation;
			entry->data = &syno_debug;
		}
		kfree(s);
	}
#endif
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_writeback_bypass"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		entry = create_proc_entry(s, 0, NULL);
		if (entry) {
			entry->proc_fops =  &flashcache_writeback_bypass_operation;
			entry->data = dmc;
		}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_writeback_bypass_operation, dmc);
		#endif
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_attribute"))) {
		entry = proc_create_data(s, 0, NULL, &attribute_ops, dmc);
		kfree(s);
	}

#endif

#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "list_set_valid_block"))) {
		entry = proc_create_data(s, 0, NULL, &list_set_valid_block_ops, dmc);
		kfree(s);
	}
#endif

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_stats"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry(s, 0, NULL);
			if (entry) {
				entry->proc_fops =  &flashcache_stats_operations;
				entry->data = dmc;
			}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_stats_operations, dmc);
		#endif
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "cache_info"))) {
		entry = proc_create_data(s, 0, NULL, &cache_info_ops, dmc);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "occupied_blocks_info"))) {
		entry = proc_create_data(s, 0, NULL, &occupied_blocks_ops, dmc);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_errors"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry(s, 0, NULL);
			if (entry) {
				entry->proc_fops =  &flashcache_errors_operations;
				entry->data = dmc;
			}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_errors_operations, dmc);
		#endif
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_iosize_hist"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry(s, 0, NULL);
			if (entry) {
				entry->proc_fops =  &flashcache_iosize_hist_operations;
				entry->data = dmc;
			}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_iosize_hist_operations, dmc);
		#endif
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_pidlists"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry(s, 0, NULL);
			if (entry) {
				entry->proc_fops =  &flashcache_pidlists_operations;
				entry->data = dmc;
			}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_pidlists_operations, dmc);
		#endif
		kfree(s);
	}

#ifdef CONFIG_SYNO_DATA_CORRECTION
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_correction_list"))) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
			entry = create_proc_entry(s, 0, NULL);
			if (entry) {
				entry->proc_fops =  &flashcache_correction_list_operations;
				entry->data = dmc;
			}
		#endif
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			entry = proc_create_data(s, 0, NULL, &flashcache_correction_list_operations, dmc);
		#endif
		kfree(s);
	}
#endif /* CONFIG_SYNO_DATA_CORRECTION */

#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_ssd_query"))) {
		entry = proc_create_data(s, 0, NULL, &ssd_query_ops, dmc);
		kfree(s);
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_internal_query"))) {
		entry = proc_create_data(s, 0, NULL, &internal_query_ops, dmc);
		kfree(s);
	}
#endif

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK)
		flashcache_writeback_sysctl_register(dmc);
	else
		flashcache_writethrough_sysctl_register(dmc);
}

void 
flashcache_dtr_procfs(struct cache_c *dmc)
{
	char *s;

	/* There'll be warning call trace if the proc is not there or there's leak */
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_progress"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "hash_mapping"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif
#ifdef SYNO_FLASHCACHE_DEBUG
	if ((s = flashcache_cons_procfs_cachename(dmc, "debug"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_writeback_bypass"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_attribute"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "list_set_valid_block"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_stats"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "cache_info"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "occupied_blocks_info"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_errors"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_iosize_hist"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_pidlists"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_ssd_query"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_internal_query"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if ((s = flashcache_cons_procfs_cachename(dmc, "flashcache_correction_list"))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}
#endif

	if ((s = flashcache_cons_procfs_cachename(dmc, ""))) {
		remove_proc_entry(s, NULL);
		kfree(s);
	}

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK)
		flashcache_writeback_sysctl_unregister(dmc);
	else
		flashcache_writethrough_sysctl_unregister(dmc);

}
