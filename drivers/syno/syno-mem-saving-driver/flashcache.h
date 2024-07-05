#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/****************************************************************************
 *  flashcache.h
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

#ifndef FLASHCACHE_H
#define FLASHCACHE_H

// Add for utils to include following LINUX_VERSION_CODE macro
#include <linux/version.h>

#include "syno_feature_defines.h"
#include "syno_common_tool.h"
#include "syno_common_4k_driver.h"

#define SYNO_VERSION_NAME "v25-pin-file"

#ifdef __KERNEL__

/*
 * CONFIG_SYNO_BLOCK_FLASHCACHE_SUPPORT is used in linux 4.4, and is changed to
 * a more explict name CONFIG_SYNO_MD_DM_EXTRA_IOCTL in linux 5.10
 */

#include <linux/list.h>
#include <linux/timer.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/ktime.h>

extern ktime_t ktime_zero;

#ifdef MY_ABC_HERE
enum {
	TEST_VMALLOC_FAIL = 1,
	TEST_MALLOC_FAIL_IN_CREATE = 2,
	TEST_PIN_FILE_BITMAP_ALLOC_FAIL = 3,
	TEST_OQF_INVALIDATE_DELAY = 4,
	TEST_OQF_BITMAP_ALLOC_FAIL = 5,
	TEST_CORRECTION_LIST_ALLOC_FAIL = 6,
	TEST_NEW_DMC_GROUP_ALLOC_FAIL = 7,
	TEST_NEW_MU_ALLOC_FAIL = 8,
};
extern long global_tester;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include "flashcache_pin_file.h"
#endif

#include "syno_kernel_functions.h"

/* Like ASSERT() but always compiled in */

#define VERIFY(x) do { \
	if (unlikely(!(x))) { \
		dump_stack(); \
		panic("VERIFY: assertion (%s) failed at %s (%d)\n", \
		      #x,  __FILE__ , __LINE__);		    \
	} \
} while(0)

#define VERIFY_WARN(x) do { \
	if (unlikely(!(x))) { \
		dump_stack(); \
		WARN(1, "VERIFY_WARN: assertion (%s) failed at %s (%d)\n", \
		      #x,  __FILE__ , __LINE__);		    \
	} \
} while(0)

#define DMC_DEBUG 0
#define DMC_DEBUG_LITE 0

// For DMERR
#include <linux/dm-io.h>
#include <linux/device-mapper.h>
#define DM_MSG_PREFIX "flashcache"
#define DMC_PREFIX "flashcache: "

#if DMC_DEBUG
#define DPRINTK( s, arg... ) printk(DMC_PREFIX s "\n", ##arg)
#else
#define DPRINTK( s, arg... )
#endif

#if BITS_PER_LONG == 32
#if defined(CONFIG_SYNO_ALPINE)
#define EC_EMULATE_U64_DIVISION
#endif
#endif


/*
 * Block checksums :
 * Block checksums seem a good idea (especially for debugging, I found a couple 
 * of bugs with this), but in practice there are a number of issues with this
 * in production.
 * 1) If a flash write fails, there is no guarantee that the failure was atomic.
 * Some sectors may have been written to flash. If so, the checksum we have
 * is wrong. We could re-read the flash block and recompute the checksum, but
 * the read could fail too. 
 * 2) On a node crash, we could have crashed between the flash data write and the
 * flash metadata update (which updates the new checksum to flash metadata). When
 * we reboot, the checksum we read from metadata is wrong. This is worked around
 * by having the cache load recompute checksums after an unclean shutdown.
 * 3) Checksums require 4 or 8 more bytes per block in terms of metadata overhead.
 * Especially because the metadata is wired into memory.
 * 4) Checksums force us to do a flash metadata IO on a block re-dirty. If we 
 * didn't maintain checksums, we could avoid the metadata IO on a re-dirty.
 * Therefore in production we disable block checksums.
 */
#if 0
#define FLASHCACHE_DO_CHECKSUMS
#endif

/* Fix writethrough error while SSDs are in RAID 0 and polled out */
/*
 * Fix the errors in writeback mode when the second ssd fail.
 * Let kernel's dm-io layer can report errors from md1 cache.
 */

//#define SYNO_FLASHCACHE_DEBUG
#ifdef SYNO_FLASHCACHE_DEBUG
struct _syno_debug {
	int entry;
} ;
#endif


#if DMC_DEBUG_LITE
#define DPRINTK_LITE( s, arg... ) printk(DMC_PREFIX s "\n", ##arg)
#else
#define DPRINTK_LITE( s, arg... )
#endif

/* Number of pages for I/O */
#define FLASHCACHE_COPY_PAGES (1024)

/* Default cache parameters */
#define DEFAULT_CACHE_SIZE	65536
#define DEFAULT_CACHE_ASSOC	512
#define DEFAULT_BLOCK_SIZE	8	/* 4 KB */
#define DEFAULT_MD_BLOCK_SIZE	8	/* 4 KB */
#define FLASHCACHE_MAX_MD_BLOCK_SIZE	128	/* 64 KB */

#define FLASHCACHE_FIFO		0
#define FLASHCACHE_LRU		1

/*
 * The LRU pointers are maintained as set-relative offsets, instead of 
 * pointers. This enables us to store the LRU pointers per cacheblock
 * using 4 bytes instead of 16 bytes. The upshot of this is that we 
 * are required to clamp the associativity at an 8K max.
 */
#define FLASHCACHE_MIN_ASSOC	 256
#define FLASHCACHE_MAX_ASSOC	8192
#define FLASHCACHE_LRU_NULL	0xFFFF

struct cacheblock;

struct cache_set {
	u_int32_t		set_fifo_next;
	u_int32_t		set_clean_next;
#ifdef MY_ABC_HERE
#else
	u_int16_t		clean_inprog;
	// use for clean_set() to calculate a threshold for writeback
	u_int16_t		nr_dirty;
	u_int16_t		dirty_fallow;
	unsigned long 		fallow_tstamp;
	unsigned long 		fallow_next_cleaning;
#endif
	u_int16_t		lru_head, lru_tail;
};

struct flashcache_errors {
	atomic_t	disk_read_errors;
	atomic_t	disk_write_errors;
	atomic_t	ssd_read_errors;
	atomic_t	ssd_write_errors;
	atomic_t	memory_alloc_errors;
	atomic_t	write_disk_sync_errors;
};

enum latency_step {
	LAT_PREPROCESS = 0,
	LAT_DISK_READ = 1,
	LAT_DISK_WRITE = 2,
	LAT_SSD_READ = 3,
	LAT_SSD_WRITE = 4,
	LAT_DISK_READ_MEM = 5,
	LAT_MEM_WRITE_SSD = 6,
	LAT_PENDING = 7,
	LAT_DO_IO_WQ = 8,
	LAT_MD_WRITE_SSD = 9,
	LAT_MD_WRITE_INPROG_WAIT = 10,
	LAT_MD_WRITE_START_WQ = 11,
	LAT_MD_WRITE_CALLBACK_WQ = 12,
	LAT_UNCACHED_READ = 13,
	LAT_UNCACHED_WRITE = 14,
	LAT_UNCACHED_IO_COMPLETE_WQ = 15,
	LAT_DO_PREREAD_WQ = 16,
	LAT_FLUSH_BIO = 17,
	LAT_END_BIO = 18,
	LAT_MD_WRITE_FUA = 19,
	LAT_MD_WRITE_REG = 20,
	LAT_TYPE_NUM,
};

/*
 * Latency Level: we use unsigned char, total 256 available levels.
 *	for latency below 1000ms, we use step of 5 ms, 200 levels
 *	for latency from 1 - 20 seconds, we use step of 1 second, 19 levels
 *	for latency from 20 - 128 seconds, we use step of 4 seconds, 27 levels
 *	for latency > 128 seconds we use only one level.
 *	for simplicity, round down.
 *	implementations are:
 *		jiffy_to_latency_level
 *		latency_level_to_ms
 */

typedef unsigned char lat_level_t;

struct latency_entry {
	unsigned char step;
	lat_level_t level;
};

enum cache_io_type {
	TYPE_DONT_CARE = -1,
	TYPE_SSD = 0,
	TYPE_DISK = 1,
	TYPE_COULD_SSD = 2,
	CACHE_IO_TYPE_NUM,
};

#define LAT_DIAGNOSE_ENABLED(plat) ((plat)->entry_cnt != LAT_INVALID_ENTRY_CNT)
#define ALT_LAT_STATS_IDX(dmc) (dmc->lat_stats_idx ^ 1)
#define LAT_INVALID_ENTRY_CNT 255

#define LAT_LEVEL_NUM (1 << (sizeof(lat_level_t) * 8))

struct io_latency {
	unsigned long start_jiffy;
	unsigned long last_jiffy;
	unsigned char next_step; // optional, only used when we cannot distinguish step in callee
	unsigned char entry_cnt;
	unsigned char io_type;
	struct latency_entry entries[LAT_TYPE_NUM];
};

struct io_latency_stats {
	unsigned long highest_lat_jiffy;
	unsigned char highest_entry_cnt;
	struct latency_entry highest_lat_entry[LAT_TYPE_NUM];
	unsigned int lat_table[LAT_TYPE_NUM][LAT_LEVEL_NUM];
	unsigned int over_thres_cnt;
};

#ifdef MY_ABC_HERE
struct flush_data {
	struct bio *master_bio;
	struct cache_c *dmc;
	struct io_latency io_lat;
};
#endif

/*
 * SYNO:
 * For the special case that flashcache statistics could have race condition,
 * we modify the type of some attributes to atomic64_t
 */
struct flashcache_stats {
	atomic64_t reads;		/* Number of reads */
	atomic64_t writes;		/* Number of writes */
	atomic64_t read_hits;	/* Number of cache hits */
	atomic64_t write_hits;	/* Number of write hits (includes dirty write hits) */
	atomic64_t dirty_write_hits;	/* Number of "dirty" write hits */
	atomic64_t replace;		/* Number of cache replacements */
	atomic64_t wr_replace;
#ifdef MY_ABC_HERE
	atomic64_t wr_miss_ssd;		/* write miss and write into ssd */
#endif
	atomic64_t wr_invalidates;	/* Number of write invalidations */
	atomic64_t rd_invalidates;	/* Number of read invalidations */
	atomic64_t pending_inval;	/* Invalidations due to concurrent ios on same block */
#ifdef MY_ABC_HERE
	atomic64_t pending_preread;	/* pending jobs that are prereaded later */
#endif
#ifdef FLASHCACHE_DO_CHECKSUMS
	atomic64_t checksum_store;
	atomic64_t checksum_valid;
	atomic64_t checksum_invalid;
#endif
	atomic64_t enqueues;		/* enqueues on pending queue */
	atomic64_t cleanings;		/* Total CB written back include error, increased after MD update or error */
	atomic64_t fallow_cleanings;
	atomic64_t noroom;		/* No room in set */
	atomic64_t md_write_dirty;	/* Metadata sector writes dirtying block */
	atomic64_t md_write_clean;	/* Metadata sector writes cleaning block */
	atomic64_t md_write_batch;	/* How many md updates did we batch ? */
	atomic64_t md_ssd_writes;	/* How many md ssd writes did we do ? */
	atomic64_t pid_drops;
	atomic64_t pid_adds;
	atomic64_t pid_dels;
	atomic64_t expiry;
	atomic64_t front_merge, back_merge;	/* Write Merging */
	atomic64_t uncached_reads, uncached_writes;
	atomic64_t uncached_sequential_reads, uncached_sequential_writes;
	atomic64_t disk_reads, disk_writes;
	atomic64_t ssd_reads, ssd_writes;
	atomic64_t uncached_io_requeue;
	atomic64_t skipclean;
	atomic64_t trim_blocks;
#ifdef MY_ABC_HERE
	atomic64_t qf_nr_writes;		/* total io sent to ssd for writeback */
	atomic64_t qf_fallow_bits_unset;	/* total fallow bits unset from 1 -> 0 */
	atomic64_t qf_bits_mismatch;		/* set in volume bitmap but no dirty cb */
	atomic64_t qf_queued_sync_io_processed;
#else
	atomic64_t clean_set_ios;
	atomic64_t cleanings_over_threshold;
#endif
	atomic64_t skip_unaligned_io;
	atomic64_t read_hit_match_none_read_disk;
	atomic64_t read_hit_match_partial;
	atomic64_t read_hit_check_write_ssd;
	atomic64_t read_hit_busy_in_io;
	atomic64_t read_hit_busy_wait_queue;
	atomic64_t write_hit_busy_inval;
	atomic64_t do_pending_no_error_drity_writeback;
	atomic64_t job_mem_get_in;
	atomic64_t job_mem_get_out;
	atomic64_t prereads;
#ifdef MY_ABC_HERE
	atomic64_t dirty_writeback_sector;
	atomic64_t dirty_writeback_sync_sector;
#endif
#ifdef MY_ABC_HERE
	atomic64_t inval_dirty_writeback;
	atomic64_t write_miss_inval;
	atomic64_t map_inval;
	atomic64_t uncacheable_inval;
	atomic64_t do_pending_no_error_inval;
	atomic64_t pending_enqueue_inval_start;
	atomic64_t pending_enqueue_inval_done;
	atomic64_t disk_flush_start;
	atomic64_t disk_flush_done;
	atomic64_t dirty_writeback_start;
	atomic64_t dirty_writeback_done;
	atomic64_t md_flush_start;
	atomic64_t md_flush_done;
#endif
};

/* 
 * Sequential block history structure - each one
 * records a 'flow' of i/o.
 */
struct sequential_io {
 	sector_t 		most_recent_sector;
	sector_t last_bio_sectors;
	// Support 4k*n bio size
	sector_t sequential_sectors;
	/* We use LRU replacement when we need to record a new i/o 'flow' */
	struct sequential_io 	*prev, *next;
};
#define SKIP_SEQUENTIAL_THRESHOLD 0			/* 0 = cache all, >0 = dont cache sequential i/o more than this (kb) */
#define SEQUENTIAL_TRACKER_QUEUE_DEPTH	32		/* How many io 'flows' to track (random i/o will hog many).
							 * This should be large enough so that we don't quickly 
							 * evict sequential i/o when we see some random,
							 * but small enough that searching through it isn't slow
							 * (currently we do linear search, we could consider hashed */
#define SKIP_SEQ_DEFAULT_FGAP_KB 128
#define SKIP_SEQ_DEFAULT_BGAP_KB 16384
#ifdef MY_ABC_HERE
// option_string[]
typedef enum _query_type {
	QUERY_ERROR = -1,
	QUERY_NUM_SETS,
	QUERY_SET_NUM,
	QUERY_DUMP_SET,
	QUERY_DUMP_SET_LRU_LIST,
	QUERY_BITMAP,
} query_type_t;

typedef struct _internal_query_t {
	query_type_t query_type;
	// QUERY_SET_NUM
	u64 block_start_sector;
	// QUERY_DUMP_SET
	unsigned int set_num;
} internal_query_t;
#endif

#ifdef MY_ABC_HERE
typedef enum _cache_state {
	CACHE_UNKNOWN = -1,
	CACHE_DISABLED,
	CACHE_ENABLED,
	CACHE_WAIT_OLD_IOS_COMPLETED,
	CACHE_WAIT_DISABLE,
} cache_state_t;

typedef enum _dm_attr {
	DM_ATTR_CACHE_UNKNOWN = -1,
	DM_ATTR_CACHE_DISABLE,
	DM_ATTR_CACHE_ENABLE,
} dm_attr_t;

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
typedef struct _bitmap_control {
	spinlock_t lock;
	int in_use;
} bitmap_control_t;
#endif

#ifdef MY_ABC_HERE
#define LIMIT_WRITEBACK_ERR 10
#define LIMIT_WRITEBACK_SYNC_ERR 10
#define LIMIT_DO_PENDING_ERR 10
#define LIMIT_DIRTY_INVAL 10
#define LIMIT_UNCACHED_IO 1
#define LIMIT_MD_IO_ERROR 10

typedef struct _print_limit {
	int writeback_err;
	int writeback_sync_err;
	int do_pending_err;
	int dirty_inval;
	int uncached_io;
	int md_io_error;
} print_limit_t;

typedef enum _attribute {
	ATTR_NONE = 0, // default
	ATTR_FORCE_REMOVE = 1,
} attribute_t;

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
typedef enum _WRITEBACK_SYNC_STATE {
	SYNC_STATE_RUNNING,
	SYNC_STATE_STOPPED,
} WRITEBACK_SYNC_STATE;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE

#define NQF_MGMT_ITVL_SEC 2
#define NQF_TIMESLICE_SEC 10

// TODO: rename, online_quickflush -> normal qf, quickflush -> sync qf
typedef struct online_quickflush {
	int inited;

	struct mutex mtx; // INSIDE oqf->bitmap_rwsem

	struct delayed_work wb_work;
	int sysctl_enable_wb_work; // For develop purpose

	/* Protected by oqf mtx */
	int nqf_inprog; // if syno_start_nqf function is running
	int detect_fallow_inprog; // if the detect_fallow inprog

	WRITEBACK_SYNC_STATE sync_state; // under oqf mtx

	atomic_t nr_fallow;
	unsigned long last_fallow_tstamp;

	/*
	 * normal quickflush works by calling main func (syno_start_nqf)
	 * to send IO, each IO loop back to call main func again.
	 * trigger_cnt records how many times the main func are expected to be
	 * called by the loop. The value is decreased at end of main func.
	 * When trigger_cnt reach 0, nqf process is paused until main func is
	 * called by external threads. wqh is waked up when trigger_cnt reaches 0
	 */
	int trigger_cnt; // protect by mtx
	wait_queue_head_t wqh;

	int sync_trigger_cnt; // protect by mtx

	/* use two semaphore to make sure there's at most one job waiting in
	 * syno_start_sqf */
	struct semaphore sqf_sem2;
	struct semaphore sqf_sem1;

	struct rw_semaphore bitmap_rwsem; // OUTSIDE oqf->mtx

	/*
	 * Every bit implies OQF_VB_PER_BIT volume blocks.
	 * for 4 VB every bit, 1T volume requires 512KB mem
	 * Record dirty when in sqf, fallow when in nqf.
	 * For dirty/fallow cb, the bit MUST be set.
	 * For a set bit, the cb might not be dirty/fallow.
	 * Fallow/Dirty set by worker thread, cleaned ONLY by syno_start_nqf/sqf
	 *
	 * Read lock: scan, fallow set.
	 *            it's possible to run scan and set fallow at the same time
	 *            since getting a non-fallow for a fallow block is ok.
	 *            The block will be handled in the next round.
	 *            fallow sets always executed by fallow wq in single thread.
	 * Write lock: reload table, unset, construct sync bitmap
	 */
	int *volume_bitmap; // protected by bitmap_rwsem
	unsigned long bitmap_num_idx; // protected by bitmap_rwsem
	/* Index of current bit in bitmap, protected by nqf_inprog in nqf,
	 * sqf_sem in sqf */
	u64 bit_idx;

	/* if dmc in dmcg->nqf_pending_dmcs, protected by dmcg->rw_sem */
	int dmcg_wb_scheduled;
} online_qf_t;

#define PLUG_WB_DEFAULT_DISK_WRITE 8
#define PLUG_WB_DEFAULT_SSD_READ 8

#if defined(CONFIG_SYNO_ALPINE)
#define PLUG_WB_DEFAULT_BATCH_SIZE 32
#define PLUG_WB_SYNC_DISK_WRITE (128 + PLUG_WB_DEFAULT_BATCH_SIZE)
#else
#define PLUG_WB_DEFAULT_BATCH_SIZE 64
/* 256 is experimental value from quickflush v1, also work for full stripe merge
 * since we send a batch at a time, add a batch size as buffer. */
#define PLUG_WB_SYNC_DISK_WRITE (256 + PLUG_WB_DEFAULT_BATCH_SIZE)
#endif
#define PLUG_WB_SYNC_SSD_READ (PLUG_WB_DEFAULT_BATCH_SIZE * 2)

#define PLUG_WB_HAS_BATCH(plug_wb) ((plug_wb)->cur_batch_len)
#define PLUG_WB_HAS_BATCH_AND_DISK_WRITE_READY(plug_wb) \
	(PLUG_WB_HAS_BATCH(plug_wb) && 0 == (plug_wb)->unfinished_read_in_batch)

/* Structure for plug writeback */
typedef struct _plug_wb_t {
	struct list_head io_queue; // jobs put in after ssd read DONE
	struct list_head sync_io_queue; // jobs put in after ssd read SENT (to keep order)
	struct list_head complete_queue;
	struct list_head wb_done_queue;

	unsigned long long seq;
	unsigned long long cur_batch_head;
	int cur_batch_len;
	int unfinished_read_in_batch;

	int ssd_read_inprog;
	int disk_write_inprog;

	/*
	 * Use two copy since the following parameters are protectd by lock.
	 * data are stored in sysctl_* first then put to the other one under lock
	 *
	 * We expect userspace to control ssd_read_ios_limit.
	 * disk_write_ios_limit is set to sync default value when sync start and
	 * reset to normal value when sync finished.
	 */
	int sysctl_disk_write_ios_limit;
	int sysctl_ssd_read_ios_limit;
	int sysctl_batch_size;
	int disk_write_ios_limit;
	int ssd_read_ios_limit; // works like the old clean ios total
	int batch_size;

	/* We lock the queue while processing, won't loop forever */
	struct work_struct work;

	/* We splice wb_done_queue, process only limited io so won't loop indefinitly */
	struct work_struct wb_done_work;

	/* WARNING: Locked inside cache_spin_lock & oqf lock */
	spinlock_t lock;
} plug_wb_t;

#endif

#ifdef MY_ABC_HERE

/*
 * MU_HAS_PENDING_REQ
 * 	Another MU that cannot merge with ongoing MU is queued for this block
 *
 * MU_FLUSH1/2:
 * 	for need flush MU, since MU might be pending
 * 	(i.e., not belong to the ongoing MU). We need 2 flags to identify which
 * 	CB to update after MU finished. Correspond to SYNCED1/2
 *
 * MU_MD_IO_COMBO:
 * 	If the other type of MU (flush/no flush) is MD IO pending when the
 * 	MD IO of the current Mu is going to be issued, then this MD IO counts
 * 	for both types.
 * 	The flag means the other type of current mu block need to be updated too
 * 	when the md io returns
 *
 * TODO: add NEED_DO_PENDING flag, now FORCED + FORCED_RESTART don't need to
 * 	 call do pending on md io error.
 * MU_FORCED:
 * 	MU must be handled immediately. For invalidate. Some IO must be pending
 * 	in one or multiple cbs.
 * 	See FORCED_RESTART for more information
 *
 * MU_IN_QUEUE:
 * 	mask for debug use only
 *
 * MU_FORCED_RESTART:
 * 	Force mu request come in when normal mu MD_IO_INRPOG.
 * 	Need to restart the MU as soon as md io finish.
 * 	The normal mu blk is marked MU_FORCED so following forced mu won't
 * 	overwrite it.
 * 	If MD IO error happen on such block, no need to do_pending.
 *
 * MU_PENDING_REQ_FORCED:
 * 	is pending request forced or not, must coexist with MU_HAS_PENDING_REQ
 *
 * MU_FUA:
 * 	must be no flush MU. Indicate a fua bio is waiting in mu->fua_job_list.
 * 	The corresponding MD IO must also be fua.
 *
 * MU_MD_IO_NEED_FUA:
 * 	Set after the mu_blk is picked to send md io and unset after io sent.
 * 	MU_FUA will only be set on no flush mu, but for combo io the flush mu
 * 	will always be picked to send io so we need some way to retain fua info.
 * 	Carrying MU_FUA to flush mu makes it too complicated. Use anther flag
 * 	instead.
 *
 * MU_PENDING_BEFORE_FLUSH:
 * 	If MU_HAS_PENDING_REQ on a no flush mu_blk set before flush bio, the
 * 	pending req should be included by flush bio. Set MU_PENDING_BEFORE_FLUSH
 * 	for mu blocks with pending request when splicing md_io_inprog_queue to
 * 	ext_flush->md_io_inprog_queue. When md io finished, mu block with this
 * 	flag will be requeueud to ext_flush->md_io_pending_queue instead so it
 * 	will be handled by this flush.
 */

#define MU_MD_IO_PENDING		(1 << 0)
#define MU_DISK_FLUSH_PENDING		(1 << 1)
#define MU_HAS_PENDING_REQ		(1 << 2)
#define MU_INPROG			(1 << 3)
#define MU_FLUSH_1			(1 << 4)
#define MU_FLUSH_2			(1 << 5)
#define MU_NEED_FLUSH			(MU_FLUSH_1 | MU_FLUSH_2)
#define MU_MD_IO_COMBO			(1 << 6)
#define MU_MD_IO_INPROG			(1 << 7)
#define MU_FORCED			(1 << 8)
#define MU_SSD_FLUSH_PENDING		(1 << 9)
#define MU_SSD_FLUSH_INPROG		(1 << 10)
#define MU_DISK_FLUSH_INPROG		(1 << 11)
#define MU_IN_QUEUE			(MU_DISK_FLUSH_PENDING | MU_DISK_FLUSH_INPROG | MU_MD_IO_PENDING | \
					MU_SSD_FLUSH_PENDING | MU_SSD_FLUSH_INPROG | MU_MD_IO_INPROG)
#define MU_FORCED_RESTART		(1 << 12)
#define MU_PENDING_REQ_FORCED		(1 << 13)
#define MU_SENT_BY_NORMAL		(1 << 14)
#define MU_SENT_BY_EXT_FLUSH		(1 << 15)
#define MU_SENT_BY_THROTL		(1 << 16)
#define MU_SENT_BY_FORCE		(1 << 17)
#define MU_SENT_BY_FUA			(1 << 18)
#define MU_SENT_BY			(MU_SENT_BY_EXT_FLUSH | MU_SENT_BY_NORMAL | MU_SENT_BY_THROTL | \
					 MU_SENT_BY_FORCE | MU_SENT_BY_FUA)
#define MU_PENDING_REQ_FUA		(1 << 19)
#define MU_FUA				(1 << 20)
#define MU_MD_IO_NEED_FUA		(1 << 21)
#define MU_PENDING_BEFORE_FLUSH		(1 << 22)

/* Each metadata block takes 32 bytes
 * each metadata block represents 16 MB data.
 * 1MB cache need 2 bytes of mem. 1T cache need 2MB of mem */
typedef struct _md_udpate_block {
	struct list_head node;
	int flags; /* MU_* flags */
	int idx;

	/* For need flush MU, start counting time after disk flush finished
	 * Flush mu already waited for fallow delays, another 1 min should be ok */
	unsigned long tstamp;
	// TODO: bitmap to record what to udpate?
} mu_blk_t;

/* True if ANY of the flag is set in the mu_blk.
 * To check if ALL flags are set, use mu_blk_flagged_all. */
static inline int mu_blk_flagged_any(mu_blk_t *mu_blk, int flag)
{
	return ((mu_blk->flags & flag));
}

static inline int mu_blk_flagged_all(mu_blk_t *mu_blk, int flag)
{
	return ((mu_blk->flags & flag) == flag);
}

static inline void mu_blk_set_flag(mu_blk_t *mu_blk, int flag)
{
	mu_blk->flags |= flag;
}

static inline void mu_blk_unset_flag(mu_blk_t *mu_blk, int flag)
{
	mu_blk->flags &= ~(flag);
}

static inline int mu_blk_test_clear_flag_any(mu_blk_t *mu_blk, int flag)
{
	if (mu_blk_flagged_any(mu_blk, flag)) {
		mu_blk_unset_flag(mu_blk, flag);
		return 1;
	}

	return 0;
}

enum {
	MU_BLK_TYPE_NOFLUSH,
	MU_BLK_TYPE_FLUSH,
	NUM_MU_BLK_TYPE,
};

typedef struct _new_mu_ext_flush_bio_node {
	struct list_head node;
	struct bio *bio;
	struct io_latency io_lat;
} ext_flush_bio_node_t;

typedef enum _EXT_FLUSH_DRIVE_FLUSH_STAGE {
	FLUSH_WAITING, // SSD flush need to wait for all md io done
	FLUSH_REQUESTED,
	FLUSH_INPROG,
	FLUSH_DONE,
} EXT_FLUSH_DRIVE_FLUSH_STAGE;

typedef struct _new_mu_ext_flush {
	int inprog;

	/* might also have flush mu inside, won't be too may, doesn't matter
	 * Since they are inprog, should finish soon. */
	struct list_head md_io_inprog_queue;
	struct list_head md_io_pending_queue;

	struct list_head flush_bios_handling;
	struct list_head flush_bios_pending;

	EXT_FLUSH_DRIVE_FLUSH_STAGE disk_flush_stage;
	EXT_FLUSH_DRIVE_FLUSH_STAGE ssd_flush_stage;

	/* This wq handles finite number IOs, won't loop forever */
	// TODO: do we need this or we can send io directly
	struct work_struct send_io_work;

	int error;
} ext_flush_t;

typedef struct _md_dev_flush {
	struct list_head pending_queue;
	struct list_head inprog_queue;

	/* Handle LIMITED number of md io, won't loop forever */
	struct work_struct complete_work;

	struct bio *bio;

	/* don't send flush too often, used to check if we can send next flush */
	unsigned long last_flush_jif;
	int inprog;
	int forced; // ignore time / item in queue constraint.

} dev_flush_t;

#define MU_DEFAULT_FLUSH_ITVL_SEC 30
#define MU_DEFAULT_DELAY_SEC 30

typedef struct _new_md_update {

	dev_flush_t disk_flush;
	dev_flush_t ssd_flush;
	int flush_itvl_sec;

	struct list_head fua_job_list;

	/* for combo mu, inprog queue doesn't not contains sib mu */
	struct list_head md_io_inprog_queue;
	struct list_head md_io_pending_queue;
	struct list_head md_io_for_flush_pending_queue;
	int md_io_pending_queue_size; // for throttle, target only no flush mu

	/* Handle LIMITED number of md io, won't loop forever */
	struct work_struct md_io_complete_work;
	struct kcached_job *md_io_complete_job_queue;

	wait_queue_head_t remove_wqh;

	struct delayed_work dwork;

	mu_blk_t *mu_blocks[NUM_MU_BLK_TYPE];
	int inprog_blk_num[NUM_MU_BLK_TYPE];
	int pending_blk_num[NUM_MU_BLK_TYPE];

	ext_flush_t ext_flush;

	// TODO: no need mu in var name
	int sysctl_mu_ios_total; // will not limit ext flush
	int sysctl_mu_delay_sec;
	int sysctl_mu_check_itvl_sec;

	int allow_throtl; // Will it be candidate of throttling, protected by dmcg->rw_sem
	// TODO: remove and use dmcg->md_io_throtl_thres
	int group_throtl_thres;

	spinlock_t lock; // cache_spin_lock under this
	int inited;
	int remove_flush;

	/* stats */
	atomic_t throtl_help;
	atomic_t throtl_self;

	int md_io_inprog_ext_flush;
	int md_io_inprog_throtl;
	int md_io_inprog_normal; // normal delayed mu, include flush/no flush
	int md_io_inprog_force;
	int md_io_inprog_fua;

	int sent_md_io_ext_flush_cnt;
	int sent_md_io_throtl_cnt;
	int sent_md_io_normal_cnt;
	int sent_md_io_force_cnt;
	int sent_md_io_fua_cnt;

	// TODO: raw debug stats, remove?
	atomic_t send_md_io_cnt;
	atomic_t md_io_callback_cnt;
	atomic_t md_io_in_queue;
	atomic_t md_io_to_queue;
} new_mu_t;
#endif

#ifdef MY_ABC_HERE
typedef struct _dmc_group dmc_group_t;
#endif
typedef struct _dmc_node dmc_node;
/*
 * Cache context
 * SYNO: only flash_superblock would be saved to metadata
 */
struct cache_c {

#ifdef MY_ABC_HERE
	/*
	 * XXX: Variables in this section are initizlied in Dummy mode
	 */
	int cache_mode;
	cache_state_t cache_state;

	spinlock_t cache_spin_lock;
	spinlock_t reload_lock;
	spinlock_t dev_lock;
	/*
	 * WARNING:
	 * disk_dev might be changed on expansion
	 * Use get_disk_bdev() instead of disk_dev->bdev
	 */
	struct dm_dev *disk_dev;   /* Source device */
	struct block_device *disk_bdev;
	char disk_devname[DEV_PATHLEN];

	// Used in enable / disable
	wait_queue_head_t wait_for_cache_disable_queue;
	wait_queue_head_t wait_old_ios_completed_queue;

	// SSD RAID name (dummy mode is "none")
	char cache_devname[DEV_PATHLEN];
	char dm_vdevname[DEV_PATHLEN];
	struct dm_target *tgt;

	// In the progress to enable or disable
	int in_switching;
	atomic_t num_master_io;

	char id[ID_LEN]; // Currently only save disk_devname
	struct cache_c	*next_cache;
	struct rw_semaphore ioctl_rwsem;
	dmc_node *node;
#endif /* MY_ABC_HERE */

	/*
	 * XXX:
	 * Variables below are initizlied when cache enabling
	 * and reset to zero while cache disabling
	 *
	 */
#ifdef MY_ABC_HERE
	int point_to_reset;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#else
	struct dm_dev 		*disk_dev;   /* Source device */
#endif /* MY_ABC_HERE */

	struct dm_dev 		*cache_dev; /* Cache device */

#ifdef MY_ABC_HERE
	// Here assume bdev won't be changed even dm_dev is reallocated
#ifdef MY_ABC_HERE
#else
	spinlock_t dev_lock;
	struct block_device *disk_bdev;
#endif /* MY_ABC_HERE */
	struct block_device *cache_bdev;

	// Save new dm_dev created at expansion
	struct dm_dev		*disk_dev_reload;
	struct dm_dev		*cache_dev_reload;
#endif
	int 			on_ssd_version;
	
#ifdef MY_ABC_HERE
	// Move above
#else
	spinlock_t		cache_spin_lock;
#endif /* MY_ABC_HERE */

	struct cacheblock	*cache;	/* Hash table for cache blocks */
	struct cache_set	*cache_sets;
	struct cache_md_block_head *md_blocks_buf;

	unsigned int md_block_size;	/* Metadata block size in sectors */
	
	// SYNO: num of cacheblk
	sector_t size;			/* Cache size */
	unsigned int assoc;		/* Cache associativity */
	unsigned int block_size;	/* Cache block size */
	unsigned int block_shift;	/* Cache block size in bits */
	unsigned int block_mask;	/* Cache block mask */
	unsigned int assoc_shift;	/* Consecutive blocks size in bits */
	unsigned int num_sets;		/* Number of cache sets */
	
#ifdef MY_ABC_HERE
#else
	int	cache_mode;
#endif /* MY_ABC_HERE */

	/* wait for nr_jobs == 0 and sync stopped when sync for removm */
	wait_queue_head_t sync_wqh;	/* Wait queue for I/O completion */
	/* XXX - Updates of nr_jobs should happen inside the lock. But doing it outside
	 * is OK since the filesystem is unmounted at this point
	 * WARNING: nr_jobs will reach zero before force abort bio finish, so
	 *          there could still be ios inside cache when nr_jobs is zero.
	 *          Use it with num_master_io to ensure no ongoing io. */
	atomic_t nr_jobs;		/* Number of I/O jobs */

#define SLOW_REMOVE    1
#define FAST_REMOVE    2
	atomic_t remove_in_prog;

	int	dirty_thresh_set;	/* Per set dirty threshold to start cleaning */
	int	max_clean_ios_set;	/* Max cleaning IOs per set */
	int	max_clean_ios_total;	/* Total max cleaning IOs */
#ifdef MY_ABC_HERE
#else
	int	clean_inprog;
#endif

#ifdef MY_ABC_HERE
	/*
	 * Set this value if dtr() or do_sync has called sync_all()
	 * This value is only set back to zero when stop_sync is set to 1
	 */
	int start_sync_all;
#ifdef MY_ABC_HERE
#else
	int	sync_index;
#endif
	/*
	 * -1: cache is not flushing (default value)
	 *  >=0: count of dirty blocks when starting flushing
	 */
	int	init_nr_dirty;
#endif
	int	nr_dirty;
	int	nr_synced;
	/* When a cache block with state DIRTY or PIN_FILE or SYNCED, it is counted as occupied */
	int nr_occupied;
	atomic64_t cached_blocks;	/* Number of cached blocks */
	unsigned long pending_jobs_count;
	int	md_blocks;		/* Numbers of metadata blocks, including header */

	/* Stats */
	struct flashcache_stats flashcache_stats;

	/* Errors */
	struct flashcache_errors flashcache_errors;

#define IO_LATENCY_GRAN_USECS	250
#define IO_LATENCY_MAX_US_TRACK	10000	/* 10 ms */
#define IO_LATENCY_BUCKETS	(IO_LATENCY_MAX_US_TRACK / IO_LATENCY_GRAN_USECS)
	unsigned long	latency_hist[IO_LATENCY_BUCKETS];
	unsigned long	latency_hist_10ms;
	

#ifdef MY_ABC_HERE
#else
	struct delayed_work delayed_clean;
#endif

	unsigned long pid_expire_check;

	struct flashcache_cachectl_pid *blacklist_head, *blacklist_tail;
	struct flashcache_cachectl_pid *whitelist_head, *whitelist_tail;
	int num_blacklist_pids, num_whitelist_pids;
	unsigned long blacklist_expire_check, whitelist_expire_check;

#define PENDING_JOB_HASH_SIZE		32
	struct pending_job *pending_job_hashbuckets[PENDING_JOB_HASH_SIZE];
	
#ifdef MY_ABC_HERE
#else
	struct cache_c	*next_cache;
#endif /* MY_ABC_HERE */

	void *sysctl_handle;

#ifdef MY_ABC_HERE
#else
	// DM virtual device name, stored in superblock and restored on load
	char dm_vdevname[DEV_PATHLEN];
	// real device names are now stored as UUIDs
	char cache_devname[DEV_PATHLEN];
	char disk_devname[DEV_PATHLEN];
#endif /* MY_ABC_HERE */

	/* 
	 * When this is set, all the IOs are bypassed to HDDs
	 *
	 * Note: If you get a SSD Read / Write error, you must check if you need to set the value
	 * because you just got the first IO error
	 */
	int bypass_cache; // Might not be read under lock since it wont' be set back to 0 from 1

	/* Per device sysctls */
	int sysctl_io_latency_hist;
	int sysctl_do_sync;
	int sysctl_stop_sync;
	int sysctl_dirty_thresh;
	int sysctl_pid_do_expiry;
	int sysctl_max_pids;
	int sysctl_pid_expiry_secs;
	int sysctl_reclaim_policy;
	int sysctl_zerostats;
	int sysctl_error_inject;
	int sysctl_fast_remove;
	int sysctl_cache_all;
	int sysctl_cache_all_input;
	int sysctl_fallow_clean_speed;
	int sysctl_fallow_delay;
	int sysctl_skip_seq_thresh_kb;
	int sysctl_skip_seq_fgap_kb; // forward gap
	int sysctl_skip_seq_bgap_kb; // backward gap
	int sysctl_new_error_inject; // use number instead of bit

	/* Sequential I/O spotter */
	struct sequential_io	seq_recent_ios[SEQUENTIAL_TRACKER_QUEUE_DEPTH];
	struct sequential_io	*seq_io_head;
	struct sequential_io 	*seq_io_tail;
	spinlock_t seq_io_spin_lock;

	// cacheblk utilization
	unsigned long bits_used;
	unsigned long bits_all;

	// for checking preread range
	sector_t disk_devsize;
	int max_io_len;
#ifdef MY_ABC_HERE
	// Block sector to query
	// Use u64 for alpine & x86_64 compatible issue
	u64 query_start_sector;
	u64 query_num_sectors;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	unsigned long long bitmap_size_byte;
	unsigned char *pbitmap_ranges;
#ifdef MY_ABC_HERE
	bitmap_control_t bitmap_control;
#endif
	internal_query_t internal_query;
	int bitmap_is_set;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	int reload_table;
#ifdef MY_ABC_HERE
#else
	spinlock_t reload_lock;
	char id[ID_LEN];
#endif /* MY_ABC_HERE */
#endif
#ifdef MY_ABC_HERE
	atomic_t num_uncached_write;
	atomic_t num_write_cache;
	atomic_t num_flush_bio;
	atomic_t in_flush;
	wait_queue_head_t wait_flush_queue;
	wait_queue_head_t wait_io_done_queue;

	struct dm_io_region disk_region;
	struct dm_io_region cache_region;
#ifdef MY_ABC_HERE
#else
	// For flush bio
	spinlock_t disk_flush_lock;
	int disk_flushing;
	struct list_head disk_flush_queue1;
	struct list_head disk_flush_queue2;
	struct list_head *disk_flush_pending_queue;
	struct list_head *disk_flush_io_queue;
	struct list_head *disk_flush_unused_queue;
	struct bio *flush_bio;
	struct work_struct flush_work;
	struct work_struct disk_flush_io_queue_work;

	/* For metadata flush */
	spinlock_t md_flush_lock;
	int md_flushing;
	atomic_t inflight_md_write_for_flush;
	struct list_head md_flush_queue1;
	struct list_head md_flush_queue2;
	struct list_head *md_flush_unused_queue;
	struct list_head *md_flush_io_queue;
	struct list_head *md_flush_pending_queue;
	struct bio *md_flush_bio;
	struct work_struct md_flush_work;
	struct work_struct md_flush_io_queue_work;
#endif /* MY_ABC_HERE */
#endif /* MY_ABC_HERE */

#ifdef CONFIG_SYNO_DATA_CORRECTION
	struct correction_entry *correction_list;
	spinlock_t corretion_spin_lock;
#endif
#ifdef MY_ABC_HERE
	print_limit_t limits;
	attribute_t attribute;
#endif
#ifdef MY_ABC_HERE
	u_int32_t hash_mapping;
	u_int32_t tail_region_idx;
	u_int32_t tail_region_num_sets;
	u_int32_t region_num_sets;
#endif
#ifdef MY_ABC_HERE
	load_action_t load_action;
	spinlock_t endio_spin_lock;
#endif

#ifdef MY_ABC_HERE
	plug_wb_t plug_wb;
#else
	int sysctl_ssd_read_ios_limit;
#endif

	/* Use double buffer so the timer won't affect latency */
	struct io_latency_stats last_itvl_lat_stats[2][CACHE_IO_TYPE_NUM];
	int lat_stats_idx;
	spinlock_t lat_spin_lock;
	int sysctl_syno_latency_diagnose;
	int sysctl_latency_diagnose_itvl_ms;
	int sysctl_io_disk_lat_thres_ms;
	int sysctl_io_ssd_lat_thres_ms;
	int sysctl_lat_perc_ten_thousandth;
	struct timer_list latency_timer;
	unsigned long lat_thresholds_jiffies[CACHE_IO_TYPE_NUM];

	/* To guard the CHANGE of bypass_cache and cache_all
	 * we need to check cache_all when setting bypass, and check bypass
	 * when setting cache_all, no need to lock on read */
	spinlock_t bypass_lock;

#ifdef MY_ABC_HERE
	online_qf_t oqf;
#endif

#ifdef MY_ABC_HERE
	new_mu_t new_mu;
#endif
#ifdef MY_ABC_HERE
	/* For enable/create get from input arguments
	 * For load, get from superblock
	 * For older version this should be emtpy string */
	char group_uuid[GROUP_ID_LEN];
	// Some entries is for sqf or nfq, e.g. writeback in turns
	dmc_group_t *dmcg;
#endif
};

#ifdef MY_ABC_HERE
typedef struct _job_node {
	struct kcached_job *job;
	struct list_head list;
} job_node;
#endif

#define PROCESS_JOB_RESCHED_BATCH 256

struct kcached_wq {
	void (*fn) (struct kcached_job *);
	/* For every PROCESS_JOB_RESCHED_BATCH job we check for reschedule
	 * in this workqueue to avoid infinite loop */
	struct work_struct kwq_work;
	struct list_head jobs;
};

typedef enum _kcached_wq_type {
	PENDING,
	DO_IO,
#ifdef MY_ABC_HERE
#else
	MD_IO,
	MD_COMPLETE,
#endif
	UNCACHED_IO_COMPLETE,
	PREREAD,
	FREE_JOB,
	KCACHED_WQ_SIZE,
} kcwq_type_t;

extern struct kcached_wq kcached_wq[KCACHED_WQ_SIZE];
extern struct delayed_work nqf_mgmt_work;

#define cache_schedule_io()	\
	do { cache_schedule_work(&kcached_wq[DO_IO].kwq_work); } while(0)
#define cache_schedule_pending()	\
	do { cache_schedule_work(&kcached_wq[PENDING].kwq_work); } while(0)
#define cache_schedule_uncached_io_complete()	\
	do { cache_schedule_work(&kcached_wq[UNCACHED_IO_COMPLETE].kwq_work); } while(0)

#ifdef MY_ABC_HERE
#else
#define cache_schedule_md_io()	\
	do { cache_schedule_work(&kcached_wq[MD_IO].kwq_work); } while(0)
#define cache_schedule_md_complete()	\
	do { cache_schedule_work(&kcached_wq[MD_COMPLETE].kwq_work); } while(0)
#endif /* !MY_ABC_HERE */

#define cache_schedule_preread_io()	\
	do { cache_schedule_work(&kcached_wq[PREREAD].kwq_work); } while(0)
#define cache_schedule_to_free_jobs()	\
	do { cache_schedule_work(&kcached_wq[FREE_JOB].kwq_work); } while(0)

void push(struct list_head *jobs, struct kcached_job *job);

static inline void push_pending(struct kcached_job *job)
{
	push(&kcached_wq[PENDING].jobs, job);
}

static inline void push_io(struct kcached_job *job)
{
	push(&kcached_wq[DO_IO].jobs, job);
}

static inline void push_uncached_io_complete(struct kcached_job *job)
{
	push(&kcached_wq[UNCACHED_IO_COMPLETE].jobs, job);
}

#ifdef MY_ABC_HERE
#else
static inline void push_md_io(struct kcached_job *job)
{
	push(&kcached_wq[MD_IO].jobs, job);
}

static inline void push_md_complete(struct kcached_job *job)
{
	push(&kcached_wq[MD_COMPLETE].jobs, job);
}
#endif /* !MY_ABC_HERE */

static inline void push_preread_io(struct kcached_job *job)
{
	push(&kcached_wq[PREREAD].jobs, job);
}

static inline void push_to_free_jobs(struct kcached_job *job)
{
	push(&kcached_wq[FREE_JOB].jobs, job);
}

static inline int flashcache_pending_empty(void)
{
	return list_empty(&kcached_wq[PENDING].jobs);
}

static inline int flashcache_io_empty(void)
{
	return list_empty(&kcached_wq[DO_IO].jobs);
}

#ifdef MY_ABC_HERE
#else
static inline int flashcache_md_io_empty(void)
{
	return list_empty(&kcached_wq[MD_IO].jobs);
}

static inline int flashcache_md_complete_empty(void)
{
	return list_empty(&kcached_wq[MD_COMPLETE].jobs);
}
#endif

static inline int flashcache_uncached_io_complete_empty(void)
{
	return list_empty(&kcached_wq[UNCACHED_IO_COMPLETE].jobs);
}

static inline int flashcache_preread_empty(void)
{
	return list_empty(&kcached_wq[PREREAD].jobs);
}

static inline int flashcache_free_job_empty(void)
{
	return list_empty(&kcached_wq[FREE_JOB].jobs);
}

#ifdef CONFIG_SYNO_DATA_CORRECTION
typedef enum _use_lock_type {
	NO_LOCK=0,
	DO_LOCK=1,
} lock_type_t;

typedef enum _correcting_type {
	NO_CORRECTING = -1,
	TYPE_UNINITED = 0,
	DISK_CORRECTING = 1,
	SSD_CORRECTING = 2,
} correcting_type_t;

#endif

#ifdef MY_ABC_HERE
#define DMCG_MU_THROTL_REBALANCE_THRES_IOS 10000
#endif

#ifdef MY_ABC_HERE
typedef struct _dmc_group {
	/* protected by dmcg_mtx */
	struct list_head node;
	char uuid[GROUP_ID_LEN]; // will not change after create

	/* following fields are protected by this */
	struct rw_semaphore rw_sem;

	/* House keeping */
	int num_dmc;
	struct list_head dmc_list;

#ifdef MY_ABC_HERE
	/* MD update throttle */
	struct cache_c *throtl_dmc; // DMC that is going to be throttled
	atomic_t md_io_pending_no_flush;
	int md_io_throtl_thres;
	atomic_t num_throtl_io;
#endif

	/* Stats */
	atomic_t rebalance_cnt;

	/* Normal Quickflush Control */
	struct list_head nqf_pending_dmcs;
	unsigned long nqf_expire_jiffies;
	struct cache_c *nqf_dmc;
	atomic_t sqf_inprog;
	wait_queue_head_t nqf_wqh;
	int nqf_yielded;

} dmc_group_t;
#endif


#ifdef MY_ABC_HERE
typedef struct _dmc_node {
	struct list_head entry;
#ifdef MY_ABC_HERE
	struct list_head group_entry;
#endif
	// For writeback in turns
	struct list_head nqf_entry;
	struct cache_c *pdmc;
	char id[ID_LEN];
} dmc_node;
#endif

/* kcached/pending job states */
#define READCACHE	1
#define WRITECACHE	2
#define READDISK	3
#define WRITEDISK	4
#define READFILL	5	/* Read Cache Miss Fill */
#define INVALIDATE	6
#define WRITEDISK_SYNC	7

#ifdef MY_ABC_HERE
#define READDISKMEM 8
#define WRITEMEMCACHE 9
#endif

/*
 * we need other GFP flag so use __vmalloc, retain original vmalloc flags
 * Note: need to check during kernel porting
 */
#define VMALLOC_INT_FLAG (GFP_KERNEL | __GFP_HIGHMEM)

typedef enum _KCACHED_JOB_FLAG {
	/* writeback attributes */
	JOB_WB_READ_DONE		= 1 << 0,
	JOB_WB_FORCED			= 1 << 1, // ignore wb throttle. Used in invalidate
	JOB_WB_NQF			= 1 << 2, // Trigger clean cache after wb done

	/* job->mem_addr attributres */
	JOB_MEM_VMALLOC			= 1 << 3,
	JOB_MEM_MEMPOOL			= 1 << 4,

	/* To distinguish if mu for job in new_mu->fua_job_list has been sent
	 * When mu finish, if job mu fua inprog then we can finish the request */
	JOB_FUA_MU_INPROG		= 1 << 5,

} KCACHED_JOB_FLAG;

struct kcached_job {
	struct list_head list; // TODO: change name to node
	struct cache_c *dmc;
	struct bio *bio;	/* Original bio */
	struct job_io_regions {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
		struct io_region disk;
		struct io_region cache;
#else
		struct dm_io_region disk;
		struct dm_io_region cache;
#endif
	} job_io_regions;
	int    index;
	int    action;
	int 	error;
	struct flash_cacheblock *md_block;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	struct bio *md_bio;
#else
	struct bio_vec md_io_bvec;
#endif
	ktime_t io_start_time;
	struct kcached_job *next;
#ifdef MY_ABC_HERE
	// For preread after read miss, always has the same size as cache block
	void * mem_addr;
	// Data (bytes) that would be read to mem_addr
	unsigned long mem_data_byte;
	// Data range this job will act on
	bitmap_t action_bitmap;
#endif
	struct io_latency io_lat;
	int flag; // KCACHED_JOB_FLAG
	unsigned long long wb_seq;
	// General context
	void *ctx;
};

struct pending_job {
	struct bio *bio;
	int	action;	
	int	index;
	struct pending_job *prev, *next;
	struct io_latency io_lat;
};
#endif /* __KERNEL__ */



/* Cache block metadata structure */
#ifdef MY_ABC_HERE

// convert unsigned int for apline compatible
#define BITMAP_BITS (unsigned int)(sizeof(bitmap_t)*8)
#define SECTOR_PER_BIT 8  // 4kb
#define BITMAP_MASK 0xffff
#define JOB_MEM_BYTE to_bytes(NEW_MODE_BLOCK_SIZE)

// limit the size of memory used by online writeback / preread / invalidate
#if defined(CONFIG_SYNO_ALPINE)
#define MAX_JOB_MEM_COUNT 512 // vmalloc avalable memory is limited on alpine
#else
#define MAX_JOB_MEM_COUNT 2048
#endif
#endif

#ifdef __KERNEL__

#ifdef MY_ABC_HERE
extern struct bio_set *pcache_bio_set;
#endif

#ifdef MY_DEF_HERE
extern mempool_t * _cache_bio_pool;
struct cache_bio {
	struct bio *master_bio;
	// Total number of sub bios
	int num_total_bios;
	// Use atomic due to it could be accessed by multiple threads
	atomic_t num_completed_bios;
	int error;
	unsigned long start_jiffy;
};

struct shared_data {
	struct bio *master_bio;
	sector_t unhandled_sectors;
	sector_t next_bio_start;
	unsigned short next_bv_idx;
	int bv_len_sum;
};

struct subbio {
	sector_t start_sector;
	sector_t num_sectors;
	unsigned int first_bv_len;
	unsigned int first_bv_offset;
	unsigned short first_bv_idx;
	unsigned int last_bv_len;
	unsigned short last_bv_idx;
};

#ifdef MY_ABC_HERE
struct subbio_info {
	struct cache_bio *pcache_bio;
	int is_pin;
};
#endif
#else /* NOT MY_DEF_HERE */
extern mempool_t * _cache_bi_private_pool;
struct cache_bi_private {
	void *master_bio_private;
	unsigned long start_jiffy;
	unsigned char is_pin;
};
#endif /* MY_DEF_HERE */

#endif /* __KERNEL__ */

#ifdef MY_ABC_HERE
typedef enum _hash_mapping_type {
	HASH_MAPPING_DISABLE = 0,
	HASH_MAPPING_V1 = 1,

	/*
	 * Hash Mapping V2 splits cache into multiple default 256GB regions
	 * when lookup, we first mod the volume lba with cache size and find the
	 * corresponding cache regions for the lba. Then we apply the hash mapping
	 * algorithm within region to find the final cache set.
	 * This keeps data locality within a region, avoid the need to update
	 * all metadata of cache for random writes within a region.
	 *
	 * Special cases: often the cache size will not be multiple of region size,
	 * in such case if the remained area is larger than half the region we
	 * consider it a valid region. Otherwise it is combined with the last
	 * full region. The last region (after combine if needed) is refer to
	 * as the tail region.
	 */
	HASH_MAPPING_V2 = 2,
} hash_mapping_t;
#endif

#define MD_BLOCK_BYTES(DMC)		((DMC)->md_block_size * 512)
#define MD_SECTORS_PER_BLOCK(DMC)	((DMC)->md_block_size)
// SYNO: num of cacheblk in a md blocks
#define MD_SLOTS_PER_BLOCK(DMC)		(MD_BLOCK_BYTES(DMC) / (sizeof(struct flash_cacheblock)))

#define INDEX_TO_MD_BLOCK(DMC, INDEX)	compatible_div((INDEX), MD_SLOTS_PER_BLOCK(DMC))
// SYNO: Index in current md block
#define INDEX_TO_MD_BLOCK_OFFSET(DMC, INDEX)	compatible_mod((INDEX), MD_SLOTS_PER_BLOCK(DMC))

#define MD_BLOCK_TO_START_IDX(DMC, MD_IDX)	((MD_IDX) * MD_SLOTS_PER_BLOCK(DMC))

#define METADATA_IO_NUM_BLOCKS(dmc)	(METADATA_IO_BLOCKSIZE / MD_BLOCK_BYTES(dmc))

// SYNO: block_shift is cacheblk shift, change block index to sector
#define INDEX_TO_CACHE_ADDR(DMC, INDEX)	\
	(((sector_t)(INDEX) << (DMC)->block_shift) + (DMC)->md_blocks * MD_SECTORS_PER_BLOCK((DMC)))

#ifdef __KERNEL__

#ifdef MY_ABC_HERE
extern struct workqueue_struct *cache_workqueue;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define MAX_BIO_SIZE 128
#define ALIGNED_SECTORS 8
#define CACHE_BLK_SIZE_SEC MAX_BIO_SIZE

#ifdef MY_ABC_HERE
#define SUB_BLOCK_SIZE ALIGNED_SECTORS
#define MAX_NUM_LOCATION 16
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#ifdef MY_DEF_HERE
#define MAX_HIST_SECTOR 2048
#else
#define MAX_HIST_SECTOR NEW_MODE_BLOCK_SIZE
#endif /* MY_DEF_HERE */
#else
#define MAX_HIST_SECTOR 32
#endif /* MY_ABC_HERE */

#define BIO_STR_MAX 64
#define JOB_STR_MAX 256
// USHRT_MAX
#define NR_CONCURRENT_MAX (u_int8_t)(~0U)
#include "syno_functions.h"
#endif /* MY_ABC_HERE */

/* Cache persistence */
#define CACHE_RELOAD		1
#define CACHE_CREATE		2
#define CACHE_FORCECREATE	3

/* Latency Diagnose */
#define LAT_HIT_DISK_THRES_MS 2000
#define LAT_HIT_SSD_THRES_MS 2000
#define LAT_INTERVAL_MS 1000
#define LAT_PERC_TEN_THOUSANDTH 9900

/* 
 * We have one of these for *every* cache metadata sector, to keep track
 * of metadata ios in progress for blocks covered in this sector. Only
 * one metadata IO per sector can be in progress at any given point in 
 * time
 */
struct cache_md_block_head {
	u_int32_t		nr_in_prog;
	/*
	 * SYNO:
	 * queued_updates: list to save queued job when md block is busy
	 * md_io_inprog: pointer to quque_updates when md_write_kickoff want to process ququed data
	 */
	struct kcached_job	*queued_updates, *md_io_inprog;
};

#define MIN_JOBS 1024
#define MIN_PREREAD_JOBS 32

/* Default values for sysctls */
#define DIRTY_THRESH_MIN	10
#define DIRTY_THRESH_MAX	90
#define DIRTY_THRESH_DEF	20

#define MAX_CLEAN_IOS_SET	2
#define MAX_CLEAN_IOS_TOTAL	4
#define MAX_PIDS		100
#define PID_EXPIRY_SECS		60
#define FALLOW_DELAY		(60*15) /* 15 Mins default */
#define FALLOW_SPEED_MIN	1
#define FALLOW_SPEED_MAX	100
#define FALLOW_CLEAN_SPEED	2

/* DM async IO mempool sizing */
#define FLASHCACHE_ASYNC_SIZE 1024

enum {
	FLASHCACHE_WHITELIST=0,
	FLASHCACHE_BLACKLIST=1,
};

struct flashcache_cachectl_pid {
	pid_t					pid;
	struct flashcache_cachectl_pid		*next, *prev;
	unsigned long				expiry;
};

struct dbn_index_pair {
	sector_t	dbn;
	int		index;
};

#ifdef CONFIG_SYNO_DATA_CORRECTION
#define NUM_SUB_BLOCK (MAX_BIO_SIZE / ALIGNED_SECTORS)
#define CORRECTION_BIO_BYTES 4096
/*
 * Use a entry to records the correction status of a 64KB-aligned block.
 * A 64KB block has multiple 4KB ranges to record if each range is performing
 * correction on SSD RAID or Disk RAID.
 */
struct correction_entry {
	u64 start_sector;
	// Which 4k range is correcting, In BTRFS spec, only sends 4KB correction bio.
	u16 correcting_bitmap;
	u8 correcting_types[NUM_SUB_BLOCK]; // Record the type of each 4KB range
	u16 need_abort_bitmap;
	sector_t cache_addr; // Record the corresponding LBA in SSD RAID
	struct list_head link;
};

struct send_force_abort_bio_task {
	struct cache_c *dmc;
	atomic_t bio_count;
	struct bio *bio;
	int error;
	sector_t entry_start_sector;
	u16 need_abort_bitmap;
	sector_t cache_addr;
	/* Handle one force abort bio only, won't loop forever */
	struct work_struct work;
	struct io_latency io_lat;
	ktime_t start_time;
};

#endif
/* Error injection flags */
#define READDISK_ERROR				0x00000001
#define READCACHE_ERROR				0x00000002
#define READFILL_ERROR				0x00000004
#define WRITECACHE_ERROR			0x00000008
#define WRITECACHE_MD_ERROR			0x00000010
#define WRITEDISK_MD_ERROR			0x00000020
#define KCOPYD_CALLBACK_ERROR			0x00000040
#define DIRTY_WRITEBACK_JOB_ALLOC_FAIL		0x00000080
#define READ_MISS_JOB_ALLOC_FAIL		0x00000100
#define READ_HIT_JOB_ALLOC_FAIL			0x00000200
#define READ_HIT_PENDING_JOB_ALLOC_FAIL		0x00000400
#define INVAL_PENDING_JOB_ALLOC_FAIL		0x00000800
#define WRITE_HIT_JOB_ALLOC_FAIL		0x00001000
#define WRITE_HIT_PENDING_JOB_ALLOC_FAIL	0x00002000
#define WRITE_MISS_JOB_ALLOC_FAIL		0x00004000
#define WRITES_LIST_ALLOC_FAIL			0x00008000
#define MD_ALLOC_SECTOR_ERROR			0x00010000
#ifdef MY_ABC_HERE
// Start from Bit 17
#define SIMULATE_CACHE_DISABLE_LONG_TIME (1 << 17)
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
#define SIMULATE_DIRTY_WRITEBACK_LONG_TIME (1 << 18)
#endif
#ifdef MY_ABC_HERE
#define QF_ALLOC_BITMAP_ERROR			(1 << 19)
#define QF_ALLOC_SEG_NOT_ENOUGH_ERROR		(1 << 20)
#define QF_ALLOC_SEG_ENOUGH_ERROR		(1 << 21)
#define QF_ALLOC_CACHEBLK_TO_CLEAN_ERROR	(1 << 22)
#define QF_ALLOC_WRITEBACK_JOB_ERROR		(1 << 23)
#endif
#ifdef MY_ABC_HERE
#define WRITEBACK_JOB_ALLOC_ERROR		(1 << 24)
#define READ_HIT_DELAY				(1 << 25)
#endif
#ifdef MY_ABC_HERE
#define MD_PREWRITE_DELAY			(1 << 26)
#define MD_PREFLUSH_DELAY			(1 << 27)
#define MD_POSTFLUSH_DELAY			(1 << 28)
#endif
#define READ_HIT_NORMAL_IO_WAIT		(1 << 29)
#define READ_HIT_CORR_IO_WAIT		(1 << 30)
#ifdef MY_ABC_HERE
#define FLUSH_BIO_ERROR				(1 << 31)
#endif

typedef enum _new_error_inject {
	OQF_RELOAD_MEM_ALLOC_FAIL = 1,
	OQF_WB_ERROR = 2,
	EXT_FLUSH_BIO_LIST_ALLOC_FAIL = 3,
	DISK_FLUSH_ERROR = 4,
	SSD_FLUSH_ERROR = 5,
	FORCE_MD_IO_ERROR = 6,
	MD_IO_ERROR = 7,
	MU_FORCED_RESTART_TEST = 8,
	// HOLE, free to use
	MD_BIO_ALLOC_FAIL = 10,
	MD_IO_JOB_ALLOC_FAIL = 11,
	FORCED_MD_IO_JOB_ALLOC_FAIL = 12,
	WB_SSD_READ_ERROR = 13,
	WB_DISK_WRITE_ERROR = 14,
	SYNCED_STATE_CHANGE_WHEN_FORCE_MU = 15,
	WB_MU_REQUEUE_FORCED_TEST = 16,
	MD_IO_COMBO_TEST = 17,
	FORCE_ABORT_BIO_WQ_DELAY = 18,
	FORCE_ABORT_BIO_ALLOC_FAIL = 19,
	WRITE_FUA_TEST = 20,
	WRITE_FUA_ERROR = 21,
	FORCE_NO_FLUSH_PENDING = 22,
	IO_INPROG_DELAY = 23,
} new_error_inject_t;

static inline int
__check_new_error_inject(struct cache_c *dmc, new_error_inject_t err)
{
	return dmc->sysctl_new_error_inject == err;
}

#define check_new_error_inject(dmc, err) unlikely(__check_new_error_inject(dmc, err))

/* Inject a 5s delay between syncing blocks and metadata */
#define FLASHCACHE_SYNC_REMOVE_DELAY		5000

#ifdef MY_ABC_HERE
int flashcache_noclone_map(struct dm_target *ti, struct bio *master_bio);
#endif /* MY_ABC_HERE */

int
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
flashcache_map(struct dm_target *ti, struct bio *bio,
	       union map_info *map_context);
#else
flashcache_map(struct dm_target *ti, struct bio *bio);
#endif

int flashcache_ctr(struct dm_target *ti, unsigned int argc,
		   char **argv);
void flashcache_dtr(struct dm_target *ti);

struct kcached_job *flashcache_alloc_cache_job(void);
void flashcache_free_cache_job(struct kcached_job *job);
struct pending_job *flashcache_alloc_pending_job(struct cache_c *dmc);
void flashcache_free_pending_job(struct pending_job *job);
#ifdef FLASHCACHE_DO_CHECKSUMS
u_int64_t flashcache_compute_checksum(struct bio *bio);
void flashcache_store_checksum(struct kcached_job *job);
int flashcache_validate_checksum(struct kcached_job *job);
int flashcache_read_compute_checksum(struct cache_c *dmc, int index, void *block);
#endif
struct kcached_job *pop(struct list_head *jobs);
void push(struct list_head *jobs, struct kcached_job *job);
void process_kcached_wq(struct work_struct *work);
struct kcached_job *new_kcached_job(struct cache_c *dmc, struct bio* bio,
				    int index, struct io_latency *plat,
				    enum cache_io_type io_type);
void flashcache_do_preread_io(struct kcached_job *job);
void process_free_job(struct kcached_job *job);

void flashcache_do_pending(struct kcached_job *job);
void flashcache_do_pending_force_mu(struct cache_c *dmc, int idx, int error);
void flashcache_md_write_done(struct kcached_job *job);
void flashcache_md_write(struct kcached_job *job);
void flashcache_md_write_kickoff(struct kcached_job *job);
void flashcache_do_io(struct kcached_job *job);
void flashcache_uncached_io_complete(struct kcached_job *job);
#ifdef MY_ABC_HERE
#else
void flashcache_clean_set(struct cache_c *dmc, int set);
#endif
void flashcache_sync_all(struct cache_c *dmc);
void flashcache_clean_sync_state(struct cache_c *dmc);
void flashcache_reclaim_lru_movetail(struct cache_c *dmc, int index);
#ifdef MY_ABC_HERE
void bitmap_control_get(struct cache_c *dmc);
void bitmap_control_put(struct cache_c *dmc);
#endif
#ifdef MY_ABC_HERE
void flashcache_sort_writes(struct dbn_index_pair *writes_list, int nr_writes, int set);
#endif
void flashcache_merge_writes(struct cache_c *dmc, 
			     struct dbn_index_pair *writes_list, 
			     int *nr_writes, int set);

int flashcache_dm_io_sync_vm(struct cache_c *dmc, struct dm_io_region *where, 
			     int rw, void *data);

void flashcache_update_sync_progress(struct cache_c *dmc);
void flashcache_enq_pending(struct cache_c *dmc, struct bio* bio, int index,
			    int action, struct pending_job *job,
			    struct io_latency *plat, enum cache_io_type io_type);
struct pending_job *flashcache_deq_pending(struct cache_c *dmc, int index);

/* job->md_bio exists after 3.14, cannot remove this */
int dm_io_async_bvec(unsigned int num_regions, 
			    struct dm_io_region *where, 
			    int rw, 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
			    struct bio *bio, io_notify_fn fn,
#else
			    struct bio_vec *bvec, io_notify_fn fn, 
#endif
			    void *context, unsigned long bi_flags);

int dm_io_async_bio_wrapper(unsigned int num_regions, struct dm_io_region *where,
	int rw, struct bio *bio, io_notify_fn fn, void *context, unsigned long bi_flags);

void flashcache_detect_fallow(struct cache_c *dmc, int index);
void flashcache_clear_fallow(struct cache_c *dmc, int index);

void flashcache_bio_endio(struct bio *bio, int error, 
			  struct cache_c *dmc, ktime_t io_start_time,
			  struct io_latency *plat);
void flashcache_bio_endio_core(struct bio *bio, int error, struct cache_c *dmc,
			       ktime_t io_start_time, struct io_latency *plat);

/* procfs */
void flashcache_module_procfs_init(void);
void flashcache_module_procfs_releae(void);
void flashcache_ctr_procfs(struct cache_c *dmc);
void flashcache_dtr_procfs(struct cache_c *dmc);

#ifdef MY_ABC_HERE
int quickflush_build_next_bitmap(struct cache_c *dmc);
int quickflush_cacheblk_get_nr_writeback(struct cache_c *dmc, int index);
int quickflush_find_cacheblk_idx(struct cache_c *dmc, unsigned long long disk_blk_idx);
void quickflush_get_next_io_range(bitmap_t data_bitmap, bitmap_t *dirty_bitmap, sector_t *offset, sector_t *size);

void dmcg_nqf_mgmt(struct work_struct *work);
#endif

sector_t bio_last_sector(struct bio *bio);

#ifdef MY_ABC_HERE
struct block_device *get_cache_bdev(struct cache_c *dmc);
struct block_device *get_disk_bdev(struct cache_c *dmc);
fmode_t get_disk_mode(struct cache_c *dmc);
#endif
#ifdef CONFIG_SYNO_DATA_CORRECTION
int correction_handle_abort_io_finish(struct cache_c *dmc,
	struct bio *bio, int error, ktime_t start_time, struct io_latency *plat);
#endif /* CONFIG_SYNO_DATA_CORRECTION */
#ifdef MY_ABC_HERE
void send_disk_flush_bio(struct work_struct *work);
void process_disk_flush_io_queue(struct work_struct *work);
void send_md_flush_bio(struct work_struct *work);
void process_md_flush_io_queue(struct work_struct *work);
#endif
#ifdef MY_ABC_HERE
void handle_master_io_finish(struct cache_c *dmc);
int superblock_cache_state_set(struct cache_c *dmc, cache_state_t state);
#endif /* MY_ABC_HERE */

// use macro here to avoid function call when latency diagnose not enabled
#define flashcache_check_record_io_latency(step, plat) do {\
	if (LAT_DIAGNOSE_ENABLED(plat)) flashcache_record_io_latency(step, plat);\
} while (0)

#define flashcache_check_process_io_latency(dmc, plat, step) do {\
	if (LAT_DIAGNOSE_ENABLED(plat)) flashcache_process_io_latency(dmc, plat, step);\
} while (0)

void flashcache_record_io_latency(enum latency_step step, struct io_latency *plat);
void flashcache_process_io_latency(struct cache_c *dmc, struct io_latency *plat, enum latency_step step);
void flashcache_free_pending_jobs(struct cache_c *dmc, struct cacheblock *cacheblk, int error);
int dm_io_vmem(unsigned int num_regions, struct dm_io_region *where, int rw,
		void *mem_addr, io_notify_fn fn, void *context, unsigned long bi_flags);
int cache_schedule_work(struct work_struct *work);
int cache_schedule_delayed_work(struct delayed_work *dwork, unsigned long delay);
int cache_mod_delayed_work(struct delayed_work *dwork, unsigned long delay);

/* IO with valid bio has preprocess time (from flashcache_map -> flashcache_init_io_latency)
 * The start time of master bio is passed in with start_jiffy
 * for io without valid bio the start_jiffy should just be the jiffies when this is called. */
static inline void
flashcache_init_io_latency(struct io_latency *plat, int enable,
				enum cache_io_type io_type,
				unsigned long start_jiffy,
				unsigned int has_preprocess)
{
	if (enable) {
		memset(plat, 0, sizeof(*plat));
		plat->start_jiffy = start_jiffy;
		plat->last_jiffy = start_jiffy;
		plat->io_type = io_type;
		if (has_preprocess) {
			flashcache_record_io_latency(LAT_PREPROCESS, plat);
		}
	} else {
		plat->entry_cnt = LAT_INVALID_ENTRY_CNT;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
void flashcache_process_lat_stats(struct timer_list *timer);
#else
void flashcache_process_lat_stats(unsigned long data);
#endif

static inline unsigned long
bio_get_start_jiffy(struct bio *bio)
{
#ifdef MY_DEF_HERE
#ifdef MY_ABC_HERE
	return ((struct subbio_info*)bio->bi_private)->pcache_bio->start_jiffy;
#else
	return ((struct cache_bio*)bio->bi_private)->start_jiffy;
#endif
#else /* NOT MY_DEF_HERE */
	return ((struct cache_bi_private*)bio->bi_private)->start_jiffy;
#endif /* MY_DEF_HERE */
}

static inline void
set_bi_op_and_flags(struct dm_io_request *iorq, comp_dm_op_t dm_op)
{

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	iorq->bi_op = dm_op & REQ_OP_MASK;
	iorq->bi_op_flags = dm_op & ~(REQ_OP_MASK);
#else
	iorq->bi_rw = dm_op;
#endif
}

#ifdef MY_ABC_HERE
#else
static inline void dec_clean_inprog(struct cache_c *dmc, struct cache_set *set, int nr_write)
{
	VERIFY_WARN(dmc->clean_inprog >= nr_write);
	dmc->clean_inprog -= nr_write;
	VERIFY_WARN(set->clean_inprog >= nr_write);
	set->clean_inprog -= nr_write;
}
#endif

/*
 * Functins to change cache block state bits and do statistics
 *
 * Must be used inside spin_lock
 *
 * TODO: Integrate nr_dirty & nr_synced updating into following functions
 */
static inline void cb_state_add_bits_update_counts(struct cache_c *dmc,
	struct cacheblock *cb, cb_state_t bits)
{
	if (bits & OCCUPIED_BITS) {
		if ((cb->cache_state & OCCUPIED_BITS) == 0) {
			dmc->nr_occupied++;
		}
	}
	cb->cache_state |= bits;
}

/*
 * WARNING: Except loading cache to initialize cacheblk from metadata,
 * OCCUPIED_BITS should be added by cb_state_add_bits_update_counts()
 * for correct calculation
 */
static inline void cb_state_assign_update_counts(struct cache_c *dmc,
	struct cacheblock *cb, cb_state_t bits)
{
	if (bits == INVALID){
		// sometime driver might not remove some flag and set INVALID directly
		if (cb->cache_state & OCCUPIED_BITS) {
			dmc->nr_occupied--;
		}
	} else if (bits & OCCUPIED_BITS) {
		if ((cb->cache_state & OCCUPIED_BITS) == 0) {
			dmc->nr_occupied++;
		}
	}

	cb->cache_state = bits;
}

static inline void cb_state_remove_bits_update_counts(struct cache_c *dmc,
	struct cacheblock *cb, cb_state_t bits)
{
	if ((bits & OCCUPIED_BITS) && (cb->cache_state & bits)) {
		if ((cb->cache_state & ~bits & OCCUPIED_BITS) == 0) {
			dmc->nr_occupied--;
		}
	}
	cb->cache_state &= ~bits;
}

/* Must be in lock, only part of the attrs set could be unset together. */
static inline void unset_partial_cb_writeback_attrs(struct cache_c *dmc,
	struct cacheblock* cb, int nr_write)
{
	cacheblk_dec_num_concurrent_by(cb, nr_write);
	cacheblk_unset_all_io_range_change_state(dmc, cb);
}

/* Must be in cache_spin_lock and plug_wb->lock */
static inline void set_wb_attrs(struct cache_c *dmc, int cb_idx, int nr_write)
{
	struct cacheblock *cb = &dmc->cache[cb_idx];

	VERIFY_WARN(cb->nr_concurrent == 0);
	cb_state_add_bits_update_counts(dmc, cb, DISKWRITEINPROG);
	cacheblk_set_all_io_range(cb);
	cacheblk_add_num_concurrent_by(cb, nr_write);
	flashcache_clear_fallow(dmc, cb_idx);
	atomic_add(nr_write, &dmc->nr_jobs);
#ifdef MY_ABC_HERE
	dmc->plug_wb.ssd_read_inprog += nr_write;
#else
	dmc->clean_inprog += nr_write;
	dmc->cache_sets[cb_idx / dmc->assoc].clean_inprog += nr_write;
#endif
}

void flashcache_free_cache_job_mem(struct kcached_job *job);

#ifdef MY_ABC_HERE
static inline void schedule_nqf_mgmt_work(void) {
	cache_schedule_delayed_work(&nqf_mgmt_work, NQF_MGMT_ITVL_SEC * HZ);
}
#endif

static inline char* mode_to_str(int mode)
{
	char *str = "Unknown";

	if (mode == FLASHCACHE_WRITE_BACK) {
		str = "WRITE_BACK";
	} else if (mode == FLASHCACHE_WRITE_THROUGH) {
		str = "WRITE_THROUGH";
	} else if (mode == FLASHCACHE_DUMMY) {
		str = "DUMMY";
	} else {
		str = "WRITE_AROUND";
	}

	return str;
}


#ifdef MY_ABC_HERE
#else
void flashcache_sync_blocks(struct cache_c *dmc);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
void flashcache_free_md_bio(struct kcached_job *job);
#else
void flashcache_free_md_sector(struct kcached_job *job);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
int flashcache_alloc_md_bio(struct kcached_job *job);
#else
int flashcache_alloc_md_sector(struct kcached_job *job);
#endif

void flashcache_handle_job_finish(struct cache_c *dmc, struct kcached_job *job);
int is_writeback_crash_safe_set_bypass(struct cache_c *dmc);

#endif /* __KERNEL__ */
#endif /* FLASHCACHE_H */
