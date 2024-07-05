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
#include <linux/vmalloc.h>
#include <linux/semaphore.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#include <linux/sched/mm.h>
#endif

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
#include "syno_md_update.h"
#endif

#ifdef MY_ABC_HERE
#ifdef CONFIG_DETECT_HUNG_TASK
unsigned long sysctl_hung_task_timeout_secs = CONFIG_DEFAULT_HUNG_TASK_TIMEOUT;
#else
unsigned long sysctl_hung_task_timeout_secs = 0;
#endif
#endif

// All the dmcs, which include dummy caches
struct cache_c *cache_list_head = NULL;
u_int64_t size_hist[MAX_HIST_SECTOR + 1];

struct kmem_cache *_job_cache;
mempool_t *_job_pool;
struct kmem_cache *_pending_job_cache;
mempool_t *_pending_job_pool;
#ifdef MY_ABC_HERE
mempool_t *_job_mem_pool;

// Also for metadata bio & flush bio
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
struct bio_set cache_bio_set;
struct bio_set *pcache_bio_set = &cache_bio_set;
#else
struct bio_set *pcache_bio_set;
#endif

int job_max_mem_count = MAX_JOB_MEM_COUNT;
module_param(job_max_mem_count, int, S_IRUGO);

#ifdef MY_DEF_HERE
struct kmem_cache *_cache_bio_kmem_cache;
mempool_t *_cache_bio_pool;
#else
struct kmem_cache *_cache_bi_private_kmem_cache;
mempool_t *_cache_bi_private_pool;
#endif /* MY_DEF_HERE */

// Lock for controlling job mem allocated
DEFINE_SPINLOCK(job_mem_lock); // XXX: mutex should be enough
int job_mem_count;
wait_queue_head_t job_mem_wait_queue;
#endif
#ifdef MY_ABC_HERE
struct workqueue_struct *cache_workqueue;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
// TODO: Rename to distinguish online quickflush and offline quickflush.
//       Maybe normal/sync quickflush? Put offline quickflush to standalone struct
DEFINE_MUTEX(nqf_mem_mtx);
#endif

#ifdef MY_ABC_HERE
// Job pool variable
DEFINE_SPINLOCK(jp_lock);
int jp_is_processing;
int jp_ref_count;
#endif

atomic_t nr_cache_jobs;
atomic_t nr_pending_jobs;

struct kcached_wq kcached_wq[KCACHED_WQ_SIZE];
#ifdef MY_ABC_HERE
struct delayed_work nqf_mgmt_work;
#endif

struct flashcache_control_s {
	unsigned long synch_flags;
};

struct flashcache_control_s *flashcache_control;

/* Bit offsets for wait_on_bit_lock() */
#define FLASHCACHE_UPDATE_LIST		0

static int flashcache_notify_reboot(struct notifier_block *this,
				    unsigned long code, void *x);
static void flashcache_sync_for_remove(struct cache_c *dmc);

static void set_dm_target_attribute(struct dm_target *ti, struct cache_c *dmc, dm_attr_t dm_attr);

static void reset_dmc_fields_for_dummy_mode(struct cache_c *dmc);
static inline void stop_wb_works(struct cache_c *dmc); // forward declaration

#ifdef MY_ABC_HERE
void quickflush_process_wb_ios(struct work_struct *work);
void quickflush_process_wb_done(struct work_struct *wb_done_work);
#endif

extern char *flashcache_sw_version;

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
flashcache_wait_schedule(struct wait_bit_key *unused_wait_bit_key, int unused_mode)
#else
flashcache_wait_schedule(void *unused)
#endif
{
	schedule();
	return 0;
}

#ifdef MY_ABC_HERE
/*
 * Special lock to only lock no-memory-allocated part
 */
static void job_pool_lock(void)
{
	unsigned long flags = 0;
	int lock = 0;

	while (1) {
		spin_lock_irqsave(&jp_lock, flags);
		if (jp_is_processing) {
			lock = 0;
		} else {
			// not in processsing
			lock = 1;
			jp_is_processing = 1;
		}
		spin_unlock_irqrestore(&jp_lock, flags);
		if (lock) {
			break;
		} else {
			msleep(1000);
		}
	}
}

/*
 * MUST check if being locked first
 *
 * lock is the return value of job_pool_lock
 */
static void job_pool_unlock(void)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&jp_lock, flags);
	if (0 == jp_is_processing) {
		serr("Job pool is not processing, no need to unlock?");
	} else {
		jp_is_processing = 0;
	}
	spin_unlock_irqrestore(&jp_lock, flags);
}

#endif

static void * do_vmalloc(unsigned long byte)
{
	void *ptr = NULL;

#ifdef MY_ABC_HERE
	if (TEST_VMALLOC_FAIL == global_tester) {
		serr("Detect TEST_VMALLOC_FAIL. Simulate vmalloc failed");
		global_tester = 0;
		ptr = NULL;
		goto end;
	}
#endif
	ptr = vmalloc(byte);
end:
	return ptr;
}

void *flashcache_mempool_vmalloc(gfp_t gfp_mask, void *pool_data)
{
	void *ret = NULL;
	unsigned int noio_flags = memalloc_noio_save();
	ret = vmalloc_wrapper(JOB_MEM_BYTE, gfp_mask | VMALLOC_INT_FLAG);
	memalloc_noio_restore(noio_flags);

	return ret;
}

void flashcache_mempool_vfree(void *element, void *pool_data)
{
	vfree(element);
}

/*
 * Allocate job pool if needed
 * To release resource, call job_pool_require()
 * 
 * Note: might sleep to wait
 */
static int 
job_pool_require(void)
{
	int alloc_bytes = 0;
	int locked = 0;

	sdbg(DF_JOB_POOL_INIT, "Start");

	job_pool_lock();
	locked = 1;

	if (0 != jp_ref_count) {
		// job pools are already initailized
		goto end;
	}

	if (debug_flags & DF_JOB_POOL_INIT) {
		sprint("Detect DF_JOB_POOL_INIT flag, wait 2 minutes to simulate init slow");
		msleep(1000*120);
		sprint("Sleep finish");
	}

	_job_cache = kmem_cache_create("kcached-jobs-syno",
	                               sizeof(struct kcached_job),
	                               __alignof__(struct kcached_job),
	                               0, NULL);
	if (!_job_cache) {
		goto err;
	}

	_job_pool = mempool_create(MIN_JOBS, mempool_alloc_slab,
	                           mempool_free_slab, _job_cache);
	if (!_job_pool) {
		kmem_cache_destroy(_job_cache);
		goto err;
	}

	alloc_bytes += MIN_JOBS * sizeof(struct kcached_job);

	_pending_job_cache = kmem_cache_create("pending-jobs-syno",
					       sizeof(struct pending_job),
					       __alignof__(struct pending_job),
					       0, NULL);
	if (!_pending_job_cache) {
		mempool_destroy(_job_pool);
		kmem_cache_destroy(_job_cache);
		goto err;
	}

	_pending_job_pool = mempool_create(MIN_JOBS, mempool_alloc_slab,
					   mempool_free_slab, _pending_job_cache);
	if (!_pending_job_pool) {
		kmem_cache_destroy(_pending_job_cache);
		mempool_destroy(_job_pool);
		kmem_cache_destroy(_job_cache);
		goto err;
	}

	alloc_bytes += MIN_JOBS * sizeof(struct pending_job);

	// For preread memory
	_job_mem_pool = mempool_create(MIN_PREREAD_JOBS, flashcache_mempool_vmalloc,
	                           flashcache_mempool_vfree, NULL);
	if (!_job_mem_pool) {
		goto err;
	}

	alloc_bytes += MIN_PREREAD_JOBS * JOB_MEM_BYTE;
#ifdef MY_DEF_HERE
	_cache_bio_kmem_cache = kmem_cache_create("flashcache-cache-bio", sizeof(struct cache_bio), 0, 0, NULL);
	if (!_cache_bio_kmem_cache) {
		goto err;
	}
	_cache_bio_pool = mempool_create(MIN_JOBS, mempool_alloc_slab,
	                           mempool_free_slab, _cache_bio_kmem_cache);
	if (!_cache_bio_pool) {
		goto err;
	}

	alloc_bytes += MIN_JOBS * sizeof(struct cache_bio);
#else
	_cache_bi_private_kmem_cache = kmem_cache_create("flashcache-cache-bi-private",
			sizeof(struct cache_bi_private), 0, 0, NULL);
	if (!_cache_bi_private_kmem_cache) {
		goto err;
	}
	_cache_bi_private_pool = mempool_create(MIN_JOBS, mempool_alloc_slab,
	                           mempool_free_slab, _cache_bi_private_kmem_cache);
	if (!_cache_bi_private_pool) {
		goto err;
	}

	alloc_bytes += MIN_JOBS * sizeof(struct cache_bi_private);
#endif /* MY_DEF_HERE */

	sprint("Preread max memmory count=%d", job_max_mem_count);
	sprint("Total pre-allocate bytes=%d", alloc_bytes);

end:
	jp_ref_count++;

	if (locked) {
		job_pool_unlock();
	}
	sdbg(DF_JOB_POOL_INIT, "Finish");
	return 0;
err:
	if (_job_cache) {
		kmem_cache_destroy(_job_cache);
	}

	if (_job_pool) {
		mempool_destroy(_job_pool);
	}

	if (_pending_job_cache) {
		kmem_cache_destroy(_pending_job_cache);
	}

	if (_pending_job_pool) {
		mempool_destroy(_pending_job_pool);
	}

	if (_job_mem_pool) {
		mempool_destroy(_job_mem_pool);
	}
#ifdef MY_DEF_HERE
	if (_cache_bio_kmem_cache) {
		kmem_cache_destroy(_cache_bio_kmem_cache);
	}
	if (_cache_bio_pool) {
		mempool_destroy(_cache_bio_pool);
	}
#else
	if (_cache_bi_private_kmem_cache) {
		kmem_cache_destroy(_cache_bi_private_kmem_cache);
	}
	if (_cache_bi_private_pool) {
		mempool_destroy(_cache_bi_private_pool);
	}
#endif /* MY_DEF_HERE */
	if (locked) {
		job_pool_unlock();
	}
	return -ENOMEM;
}

static void check_kcached_wq_empty(void)
{
	VERIFY_WARN(flashcache_pending_empty());
	VERIFY_WARN(flashcache_io_empty());

#ifdef MY_ABC_HERE
#else
	VERIFY_WARN(flashcache_md_io_empty());
	VERIFY_WARN(flashcache_md_complete_empty());
#endif

	VERIFY_WARN(flashcache_uncached_io_complete_empty());
	VERIFY_WARN(flashcache_preread_empty());
	VERIFY_WARN(flashcache_free_job_empty());
}

#ifdef MY_ABC_HERE
/*
 * MUST call job_pool_require first
 */
static void 
job_pool_release(void)
#else
static void 
flashcache_jobs_exit(void)
#endif /* MY_ABC_HERE */
{
	int locked = 0;

	job_pool_lock();
	locked = 1;
	jp_ref_count--;

	if (0 != jp_ref_count) {
		sprint("Ref count=%d, no need to destroy jobs", jp_ref_count);
		goto end;
	}

	sprint("Ref count=0, Start to destroy jobs");
	check_kcached_wq_empty();

	mempool_destroy(_job_pool);
	kmem_cache_destroy(_job_cache);
	_job_pool = NULL;
	_job_cache = NULL;
	mempool_destroy(_pending_job_pool);
	kmem_cache_destroy(_pending_job_cache);
	_pending_job_pool = NULL;
	_pending_job_cache = NULL;
	mempool_destroy(_job_mem_pool);
	_job_mem_pool = NULL;
#ifdef MY_DEF_HERE
	mempool_destroy(_cache_bio_pool);
	_cache_bio_pool = NULL;
	kmem_cache_destroy(_cache_bio_kmem_cache);
	_cache_bio_kmem_cache = NULL;
#else
	mempool_destroy(_cache_bi_private_pool);
	_cache_bi_private_pool = NULL;
	kmem_cache_destroy(_cache_bi_private_kmem_cache);
	_cache_bi_private_kmem_cache = NULL;
#endif /* MY_DEF_HERE */

end:
	if (locked) {
		job_pool_unlock();
	}
}

static int 
flashcache_kcached_init(struct cache_c *dmc)
{
	init_waitqueue_head(&dmc->sync_wqh);
	atomic_set(&dmc->nr_jobs, 0);
	atomic_set(&dmc->remove_in_prog, 0);
	return 0;
}

#ifdef MY_ABC_HERE
int
superblock_cache_state_set(struct cache_c *dmc, cache_state_t state)
{
	struct flash_superblock *header = NULL;
	int ret = 1;
	struct dm_io_region where = {0};

	if (!dmc || (CACHE_UNKNOWN == state)) {
		serr("Incorrect parameter");
		return -1;
	}

	header = (struct flash_superblock *)vmalloc(MD_BLOCK_BYTES(dmc));
	if (!header) {
		serr("Allocate memory failed(size=%dbytes)", MD_BLOCK_BYTES(dmc));
		goto err;
	}

	memset(header, 0, MD_BLOCK_BYTES(dmc));

	where.bdev = get_cache_bdev(dmc);
	where.sector = 0;
	where.count = dmc->md_block_size;

	ret = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_READ, header);
	if (ret) {
		serr("Can't read header");
		goto err;
	}

	// Change state
	header->cache_state = state;

	ret = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, header);
	if (ret) {
		serr("Can't write header");
		goto err;
	}

	ret = 0;
err:
	if (header) {
		vfree((void *)header);
	}

	return ret;
}
#endif /* MY_ABC_HERE */

/*
 * Write out the metadata one sector at a time.
 * Then dump out the superblock.
 * SYNO: Only call in dtr() or reboot
 */
static int 
flashcache_writeback_md_store(struct cache_c *dmc)
{
	struct flash_cacheblock *meta_data_cacheblock, *next_ptr;
	struct flash_superblock *header;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	struct io_region where;
#else
	struct dm_io_region where;
#endif
	int i, j;
	int num_valid = 0, num_dirty = 0;
	int error;
	int write_errors = 0;
	int sectors_written = 0, sectors_expected = 0; /* debug */
	int slots_written = 0; /* How many cache slots did we fill in this MD io block ? */

	meta_data_cacheblock = (struct flash_cacheblock *)vmalloc(METADATA_IO_BLOCKSIZE);
	if (!meta_data_cacheblock) {
		DMERR("flashcache_writeback_md_store: Unable to allocate memory");
		DMERR("flashcache_writeback_md_store: Could not write out cache metadata !");
		return 1;
	}	

	where.bdev = get_cache_bdev(dmc);
	where.sector = MD_SECTORS_PER_BLOCK(dmc);
	slots_written = 0;
	next_ptr = meta_data_cacheblock;
	j = MD_SLOTS_PER_BLOCK(dmc);
	for (i = 0 ; i < dmc->size ; i++) {
		if (dmc->cache[i].cache_state & VALID)
			num_valid++;
		if (dmc->cache[i].cache_state & DIRTY)
			num_dirty++;
		next_ptr->dbn = dmc->cache[i].dbn;
#ifdef MY_ABC_HERE
		next_ptr->data_bitmap = dmc->cache[i].data_bitmap;
		next_ptr->dirty_bitmap = dmc->cache[i].dirty_bitmap;
#endif
#ifdef FLASHCACHE_DO_CHECKSUMS
		next_ptr->checksum = dmc->cache[i].checksum;
#endif
#ifdef MY_ABC_HERE
		next_ptr->cache_state = dmc->cache[i].cache_state &
			(INVALID | VALID | DIRTY | PIN_FILE);
#else
		next_ptr->cache_state = dmc->cache[i].cache_state & 
			(INVALID | VALID | DIRTY);
#endif
		next_ptr++;
		slots_written++;
		j--;
		if (j == 0) {
			/* 
			 * Filled the block, write and goto the next metadata block.
			 */
			if (slots_written == MD_SLOTS_PER_BLOCK(dmc) * METADATA_IO_NUM_BLOCKS(dmc)) {
				/*
				 * Wrote out an entire metadata IO block, write the block to the ssd.
				 */
				where.count = (slots_written / MD_SLOTS_PER_BLOCK(dmc)) * 
					MD_SECTORS_PER_BLOCK(dmc);
				slots_written = 0;
				sectors_written += where.count;	/* debug */
				error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, meta_data_cacheblock);
				if (error) {
					write_errors++;
#ifdef MY_ABC_HERE
					// printk_ratelimit start
					if (printk_ratelimit()) {
#endif
					DMERR("flashcache_writeback_md_store: Could not write out cache metadata block %llu error %d !",
					      (u64)where.sector, error);
#ifdef MY_ABC_HERE
					}
					// printk_ratelimit end
#endif
				}
				where.sector += where.count;	/* Advance offset */
			}
			/* Move next slot pointer into next block */
			next_ptr = (struct flash_cacheblock *)
				((caddr_t)meta_data_cacheblock + ((slots_written / MD_SLOTS_PER_BLOCK(dmc)) * MD_BLOCK_BYTES(dmc)));
			j = MD_SLOTS_PER_BLOCK(dmc);
		}
	} // for i in dmc->size

	if (next_ptr != meta_data_cacheblock) {
		/* Write the remaining last blocks out */
		VERIFY_WARN(slots_written > 0);
		where.count = (slots_written / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
		if (slots_written % MD_SLOTS_PER_BLOCK(dmc))
			where.count += MD_SECTORS_PER_BLOCK(dmc);
		sectors_written += where.count;
		error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, meta_data_cacheblock);
		if (error) {
			write_errors++;
			if (printk_ratelimit()) {
				DMERR("flashcache_writeback_md_store: Could not write out cache metadata block %llu error %d !",
				  (u64)where.sector, error);
			}
		}
	}
	vfree((void *)meta_data_cacheblock);

	/* Debug Tests */
#ifdef EC_EMULATE_U64_DIVISION
	sectors_expected = div64_u64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (mod_u64_rem64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)))
#else
	sectors_expected = (dmc->size / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (dmc->size % MD_SLOTS_PER_BLOCK(dmc))
#endif
		sectors_expected += MD_SECTORS_PER_BLOCK(dmc);
	if (sectors_expected != sectors_written) {
		printk("flashcache_writeback_md_store" "Sector Mismatch ! sectors_expected=%d, sectors_written=%d\n",
		       sectors_expected, sectors_written);
		sprint("flashcache_writeback_md_store: sector mismatch\n");
		return 1;
	}

	header = (struct flash_superblock *)vmalloc(MD_BLOCK_BYTES(dmc));
	if (!header) {
		DMERR("flashcache_writeback_md_store: Unable to allocate memory");
		DMERR("flashcache_writeback_md_store: Could not write out cache metadata !");
		return 1;
	}	
	memset(header, 0, MD_BLOCK_BYTES(dmc));
	
	/* Write the header out last */
	if (write_errors == 0) {
		if (num_dirty == 0)
			header->cache_sb_state = CACHE_MD_STATE_CLEAN;
		else
			header->cache_sb_state = CACHE_MD_STATE_FASTCLEAN;			
	} else
		header->cache_sb_state = CACHE_MD_STATE_UNSTABLE;
	header->block_size = dmc->block_size;
	header->md_block_size = dmc->md_block_size;
	header->size = dmc->size;
	header->assoc = dmc->assoc;
	snprintf(header->disk_devname, DEV_PATHLEN, "%s", dmc->disk_devname);
	snprintf(header->cache_devname, DEV_PATHLEN, "%s", dmc->dm_vdevname);
	header->cache_devsize = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);
	header->disk_devsize = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);
	header->cache_version = dmc->on_ssd_version;
#ifdef MY_ABC_HERE
	header->cache_state = dmc->cache_state;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (header->cache_version >= SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2) {
		snprintf(header->group_uuid, sizeof(header->group_uuid), "%s", dmc->group_uuid);
	}
#endif

	DPRINTK("Store metadata to disk: block size(%u), md block size(%u), cache size(%llu)" \
	        "associativity(%u)",
	        header->block_size, header->md_block_size, header->size,
	        header->assoc);

	where.sector = 0;
	where.count = dmc->md_block_size;
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, header);
	if (error) {
		write_errors++;
		DMERR("flashcache_writeback_md_store: Could not write out cache metadata superblock %llu error %d !",
		      (u64)where.sector, error);
	}

	vfree((void *)header);

	if (write_errors == 0)
		DMINFO("Cache metadata saved to disk");
	else {
		DMINFO("CRITICAL : There were %d errors in saving cache metadata saved to disk", 
		       write_errors);
		if (num_dirty)
			DMINFO("CRITICAL : You have likely lost %d dirty blocks", num_dirty);
	}

	DMINFO("flashcache_writeback_md_store: valid blocks = %d dirty blocks = %d md_sectors = %d\n", 
	       num_valid, num_dirty, dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc));

	return 0;
}

#ifdef MY_ABC_HERE
static void handle_tail_region(struct cache_c *dmc)
{
	u64 num_cb = dmc->size;
	u64 tail_size = compatible_mod(num_cb, HASH_REGION_BLOCK_NUM);

	dmc->region_num_sets = compatible_div(HASH_REGION_BLOCK_NUM, dmc->assoc);

	if (!tail_size) {
		dmc->tail_region_idx = compatible_div(num_cb, HASH_REGION_BLOCK_NUM) - 1;
		dmc->tail_region_num_sets = dmc->region_num_sets;
	} else if (tail_size >= HASH_REGION_BLOCK_NUM / 2) {
		/* tail is its own region */
		dmc->tail_region_idx = compatible_div(num_cb, HASH_REGION_BLOCK_NUM);
		dmc->tail_region_num_sets = compatible_div(tail_size, dmc->assoc);
	} else {
		if (num_cb < HASH_REGION_BLOCK_NUM) {
			/* only one region in the cache */
			dmc->tail_region_idx = 0;
			dmc->tail_region_num_sets = compatible_div(num_cb, dmc->assoc);
		} else {
			/* tail combine with last region */
			dmc->tail_region_idx = compatible_div(num_cb, HASH_REGION_BLOCK_NUM) - 1;
			dmc->tail_region_num_sets =
				compatible_div(tail_size + HASH_REGION_BLOCK_NUM, dmc->assoc);
		}
	}

	sprint("dmc size: %llu, tail region idx: %d num sets: %d",
		(u64)dmc->size, dmc->tail_region_idx, dmc->tail_region_num_sets);
}
#endif

static int 
flashcache_writethrough_create(struct cache_c *dmc, int cache_version)
{
	sector_t cache_size, dev_size;
	sector_t order;
	int i;
	
	/* 
	 * Convert size (in sectors) to blocks.
	 * Then round size (in blocks now) down to a multiple of associativity 
	 */
#ifdef EC_EMULATE_U64_DIVISION
	dmc->size = div64_u64(dmc->size, dmc->block_size);
	dmc->size = div64_u64(dmc->size, dmc->assoc) * dmc->assoc;
#else
	dmc->size /= dmc->block_size;
	dmc->size = (dmc->size / dmc->assoc) * dmc->assoc;
#endif

#ifdef MY_ABC_HERE
	handle_tail_region(dmc);
#endif

	/* Check cache size against device size */
	dev_size = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);
	cache_size = dmc->size * dmc->block_size;
	if (cache_size > dev_size) {
		DMERR("Requested cache size exeeds the cache device's capacity" \
		      "(%llu>%llu)",
		      (u64)cache_size, (u64)dev_size);
		return 1;
	}
	order = dmc->size * sizeof(struct cacheblock);

	DMINFO("Allocate %lluKB (%zuB per) mem for %llu-entry cache" \
	       "(capacity:%lluMB, associativity:%u, block size:%u " \
	       "sectors(%uKB))",
	       (u64)(order >> 10), sizeof(struct cacheblock), (u64)dmc->size,
	       (u64)(cache_size >> (20-SECTOR_SHIFT)), dmc->assoc, dmc->block_size,
	       dmc->block_size >> (10-SECTOR_SHIFT));

	dmc->cache = (struct cacheblock *)do_vmalloc(order);
	if (!dmc->cache) {
		DMERR("flashcache_writethrough_create: Unable to allocate cache md");
		return 1;
	}

#ifdef MY_ABC_HERE
	// should be set in both WB and WT mode
	memset(dmc->cache, 0, order);
	dmc->disk_devsize = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);
#endif

	dmc->on_ssd_version = cache_version;

	/* Initialize the cache structs */
	for (i = 0; i < dmc->size ; i++) {
		dmc->cache[i].dbn = 0;
#ifdef FLASHCACHE_DO_CHECKSUMS
		dmc->cache[i].checksum = 0;
#endif
		cb_state_assign_update_counts(dmc, &dmc->cache[i], INVALID);
		dmc->cache[i].nr_queued = 0;
	}
	dmc->md_blocks = 0;
	return 0;
}

static int 
flashcache_writeback_create(struct cache_c *dmc, int cache_version, int force)
{
	struct flash_cacheblock *meta_data_cacheblock, *next_ptr;
	struct flash_superblock *header;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	struct io_region where;
#else
	struct dm_io_region where;
#endif
	int i, j, error;
	sector_t cache_size, dev_size;
	sector_t order;
	int sectors_written = 0, sectors_expected = 0; /* debug */
	int slots_written = 0; /* How many cache slots did we fill in this MD io block ? */
	
	header = (struct flash_superblock *)vmalloc(MD_BLOCK_BYTES(dmc));
	if (!header) {
		DMERR("flashcache_writeback_create: Unable to allocate sector");
		return 1;
	}
	where.bdev = get_cache_bdev(dmc);
	where.sector = 0;
	where.count = dmc->md_block_size;
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_READ, header);
	if (error) {
		vfree((void *)header);
		DMERR("flashcache_writeback_create: Could not read cache superblock %llu error %d !",
		      (u64)where.sector, error);
		return 1;
	}
	if (!force &&
	    ((header->cache_sb_state == CACHE_MD_STATE_DIRTY) ||
	     (header->cache_sb_state == CACHE_MD_STATE_CLEAN) ||
	     (header->cache_sb_state == CACHE_MD_STATE_FASTCLEAN))) {
		vfree((void *)header);
		DMERR("flashcache_writeback_create: Existing Cache Detected, use force to re-create");
		return 1;
	}
	/* Compute the size of the metadata, including header. 
	   Note dmc->size is in raw sectors */
	dmc->md_blocks = INDEX_TO_MD_BLOCK(dmc, compatible_div(dmc->size, dmc->block_size)) + 1 + 1;
	dmc->size -= dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc);	/* total sectors available for cache */
	dmc->size = compatible_div(dmc->size, dmc->block_size);
	dmc->size = compatible_div(dmc->size, dmc->assoc) * dmc->assoc;
	/* Recompute since dmc->size was possibly trunc'ed down */
	dmc->md_blocks = INDEX_TO_MD_BLOCK(dmc, dmc->size) + 1 + 1;

#ifdef MY_ABC_HERE
	handle_tail_region(dmc);
#endif

	DMINFO("flashcache_writeback_create: md_blocks = %d, md_sectors = %d\n", 
	       dmc->md_blocks, dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc));
	dev_size = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);
	cache_size = dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc) + (dmc->size * dmc->block_size);
	if (cache_size > dev_size) {
		DMERR("Requested cache size exceeds the cache device's capacity" \
		      "(%llu>%llu)",
  		      (u64)cache_size, (u64)dev_size);
		vfree((void *)header);
		return 1;
	}
	order = dmc->size * sizeof(struct cacheblock);
	DMINFO("Allocate %lluKB (%zdB per) mem for %llu-entry cache" \
	       "(capacity:%lluMB, associativity:%u, block size:%u " \
	       "sectors(%uKB))",
	       (u64)order >> 10, sizeof(struct cacheblock), (u64)dmc->size,
	       (u64)cache_size >> (20-SECTOR_SHIFT), dmc->assoc, dmc->block_size,
	       dmc->block_size >> (10-SECTOR_SHIFT));
	dmc->cache = (struct cacheblock *)do_vmalloc(order);
	if (!dmc->cache) {
		vfree((void *)header);
		DMERR("flashcache_writeback_create: Unable to allocate cache md");
		return 1;
	}

#ifdef MY_ABC_HERE
	memset(dmc->cache, 0, order);
#endif

	/* Initialize the cache structs */
	for (i = 0; i < dmc->size ; i++) {
		dmc->cache[i].dbn = 0;
#ifdef FLASHCACHE_DO_CHECKSUMS
		dmc->cache[i].checksum = 0;
#endif
		cb_state_assign_update_counts(dmc, &dmc->cache[i], INVALID);
		dmc->cache[i].nr_queued = 0;
	}
	meta_data_cacheblock = (struct flash_cacheblock *)vmalloc(METADATA_IO_BLOCKSIZE);
	if (!meta_data_cacheblock) {
		DMERR("flashcache_writeback_create: Unable to allocate memory");
		DMERR("flashcache_writeback_create: Could not write out cache metadata !");
		return 1;
	}	

	where.sector = MD_SECTORS_PER_BLOCK(dmc);
	slots_written = 0;
	next_ptr = meta_data_cacheblock;
	j = MD_SLOTS_PER_BLOCK(dmc);
	for (i = 0 ; i < dmc->size ; i++) {
		next_ptr->dbn = dmc->cache[i].dbn;
#ifdef FLASHCACHE_DO_CHECKSUMS
		next_ptr->checksum = dmc->cache[i].checksum;
#endif
		next_ptr->data_bitmap = dmc->cache[i].data_bitmap;
		next_ptr->dirty_bitmap = dmc->cache[i].dirty_bitmap;
#ifdef MY_ABC_HERE
		next_ptr->cache_state = dmc->cache[i].cache_state &
			(INVALID | VALID | DIRTY | PIN_FILE);
#else
		next_ptr->cache_state = dmc->cache[i].cache_state & 
			(INVALID | VALID | DIRTY);
#endif
		next_ptr++;
		slots_written++;
		j--;
		if (j == 0) {
			/* 
			 * Filled the block, write and goto the next metadata block.
			 */
			if (slots_written == MD_SLOTS_PER_BLOCK(dmc) * METADATA_IO_NUM_BLOCKS(dmc)) {
				/*
				 * Wrote out an entire metadata IO block, write the block to the ssd.
				 */
				where.count = (slots_written / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
				slots_written = 0;
				sectors_written += where.count;	/* debug */
				error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, 
								 meta_data_cacheblock);
				if (error) {
					vfree((void *)header);
					vfree((void *)meta_data_cacheblock);
					vfree(dmc->cache);
					DMERR("flashcache_writeback_create: Could not write cache metadata block %llu error %d !",
					      (u64)where.sector, error);
					return 1;
				}
				where.sector += where.count;	/* Advance offset */
			}
			/* Move next slot pointer into next metadata block */
			next_ptr = (struct flash_cacheblock *)
				((caddr_t)meta_data_cacheblock + ((slots_written / MD_SLOTS_PER_BLOCK(dmc)) * MD_BLOCK_BYTES(dmc)));
			j = MD_SLOTS_PER_BLOCK(dmc);
		}
	}
	if (next_ptr != meta_data_cacheblock) {
		/* Write the remaining last blocks out */
		VERIFY_WARN(slots_written > 0);
		where.count = (slots_written / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
		if (slots_written % MD_SLOTS_PER_BLOCK(dmc))
			where.count += MD_SECTORS_PER_BLOCK(dmc);
		sectors_written += where.count;
		error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, meta_data_cacheblock);
		if (error) {
			vfree((void *)header);
			vfree((void *)meta_data_cacheblock);
			vfree(dmc->cache);
			DMERR("flashcache_writeback_create: Could not write cache metadata block %llu error %d !",
			      (u64)where.sector, error);
			return 1;		
		}
	}
	/* Debug Tests */
#ifdef EC_EMULATE_U64_DIVISION
	sectors_expected = div64_u64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (mod_u64_rem64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)))
#else
	sectors_expected = (dmc->size / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (dmc->size % MD_SLOTS_PER_BLOCK(dmc))
#endif
		sectors_expected += MD_SECTORS_PER_BLOCK(dmc);
	if (sectors_expected != sectors_written) {
		printk("flashcache_writeback_create" "Sector Mismatch ! sectors_expected=%d, sectors_written=%d\n",
		       sectors_expected, sectors_written);
#ifdef MY_ABC_HERE
		sprint("flashcache_writeback_create: sector mismatch\n");
		vfree((void *)header);
		vfree((void *)meta_data_cacheblock);
		vfree(dmc->cache);
		return 1;
#else
		panic("flashcache_writeback_create: sector mismatch\n");
#endif
	}
	vfree((void *)meta_data_cacheblock);
	/* Write the header */
	header->cache_sb_state = CACHE_MD_STATE_DIRTY;
	header->block_size = dmc->block_size;
	header->md_block_size = dmc->md_block_size;
	header->size = dmc->size;
	header->assoc = dmc->assoc;
	snprintf(header->disk_devname, DEV_PATHLEN, "%s", dmc->disk_devname);
	snprintf(header->cache_devname, DEV_PATHLEN, "%s", dmc->dm_vdevname);
	header->cache_devsize = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);
	header->disk_devsize = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);
	dmc->disk_devsize = header->disk_devsize;
	dmc->on_ssd_version = header->cache_version = cache_version;
#ifdef MY_ABC_HERE
	// Write to superblock
	header->cache_state = CACHE_ENABLED;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	snprintf(header->group_uuid, sizeof(header->group_uuid), "%s", dmc->group_uuid);
#endif

	where.sector = 0;
	where.count = dmc->md_block_size;
	
	printk("flashcache-dbg: cachedev check - %s %s", header->cache_devname,
				dmc->dm_vdevname);
	
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, header);
	if (error) {
		vfree((void *)header);
		vfree(dmc->cache);
		DMERR("flashcache_writeback_create: Could not write cache superblock %llu error %d !",
		      (u64)where.sector, error);
		return 1;		
	}
	vfree((void *)header);

	return 0;
}
#ifdef MY_ABC_HERE

typedef enum bad_states_action {
	BAD_STATES_ACCUME_STAT = 0,
	BAD_STATES_RESCUE,
} bad_states_action_t;

void
set_block_invalid(struct cache_c *dmc, struct cacheblock *pblock)
{
	cb_state_assign_update_counts(dmc, pblock, INVALID);
	pblock->data_bitmap = 0;
	pblock->dirty_bitmap = 0;
	pblock->dbn = 0;
#ifdef FLASHCACHE_DO_CHECKSUMS
	pblock->checksum = 0;
#endif
}

void
handle_bad_states(struct cache_c *dmc, struct cacheblock *pblock,
	bad_states_action_t action, bad_states_t *pbad_stat,
	sector_t disk_dev_sectors)
{
	if (!pblock || ((BAD_STATES_ACCUME_STAT == action) && !pbad_stat) || !disk_dev_sectors) {
		if (printk_ratelimit()) {
			serr("Bad parameter");
		}
		return;
	}
	// Check dbn & size
	if ((pblock->dbn + MAX_BIO_SIZE) > disk_dev_sectors) {
		if (BAD_STATES_RESCUE == action) {
			set_block_invalid(dmc, pblock);
		} else {
			pbad_stat->bad_dbn_size++;
		}
		goto end;
	}

	// Check INVALID
	if ((pblock->cache_state & INVALID) == INVALID) {
		/*
		 * Unable to check data_bitmap or dbn since flashcache_inval_block_set() doesn't clear them
		 *
		 * Unable to check dirty_bitmap since old flashcache_writeback_create() doesn't initialize it
		 * to zero, which seems not to cause issues because it just marks some subblocks that is not used
		 * by file system yet to dirty
		 */
#ifdef MY_ABC_HERE
		if (pblock->cache_state & (VALID | DIRTY | PIN_FILE)) {
#else
		if (pblock->cache_state & (VALID | DIRTY)) {
#endif
			if (BAD_STATES_RESCUE == action) {
				set_block_invalid(dmc, pblock);
			} else {
				pbad_stat->invalid++;
			}
		}
		goto end;
	}

	// Check VALID & not DIRTY
	if ((pblock->cache_state & (DIRTY | VALID)) == VALID) {
		/*
		 * Don't check dirty bitmap since current dirty writeback logic still write old dirty_bitmap
		 * in md_write_kickoff()
		 */
		if (pblock->data_bitmap == 0) {
			if (BAD_STATES_RESCUE == action) {
				set_block_invalid(dmc, pblock);
			} else {
				pbad_stat->valid++;
			}
			goto end;
		}
	}

	// Check DIRTY
	if ((pblock->cache_state & DIRTY) == DIRTY) {
		if ((pblock->data_bitmap == 0) || (pblock->dirty_bitmap == 0) ||
			((pblock->cache_state & VALID) != VALID)) {
			if (BAD_STATES_RESCUE == action) {
				set_block_invalid(dmc, pblock);
			} else {
				pbad_stat->dirty++;
			}
			goto end;
		}
	}

	// Check dirty_bitmap is a subset of data bitmap
	if ((pblock->dirty_bitmap & pblock->data_bitmap) != pblock->dirty_bitmap) {
			if (BAD_STATES_RESCUE == action) {
				set_block_invalid(dmc, pblock);
			} else {
				pbad_stat->bitmap++;
			}
			goto end;
	}

	/*
	 * If VALID & INVALID & DIRTY & PIN state is OK, go here to check extra
	 * flags
	 */

	// Should not happen unless there's memory issue
	if ((pblock->cache_state & BLOCK_IO_INPROG)) {
		if (BAD_STATES_RESCUE == action) {
			cb_state_remove_bits_update_counts(dmc, pblock, BLOCK_IO_INPROG);
		} else {
			pbad_stat->in_prog++;
		}
	}

	// Should not happen unless there's memory issue
	if ((pblock->cache_state & FALLOW_DOCLEAN)) {
		if (BAD_STATES_RESCUE == action) {
			cb_state_remove_bits_update_counts(dmc, pblock, FALLOW_DOCLEAN);
		} else {
			pbad_stat->fallow++;
		}
	}
end:
	return;
}

#endif

#ifdef MY_ABC_HERE

static void free_online_quickflush(struct cache_c *dmc)
{
	if (dmc->oqf.inited) {
		cancel_delayed_work_sync(&dmc->oqf.wb_work);
	}

	if (dmc->oqf.volume_bitmap) {
		vfree(dmc->oqf.volume_bitmap);
		dmc->oqf.volume_bitmap = NULL;
	}
}

static int alloc_online_quickflush(struct cache_c *dmc)
{
	int ret = -ENOMEM;

	dmc->oqf.bitmap_num_idx = OQF_FALLOW_MAP_NUM_IDX(dmc);
	dmc->oqf.volume_bitmap = (int *) vzalloc(sizeof(int) * dmc->oqf.bitmap_num_idx);

	sdbg(DF_QUICKFLUSH, "Fallow bitmap size: %lu", dmc->oqf.bitmap_num_idx);

	if (TEST_OQF_BITMAP_ALLOC_FAIL == global_tester) {
		sprint("Simulate allocate oqf bitmap failed");
		global_tester = 0;
		vfree(dmc->oqf.volume_bitmap);
		dmc->oqf.volume_bitmap = NULL;
	}

	if (NULL == dmc->oqf.volume_bitmap) {
		serr("failed to allocate fallow bitmap");
		goto err;
	}


	ret = 0;
err:
	if (ret) {
		free_online_quickflush(dmc);
	}

	return ret;
}

static void init_online_quickflush(online_qf_t *oqf)
{
	unsigned long i = 0;

	atomic_set(&oqf->nr_fallow, 0);
	mutex_init(&oqf->mtx);
	oqf->nqf_inprog = 0;
	oqf->bit_idx = 0;
	oqf->last_fallow_tstamp = jiffies;
	oqf->sysctl_enable_wb_work = 1;
	oqf->sync_state = SYNC_STATE_STOPPED;
	init_waitqueue_head(&oqf->wqh);
	INIT_DELAYED_WORK(&oqf->wb_work, quickflush_do_writeback);

	sema_init(&oqf->sqf_sem2, 2);
	sema_init(&oqf->sqf_sem1, 1);
	init_rwsem(&oqf->bitmap_rwsem);

	for (i = 0; i < oqf->bitmap_num_idx; ++i) {
		oqf->volume_bitmap[i] = 0;
	}

	oqf->inited = 1;
}

static void init_plug_wb(plug_wb_t *plug_wb)
{
	INIT_LIST_HEAD(&plug_wb->io_queue);
	INIT_LIST_HEAD(&plug_wb->sync_io_queue);
	INIT_LIST_HEAD(&plug_wb->complete_queue);
	INIT_LIST_HEAD(&plug_wb->wb_done_queue);

	plug_wb->seq = 0;
	plug_wb->cur_batch_head = 0;
	plug_wb->cur_batch_len = 0;
	plug_wb->unfinished_read_in_batch = 0;

	plug_wb->ssd_read_inprog = 0;
	plug_wb->disk_write_inprog = 0;

	plug_wb->sysctl_batch_size = PLUG_WB_DEFAULT_BATCH_SIZE;
	plug_wb->sysctl_ssd_read_ios_limit = PLUG_WB_DEFAULT_SSD_READ;
	plug_wb->sysctl_disk_write_ios_limit = PLUG_WB_DEFAULT_DISK_WRITE;

	plug_wb->batch_size = plug_wb->sysctl_batch_size;
	plug_wb->ssd_read_ios_limit = plug_wb->sysctl_ssd_read_ios_limit;
	plug_wb->disk_write_ios_limit = plug_wb->sysctl_disk_write_ios_limit;

	INIT_WORK(&plug_wb->work, quickflush_process_wb_ios);
	INIT_WORK(&plug_wb->wb_done_work, quickflush_process_wb_done);

	spin_lock_init(&plug_wb->lock);

}

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static void free_new_md_update(new_mu_t *mu)
{
	int i = 0;

	if (mu->inited) {
		cancel_delayed_work_sync(&mu->dwork);
		cancel_work_sync(&mu->md_io_complete_work);
		cancel_work_sync(&mu->disk_flush.complete_work);
		cancel_work_sync(&mu->ssd_flush.complete_work);
	}

	for (i = 0; i < NUM_MU_BLK_TYPE; ++i) {
		if (mu->mu_blocks[i]) {
			vfree(mu->mu_blocks[i]);
			mu->mu_blocks[i] = NULL;
		}
	}

	if (mu->disk_flush.bio) {
		bio_put(mu->disk_flush.bio);
		mu->disk_flush.bio = NULL;
	}

	if (mu->ssd_flush.bio) {
		bio_put(mu->ssd_flush.bio);
		mu->ssd_flush.bio = NULL;
	}
}

static inline mu_blk_t* alloc_md_blocks(struct cache_c *dmc)
{
	if (check_new_error_inject(dmc, TEST_NEW_MU_ALLOC_FAIL)) {
		sprint("Simulate mu block alloc fail");
		dmc->sysctl_new_error_inject = 0;
		return NULL;
	}

	return vzalloc(sizeof(mu_blk_t) * dmc->md_blocks);
}

static int alloc_new_md_update(struct cache_c *dmc)
{
	int r = -ENOMEM;
	int i = 0;
	new_mu_t *mu = &dmc->new_mu;

	for (i = 0; i < NUM_MU_BLK_TYPE; ++i) {
		mu->mu_blocks[i] = alloc_md_blocks(dmc);
		if (NULL == mu->mu_blocks[i]) {
			serr("Failed to allocate metadata update blocks");
			goto err;
		}
	}

	mu->disk_flush.bio = bio_alloc_bioset(GFP_NOIO, 0, pcache_bio_set);
	mu->ssd_flush.bio = bio_alloc_bioset(GFP_NOIO, 0, pcache_bio_set);
	if (NULL == mu->ssd_flush.bio || NULL == mu->disk_flush.bio) {
		serr("Failed to allocate ssd flush bio for metadata update");
		goto err;
	}

	r = 0;

err:
	if (r) {
		free_new_md_update(mu);
	}

	return r;
}

static void init_ext_flush(ext_flush_t *ext_flush)
{
	INIT_LIST_HEAD(&ext_flush->md_io_inprog_queue);
	INIT_LIST_HEAD(&ext_flush->md_io_pending_queue);
	INIT_LIST_HEAD(&ext_flush->flush_bios_handling);
	INIT_LIST_HEAD(&ext_flush->flush_bios_pending);

	INIT_WORK(&ext_flush->send_io_work, syno_mu_send_md_ios_from_ext_flush);
}

static void init_dev_flush(dev_flush_t *flush, void (*func)(struct work_struct *))
{
	INIT_LIST_HEAD(&flush->pending_queue);
	INIT_LIST_HEAD(&flush->inprog_queue);

	INIT_WORK(&flush->complete_work, func);
	flush->last_flush_jif = jiffies;

	*bio_bi_sector_ptr(flush->bio) = 0;
	*bio_bi_size_ptr(flush->bio) = 0;
}

static void init_new_md_update(struct cache_c *dmc)
{
	int i = 0;
	int j = 0;
	new_mu_t *mu = &dmc->new_mu;

	for (i = 0; i < NUM_MU_BLK_TYPE; ++i) {
		for (j = 0; j < dmc->md_blocks; ++j) {
			mu->mu_blocks[i][j].idx = j;
			INIT_LIST_HEAD(&mu->mu_blocks[i][j].node);
		}
	}

	init_dev_flush(&mu->disk_flush, syno_mu_disk_flush_complete);
	init_dev_flush(&mu->ssd_flush, syno_mu_ssd_flush_complete);
	mu->flush_itvl_sec = MU_DEFAULT_FLUSH_ITVL_SEC;

	INIT_LIST_HEAD(&mu->fua_job_list);

	INIT_LIST_HEAD(&mu->md_io_inprog_queue);
	INIT_LIST_HEAD(&mu->md_io_pending_queue);
	INIT_LIST_HEAD(&mu->md_io_for_flush_pending_queue);
	INIT_WORK(&mu->md_io_complete_work, syno_mu_md_io_complete);

	INIT_DELAYED_WORK(&mu->dwork, syno_mu_do_md_update);
	mu->sysctl_mu_ios_total = 32;
	mu->sysctl_mu_delay_sec = MU_DEFAULT_DELAY_SEC;
	mu->sysctl_mu_check_itvl_sec = 1;

	init_waitqueue_head(&mu->remove_wqh);

	atomic_set(&mu->send_md_io_cnt, 0);
	atomic_set(&mu->md_io_callback_cnt, 0);

	init_ext_flush(&mu->ext_flush);

	spin_lock_init(&mu->lock);
	mu->inited = 1;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline int get_hash_mapping_from_cache_version(int version)
{
	switch (version) {
	case SYNO_FLASHCACHE_VERSION:
		return HASH_MAPPING_DISABLE;
	case SYNO_FLASHCACHE_VERSION_HASH_MAPPING:
		return HASH_MAPPING_V1;
	case SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2:
		return HASH_MAPPING_V2;
	default:
		serr("Invalid cache version: %d", version);
		return -1;
	}
}
#endif

static int
flashcache_writeback_load(struct cache_c *dmc)
{
	struct flash_cacheblock *meta_data_cacheblock, *next_ptr;
	struct flash_superblock *header;
	struct dm_io_region where;
	int i, j;
	u_int64_t size, slots_read;
	int clean_shutdown;
	int dirty_loaded = 0;
	sector_t order, data_size;
	int num_valid = 0;
	int error;
	int sectors_read = 0, sectors_expected = 0;	/* Debug */
	unsigned long long bad_states_cnt = 0;
	bad_states_t bad_states;
	sector_t disk_dev_sectors = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);


	memset(&bad_states, 0, sizeof(bad_states));

	/* 
	 * We don't know what the preferred block size is, just read off 
	 * the default md blocksize.
	 */
	// should convert sectors to bytes
	header = (struct flash_superblock *)vmalloc(to_bytes(DEFAULT_MD_BLOCK_SIZE));
	if (!header) {
		DMERR("flashcache_writeback_load: Unable to allocate memory");
		return 1;
	}
	where.bdev = get_cache_bdev(dmc);
	where.sector = 0;
	where.count = DEFAULT_MD_BLOCK_SIZE;
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_READ, header);
	if (error) {
		vfree((void *)header);
		DMERR("flashcache_writeback_load: Could not read cache superblock %llu error %d!",
		      (u64)where.sector, error);
		return 1;
	}

	if (header->cache_version == 1) {
		/* Backwards compatibility, md was 512 bytes always in V1.0 */
		header->md_block_size = 1;
	} else if (!IS_NEW_MODE_VERSION(header->cache_version)) {
		DMERR("flashcache_writeback_load: Unknown version %d found in superblock!", header->cache_version);
		vfree((void *)header);
		return 1;
	}
	dmc->on_ssd_version = header->cache_version;

	DPRINTK("Loaded cache conf: version(%d), block size(%u), md block size(%u), cache size(%llu), " \
	        "associativity(%u)",
	        header->cache_version, header->block_size, header->md_block_size, header->size,
	        header->assoc);
	if (!((header->cache_sb_state == CACHE_MD_STATE_DIRTY) ||
	      (header->cache_sb_state == CACHE_MD_STATE_CLEAN) ||
	      (header->cache_sb_state == CACHE_MD_STATE_FASTCLEAN))) {
		vfree((void *)header);
		DMERR("flashcache_writeback_load: Corrupt Cache Superblock");
		return 1;
	}
	if (header->cache_sb_state == CACHE_MD_STATE_DIRTY) {
		DMINFO("Unclean Shutdown Detected");
		printk(KERN_ALERT "Only DIRTY blocks exist in cache");
		clean_shutdown = 0;
	} else if (header->cache_sb_state == CACHE_MD_STATE_CLEAN) {
		DMINFO("Slow (clean) Shutdown Detected");
		printk(KERN_ALERT "Only CLEAN blocks exist in cache");
		clean_shutdown = 1;
	} else {
		DMINFO("Fast (clean) Shutdown Detected");
		printk(KERN_ALERT "Both CLEAN and DIRTY blocks exist in cache");
		clean_shutdown = 1;
	}
	dmc->block_size = header->block_size;
	dmc->md_block_size = header->md_block_size;
	dmc->block_shift = ffs(dmc->block_size) - 1;
	dmc->block_mask = dmc->block_size - 1;
	dmc->size = header->size;
	dmc->assoc = header->assoc;
#ifdef MY_ABC_HERE
	if (header->cache_version >= SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2) {
		snprintf(dmc->group_uuid, sizeof(dmc->group_uuid), "%s", header->group_uuid);
	} // empty string for old version
#endif
	if (CACHE_ENABLED != header->cache_state) {
		sprint("Force change driver's cache state from %d to %d",
				header->cache_state, CACHE_ENABLED);
		dmc->cache_state = CACHE_ENABLED;
	} else {
		dmc->cache_state = header->cache_state;
	}

#ifdef MY_ABC_HERE
	handle_tail_region(dmc);
	dmc->hash_mapping = get_hash_mapping_from_cache_version(header->cache_version);
	if (-1 == dmc->hash_mapping) {
		serr("Failed to hash mapping from version %d", header->cache_version);
		return 1;
	}
	sprint("Hash mapping = %d", dmc->hash_mapping);
#endif
	dmc->assoc_shift = ffs(dmc->assoc) - 1;

	dmc->md_blocks = INDEX_TO_MD_BLOCK(dmc, dmc->size) + 1 + 1;

	sprint("flashcache_writeback_load: md_blocks = %d, md_sectors = %d, md_block_size = %d\n",
	       dmc->md_blocks, dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc), dmc->md_block_size);
	data_size = dmc->size * dmc->block_size;
	order = dmc->size * sizeof(struct cacheblock);
	DMINFO("Allocate %lluKB (%zdB per) mem for %llu-entry cache" \
	       "(capacity:%lluMB, associativity:%u, block size:%u " \
	       "sectors(%uKB))",
	       (u64)order >> 10, sizeof(struct cacheblock), (u64)dmc->size,
	       (u64)(dmc->md_blocks * MD_SECTORS_PER_BLOCK(dmc) + data_size) >> (20-SECTOR_SHIFT), 
	       dmc->assoc, dmc->block_size,
	       dmc->block_size >> (10-SECTOR_SHIFT));
	dmc->cache = (struct cacheblock *)do_vmalloc(order);
	if (!dmc->cache) {
		DMERR("load_metadata: Unable to allocate memory");
		vfree((void *)header);
		return 1;
	}

	// should be set in both WB and WT mode
	memset(dmc->cache, 0, order);

	/* Read the metadata in large blocks and populate incore state */
	meta_data_cacheblock = (struct flash_cacheblock *)vmalloc(METADATA_IO_BLOCKSIZE);
	if (!meta_data_cacheblock) {
		vfree((void *)header);
		vfree(dmc->cache);
		DMERR("flashcache_writeback_load: Unable to allocate memory");
		return 1;
	}
	where.sector = MD_SECTORS_PER_BLOCK(dmc);
	size = dmc->size;
	i = 0;
	while (size > 0) {
		slots_read = min(size, (u_int64_t)(MD_SLOTS_PER_BLOCK(dmc) * METADATA_IO_NUM_BLOCKS(dmc)));
#ifdef EC_EMULATE_U64_DIVISION
		if (mod_u64_rem64(slots_read, MD_SLOTS_PER_BLOCK(dmc)))
			where.count = (1 + (div64_u64(slots_read, MD_SLOTS_PER_BLOCK(dmc)))) * MD_SECTORS_PER_BLOCK(dmc);
		else
			where.count = (div64_u64(slots_read, MD_SLOTS_PER_BLOCK(dmc))) * MD_SECTORS_PER_BLOCK(dmc);
#else
		if (slots_read % MD_SLOTS_PER_BLOCK(dmc))
			where.count = (1 + (slots_read / MD_SLOTS_PER_BLOCK(dmc))) * MD_SECTORS_PER_BLOCK(dmc);
		else
			where.count = (slots_read / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
#endif
		sectors_read += where.count;	/* Debug */
		error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_READ, meta_data_cacheblock);
		if (error) {
			vfree((void *)header);
			vfree(dmc->cache);
			vfree((void *)meta_data_cacheblock);
			DMERR("flashcache_writeback_load: Could not read cache metadata block %llu error %d !",
			      (u64)where.sector, error);
			return 1;
		}
		where.sector += where.count;
		next_ptr = meta_data_cacheblock;
		for (j = 0 ; j < slots_read ; j++) {
			/*
			 * XXX - Now that we force each on-ssd metadata cache slot to be a ^2, where
			 * we are guaranteed that the slots will exactly fit within a sector (and 
			 * a metadata block), we can simplify this logic. We don't need this next test.
			 */
			if ((j % MD_SLOTS_PER_BLOCK(dmc)) == 0) {
				/* Move onto next block */
				next_ptr = (struct flash_cacheblock *)
					((caddr_t)meta_data_cacheblock + MD_BLOCK_BYTES(dmc) * (j / MD_SLOTS_PER_BLOCK(dmc)));
			}
			dmc->cache[i].nr_queued = 0;
			/* 
			 * If unclean shutdown, only the DIRTY blocks are loaded.
			 */
			if (clean_shutdown || (next_ptr->cache_state & DIRTY)) {
				if (next_ptr->cache_state & DIRTY)
					dirty_loaded++;
				cb_state_assign_update_counts(dmc, &dmc->cache[i], next_ptr->cache_state);
				dmc->cache[i].data_bitmap = next_ptr->data_bitmap;
				dmc->cache[i].dirty_bitmap = next_ptr->dirty_bitmap;

				if (dmc->cache[i].cache_state & VALID)
					num_valid++;
				dmc->cache[i].dbn = next_ptr->dbn;
#ifdef FLASHCACHE_DO_CHECKSUMS
				if (clean_shutdown)
					dmc->cache[i].checksum = next_ptr->checksum;
				else {
					error = flashcache_read_compute_checksum(dmc, i, block);
					if (error) {
						vfree((void *)header);
						vfree(dmc->cache);
						vfree((void *)meta_data_cacheblock);
						DMERR("flashcache_writeback_load: Could not read cache metadata block %llu error %d !",
						      (u64)dmc->cache[i].dbn, error);
						return 1;
					}
				}
#endif
				if (data_rescue_flags & DATA_RESCUE_BLOCK_BAD_STATES) {
					handle_bad_states(dmc, &dmc->cache[i], BAD_STATES_RESCUE, NULL,
						disk_dev_sectors);
				}
				handle_bad_states(dmc, &dmc->cache[i], BAD_STATES_ACCUME_STAT, &bad_states,
					disk_dev_sectors);
			} else {
				set_block_invalid(dmc, &dmc->cache[i]);
			}
			next_ptr++;
			i++;
		}
		size -= slots_read;
	}

	bad_states_cnt = bad_states.bad_dbn_size +
		bad_states.invalid + bad_states.valid + bad_states.dirty +
		bad_states.in_prog + bad_states.fallow + bad_states.bitmap;

	if (bad_states_cnt) {
		sprint("Detect bad cache blocks:");
		sprint("Bad start sector or size: %llu", bad_states.bad_dbn_size);
		sprint("Bad invalid state: %llu", bad_states.invalid);
		sprint("Bad valid state: %llu", bad_states.valid);
		sprint("Bad dirty state: %llu", bad_states.dirty);
		sprint("Bad inprog state: %llu", bad_states.in_prog);
		sprint("Bad fallow state: %llu", bad_states.fallow);
		sprint("Bad bitmap : %llu", bad_states.bitmap);
		sprint("Bad blocks / Total blocks: %llu / %llu", bad_states_cnt, (u64)dmc->size);

		if (LOAD_ERROR_CHECK == dmc->load_action) {
			sprint("Detect incorrect cache block");
			vfree((void *)meta_data_cacheblock);
			return 1;
		}
	}

	if (data_rescue_flags & DATA_RESCUE_BLOCK_BAD_STATES) {
		data_rescue_flags &= ~ DATA_RESCUE_BLOCK_BAD_STATES;
		sprint("Finish data rescue for cache bad state");
		sprint("NOTE: Reboot your system now to make all cache blocks sates are updated to SSD");
	}

	/* Debug Tests */
#ifdef EC_EMULATE_U64_DIVISION
	sectors_expected = div64_u64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (mod_u64_rem64(dmc->size, MD_SLOTS_PER_BLOCK(dmc)))
#else
	sectors_expected = (dmc->size / MD_SLOTS_PER_BLOCK(dmc)) * MD_SECTORS_PER_BLOCK(dmc);
	if (dmc->size % MD_SLOTS_PER_BLOCK(dmc))
#endif
		sectors_expected += MD_SECTORS_PER_BLOCK(dmc);
	if (sectors_expected != sectors_read) {
		printk("flashcache_writeback_load" "Sector Mismatch ! sectors_expected=%d, sectors_read=%d\n",
		       sectors_expected, sectors_read);
		vfree((void *)meta_data_cacheblock);
		return 1;
	}
	vfree((void *)meta_data_cacheblock);
	/*
	 * For writing the superblock out, use the preferred blocksize that 
	 * we read from the superblock above.
	 */
	if (DEFAULT_MD_BLOCK_SIZE != dmc->md_block_size) {
		vfree((void *)header);
		header = (struct flash_superblock *)vmalloc(MD_BLOCK_BYTES(dmc));
		if (!header) {
			DMERR("flashcache_writeback_load: Unable to allocate memory");
			return 1;
		}
	}	
	/* Before we finish loading, we need to dirty the superblock and 
	   write it out */
	header->size = dmc->size;
	header->block_size = dmc->block_size;
	header->md_block_size = dmc->md_block_size;
	header->assoc = dmc->assoc;
	header->cache_sb_state = CACHE_MD_STATE_DIRTY;
	snprintf(header->disk_devname, DEV_PATHLEN, "%s", dmc->disk_devname);
	snprintf(header->cache_devname, DEV_PATHLEN, "%s", dmc->dm_vdevname);
	header->cache_devsize = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);
	header->disk_devsize = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);
	dmc->disk_devsize = header->disk_devsize;

	header->cache_version = dmc->on_ssd_version;
	where.sector = 0;
	where.count = dmc->md_block_size;
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_WRITE, header);
	if (error) {
		vfree((void *)header);
		vfree(dmc->cache);
		DMERR("flashcache_writeback_load: Could not write cache superblock %llu error %d !",
		      (u64)where.sector, error);
		return 1;
	}
	vfree((void *)header);
	DMINFO("flashcache_writeback_load: Cache metadata loaded from disk with %d valid %d DIRTY blocks", 
	       num_valid, dirty_loaded);
	return 0;
}

#ifdef MY_ABC_HERE
#else
static void
flashcache_clean_all_sets(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, 
					   delayed_clean.work);
	int i;
	sdbg(DF_WORKQUEUE, "function executed from workqueue");
	for (i = 0 ; i < dmc->num_sets ; i++)
		flashcache_clean_set(dmc, i);
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int is_bdev_in_use(char *path)
{
	int ret = 0;
	struct block_device *bdev = NULL;
	char *holder = "Holder by flashcache";

	bdev = blkdev_get_by_path(path, FMODE_EXCL, holder);

	if (IS_ERR(bdev)) {
		sprint("%s exclude check failed(%ld)", path, PTR_ERR(bdev));
		ret = 1;
	} else {
		sprint("%s exclude check success", path);
		blkdev_put(bdev, FMODE_EXCL);
	}

	return ret;
}
#endif

typedef enum _CHECK_TYPE {
	CHECK_NONE,
	CHECK_IN_USE
} CHECK_TYPE;

// If dmc_dname is NULL, not write pth to dmc_dname
static int inline
flashcache_get_dev(struct dm_target *ti, char *pth, struct dm_dev **dmd,
		   char *dmc_dname, sector_t tilen, CHECK_TYPE check_type)
{
	int rc;

	if (CHECK_IN_USE == check_type) {
		rc = is_bdev_in_use(pth);
		if (rc) {
			return -EBUSY;
		}
	}

	rc = dm_get_device(ti, pth, dm_table_get_mode(ti->table), dmd);
	if (!rc && dmc_dname) {
		snprintf(dmc_dname, DEV_PATHLEN, "%s", pth);
	}
	return rc;
}

#ifdef MY_ABC_HERE
// Must have dmc->disk_dev first
static int
dmc_id_set(struct cache_c *dmc)
{
	int ret = -1;

	if (!dmc) {
		serr("Incorrect parameter");
		goto err;
	}

	dmc->disk_devname[DEV_PATHLEN - 1] = 0;
	snprintf(dmc->id, sizeof(dmc->id), "%s", dmc->disk_devname);

	ret = 0;

err:
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#ifdef MY_ABC_HERE

/* protect the operation of dmc_group_list (add, search, remove)
 * for every operation that might involve changing dmc_group_list, this mutex
 * must be locked before dmc_group->rw_sem */
struct mutex dmcg_mtx;

// TODO: should make an interface to export group uuid to userspace
/* For older version of cache, there's no group uuid available in the superblock
 * But we still need dmc group for throttling. Use empty group uuid
 * dummy cache are not in dmc group */
LIST_HEAD(dmc_group_list);
#endif
/* dmc_list is used by online expansion to find the correct dmc to reload */
LIST_HEAD(dmc_list);

struct mutex dmc_list_mtx;

#ifdef MY_ABC_HERE
/* In under dmcg->mtx */
static dmc_group_t *
dmc_group_new(char *uuid)
{
	dmc_group_t *dmcg = NULL;

	if (global_tester == TEST_NEW_DMC_GROUP_ALLOC_FAIL) {
		sprint("Simulate dmc group alloc fail");
		global_tester = 0;
		goto err;
	}

	if (NULL == (dmcg = kzalloc(sizeof(*dmcg), GFP_NOIO))) {
		goto err;
	}

	snprintf(dmcg->uuid, sizeof(dmcg->uuid), "%s", uuid);
	INIT_LIST_HEAD(&dmcg->node);
	INIT_LIST_HEAD(&dmcg->dmc_list);
	list_add(&dmcg->node, &dmc_group_list);
	init_rwsem(&dmcg->rw_sem);
#ifdef MY_ABC_HERE
	dmcg->md_io_throtl_thres = 20000;
#endif
	init_waitqueue_head(&dmcg->nqf_wqh);

	INIT_LIST_HEAD(&dmcg->nqf_pending_dmcs);
err:
	return dmcg;
}

/* WARNING: should ONLY be used in dmc_group_add, should held dmcg_mutex */
static dmc_group_t*
dmc_group_search(char *uuid)
{
	struct list_head *pos = NULL;
	dmc_group_t *dmcg = NULL;

	list_for_each(pos, &dmc_group_list) {
		dmcg = container_of(pos, dmc_group_t, node);
		if (0 == strcmp(uuid, dmcg->uuid)) {
			return dmcg;
		}
	}

	return NULL;
}

static dmc_group_t *
dmc_group_add(struct cache_c *dmc)
{
	dmc_group_t *dmcg = NULL;

	mutex_lock(&dmcg_mtx);

	/* empty uuid for old version. Has it's own dmc group */
	if (0 == strlen(dmc->group_uuid) || NULL == (dmcg = dmc_group_search(dmc->group_uuid))) {
		sdbg(DF_DMCG, "Create new dmcg");
		dmcg = dmc_group_new(dmc->group_uuid);
		if (!dmcg) {
			serr("Failed to allocate dmc group for %s", dmc->group_uuid);
			goto err_unlock;
		}
	}

	down_write(&dmcg->rw_sem);

#ifdef MY_ABC_HERE
	list_add(&dmc->node->group_entry, &dmcg->dmc_list);
#endif
	dmcg->num_dmc++;
	sdbg(DF_DMCG, "Add to dmc=%p", dmc);
	dmc->dmcg = dmcg;

	up_write(&dmcg->rw_sem);

err_unlock:
	mutex_unlock(&dmcg_mtx);

	sprint("Group UUID: %s", dmc->group_uuid);
	return dmcg;
}

// Note: Dummy cache won't be in a dmc group and no need to remove from it
static void
dmc_group_remove(struct cache_c *dmc)
{
	dmc_group_t *dmcg = dmc->dmcg;

	sdbg(DF_DMCG, "remove dmc=%p from dmcg", dmc);

	mutex_lock(&dmcg_mtx);
	if (!dmcg) {
		VERIFY_WARN(0);
		mutex_unlock(&dmcg_mtx);
		return;
	}

	/* Need to lock dmcg_mtx first since we might change dmc_group_list */
	down_write(&dmcg->rw_sem);

#ifdef MY_ABC_HERE
	list_del(&dmc->node->group_entry);
#endif
	dmcg->num_dmc--;
	if (list_empty(&dmcg->dmc_list)) {
		/* No entry in dmc group, remove it */
		VERIFY_WARN(0 == dmcg->num_dmc);
		list_del(&dmcg->node);
		up_write(&dmcg->rw_sem);
		kfree(dmcg);
	} else {
		up_write(&dmcg->rw_sem);
	}

	dmc->dmcg = NULL;
	mutex_unlock(&dmcg_mtx);
}
#endif

// Return the count of matched item, in under dmc_list_mtx
static int
dmc_list_search(const char *id, struct cache_c **ppdmc)
{
	int ret = 1;
	int count = 0;
	dmc_node *pnode = NULL;

	VERIFY_WARN(id && ppdmc);

	list_for_each_entry(pnode, &dmc_list, entry) {
		sdbg(DF_RELOAD_TABLE, "search id=%s dmc=%p", pnode->id, pnode->pdmc);
		if (0 == strcmp(pnode->id, id)) {
			*ppdmc = pnode->pdmc;
			ret = 0;
			count++;
		}
	}

	if (count > 1) {
		serr("Count should be 0 or 1");
	}

	return count;
}

static int
dmc_list_add(const char *id, struct cache_c *pdmc)
{
	int ret = 1;
	struct cache_c *tmp = NULL;
	dmc_node *pnode = NULL;

	sdbg_in(DF_RELOAD_TABLE, "in");
	VERIFY_WARN(id && pdmc);

	mutex_lock(&dmc_list_mtx);

	if (0 < dmc_list_search(id, &tmp)) {
		serr("Should not find dmc with the same id");
		goto err_unlock;
	}

	pnode = kzalloc(sizeof(dmc_node), GFP_NOIO);
	if (!pnode) {
		serr("Allocate dmc node failed");
		goto err_unlock;
	}

	snprintf(pnode->id, sizeof(pnode->id), "%s", id);
	pnode->pdmc = pdmc;
	pdmc->node = pnode;

	sdbg(DF_RELOAD_TABLE, "id=%s dmc=%p", pnode->id, pnode->pdmc);
	list_add(&pnode->entry, &dmc_list);


	ret = 0;
err_unlock:
	mutex_unlock(&dmc_list_mtx);
	sdbg_out(DF_RELOAD_TABLE, "out");
	return ret;
}

static int
dmc_list_remove(const char *id, struct cache_c *pdmc)
{
	int ret = 1;
	dmc_node *pnode = NULL;
	dmc_node *next = NULL;

	VERIFY_WARN(id && pdmc);
	sdbg(DF_RELOAD_TABLE, "start remove id=\"%s\" dmc=%p", id, pdmc);

	mutex_lock(&dmc_list_mtx);

	list_for_each_entry_safe(pnode, next, &dmc_list, entry) {
		sdbg(DF_RELOAD_TABLE, "next node id=\"%s\"", pnode->id);
		if (0 == strcmp(pnode->id, id)) {

			if (pnode->pdmc != pdmc) {
				serr("dmc doesn't match: dmc=%p", pnode->pdmc);
				goto end_unlock;
			}

			list_del(&pnode->entry);
			kfree(pnode);
			pdmc->node = NULL;
			ret = 0;
			goto end_unlock;
		}
	}

end_unlock:
	mutex_unlock(&dmc_list_mtx);
	sdbg(DF_RELOAD_TABLE, "finish ret=%d", ret);
	return ret;
}

static
void set_dm_target_attribute(struct dm_target *ti, struct cache_c *dmc, dm_attr_t dm_attr)
{
	VERIFY(dmc);

	ti->private = dmc;

	if (DM_ATTR_CACHE_ENABLE == dm_attr) {
#ifdef MY_DEF_HERE
		ti->max_io_len = 0;
#else
		// Let dm layer split IO automatically
		ti->max_io_len = MAX_BIO_SIZE;
#endif
		dmc->max_io_len = ti->max_io_len;
		sdbg(DF_ONLINE, "cache is enabled, set num_discard_bios = 0");
		// Not support trim as the default driver behavior
		ti->num_discard_bios = 0;
	} else if (DM_ATTR_CACHE_DISABLE == dm_attr) {
		ti->max_io_len = 0;
		dmc->max_io_len = ti->max_io_len;
		sdbg(DF_ONLINE, "cache is disabled, set num_discard_bios = 1");
		ti->num_discard_bios = 1;
	} else {
		serr("Incorect dm attr=%d", dm_attr);
	}

#ifdef MY_ABC_HERE
	ti->num_flush_bios = 1;
	ti->flush_supported = 1;
#endif

#ifdef CONFIG_SYNO_MD_UNUSED_HINT
	if (blk_queue_unused_hint(bdev_get_queue(get_disk_bdev(dmc))))
		ti->num_unused_hint_bios = 1;
#endif
}

#ifdef MY_ABC_HERE
/*
 * WARNING: Only use this function in one-time logic (e.g. reload)
 * Use it in frequently-called logic (e.g. IO) might cause performance issue
 * since it calls spinlock many times
 *
 * Use the following functions to eliminate the need
 * to allocate / free memory in spin_lock functions
 */
void
bitmap_control_get(struct cache_c *dmc)
{
	int get_control = 0;
	unsigned long flags = 0;

	while (1) {
		spin_lock_irqsave(&dmc->bitmap_control.lock, flags);

		if (!dmc->bitmap_control.in_use) {
			dmc->bitmap_control.in_use = 1;
			get_control = 1;
		}

		spin_unlock_irqrestore(&dmc->bitmap_control.lock, flags);

		// Prevent to call spin lock too often
		mdelay(100);

		if (get_control) {
			break;
		}
	}
}

void
bitmap_control_put(struct cache_c *dmc)
{
	unsigned long flags;

	spin_lock_irqsave(&dmc->bitmap_control.lock, flags);

	if (dmc->bitmap_control.in_use) {
		dmc->bitmap_control.in_use = 0;
	} else {
		serr("bitmap Must be in use!");
		WARN_ON(1);
	}

	spin_unlock_irqrestore(&dmc->bitmap_control.lock, flags);
}
#endif

#ifdef MY_ABC_HERE
static int
flashcache_create_dummy(struct dm_target *ti, int argc, char **argv)
{
	int r = -EINVAL;
	struct cache_c *dmc = NULL;

	sdbg(DF_ONLINE, "start");

	if (!ti || !argc) {
		serr("Incorrect parameter");
		goto err;
	}

	if (TEST_MALLOC_FAIL_IN_CREATE == global_tester) {
		serr("Simulate memory allocate failed");
	} else {
		dmc = vzalloc(sizeof(*dmc));
	}
	if (!dmc) {
		ti->error = "flashcache: Failed to allocate cache context";
		r = -ENOMEM;
		goto err;
	}

	dmc->tgt = ti;

	dmc->cache_mode = FLASHCACHE_DUMMY;
	dmc->cache_state = CACHE_DISABLED;

	// disk_dev / disk_devname
	if ((r = flashcache_get_dev(ti, argv[0], &dmc->disk_dev,
				    dmc->disk_devname, ti->len, CHECK_IN_USE))) {
		if (r == -EBUSY)
			ti->error = "flashcache: Disk device is busy, cannot create cache";
		else
			ti->error = "flashcache: Disk device lookup failed";

		serr("initalized disk_dev failed");
		goto err;
	}

	// Locks
	spin_lock_init(&dmc->cache_spin_lock);

	// For expansion
	spin_lock_init(&dmc->reload_lock);
	spin_lock_init(&dmc->dev_lock);
	init_rwsem(&dmc->ioctl_rwsem);
	dmc->disk_bdev = dmc->disk_dev->bdev;

	// Wait queues for enable / disable
	init_waitqueue_head(&dmc->wait_for_cache_disable_queue);
	init_waitqueue_head(&dmc->wait_old_ios_completed_queue);

	// cache_dev
	strncpy(dmc->cache_devname, argv[1], sizeof(dmc->cache_devname) - 1);

	// cachedev_name
	if (sscanf(argv[2], "%s", (char *)&dmc->dm_vdevname) != 1) {
		ti->error = "flashcache: Virtual device name lookup failed";
		goto err;
	}

	if (dmc_id_set(dmc)) {
		serr("can't generate id");
		goto err;
	}

	// For enabling later
	if (dmc_list_add(dmc->id, dmc)) {
		serr("Can't add dmc to list");
		goto err;
	}

	set_dm_target_attribute(ti, dmc, DM_ATTR_CACHE_DISABLE);

	// Add to global list
	(void)wait_on_bit_lock_wrapper(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST,
			       flashcache_wait_schedule, TASK_UNINTERRUPTIBLE);
	dmc->next_cache = cache_list_head;
	cache_list_head = dmc;
	clear_bit(FLASHCACHE_UPDATE_LIST, &flashcache_control->synch_flags);
	smp_mb__after_clear_bit_wrapper();
	wake_up_bit(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST);

	dmc->init_nr_dirty = -1;

	r = 0;

	return r;

err:
	if (dmc && dmc->disk_dev) {
		dm_put_device(ti, dmc->disk_dev);
	}

	if (dmc) {
		vfree(dmc);
	}

	return r;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int reload_nqf(struct cache_c *dmc)
{
	int r = -1;
	unsigned int noio_flags = 0;

	unsigned long new_bitmap_num_idx = OQF_FALLOW_MAP_NUM_IDX(dmc);
	int *old_bitmap = NULL;
	int *new_bitmap = NULL;

	if (new_bitmap_num_idx == dmc->oqf.bitmap_num_idx) {
		r = 0;
		goto end;
	}

	noio_flags = memalloc_noio_save();
	new_bitmap = (int *) vzalloc(sizeof(int) * new_bitmap_num_idx);
	memalloc_noio_restore(noio_flags);

	if (check_new_error_inject(dmc, OQF_RELOAD_MEM_ALLOC_FAIL)) {
		sprint("Error inject reload oqf bitmap mem alloc fail");
		dmc->sysctl_new_error_inject = 0;
		vfree(new_bitmap);
		new_bitmap = NULL;
	}

	if (NULL == new_bitmap) {
		serr("Failed to allocate %lu bytes of memory for fallow bitmap",
			sizeof(int) * new_bitmap_num_idx);
		r = -ENOMEM;
		goto err;
	}

	down_write(&dmc->oqf.bitmap_rwsem);
	memcpy(new_bitmap, dmc->oqf.volume_bitmap, dmc->oqf.bitmap_num_idx * sizeof(int));
	dmc->oqf.bitmap_num_idx = new_bitmap_num_idx;
	old_bitmap = dmc->oqf.volume_bitmap;
	dmc->oqf.volume_bitmap = new_bitmap;
	up_write(&dmc->oqf.bitmap_rwsem);

	vfree(old_bitmap);

	r = 0;
end:
err:
	return r;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
// Resize pin bitmap when volume size is changed
static int reload_pinfile(struct cache_c *dmc)
{
	int r = 0;
	unsigned char *pnew_bitmap = NULL;
	u64 new_size_bit = 0;
	u64 new_size_byte = 0;
	unsigned int noio_flags = 0;

	new_size_bit = compatible_div(get_disk_bdev(dmc)->bd_inode->i_size,
			PIN_BYTES_PER_BIT);
	new_size_byte = compatible_div(new_size_bit, 8);

	if (new_size_byte == dmc->bitmap_size_byte) {
		goto end;
	}

	if (new_size_byte < dmc->bitmap_size_byte) {
		serr("New bitmap size (%llu) should not less than the origina bitmap size (%llu)",
				(u64)new_size_byte, (u64)dmc->bitmap_size_byte);
		r = -EINVAL;
		goto err;
	}

	noio_flags = memalloc_noio_save();
	pnew_bitmap = vzalloc(new_size_byte);
	memalloc_noio_restore(noio_flags);
	if (!pnew_bitmap) {
		serr("Allocate new bitmap memory (%llu bytes) failed", new_size_byte);
		r = -ENOMEM;
		goto err;
	}

	bitmap_control_get(dmc);
	memcpy(pnew_bitmap, dmc->pbitmap_ranges, dmc->bitmap_size_byte);

	sdbg(DF_PIN, "bitmap resize start");

	vfree(dmc->pbitmap_ranges);
	dmc->pbitmap_ranges = pnew_bitmap;
	dmc->bitmap_size_byte = new_size_byte;

	bitmap_control_put(dmc);
	sdbg(DF_PIN, "bitmap resize end");

	sdbg(DF_PIN, "Allocate & memcopy size = %llu KB", new_size_byte / 1024);

end:
err:
	return r;
}
#endif /* MY_ABC_HERE */

/*
 * argv[0]: current disk_path of cache
 * argv[1]: current ssd_path of cache. Must be "none" after cache is disabled
 */
static int
flashcache_reload_table(struct dm_target *ti, int argc, char **argv)
{
	int r = 0;
	unsigned long flags = 0;
	struct cache_c *dmc = NULL;
	int can_reload = 0;
	struct dm_target *ori_ti = NULL;
	sector_t new_disk_size = 0;

	mutex_lock(&dmc_list_mtx);
	if (0 == dmc_list_search(argv[0], &dmc)) {
		mutex_unlock(&dmc_list_mtx);
		serr("Can't find correct dmc, id=%s", argv[0]);
		ti->error = "Can't find correct dmc";
		r = -EINVAL;
		goto err;
	}
	mutex_unlock(&dmc_list_mtx);

	/*
	 * Make sure user doesn't input wrong disk devname or cache devname.
	 * Otherwise, it might lead to free original block dev to get call trace
	 * since no other dm_dev use it anymore
	 */

	if (0 != strcmp(argv[0], dmc->disk_devname)) {
		serr("Disk dev name not match (%s, %s)", argv[0],
			dmc->disk_devname)
		r = -EPERM;
		goto err;
	}

	if (0 != strcmp(argv[1], dmc->cache_devname)) {
		serr("SSD dev name not match (%s, %s)", argv[1],
			dmc->cache_devname)
		r = -EPERM;
		goto err;
	}

	if ((CACHE_DISABLED != dmc->cache_state) && (CACHE_ENABLED != dmc->cache_state)) {
		serr("This state (%d) doesn't allow to expand", dmc->cache_state);
		r = -EPERM;
		goto err;
	}

	sdbg(DF_ONLINE, "Get matched dmc !!!");

	/*
	 * Prevent the error caused by issuing reload command multiple times
	 * The reload table flag would be unset after the destroy function ended
	 */
	spin_lock_irqsave(&dmc->reload_lock, flags);
	if (0 == dmc->reload_table) {
		dmc->reload_table = 1;
		can_reload = 1;
	}
	spin_unlock_irqrestore(&dmc->reload_lock, flags);

	if (!can_reload) {
		serr("Error! cache is in reloading table");
		r = -EPERM;
		goto err_can_reload;
	}

	ori_ti = dmc->tgt;
	dmc->tgt = ti;

	// Access the old dm_dev saved on dmc
	new_disk_size = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);

	if (new_disk_size < dmc->disk_devsize) {
		serr("New disk size (%llu) should be larger then original disk size (%llu)",
				(u64)new_disk_size, (u64)dmc->disk_devsize);
		r = -EINVAL;
		goto err;
	}

	/*
	 * Create new dm_devs for new mapping table
	 * Ignore the block device reuse check since it is still used by original
	 * mapping table now
	 */
	if ((r = flashcache_get_dev(ti, dmc->disk_devname,
			&dmc->disk_dev_reload, NULL, ti->len, CHECK_NONE))) {
		if (r == -EBUSY)
			ti->error = "flashcache: Disk device is busy, cannot create cache";
		else
			ti->error = "flashcache: Disk device lookup failed";
		goto err;
	}

	if (CACHE_ENABLED == dmc->cache_state) {
		// Not dummy cache
		if ((r = flashcache_get_dev(ti, dmc->cache_devname,
			&dmc->cache_dev_reload, NULL, 0, CHECK_NONE))) {
			if (r == -EBUSY)
				ti->error = "flashcache: Cache device is busy, cannot create cache";
			else
				ti->error = "flashcache: Cache device lookup failed";
			goto err;
		}
	}

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		if ((r = reload_pinfile(dmc))) {
			goto err;
		}
	}
#endif

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		sdbg(DF_QUICKFLUSH, "Reloading Online Quickflush");
		if ((r = reload_nqf(dmc))) {
			goto err;
		}
	}
#endif

	// Don't really change any value about dmc, only initialize dm_target
	if (FLASHCACHE_DUMMY == dmc->cache_mode) {
		set_dm_target_attribute(ti, dmc, DM_ATTR_CACHE_DISABLE);
	} else if ((FLASHCACHE_WRITE_BACK == dmc->cache_mode) || (FLASHCACHE_WRITE_AROUND == dmc->cache_mode)) {
		set_dm_target_attribute(ti, dmc, DM_ATTR_CACHE_ENABLE);
	}

	r = 0;

	return r;

err:
	if (dmc && can_reload) {
		if (dmc->tgt != ori_ti) {
			dmc->tgt = ori_ti;
		}

		if (dmc->cache_dev_reload) {
			dm_put_device(ti, dmc->cache_dev_reload);
			dmc->cache_dev_reload = NULL;
		}

		if (dmc->disk_dev_reload) {
			dm_put_device(ti, dmc->disk_dev_reload);
			dmc->disk_dev_reload = NULL;
		}

		spin_lock_irqsave(&dmc->reload_lock, flags);
		// Only set to 0 when failed
		if (dmc->reload_table) {
			dmc->reload_table = 0;
		}
		spin_unlock_irqrestore(&dmc->reload_lock, flags);
	}

err_can_reload:
	// Should not release any resources that are not requested by us

	return r;
}
#endif

/*
 * Construct a cache mapping.
 *  arg[0]: path to source device
 *  arg[1]: path to cache device
 *  arg[2]: md virtual device name
 *  arg[3]: cache mode (from flashcache.h)
 *		FLASHCACHE_WRITE_BACK=1
 *		FLASHCACHE_WRITE_THROUGH=2
 *		FLASHCACHE_WRITE_AROUND=3
 *		FLASHCACHE_DUMMY=4
 *  arg[4]: cache persistence (if set, cache conf is loaded from disk)
 *		CACHE_RELOAD=1 (For load)
 *		CACHE_CREATE=2
 *		CACHE_FORCECREATE=3
 *		CACHE_RELOAD_TABLE=4
 * Cache configuration parameters (if not set, default values are used.
 *  arg[5]:
 *		case 1: "error-check": string, return error when load error
 *		case 2: cache block size (in sectors)
 *  arg[6]: cache size (in blocks)
 *  arg[7]: cache associativity
 *  arg[8]: md block size (in sectors)
 *  arg[9]: cache version
 *  arg[10]: cache group uuid for allocated cache
 *
 * =================================================================
 *
 *  WARNING: Three cases use this function:
 *  1. Load a cache (dmc doesn't exist)
 *  2. Reload (expand) a cache (dmc exists)
 *  3. Enable a cache (dmc exists)
 */
// In dummy mode, dmc is already existing and should not be freed
int
flashcache_enable_or_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct cache_c *dmc;
	sector_t i, order;
	int r = 0;
	int persistence = 0;
#ifdef MY_ABC_HERE
	unsigned long long bitmap_size_bit = 0;
#endif
#ifdef MY_ABC_HERE
	unsigned int cache_mode = -1;
	// Already has dummy dmc, just to init it to enable cache
	int dummy_mode_to_enable = 0;
#endif /* MY_ABC_HERE */
	int cache_version = -1;
	int jp_required = 0;
	int procfs_created = 0;

	if (argc < 3) {
		ti->error = "flashcache: Need at least 3 arguments";
		serr("need at least 3 arguments");
		r = -EINVAL;
		goto bad;
	}

#ifdef MY_ABC_HERE
	sdbg(DF_RELOAD_TABLE, "argc=%d", argc);

	if (argc >= 5) {
		if (1 != sscanf(argv[4], "%u", &persistence)) {
			ti->error = "flashcache: Parse psersistence failed";
			serr("Failed to parse persistence %s", argv[4]);
			r = -EINVAL;
			goto bad;
		}
		sdbg(DF_RELOAD_TABLE, "persistence=%d", persistence);
	}

	/*
	 * ########### Reload Table ###########
	 * e.g. volume expansion
	 */
	if (CACHE_RELOAD_TABLE == persistence) {
		r = flashcache_reload_table(ti, argc, argv);
		// Just return r in end
		goto end;
	}
#endif

#ifdef MY_ABC_HERE
	if (sscanf(argv[3], "%u", &cache_mode) != 1) {
		ti->error = "flashcache: sscanf failed, invalid cache mode";
		serr("Failed to parse cache mode: %s", argv[3]);
		r = -EINVAL;
		goto bad;
	}

	if (cache_mode < FLASHCACHE_WRITE_BACK ||
	    cache_mode > FLASHCACHE_DUMMY) {
		ti->error = "flashcache: Invalid cache mode";
		serr("cache_mode = %d", cache_mode);
		r = -EINVAL;
		goto bad;
	}

	sdbg(DF_ONLINE, "persiscence=%d cache_mode=%d", persistence, cache_mode);

	/*
	 * ########### Create Dummy Cache ###########
	 */
	if ((CACHE_CREATE == persistence) && (FLASHCACHE_DUMMY == cache_mode)) {
		// Dummy mode only do here
		r = flashcache_create_dummy(ti, argc, argv);
		goto end;
	}

	/*
	 * ########### Only Enable / Load / Crate Cache go here ###########
	 *
	 * Cases go below:
	 * - Enable RO/RW cache from dummy mode (dmc exists)
	 * - Load RW Cache / Create RO Cache at startup (dmc doesn't exsit )
	 */

	r = job_pool_require();
	if (0 != r) {
		serr("Failed to requre job pool, ret=%d", r);
		goto bad;
	}
	jp_required = 1;
	sprint("Check and init Jobs finish");

	if (NULL != ti->private) {
		// Cases: Enable RO/RW Cache from dummy mode
		sdbg(DF_ONLINE, "in dummy mode");
		dummy_mode_to_enable = 1;

		dmc = ti->private;

		if (CACHE_DISABLED != dmc->cache_state) {
			ti->error = "flashcache: Cache only only be enabled from disabled state";
			serr("Cache enable not from disable state, current state: %d", dmc->cache_state);
			r = -EINVAL;
			goto bad;
		}
	} else {
		// Cases: Load RW Cache or create RO Cache at startup

#endif /* MY_ABC_HERE */
		dmc = vzalloc(sizeof(*dmc));
		if (dmc == NULL) {
			ti->error = "flashcache: Failed to allocate cache context";
			serr("Failed to allocate cache context");
			r = -ENOMEM;
			goto bad;
		}

		dmc->tgt = ti;

#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (!dummy_mode_to_enable) {
		dmc->init_nr_dirty = -1;
	}
#endif /* MY_ABC_HERE */


#ifdef MY_ABC_HERE
	if (!dmc->disk_dev) {
#endif /*MY_ABC_HERE */
#ifdef MY_ABC_HERE
		// Check if bdev is in use
		if ((r = flashcache_get_dev(ti, argv[0], &dmc->disk_dev,
						dmc->disk_devname, ti->len, CHECK_IN_USE))) {
#else
		if ((r = flashcache_get_dev(ti, argv[0], &dmc->disk_dev,
						dmc->disk_devname, ti->len))) {
#endif
			if (r == -EBUSY) {
				ti->error = "flashcache: Disk device is busy, cannot create cache";
				serr("disk device busy");
			} else {
				ti->error = "flashcache: Disk device lookup failed";
				serr("disk device lookup failed");
			}
			goto bad1;
		}
#ifdef MY_ABC_HERE
	}
#endif /*MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if ((r = flashcache_get_dev(ti, argv[1], &dmc->cache_dev,
				    dmc->cache_devname, 0, CHECK_IN_USE))) {
#else
	if ((r = flashcache_get_dev(ti, argv[1], &dmc->cache_dev,
				    dmc->cache_devname, 0))) {
#endif
		if (r == -EBUSY) {
			ti->error = "flashcache: Cache device is busy, cannot create cache";
			serr("cache device busy");
		} else {
			ti->error = "flashcache: Cache device lookup failed";
			serr("cache device lookup failed");
		}
		goto bad2;
	}
#ifdef MY_ABC_HERE
	if (dummy_mode_to_enable) {
		dmc->cache_bdev = dmc->cache_dev->bdev;
	} else {
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
		dmc->disk_bdev = dmc->disk_dev->bdev;
		dmc->cache_bdev = dmc->cache_dev->bdev;
#ifdef MY_ABC_HERE
		if (dmc_id_set(dmc)) {
			serr("generate id failed");
			r = -EINVAL;
			goto bad3;
		}
#else
		snprintf(dmc->id, sizeof(dmc->id), "%s-%s", dmc->disk_devname, dmc->cache_devname);
#endif /* MY_ABC_HERE */
#endif
		if (sscanf(argv[2], "%s", (char *)&dmc->dm_vdevname) != 1) {
			ti->error = "flashcache: Virtual device name lookup failed";
			serr("virtual device name lookup failed: %s", argv[2]);
			r = -EINVAL;
			goto bad3;
		}
#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */

	r = flashcache_kcached_init(dmc);
	if (r) {
		ti->error = "Failed to initialize kcached";
		serr("Failed to init kcached");
		goto bad3;
	}

#ifdef MY_ABC_HERE
	// Move the sscanf above

	dmc->cache_mode = cache_mode;
#else
	if (sscanf(argv[3], "%u", &dmc->cache_mode) != 1) {
		ti->error = "flashcache: sscanf failed, invalid cache mode";
		serr("failed to get cache mode from %s", argv[3]);
		r = -EINVAL;
		goto bad3;
	}
	if (dmc->cache_mode < FLASHCACHE_WRITE_BACK || 
	    dmc->cache_mode > FLASHCACHE_WRITE_AROUND) {
		DMERR("cache_mode = %d", dmc->cache_mode);
		ti->error = "flashcache: Invalid cache mode";
		serr("invalid cache mode %d", dmc->cache_mode);
		r = -EINVAL;
		goto bad3;
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		bitmap_size_bit = compatible_div(get_disk_bdev(dmc)->bd_inode->i_size, PIN_BYTES_PER_BIT);
		dmc->bitmap_size_byte = compatible_div(bitmap_size_bit, 8);
		dmc->bitmap_is_set = 0;
		sdbg(DF_PIN, "allocate bitmap, size=%llu", dmc->bitmap_size_byte);

		dmc->pbitmap_ranges = vmalloc(dmc->bitmap_size_byte);
		if (!dmc->pbitmap_ranges) {
			ti->error = "Can't allocate bitmap ranges";
			serr("Failed to allocate bitmap ranges");
			r = -ENOMEM;
			goto bad3;
		}

		memset(dmc->pbitmap_ranges, 0, dmc->bitmap_size_byte);

		sdbg(DF_PIN, "allocate finish");
	}
#endif

	
	/* 
	 * XXX - Persistence is totally ignored for write through and write around.
	 * Maybe this should really be moved to the end of the param list ?
	 */
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		if (argc >= 5) {
#ifdef MY_ABC_HERE
			// Move ahead
#else
			if (sscanf(argv[4], "%u", &persistence) != 1) {
				ti->error = "flashcache: sscanf failed, invalid cache persistence";
				serr("parse cache persistence failed. persistence: %s", argv[4]);
				r = -EINVAL;
				goto bad3;
			}
#endif
			if (persistence < CACHE_RELOAD || persistence > CACHE_FORCECREATE) {
				ti->error = "flashcache: Invalid cache persistence";
				serr("Invalid cache persistence = %d", persistence);
				r = -EINVAL;
				goto bad3;
			}			
		}
		if (persistence == CACHE_RELOAD) {
			dmc->load_action = LOAD_ERROR_IGNORE;
			if (argc == 6) {
				if (0 == strcmp(argv[5], SZ_LOAD_ERROR_CHECK)) {
					sprint("Enable load check");
					dmc->load_action = LOAD_ERROR_CHECK;
				}
			}
			if (flashcache_writeback_load(dmc)) {
				ti->error = "flashcache: Cache reload failed";
				serr("writeback cache load failed");
				r = -EINVAL;
				goto bad3;
			}
			goto init; /* Skip reading cache parameters from command line */
		}
	} else
		persistence = CACHE_CREATE;

	if (argc >= 6) {
		if (sscanf(argv[5], "%u", &dmc->block_size) != 1) {
			ti->error = "flashcache: Invalid block size";
			serr("Failed to parse block size: %s", argv[5]);
			r = -EINVAL;
			goto bad3;
		}
		if (!dmc->block_size || (dmc->block_size & (dmc->block_size - 1))) {
			ti->error = "flashcache: Invalid block size";
			serr("Invalid block size: %u", dmc->block_size);
			r = -EINVAL;
			goto bad3;
		}
	}
	
	if (!dmc->block_size)
		dmc->block_size = DEFAULT_BLOCK_SIZE;
	dmc->block_shift = ffs(dmc->block_size) - 1;
	dmc->block_mask = dmc->block_size - 1;

	/* dmc->size is specified in sectors here, and converted to blocks later
	 * conversion happens in flashcache_xxx_create() */
	if (argc >= 7) {
		if (sscanf(argv[6], "%llu", (u64 *)&dmc->size) != 1) {
			ti->error = "flashcache: Invalid cache size";
			serr("Failed to parse cache size: %s", argv[6]);
			r = -EINVAL;
			goto bad3;
		}
	}
	
	if (!dmc->size)
		dmc->size = to_sector(get_cache_bdev(dmc)->bd_inode->i_size);

	if (argc >= 8) {
		if (sscanf(argv[7], "%u", &dmc->assoc) != 1) {
			ti->error = "flashcache: Invalid cache associativity";
			serr("Failed to parse associativity: %s", argv[7]);
			r = -EINVAL;
			goto bad3;
		}
		if (!dmc->assoc || (dmc->assoc & (dmc->assoc - 1)) ||
		    dmc->assoc > FLASHCACHE_MAX_ASSOC ||
		    dmc->assoc < FLASHCACHE_MIN_ASSOC ||
		    dmc->size < dmc->assoc) {
			ti->error = "flashcache: Invalid cache associativity";
			serr("Invalid associativity, %u", dmc->assoc);
			r = -EINVAL;
			goto bad3;
		}
	}

	if (!dmc->assoc)
		dmc->assoc = DEFAULT_CACHE_ASSOC;
	dmc->assoc_shift = ffs(dmc->assoc) - 1;

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		if (argc >= 9) {
			if (sscanf(argv[8], "%u", &dmc->md_block_size) != 1) {
				ti->error = "flashcache: Invalid metadata block size";
				serr("Failed to parse md block size: %s", argv[8]);
				r = -EINVAL;
				goto bad3;
			}
			if (!dmc->md_block_size || (dmc->md_block_size & (dmc->md_block_size - 1)) ||
			    dmc->md_block_size > FLASHCACHE_MAX_MD_BLOCK_SIZE) {
				ti->error = "flashcache: Invalid metadata block size";
				serr("invalid md block size: %u", dmc->md_block_size);
				r = -EINVAL;
				goto bad3;
			}
			if (dmc->assoc < 
			    (dmc->md_block_size * 512 / sizeof(struct flash_cacheblock))) {
				ti->error = "flashcache: Please choose a smaller metadata block size or larger assoc";
				serr("Metadata block size too large or assoc too small");
				r = -EINVAL;
				goto bad3;
			}
		}

		if (!dmc->md_block_size)
			dmc->md_block_size = DEFAULT_MD_BLOCK_SIZE;

		/*
		 * Here should check if md block size is large then block device's sector size
		 * (as flashcache_create command does). So use following function instead.
		 */
		if (dmc->md_block_size * 512 < bdev_logical_block_size(get_cache_bdev(dmc))) {
			ti->error = "flashcache: Metadata block size must be >= cache device sector size";
			serr("Metadata block size must be >= cache device sector size");
			r = -EINVAL;
			goto bad3;
		}
	}

	// Load RW cache doesn't go here
	if (argc >= 10) {
		if (sscanf(argv[9], "%d", &cache_version) != 1) {
			ti->error = "flashcache: Invalid cache version parameter";
			serr("Failed to parse cache version: %s", argv[9]);
			r = -EINVAL;
			goto bad3;
		}
	}
	if (!IS_NEW_MODE_VERSION(cache_version)) {
		ti->error = "flashcache: Invalid cache version";
		serr("Invalid cache version: %d", cache_version);
		r = -EINVAL;
		goto bad3;
	}

	sprint("Cache Version: %d", cache_version);
#ifdef MY_ABC_HERE
	dmc->hash_mapping = get_hash_mapping_from_cache_version(cache_version);
	if (-1 == dmc->hash_mapping) {
		serr("Failed to get hash mapping version from cache version %d", cache_version);
		r = -EINVAL;
		goto bad3;
	}
	sprint("Hash mapping = %d", dmc->hash_mapping);
#endif

#ifdef MY_ABC_HERE
	if (argc >= 11) {
		snprintf(dmc->group_uuid, sizeof(dmc->group_uuid), "%s", argv[10]);
	}
#endif

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {	
		if (persistence == CACHE_CREATE) {
			if (flashcache_writeback_create(dmc, cache_version, 0)) {
				ti->error = "flashcache: Cache Create Failed";
				serr("writeback cache create failed");
				r = -EINVAL;
				goto bad3;
			}
		} else {
			if (flashcache_writeback_create(dmc, cache_version, 1)) {
				ti->error = "flashcache: Cache Force Create Failed";
				serr("writeback cache create failed");
				r = -EINVAL;
				goto bad3;
			}
		}
#ifdef MY_ABC_HERE
	} else {
		DMERR("Can handle failure in creation");
		// SYNO: Also for WRITE_AROUND
		if (flashcache_writethrough_create(dmc, cache_version)) {
				ti->error = "flashcache: Cache Create Failed";
				serr("writethough cache create failed");
				r = -EINVAL;
				goto bad3;
		}
	}
#else
	} else
		flashcache_writethrough_create(dmc);
#endif


init:
	dmc->num_sets = dmc->size >> dmc->assoc_shift;
	order = dmc->num_sets * sizeof(struct cache_set);
	dmc->cache_sets = (struct cache_set *)vmalloc(order);
	if (!dmc->cache_sets) {
		ti->error = "Unable to allocate memory";
		serr("Failed to allocate cache sets");
		r = -ENOMEM;
		goto bad3;
	}

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		if ((r = alloc_online_quickflush(dmc))) {
			ti->error = "Unable to allocate fallow bitmap";
			goto bad3;
		}
	}
#endif

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		if ((r = alloc_new_md_update(dmc))) {
			ti->error = "Unable to allocate md updates";
			serr("Failed to allocate md updates");
			goto bad3;
		}
	}
#endif

	for (i = 0 ; i < dmc->num_sets ; i++) {
		dmc->cache_sets[i].set_fifo_next = i * dmc->assoc;
		dmc->cache_sets[i].set_clean_next = i * dmc->assoc;
#ifdef MY_ABC_HERE
#else
		dmc->cache_sets[i].nr_dirty = 0;
		dmc->cache_sets[i].clean_inprog = 0;
		dmc->cache_sets[i].fallow_next_cleaning = jiffies;
		dmc->cache_sets[i].dirty_fallow = 0;
		dmc->cache_sets[i].fallow_tstamp = jiffies;
#endif
		dmc->cache_sets[i].lru_tail = FLASHCACHE_LRU_NULL;
		dmc->cache_sets[i].lru_head = FLASHCACHE_LRU_NULL;
	}

	/* Push all blocks into the set specific LRUs */
	for (i = 0 ; i < dmc->size ; i++) {
		dmc->cache[i].lru_prev = FLASHCACHE_LRU_NULL;
		dmc->cache[i].lru_next = FLASHCACHE_LRU_NULL;
		flashcache_reclaim_lru_movetail(dmc, i);
	}

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		order = (dmc->md_blocks - 1) * sizeof(struct cache_md_block_head);
		dmc->md_blocks_buf = (struct cache_md_block_head *)vmalloc(order);
		if (!dmc->md_blocks_buf) {
			ti->error = "Unable to allocate memory";
			serr("Failed to allocate md_block_buf");
			r = -ENOMEM;
			goto bad3;
		}

		for (i = 0 ; i < dmc->md_blocks - 1 ; i++) {
			dmc->md_blocks_buf[i].nr_in_prog = 0;
			dmc->md_blocks_buf[i].queued_updates = NULL;
		}
	}

#ifdef MY_ABC_HERE
	if (!dummy_mode_to_enable) {
#endif /* MY_ABC_HERE */
		spin_lock_init(&dmc->cache_spin_lock);
#ifdef MY_ABC_HERE
		spin_lock_init(&dmc->reload_lock);
		spin_lock_init(&dmc->dev_lock);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
		init_rwsem(&dmc->ioctl_rwsem);
	}
#endif /* MY_ABC_HERE */

	spin_lock_init(&dmc->endio_spin_lock);

	spin_lock_init(&dmc->bypass_lock);

#ifdef MY_ABC_HERE
	spin_lock_init(&dmc->bitmap_control.lock);
#endif

#ifdef MY_ABC_HERE
#else
	dmc->sync_index = 0;
	dmc->clean_inprog = 0;
#endif

	set_dm_target_attribute(ti, dmc, DM_ATTR_CACHE_ENABLE);

	/* Cleaning Thresholds */
	dmc->sysctl_dirty_thresh = DIRTY_THRESH_DEF;
	dmc->dirty_thresh_set = (dmc->assoc * dmc->sysctl_dirty_thresh) / 100;
	dmc->max_clean_ios_total = MAX_CLEAN_IOS_TOTAL;
	dmc->max_clean_ios_set = MAX_CLEAN_IOS_SET;

	/* Other sysctl defaults */
	dmc->sysctl_io_latency_hist = 0;
	dmc->sysctl_do_sync = 0;
	dmc->sysctl_stop_sync = 0;
	dmc->sysctl_pid_do_expiry = 0;
	dmc->sysctl_max_pids = MAX_PIDS;
	dmc->sysctl_pid_expiry_secs = PID_EXPIRY_SECS;
	// 64KB SSD Cache only support LRU
	dmc->sysctl_reclaim_policy = FLASHCACHE_LRU;
	dmc->sysctl_zerostats = 0;
	dmc->sysctl_error_inject = 0;
	dmc->sysctl_fast_remove = 0;
	dmc->sysctl_cache_all = 1;
	dmc->sysctl_cache_all_input = 1;
	dmc->sysctl_fallow_clean_speed = FALLOW_CLEAN_SPEED;
	dmc->sysctl_fallow_delay = FALLOW_DELAY;
	dmc->sysctl_skip_seq_thresh_kb = SKIP_SEQUENTIAL_THRESHOLD;
	dmc->sysctl_skip_seq_fgap_kb = SKIP_SEQ_DEFAULT_FGAP_KB;
	dmc->sysctl_skip_seq_bgap_kb = SKIP_SEQ_DEFAULT_BGAP_KB;

	/* Latency Diagnose */
	spin_lock_init(&dmc->lat_spin_lock);
	dmc->sysctl_syno_latency_diagnose = 0;
	dmc->sysctl_latency_diagnose_itvl_ms = LAT_INTERVAL_MS;
	dmc->sysctl_io_disk_lat_thres_ms = LAT_HIT_DISK_THRES_MS;
	dmc->sysctl_io_ssd_lat_thres_ms = LAT_HIT_SSD_THRES_MS;
	dmc->sysctl_lat_perc_ten_thousandth = LAT_PERC_TEN_THOUSANDTH;
	dmc->lat_stats_idx = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	timer_setup(&dmc->latency_timer, flashcache_process_lat_stats, 0);
#else
	init_timer(&dmc->latency_timer);
	dmc->latency_timer.function = flashcache_process_lat_stats;
	/* Newer kernel remove data field in timer_list, and pass timer_list
	 * to the handler function. align behavior */
	dmc->latency_timer.data = (unsigned long)&dmc->latency_timer;
#endif

	/* Sequential i/o spotting */	
	for (i = 0; i < SEQUENTIAL_TRACKER_QUEUE_DEPTH; i++) {
		dmc->seq_recent_ios[i].most_recent_sector = 0;
		dmc->seq_recent_ios[i].last_bio_sectors = 0;
		dmc->seq_recent_ios[i].sequential_sectors = 0;
		dmc->seq_recent_ios[i].prev = (struct sequential_io *)NULL;
		dmc->seq_recent_ios[i].next = (struct sequential_io *)NULL;
		seq_io_move_to_lruhead(dmc, &dmc->seq_recent_ios[i]);
	}
	dmc->seq_io_tail = &dmc->seq_recent_ios[0];
	spin_lock_init(&dmc->seq_io_spin_lock);

	(void)wait_on_bit_lock_wrapper(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST,
			       flashcache_wait_schedule, TASK_UNINTERRUPTIBLE);
#ifdef MY_ABC_HERE
	// dmc has been added to cache_list in dummy mode
	if (!dummy_mode_to_enable) {
#endif /* MY_ABC_HERE */
		dmc->next_cache = cache_list_head;
		cache_list_head = dmc;
#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */
	clear_bit(FLASHCACHE_UPDATE_LIST, &flashcache_control->synch_flags);
	smp_mb__after_clear_bit_wrapper();
	wake_up_bit(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST);

	for (i = 0 ; i < dmc->size ; i++) {
		if (dmc->cache[i].cache_state & VALID) {
			atomic64_inc(&dmc->cached_blocks);
		}
		if (dmc->cache[i].cache_state & DIRTY) {
#ifdef MY_ABC_HERE
#else
#ifdef EC_EMULATE_U64_DIVISION
			dmc->cache_sets[div64_u64(i, dmc->assoc)].nr_dirty++;
#else
			dmc->cache_sets[i / dmc->assoc].nr_dirty++;
#endif
#endif /* MY_ABC_HERE */
			dmc->nr_dirty++;
		}
	}

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		init_online_quickflush(&dmc->oqf);
		init_plug_wb(&dmc->plug_wb);
	}
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&dmc->delayed_clean, flashcache_clean_all_sets, dmc);
#else
	INIT_DELAYED_WORK(&dmc->delayed_clean, flashcache_clean_all_sets);
#endif
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		init_new_md_update(dmc);
	}
#endif

	dmc->whitelist_head = NULL;
	dmc->whitelist_tail = NULL;
	dmc->blacklist_head = NULL;
	dmc->blacklist_tail = NULL;
	dmc->num_whitelist_pids = 0;
	dmc->num_blacklist_pids = 0;

	flashcache_ctr_procfs(dmc);
	procfs_created = 1;

#ifdef MY_DEF_HERE
	dmc->max_io_len = 0;
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
	if (!dummy_mode_to_enable) {
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
		if (dmc_list_add(dmc->id, dmc)) {
			serr("Can't add dmc to list");
			r = -EINVAL;
			goto bad3;
		}
#endif
#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	atomic_set(&dmc->num_uncached_write, 0);
	atomic_set(&dmc->num_write_cache, 0);
	atomic_set(&dmc->in_flush, 0);
	atomic_set(&dmc->num_flush_bio, 0);
	init_waitqueue_head(&dmc->wait_flush_queue);
	init_waitqueue_head(&dmc->wait_io_done_queue);

	dmc->disk_region.bdev = get_disk_bdev(dmc);
	dmc->disk_region.sector = 0;
	dmc->disk_region.count = 0;

	dmc->cache_region.bdev = get_cache_bdev(dmc);
	dmc->cache_region.sector = 0;
	dmc->cache_region.count = 0;

#ifdef MY_ABC_HERE
#else
	/* for flush disk bio */
	spin_lock_init(&dmc->disk_flush_lock);
	dmc->disk_flushing = 0;
	dmc->flush_bio = bio_alloc_bioset(GFP_NOIO, 0, pcache_bio_set);
	*bio_bi_sector_ptr(dmc->flush_bio) = 0;
	*bio_bi_size_ptr(dmc->flush_bio) = 0;
	INIT_LIST_HEAD(&dmc->disk_flush_queue1);
	INIT_LIST_HEAD(&dmc->disk_flush_queue2);
	dmc->disk_flush_pending_queue = &dmc->disk_flush_queue1;
	dmc->disk_flush_unused_queue = &dmc->disk_flush_queue2;
	dmc->disk_flush_io_queue = NULL;
	INIT_WORK(&dmc->flush_work, send_disk_flush_bio);
	INIT_WORK(&dmc->disk_flush_io_queue_work, process_disk_flush_io_queue);

	/* for flush cache bio */
	spin_lock_init(&dmc->md_flush_lock);
	dmc->md_flushing = 0;
	dmc->md_flush_bio = bio_alloc_bioset(GFP_NOIO, 0, pcache_bio_set);
	*bio_bi_sector_ptr(dmc->md_flush_bio) = 0;
	*bio_bi_size_ptr(dmc->md_flush_bio) = 0;
	INIT_LIST_HEAD(&dmc->md_flush_queue1);
	INIT_LIST_HEAD(&dmc->md_flush_queue2);
	dmc->md_flush_pending_queue = &dmc->md_flush_queue1;
	dmc->md_flush_unused_queue = &dmc->md_flush_queue2;
	dmc->md_flush_io_queue = NULL;
	INIT_WORK(&dmc->md_flush_work, send_md_flush_bio);
	INIT_WORK(&dmc->md_flush_io_queue_work, process_md_flush_io_queue);
#endif /* ! MY_ABC_HERE */
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (!dummy_mode_to_enable) {
		atomic_set(&dmc->num_master_io, 0);
		init_waitqueue_head(&dmc->wait_for_cache_disable_queue);
		init_waitqueue_head(&dmc->wait_old_ios_completed_queue);
	}
#endif /* MY_ABC_HERE */

#ifdef CONFIG_SYNO_DATA_CORRECTION
	dmc->correction_list = kzalloc(sizeof(struct correction_entry), GFP_KERNEL);
	if (TEST_CORRECTION_LIST_ALLOC_FAIL == global_tester) {
		sprint("Simulate correction list allocate fail");
		global_tester = 0;
		kfree(dmc->correction_list);
		dmc->correction_list = NULL;
	}
	if (!dmc->correction_list) {
		serr("Allocate dmc correction list failed");
		r = -ENOMEM;
		goto bad3;
	}
	INIT_LIST_HEAD(&dmc->correction_list->link);

	spin_lock_init(&dmc->corretion_spin_lock);
#endif /* CONFIG_SYNO_DATA_CORRECTION */

#ifdef MY_ABC_HERE
	dmc->limits.writeback_err = LIMIT_WRITEBACK_ERR;
	dmc->limits.writeback_sync_err = LIMIT_WRITEBACK_SYNC_ERR;
	dmc->limits.do_pending_err = LIMIT_DO_PENDING_ERR;
	dmc->limits.dirty_inval = LIMIT_DIRTY_INVAL;
	dmc->limits.uncached_io = LIMIT_UNCACHED_IO;
	dmc->limits.md_io_error = LIMIT_MD_IO_ERROR;
#endif

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		quickflush_schedule_wb_work(dmc);
	}
#endif

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		/* RO need not to be throttled */
		dmc->new_mu.allow_throtl = 1;
	}
#endif

#ifdef MY_ABC_HERE
	if (NULL == dmc_group_add(dmc)) {
		serr("Failed to add dmc to dmc_group");
		goto bad3;
	}
#endif

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
		cache_schedule_delayed_work(&dmc->new_mu.dwork, dmc->new_mu.sysctl_mu_check_itvl_sec * HZ);
	}
#endif

	// NOTE: most be the last step to do
	down_write(&dmc->ioctl_rwsem);
	dmc->cache_state = CACHE_ENABLED;
	up_write(&dmc->ioctl_rwsem);

	sprint("Create finish");
	r = 0;
end:
	return r;

bad3:
	if (dmc) {
#ifdef MY_ABC_HERE
		if (dmc->pbitmap_ranges) {
			vfree(dmc->pbitmap_ranges);
			dmc->pbitmap_ranges = NULL;
		}
#endif
#ifdef MY_ABC_HERE
		if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
			free_online_quickflush(dmc);
		}
#endif

		if (procfs_created) {
			flashcache_dtr_procfs(dmc);
			procfs_created = 0;
		}

		if (dmc->cache) {
			vfree(dmc->cache);
			dmc->cache = NULL;
		}
		if (dmc->cache_sets) {
			vfree(dmc->cache_sets);
			dmc->cache_sets = NULL;
		}
		if (dmc->md_blocks_buf) {
			vfree(dmc->md_blocks_buf);
			dmc->md_blocks_buf = NULL;
		}
#ifdef CONFIG_SYNO_DATA_CORRECTION
		if (dmc->correction_list) {
			kfree(dmc->correction_list);
			dmc->correction_list = NULL;
		}
#endif
		dm_put_device(ti, dmc->cache_dev);
		if (dummy_mode_to_enable) {
			dmc->cache_dev = NULL;
		}
#ifdef MY_ABC_HERE
#else
		if (dmc->flush_bio) {
			bio_put(dmc->flush_bio);
			dmc->flush_bio = NULL; // should be freed, set it to NULL
		}
		if (dmc->md_flush_bio) {
			bio_put(dmc->md_flush_bio);
			dmc->md_flush_bio = NULL; // should be freed, set it to NULL
		}
#endif
#ifdef MY_ABC_HERE
		if (dmc->dmcg) {
#ifdef MY_ABC_HERE
			if (dmc->new_mu.inited) {
				syno_mu_disable_dmcg_throtl(dmc);
			}
#endif
			dmc_group_remove(dmc);
		}
#endif

#ifdef MY_ABC_HERE
		if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
			free_new_md_update(&dmc->new_mu);
		}
#endif
		if (dmc->node && !dummy_mode_to_enable) {
			dmc_list_remove(dmc->id, dmc);
		}
	}
bad2:
	if (dmc) {
#ifdef MY_ABC_HERE
		if (dummy_mode_to_enable) {
			// no need to free disk dev
		} else {
			dm_put_device(ti, dmc->disk_dev);
		}
#else
		dm_put_device(ti, dmc->disk_dev);
#endif /* MY_ABC_HERE */
	}
bad1:
	if (dmc) {
#ifdef MY_ABC_HERE
		if (dummy_mode_to_enable) {
			reset_dmc_fields_for_dummy_mode(dmc);
			dmc->cache_mode = FLASHCACHE_DUMMY;
		} else {
			vfree(dmc);
		}
#else
		kfree(dmc);
#endif /* MY_ABC_HERE */
	}
bad:
	if (jp_required) {
		job_pool_release();
	}
	serr("Failed to create or init dmc");
	VERIFY_WARN(r);
	return r;
}

static void
flashcache_dtr_stats_print(struct cache_c *dmc)
{
	int read_hit_pct, write_hit_pct, dirty_write_hit_pct;
	struct flashcache_stats *stats = &dmc->flashcache_stats;
	struct flashcache_errors *errors = &dmc->flashcache_errors;
	u_int64_t  cache_pct, dirty_pct;
	char *cache_mode;
	int i;

	if (atomic64_read(&stats->reads) > 0)
		read_hit_pct = compatible_div(atomic64_read(&stats->read_hits) * 100, atomic64_read(&stats->reads));
	else
		read_hit_pct = 0;
	if (atomic64_read(&stats->writes) > 0) {
		write_hit_pct = compatible_div(atomic64_read(&stats->write_hits) * 100, atomic64_read(&stats->writes));
		dirty_write_hit_pct = compatible_div(atomic64_read(&stats->dirty_write_hits) * 100, atomic64_read(&stats->writes));
	} else {
		write_hit_pct = 0;
		dirty_write_hit_pct = 0;
	}
	
	DMINFO("stats: \n\treads(%lld), writes(%lld)",
			(long long)atomic64_read(&stats->reads),
			(long long)atomic64_read(&stats->writes));

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		DMINFO("\tread hits(%lld), read hit percent(%d)\n"	\
		       "\twrite hits(%lld) write hit percent(%d)\n"	\
		       "\tdirty write hits(%lld) dirty write hit percent(%d)\n" \
		       "\treplacement(%lld), write replacement(%lld)\n"	\
		       "\twrite invalidates(%lld), read invalidates(%lld)\n" ,
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->write_hits), write_hit_pct,
		       (long long)atomic64_read(&stats->dirty_write_hits), dirty_write_hit_pct,
		       (long long)atomic64_read(&stats->replace), (long long)atomic64_read(&stats->wr_replace),
		       (long long)atomic64_read(&stats->wr_invalidates), (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMINFO("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMINFO("\tpending enqueues(%lld), pending inval(%lld)\n"	\
		       "\tmetadata dirties(%lld), metadata cleans(%lld)\n" \
		       "\tmetadata batch(%lld) metadata ssd writes(%lld)\n" \
		       "\tcleanings(%lld) fallow cleanings(%lld)\n"	\
		       "\tno room(%lld) front merge(%lld) back merge(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->md_write_dirty), (long long)atomic64_read(&stats->md_write_clean),
		       (long long)atomic64_read(&stats->md_write_batch), (long long)atomic64_read(&stats->md_ssd_writes),
		       (long long)atomic64_read(&stats->cleanings), (long long)atomic64_read(&stats->fallow_cleanings),
		       (long long)atomic64_read(&stats->noroom), (long long)atomic64_read(&stats->front_merge), (long long)atomic64_read(&stats->back_merge));
	} else if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
		DMINFO("\tread hits(%lld), read hit percent(%d)\n"	\
		       "\twrite hits(%lld) write hit percent(%d)\n"	\
		       "\treplacement(%lld)\n"				\
		       "\twrite invalidates(%lld), read invalidates(%lld)\n",
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->write_hits), write_hit_pct,
		       (long long)atomic64_read(&stats->replace),
		       (long long)atomic64_read(&stats->wr_invalidates), (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMINFO("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMINFO("\tpending enqueues(%lld), pending inval(%lld)\n"	\
		       "\tno room(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->noroom));
	} else 	{	/* WRITE_AROUND */
		DMINFO("\tread hits(%lld), read hit percent(%d)\n"	\
		       "\treplacement(%lld)\n"				\
		       "\tinvalidates(%lld)\n",
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->replace),
		       (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMINFO("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMINFO("\tpending enqueues(%lld), pending inval(%lld)\n"	\
		       "\tno room(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->noroom));
	}
	/* All modes */
        DMINFO("\tdisk reads(%lld), disk writes(%lld) ssd reads(%lld) ssd writes(%lld)\n" \
               "\tuncached reads(%lld), uncached writes(%lld), uncached IO requeue(%lld)\n" \
           "\tdisk read errors(%d), disk write errors(%d) ssd read errors(%d) ssd write errors(%d)\n" \
           "\tuncached sequential reads(%lld), uncached sequential writes(%lld)\n" \
               "\tpid_adds(%lld), pid_dels(%lld), pid_drops(%lld) pid_expiry(%lld)",
               (long long)atomic64_read(&stats->disk_reads),
               (long long)atomic64_read(&stats->disk_writes),
               (long long)atomic64_read(&stats->ssd_reads), (long long)atomic64_read(&stats->ssd_writes),
               (long long)atomic64_read(&stats->uncached_reads), (long long)atomic64_read(&stats->uncached_writes), (long long)atomic64_read(&stats->uncached_io_requeue),
               atomic_read(&errors->disk_read_errors), atomic_read(&errors->disk_write_errors),
	       atomic_read(&errors->ssd_read_errors), atomic_read(&errors->ssd_write_errors),
               (long long)atomic64_read(&stats->uncached_sequential_reads),
               (long long)atomic64_read(&stats->uncached_sequential_writes),
               (long long)atomic64_read(&stats->pid_adds), (long long)atomic64_read(&stats->pid_dels), (long long)atomic64_read(&stats->pid_drops), (long long)atomic64_read(&stats->expiry));
#ifdef MY_ABC_HERE
		DMINFO("\n\twrite miss ssd(%lld)",
			   (long long)atomic64_read(&stats->wr_miss_ssd));
#endif
	if (dmc->size > 0) {
#ifdef EC_EMULATE_U64_DIVISION
		dirty_pct = div64_u64((u_int64_t)dmc->nr_dirty * 100, dmc->size);
		cache_pct = div64_u64((u_int64_t)atomic64_read(&dmc->cached_blocks) * 100, dmc->size);
#else
		dirty_pct = ((u_int64_t)dmc->nr_dirty * 100) / dmc->size;
		cache_pct = ((u_int64_t)atomic64_read(&dmc->cached_blocks) * 100) / dmc->size;
#endif
	} else {
		cache_pct = 0;
		dirty_pct = 0;
	}
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK)
		cache_mode = "WRITE_BACK";
	else if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH)
		cache_mode = "WRITE_THROUGH";
	else
		cache_mode = "WRITE_AROUND";
	DMINFO("conf:\n"						\
	       "\tvirt dev (%s), ssd dev (%s), disk dev (%s) cache mode(%s)\n"		\
	       "\tcapacity(%lluM), associativity(%u), data block size(%uK) metadata block size(%ub)\n" \
	       "\tskip sequential thresh(%uK)\n" \
	       "\ttotal blocks(%llu), cached blocks(%lld), cache percent(%d)\n" \
	       "\tdirty blocks(%d), dirty percent(%d)\n",
	       dmc->dm_vdevname, dmc->cache_devname, dmc->disk_devname,
	       cache_mode,
	       (u64)dmc->size*dmc->block_size>>11, dmc->assoc,
	       dmc->block_size>>(10-SECTOR_SHIFT), 
	       dmc->md_block_size * 512, 
	       dmc->sysctl_skip_seq_thresh_kb,
	       (u64)dmc->size, (long long)atomic64_read(&dmc->cached_blocks), 
	       (int)cache_pct, dmc->nr_dirty, (int)dirty_pct);

	DMINFO("\tnr_queued(%lu)\n", dmc->pending_jobs_count);
	DMINFO("Size Hist: ");
#ifdef MY_ABC_HERE
	for (i = 1 ; i <= ((sizeof(size_hist)/sizeof(size_hist[0])) - 1) ; i++) {
		if (size_hist[i] > 0) {
			DMINFO("%d:%llu ", i*512, size_hist[i]);
		}
	}
#else
	for (i = 1 ; i <= 32 ; i++) {
		if (size_hist[i] > 0)
			DMINFO("%d:%llu ", i*512, size_hist[i]);
	}
#endif
}

#ifdef MY_ABC_HERE
fmode_t get_disk_mode(struct cache_c *dmc)
{
	unsigned long flags = 0;
	fmode_t mode = 0;

	// Use lock to prevent the dmc->disk_dev is reallocated during expansion
	spin_lock_irqsave(&dmc->dev_lock, flags);
	mode = dmc->disk_dev->mode;
	spin_unlock_irqrestore(&dmc->dev_lock, flags);

	return mode;
}

/*
 * WARNING:
 * disk_dev / cache_dev might be replaced on expansion
 * Use get_cache_dev() / get_disk_bdev() instead of disk_dev->bdev
 */
struct block_device *get_cache_bdev(struct cache_c *dmc)
{
	return dmc->cache_bdev;
}

struct block_device *get_disk_bdev(struct cache_c *dmc)
{
	return dmc->disk_bdev;
}

#endif


#ifdef MY_ABC_HERE
static void
handle_table_reload_at_dtr(struct dm_target *ti)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	unsigned long flags = 0;
	struct dm_dev *ori_disk_dev = NULL;
	struct dm_dev *ori_cache_dev = NULL;

	sdbg(DF_RELOAD_TABLE, "In reloading table, don't destory anything");

	ori_disk_dev = dmc->disk_dev;
	ori_cache_dev = dmc->cache_dev;

	spin_lock_irqsave(&dmc->dev_lock, flags);
	// Just put devices for old dm_target
	// Set to new dm_devs
	dmc->disk_dev = dmc->disk_dev_reload;
	dmc->disk_dev_reload = NULL;
#ifdef MY_ABC_HERE
	if (CACHE_DISABLED != dmc->cache_state) {
		dmc->cache_dev = dmc->cache_dev_reload;
		dmc->cache_dev_reload = NULL;
	}
#else
	dmc->cache_dev = dmc->cache_dev_reload;
	dmc->cache_dev_reload = NULL;
#endif
	spin_unlock_irqrestore(&dmc->dev_lock, flags);

	dm_put_device(ti, ori_disk_dev);
	if (CACHE_DISABLED != dmc->cache_state) {
		dm_put_device(ti, ori_cache_dev);
	}

	dmc->disk_devsize = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);

	// Finish the reload operation
	sdbg(DF_RELOAD_TABLE, "Set dmc->reload_table = 0");
	spin_lock_irqsave(&dmc->reload_lock, flags);
	dmc->reload_table = 0;
	spin_unlock_irqrestore(&dmc->reload_lock, flags);
	sdbg(DF_RELOAD_TABLE, "Reloading finish");
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static void
destroy_in_dummy_mode(struct dm_target *ti)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	struct cache_c **nodepp;

	// Only occupy disk_dev & allocate dmc in this mode
	dm_put_device(ti, dmc->disk_dev);

	(void)wait_on_bit_lock_wrapper(&flashcache_control->synch_flags,
				   FLASHCACHE_UPDATE_LIST,
				   flashcache_wait_schedule,
				   TASK_UNINTERRUPTIBLE);
	// SYNO: Remove cache from the cache list
	nodepp = &cache_list_head;
	while (*nodepp != NULL) {
		if (*nodepp == dmc) {
			*nodepp = dmc->next_cache;
			break;
		}
		nodepp = &((*nodepp)->next_cache);
	}
	clear_bit(FLASHCACHE_UPDATE_LIST, &flashcache_control->synch_flags);
	smp_mb__after_clear_bit_wrapper();
	wake_up_bit(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST);

	if (dmc_list_remove(dmc->id, dmc)) {
		serr("Can't find dmc with id %s to remove", dmc->id);
	}

	vfree(dmc);
}

static void
reset_dmc_fields_for_dummy_mode(struct cache_c *dmc)
{
	// Reset data starting at the reset point
	memset(&dmc->point_to_reset, 0, sizeof(struct cache_c) - ((unsigned char *)&dmc->point_to_reset - (unsigned char *)dmc));

	// Reset ssd dev name
	snprintf(dmc->cache_devname, sizeof(dmc->cache_devname), "none");
	dmc->init_nr_dirty = -1;
}

static void
free_dmc_resources_for_disable(struct dm_target *ti, disable_type_t disable_type)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	int i = 0;
	int nr_queued = 0;

	if (DISABLE_FORCE != disable_type) {
		VERIFY_WARN(0 == dmc->nr_dirty);
	}

	dmc->sysctl_syno_latency_diagnose = 0;
	del_timer_sync(&dmc->latency_timer);

	// Destroy proc & sysctl
	flashcache_dtr_procfs(dmc);

	// Partial Steps in destory()
	DMINFO("cache jobs %d, pending jobs %d", atomic_read(&nr_cache_jobs),
	       atomic_read(&nr_pending_jobs));
	for (i = 0 ; i < dmc->size ; i++)
		nr_queued += dmc->cache[i].nr_queued;
	DMINFO("cache queued jobs %d", nr_queued);
	flashcache_dtr_stats_print(dmc);

#ifdef MY_ABC_HERE
	dmc_group_remove(dmc);
#endif

	// Free dynamic resource
	vfree((void *)dmc->cache);
	vfree((void *)dmc->cache_sets);

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
#ifdef MY_ABC_HERE
		free_online_quickflush(dmc);
#endif
#ifdef MY_ABC_HERE
		free_new_md_update(&dmc->new_mu);
#endif
		vfree((void *)dmc->md_blocks_buf);
	}

	flashcache_del_all_pids(dmc, FLASHCACHE_WHITELIST, 1);
	flashcache_del_all_pids(dmc, FLASHCACHE_BLACKLIST, 1);
	VERIFY(dmc->num_whitelist_pids == 0);
	VERIFY(dmc->num_blacklist_pids == 0);

#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (dmc->correction_list) {
		kfree(dmc->correction_list);
		dmc->correction_list = NULL;
	}
#endif /* CONFIG_SYNO_DATA_CORRECTION */

	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
#ifdef MY_ABC_HERE
		if (dmc->pbitmap_ranges) {
			vfree(dmc->pbitmap_ranges);
		}
#endif
	}

	// Only release cache device
	dm_put_device(ti, dmc->cache_dev);

#ifdef MY_ABC_HERE
#else
	bio_put(dmc->flush_bio);
	bio_put(dmc->md_flush_bio);
#endif /* MY_ABC_HERE */

	reset_dmc_fields_for_dummy_mode(dmc);

	// No need to remove dmc from dmc_list
}
#endif /* MY_ABC_HERE */

/*
 * Destroy the cache mapping.
 */
void
flashcache_dtr(struct dm_target *ti)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	struct cache_c **nodepp;
	int i;
	int nr_queued = 0;

#ifdef MY_ABC_HERE
	if (dmc->reload_table) {
		handle_table_reload_at_dtr(ti);
		return;
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	// TODO: Handle disable / enable with shutdown at the same time?
	if ((CACHE_ENABLED != dmc->cache_state) && (CACHE_DISABLED != dmc->cache_state)) {
		serr("Cache state (%d) is not in enabled or disabled states, can't destroy cache mapping",
				dmc->cache_state);
		return;
	}

	if (CACHE_DISABLED == dmc->cache_state) {
		destroy_in_dummy_mode(ti);
		return;
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	// Let the proc entry be removed later for showing the flush progress
#else
	flashcache_dtr_procfs(dmc);
#endif


	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		sprint("sync for remove");
		flashcache_sync_for_remove(dmc);
#ifdef MY_ABC_HERE
		if (1 == dmc->bypass_cache) {
			DMERR("flashcache: Due to the cache is crashed, don't write back metadata to it");
		} else {
#ifdef MY_ABC_HERE
			/* Clear all md update in progress */
			syno_mu_flush_all_for_remove_sync(dmc);
#endif
			sprint("save metadata to ssd");
			flashcache_writeback_md_store(dmc);
		}
#else
		flashcache_writeback_md_store(dmc);
#endif
	}

	dmc->sysctl_syno_latency_diagnose = 0;
	del_timer_sync(&dmc->latency_timer);

#ifdef MY_ABC_HERE
	sprint("destroy proc entries and status");
	flashcache_dtr_procfs(dmc);
#endif

	if (!dmc->sysctl_fast_remove && dmc->nr_dirty > 0)
		DMERR("Could not sync %d blocks to disk, cache still dirty", 
		      dmc->nr_dirty);
	DMINFO("cache jobs %d, pending jobs %d", atomic_read(&nr_cache_jobs), 
	       atomic_read(&nr_pending_jobs));
	for (i = 0 ; i < dmc->size ; i++)
		nr_queued += dmc->cache[i].nr_queued;
	DMINFO("cache queued jobs %d", nr_queued);	

	flashcache_dtr_stats_print(dmc);
#ifdef MY_ABC_HERE
	dmc_group_remove(dmc);
#endif

	vfree((void *)dmc->cache);
	vfree((void *)dmc->cache_sets);
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK)
		vfree((void *)dmc->md_blocks_buf);
	flashcache_del_all_pids(dmc, FLASHCACHE_WHITELIST, 1);
	flashcache_del_all_pids(dmc, FLASHCACHE_BLACKLIST, 1);
	VERIFY_WARN(dmc->num_whitelist_pids == 0);
	VERIFY_WARN(dmc->num_blacklist_pids == 0);
	sprint("start to put devices");
	dm_put_device(ti, dmc->disk_dev);
	dm_put_device(ti, dmc->cache_dev);

	sprint("wait on synch flags");
	(void)wait_on_bit_lock_wrapper(&flashcache_control->synch_flags,
				   FLASHCACHE_UPDATE_LIST,
				   flashcache_wait_schedule,
				   TASK_UNINTERRUPTIBLE);
	// SYNO: Remove cache from the cache list
	nodepp = &cache_list_head;
	while (*nodepp != NULL) {
		if (*nodepp == dmc) {
			*nodepp = dmc->next_cache;
			break;
		}
		nodepp = &((*nodepp)->next_cache);
	}
	sprint("continue to destroy");
	clear_bit(FLASHCACHE_UPDATE_LIST, &flashcache_control->synch_flags);
	smp_mb__after_clear_bit_wrapper();
	wake_up_bit(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST);


#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
#ifdef MY_ABC_HERE
		free_online_quickflush(dmc);
#endif
#ifdef MY_ABC_HERE
		free_new_md_update(&dmc->new_mu);
#endif
		vfree(dmc->pbitmap_ranges);
	}
#endif
#ifdef MY_ABC_HERE
	if (dmc_list_remove(dmc->id, dmc)) {
		serr("Can't find dmc with id %s to remove", dmc->id);
	}
#endif

#ifdef MY_ABC_HERE
#else
#ifdef MY_ABC_HERE
	bio_put(dmc->flush_bio);
	bio_put(dmc->md_flush_bio);
#endif
#endif
	vfree(dmc);
#ifdef MY_ABC_HERE
	job_pool_release();
#endif
	sprint("Destroy finish");
}

void
flashcache_status_info(struct cache_c *dmc, status_type_t type,
		       char *result, unsigned int maxlen)
{
	int read_hit_pct, write_hit_pct, dirty_write_hit_pct;
	int sz = 0; /* DMEMIT */
	struct flashcache_stats *stats = &dmc->flashcache_stats;
	struct flashcache_errors *errors = &dmc->flashcache_errors;
#ifdef MY_ABC_HERE
	plug_wb_t *plug_wb = &dmc->plug_wb;
#endif
#ifdef MY_ABC_HERE
	new_mu_t *mu = &dmc->new_mu;
#endif

	if (atomic64_read(&stats->reads) > 0)
		read_hit_pct = compatible_div(atomic64_read(&stats->read_hits) * 100, atomic64_read(&stats->reads));
	else
		read_hit_pct = 0;
	if (atomic64_read(&stats->writes) > 0) {
		write_hit_pct = compatible_div(atomic64_read(&stats->write_hits) * 100, atomic64_read(&stats->writes));
		dirty_write_hit_pct = compatible_div(atomic64_read(&stats->dirty_write_hits) * 100, atomic64_read(&stats->writes));
	} else {
		write_hit_pct = 0;
		dirty_write_hit_pct = 0;
	}
	DMEMIT("stats: \n\treads(%lld), writes(%lld)\n",
		   (long long)atomic64_read(&stats->reads),
		   (long long)atomic64_read(&stats->writes));

	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		DMEMIT("\tread hits(%lld), read hit percent(%d)\n"
		       "\twrite hits(%lld) write hit percent(%d)\n"
		       "\tdirty write hits(%lld) dirty write hit percent(%d)\n"
		       "\treplacement(%lld), write replacement(%lld)\n"
		       "\twrite invalidates(%lld), read invalidates(%lld)\n",
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->write_hits), write_hit_pct,
		       (long long)atomic64_read(&stats->dirty_write_hits), dirty_write_hit_pct,
		       (long long)atomic64_read(&stats->replace), (long long)atomic64_read(&stats->wr_replace),
		       (long long)atomic64_read(&stats->wr_invalidates), (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMEMIT("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMEMIT("\tpending enqueues(%lld), pending inval(%lld)\n"
		       "\tmetadata dirties(%lld), metadata cleans(%lld)\n"
		       "\tmetadata batch(%lld) metadata ssd writes(%lld)\n"
		       "\tcleanings(%lld) fallow cleanings(%lld)\n"
		       "\tno room(%lld) front merge(%lld) back merge(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->md_write_dirty), (long long)atomic64_read(&stats->md_write_clean),
		       (long long)atomic64_read(&stats->md_write_batch), (long long)atomic64_read(&stats->md_ssd_writes),
		       (long long)atomic64_read(&stats->cleanings), (long long)atomic64_read(&stats->fallow_cleanings),
		       (long long)atomic64_read(&stats->noroom), (long long)atomic64_read(&stats->front_merge), (long long)atomic64_read(&stats->back_merge));
	} else if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
		DMEMIT("\tread hits(%lld), read hit percent(%d)\n"
		       "\twrite hits(%lld) write hit percent(%d)\n"
		       "\treplacement(%lld), write replacement(%lld)\n"
		       "\twrite invalidates(%lld), read invalidates(%lld)\n",
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->write_hits), write_hit_pct,
		       (long long)atomic64_read(&stats->replace), (long long)atomic64_read(&stats->wr_replace),
		       (long long)atomic64_read(&stats->wr_invalidates), (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMEMIT("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMEMIT("\tpending enqueues(%lld), pending inval(%lld)\n"
		       "\tno room(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->noroom));
	} else {	/* WRITE_AROUND */
		DMEMIT("\tread hits(%lld), read hit percent(%d)\n"
		       "\treplacement(%lld), write replacement(%lld)\n"
		       "\tinvalidates(%lld)\n",
		       (long long)atomic64_read(&stats->read_hits), read_hit_pct,
		       (long long)atomic64_read(&stats->replace), (long long)atomic64_read(&stats->wr_replace),
		       (long long)atomic64_read(&stats->rd_invalidates));
#ifdef FLASHCACHE_DO_CHECKSUMS
		DMEMIT("\tchecksum store(%lld), checksum valid(%lld), checksum invalid(%lld)\n",
		       (long long)atomic64_read(&stats->checksum_store), (long long)atomic64_read(&stats->checksum_valid), (long long)atomic64_read(&stats->checksum_invalid));
#endif
		DMEMIT("\tpending enqueues(%lld), pending inval(%lld)\n"
		       "\tno room(%lld)\n",
		       (long long)atomic64_read(&stats->enqueues), (long long)atomic64_read(&stats->pending_inval),
		       (long long)atomic64_read(&stats->noroom));
	}
	/* All modes */
	DMEMIT("\tdisk reads(%lld), disk writes(%lld) ssd reads(%lld) ssd writes(%lld)\n"
	       "\tuncached reads(%lld), uncached writes(%lld), uncached IO requeue(%lld)\n"
	       "\tdisk read errors(%d), disk write errors(%d) ssd read errors(%d) ssd write errors(%d)\n"
	       "\tuncached sequential reads(%lld), uncached sequential writes(%lld)\n"
	       "\tpid_adds(%lld), pid_dels(%lld), pid_drops(%lld) pid_expiry(%lld)",
	       (long long)atomic64_read(&stats->disk_reads),
	       (long long)atomic64_read(&stats->disk_writes),
	       (long long)atomic64_read(&stats->ssd_reads), (long long)atomic64_read(&stats->ssd_writes),
	       (long long)atomic64_read(&stats->uncached_reads), (long long)atomic64_read(&stats->uncached_writes), (long long)atomic64_read(&stats->uncached_io_requeue),
	       atomic_read(&errors->disk_read_errors), atomic_read(&errors->disk_write_errors),
	       atomic_read(&errors->ssd_read_errors), atomic_read(&errors->ssd_write_errors),
	       (long long)atomic64_read(&stats->uncached_sequential_reads),
	       (long long)atomic64_read(&stats->uncached_sequential_writes),
	       (long long)atomic64_read(&stats->pid_adds), (long long)atomic64_read(&stats->pid_dels), (long long)atomic64_read(&stats->pid_drops), (long long)atomic64_read(&stats->expiry));
#ifdef MY_ABC_HERE
	DMEMIT("\n\twrite miss ssd(%lld)",
		   (long long)atomic64_read(&stats->wr_miss_ssd));
#endif
#ifdef MY_ABC_HERE
	DMEMIT("\n\tdirty writeback kb(%lld), dirty writeback sync kb(%lld)",
		   compatible_div((long long)atomic64_read(&stats->dirty_writeback_sector), 2),
		   compatible_div((long long)atomic64_read(&stats->dirty_writeback_sync_sector), 2));
#endif
#ifdef MY_ABC_HERE
		DMEMIT("\n\t==== Internal Diagnosis ====\n"
			   "\tRead hit statistics:\n"
			   "\tpending preread(%lld)\n"
			   "\tmatch none read disk(%lld) busy in io(%lld) busy wait queue(%lld)\n"
			   "\tmatch partial: total(%lld) check write ssd(%lld) \n"
			   "\tWrite hit statistics: \n"
			   "\tbusy inval(%lld)\n"
			   "\tdo pending write back(%lld)\n"
			   "\tmap skip_unaligned_io(%lld)\n"
			   "\tdirty_writeback(%lld)\n"
#ifdef MY_ABC_HERE
#else
			   "\tcleanings_over_threshold(%lld)\n"
			   "\tcleanings_total(%lld)\n"
#endif
			   "\tprereads(%lld)\n"
			   "\tcurrent job_mem_cnt(%d) job mem get in(%lld) job mem get out(%lld)\n",
			   (long long)atomic64_read(&stats->pending_preread),
			   (long long)atomic64_read(&stats->read_hit_match_none_read_disk),
			   (long long)atomic64_read(&stats->read_hit_busy_in_io), (long long)atomic64_read(&stats->read_hit_busy_wait_queue),
			   (long long)atomic64_read(&stats->read_hit_match_partial),
			   (long long)atomic64_read(&stats->read_hit_check_write_ssd),
			   (long long)atomic64_read(&stats->write_hit_busy_inval),
			   (long long)atomic64_read(&stats->do_pending_no_error_drity_writeback),
			   (long long)atomic64_read(&stats->skip_unaligned_io),
			   (long long)atomic64_read(&stats->dirty_writeback_start), // Do not remove to keep format
#ifdef MY_ABC_HERE
#else
			   (long long)atomic64_read(&stats->cleanings_over_threshold),
			   (long long)atomic64_read(&stats->clean_set_ios),
#endif
			   (long long)atomic64_read(&stats->prereads),
			   job_mem_count, (long long)atomic64_read(&stats->job_mem_get_in),
			   (long long)atomic64_read(&stats->job_mem_get_out)
			   );
#endif
#ifdef MY_ABC_HERE
		DMEMIT("\tnum_uncached_write(%d) num_write_cache(%d) num_flush_bio(%d)\n"
			"\tinval dirty writeback(%lld)\n"
			"\twrite miss invalidate(%lld)\n"
			"\tmap_inval(%lld) uncacheable_inval(%lld) do_pending_no_error_inval(%lld)\n"
			"\tpending enqueue inval(%lld) pending enqueue inval handled(%lld)\n"
			"\tdisk flush start(%lld) disk flush done(%lld)\n"
			"\tcache flush start(%lld) cache flush done(%lld)\n"
			"\tdirty writeback start(%lld) dirty writeback done(%lld)\n"
			"\tmaster bio in processing(%d)\n",
			atomic_read(&dmc->num_uncached_write),
			atomic_read(&dmc->num_write_cache),
			atomic_read(&dmc->num_flush_bio),
			(long long)atomic64_read(&stats->inval_dirty_writeback),
			(long long)atomic64_read(&stats->write_miss_inval),
			(long long)atomic64_read(&stats->map_inval), (long long)atomic64_read(&stats->uncacheable_inval),
			(long long)atomic64_read(&stats->do_pending_no_error_inval),
			(long long)atomic64_read(&stats->pending_enqueue_inval_start),
			(long long)atomic64_read(&stats->pending_enqueue_inval_done),
			(long long)atomic64_read(&stats->disk_flush_start),
			(long long)atomic64_read(&stats->disk_flush_done),
			(long long)atomic64_read(&stats->md_flush_start),
			(long long)atomic64_read(&stats->md_flush_done),
			(long long)atomic64_read(&stats->dirty_writeback_start),
			(long long)atomic64_read(&stats->dirty_writeback_done),
			atomic_read(&dmc->num_master_io));
#ifdef MY_ABC_HERE
		DMEMIT("\tfallow blocks(%d) fallow bits unset(%lld) nr writes(%lld)\n"
			"\tbits mismatch(%lld) jobs(%d)\n"
			"\tssd wb in progress(%d) disk wb in progress(%d)\n"
			"\twb seq(%llu) batch head(%llu) batch len(%d) unfinished read in batch(%d)\n"
			"\toqf trigger count(%d) sync trigger count(%d) synced count(%d)\n"
#ifdef MY_ABC_HERE
			"\tmd io inprog normal(%d) force(%d) flush(%d) throtl(%d)\n"
			"\tmd io sent normal(%d force(%d) flush(%d) throtl(%d)\n"
			"\tsend md io cnt(%d) md io callback cnt(%d)\n"
			"\tsend md io in queue(%d) md io to queue(%d)\n"
			"\tmu flush valid(%d) mu flush pending(%d)\n"
			"\tmu no flush valid(%d) mu no flush pending(%d) mu io waiting (%d)\n"
			"\tmu throtl self(%d) mu throtl help(%d)\n"
			"\tdmcg rebalance cnt(%d) dmcg num io(%d) dmcg io pending(%d)\n"
#endif
			,
			atomic_read(&dmc->oqf.nr_fallow),
			(long long) atomic64_read(&stats->qf_fallow_bits_unset),
			(long long) atomic64_read(&stats->qf_nr_writes),
			(long long) atomic64_read(&stats->qf_bits_mismatch),
			atomic_read(&dmc->nr_jobs),
			plug_wb->ssd_read_inprog, plug_wb->disk_write_inprog,
			plug_wb->seq, plug_wb->cur_batch_head, plug_wb->cur_batch_len, plug_wb->unfinished_read_in_batch,
			dmc->oqf.trigger_cnt, dmc->oqf.sync_trigger_cnt, dmc->nr_synced
#ifdef MY_ABC_HERE
			,
			mu->md_io_inprog_normal, mu->md_io_inprog_force,
			mu->md_io_inprog_ext_flush, mu->md_io_inprog_throtl,
			mu->sent_md_io_normal_cnt, mu->sent_md_io_force_cnt,
			mu->sent_md_io_ext_flush_cnt, mu->sent_md_io_throtl_cnt,
			atomic_read(&mu->send_md_io_cnt), atomic_read(&mu->md_io_callback_cnt),
			atomic_read(&mu->md_io_in_queue), atomic_read(&mu->md_io_to_queue),
			mu->inprog_blk_num[MU_BLK_TYPE_FLUSH], mu->pending_blk_num[MU_BLK_TYPE_FLUSH],
			mu->inprog_blk_num[MU_BLK_TYPE_NOFLUSH], mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH],
			mu->md_io_pending_queue_size,
			atomic_read(&mu->throtl_self), atomic_read(&mu->throtl_help),
			dmc->dmcg ? atomic_read(&dmc->dmcg->rebalance_cnt) : 0, // no dmcg for dummy cache
			dmc->dmcg ? atomic_read(&dmc->dmcg->num_throtl_io) : 0,
			dmc->dmcg ? atomic_read(&dmc->dmcg->md_io_pending_no_flush) : 0
#endif
			);
#else
		// FIXME: should be removed when original wb is removed
		DMEMIT("\tclean in progress(%d)\n",
			dmc->clean_inprog);
#endif
#endif

	if (dmc->sysctl_io_latency_hist) {
		int i;
		
		DMEMIT("\nIO Latency Histogram: \n");
		for (i = 1 ; i <= IO_LATENCY_BUCKETS ; i++) {
			DMEMIT("< %d\tusecs : %lu\n", i * IO_LATENCY_GRAN_USECS, dmc->latency_hist[i - 1]);
		}
		DMEMIT("> 10\tmsecs : %lu", dmc->latency_hist_10ms);		
	}
}

static void
flashcache_status_table(struct cache_c *dmc, status_type_t type,
			     char *result, unsigned int maxlen)
{
	u_int64_t  cache_pct, dirty_pct;
	int i;
	int sz = 0; /* DMEMIT */
#ifdef MY_ABC_HERE
	int support_pin = 0;
#endif
	

	if (dmc->size > 0) {
#ifdef EC_EMULATE_U64_DIVISION
		dirty_pct = div64_u64((u_int64_t)dmc->nr_dirty * 100, dmc->size);
		cache_pct = div64_u64((u_int64_t)atomic64_read(&dmc->cached_blocks) * 100, dmc->size);
#else
		dirty_pct = ((u_int64_t)dmc->nr_dirty * 100) / dmc->size;
		cache_pct = ((u_int64_t)atomic64_read(&dmc->cached_blocks) * 100) / dmc->size;
#endif
	} else {
		cache_pct = 0;
		dirty_pct = 0;
	}

	DMEMIT("conf:\n");
	DMEMIT("\tssd dev (%s), disk dev (%s) cache mode(%s)\n",
	       dmc->cache_devname, dmc->disk_devname,
	       mode_to_str(dmc->cache_mode));
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		DMEMIT("\tcapacity(%lluM), associativity(%u), data block size(%uK) metadata block size(%ub)\n",
		       (u64)dmc->size*dmc->block_size>>11, dmc->assoc,
		       dmc->block_size>>(10-SECTOR_SHIFT), 
		       dmc->md_block_size * 512);
	} else {
		DMEMIT("\tcapacity(%lluM), associativity(%u), data block size(%uK)\n",
		       (u64)dmc->size*dmc->block_size>>11, dmc->assoc,
		       dmc->block_size>>(10-SECTOR_SHIFT));
	}
	DMEMIT("\tskip sequential thresh(%uK)\n",
	       dmc->sysctl_skip_seq_thresh_kb);
	DMEMIT("\ttotal blocks(%llu), cached blocks(%lld), cache percent(%d)\n",
	       (u64)dmc->size, (long long)atomic64_read(&dmc->cached_blocks),
	       (int)cache_pct);
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		DMEMIT("\tdirty blocks(%d), dirty percent(%d)\n",
		       dmc->nr_dirty, (int)dirty_pct);
	}
	DMEMIT("\tnr_queued(%lu)\n", dmc->pending_jobs_count);

#ifdef MY_ABC_HERE
	if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
		support_pin = 1;
	}
#endif
	DMEMIT("\tsplit-io(%d) support pin file(%d) version(%d)\n", dmc->max_io_len, support_pin, dmc->on_ssd_version);

	DMEMIT("Size Hist: ");
#ifdef MY_ABC_HERE
	for (i = 1 ; i <= ((sizeof(size_hist)/sizeof(size_hist[0])) - 1) ; i++) {
		if (size_hist[i] > 0) {
			DMEMIT("%d:%llu ", i*512, size_hist[i]);
		}
	}
	DMEMIT("\nbits_all=%lu bits_used=%lu", dmc->bits_all, dmc->bits_used);
#else
	for (i = 1 ; i <= 32 ; i++) {
		if (size_hist[i] > 0)
			DMEMIT("%d:%llu ", i*512, size_hist[i]);
	}
#endif
}

/*
 * Report cache status:
 *  Output cache stats upon request of device status;
 *  Output cache configuration upon request of table status.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
void
flashcache_status(struct dm_target *ti, status_type_t type,
		  unsigned int unused_status_flags,
		  char *result, unsigned int maxlen)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
int
flashcache_status(struct dm_target *ti, status_type_t type,
		  unsigned int unused_status_flags,
		  char *result, unsigned int maxlen)
#else
int
flashcache_status(struct dm_target *ti, status_type_t type,
		  char *result, unsigned int maxlen)
#endif
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	
	switch (type) {
	case STATUSTYPE_INFO:
		flashcache_status_info(dmc, type, result, maxlen);
		break;
	case STATUSTYPE_TABLE:
		flashcache_status_table(dmc, type, result, maxlen);
		break;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	return 0;
#endif
}

#ifdef MY_DEF_HERE
static int flashcache_handle_4kn_target_support(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct cache_c *dmc = ti->private;

	return fn(ti, dmc->disk_dev, 0, ti->len, data);
}
#endif

#ifdef MY_ABC_HERE
static int flashcache_support_noclone(struct dm_target *ti)
{
	struct cache_c *dmc = ti->private;
	return (CACHE_DISABLED == dmc->cache_state);
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int
flashcache_disable(struct dm_target *ti, disable_type_t disable_type)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	unsigned long flags = 0;
	int ret = -1;
	int cache_mode = dmc->cache_mode;

	sdbg(DF_ONLINE, "enter");

	if (CACHE_ENABLED != dmc->cache_state) {
		serr("Can only switch from cache enabled state");
		goto err;
	}

	if (FLASHCACHE_WRITE_BACK == cache_mode) {
		if (DISABLE_NORMAL == disable_type) {
			if ((0 != dmc->sysctl_cache_all) || (0 != dmc->nr_dirty)) {
				serr("Cache can't be disabled (cache_all=%d nr_dirty=%u)",
						dmc->sysctl_cache_all,  dmc->nr_dirty);
				goto err;
			}
		} else if (DISABLE_FORCE == disable_type) {
			sprint("Perfom force disable dirty=%u", dmc->nr_dirty);
		} else {
			serr("incorrect disable type = %d", disable_type);
			goto err;
		}
	}

	down_write(&dmc->ioctl_rwsem);
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if (0 != atomic_read(&dmc->num_master_io)) {
		dmc->cache_state = CACHE_WAIT_OLD_IOS_COMPLETED;
	} else {
		dmc->cache_state = CACHE_WAIT_DISABLE;
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	up_write(&dmc->ioctl_rwsem);

	/* The work shouldn't be queued at this point. But it is not guaranteed
	 * Add here to guarantee no work left. */
	if (FLASHCACHE_WRITE_BACK == cache_mode) {
		stop_wb_works(dmc);
	}

	if (CACHE_WAIT_OLD_IOS_COMPLETED == dmc->cache_state) {

		sdbg(DF_ONLINE, "wait until no master io start");
		wait_event(dmc->wait_old_ios_completed_queue,
				0 == atomic_read(&dmc->num_master_io));
		sdbg(DF_ONLINE, "wait until no master io finish");
	}

#ifdef MY_ABC_HERE
	if (FLASHCACHE_WRITE_BACK == cache_mode) {
		syno_mu_flush_all_for_remove_sync(dmc);
	}
#endif

	if (FLASHCACHE_WRITE_BACK == cache_mode) {
		superblock_cache_state_set(dmc, CACHE_DISABLED);
	}

	if (dmc->sysctl_error_inject & SIMULATE_CACHE_DISABLE_LONG_TIME) {
		serr("Simulate a long disable procedure, sleep 60s");
		msleep(60*1000);
		dmc->sysctl_error_inject &=~ SIMULATE_CACHE_DISABLE_LONG_TIME;
	}

	set_dm_target_attribute(ti, dmc, DM_ATTR_CACHE_DISABLE);

	free_dmc_resources_for_disable(ti, disable_type);

	dmc->cache_mode = FLASHCACHE_DUMMY;
	dmc->cache_state = CACHE_DISABLED;

#ifdef MY_ABC_HERE
	job_pool_release();
#endif

	wake_up(&dmc->wait_for_cache_disable_queue);

	ret = 0;
err:
	return ret;
}

/*
 * Param:
 * enable
 * disable force(0|1)
 */
static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
flashcache_message(struct dm_target *ti, unsigned argc, char **argv, char *result, unsigned maxlen)
#else
flashcache_message(struct dm_target *ti, unsigned argc, char **argv)
#endif
{
	struct cache_c *dmc = NULL;
	unsigned long flags = 0;
	int ret = -EINVAL;
	disable_type_t disable_type = DISABLE_UNKNOWN;
	/*
	 * XXX: Make vmalloc calls in following scope with GFP_NOIO
	 * to avoid deadlock since our tool calls dmsetup suspend to block
	 * IO before enable & disable operations
	 */
	unsigned int noio_flags = memalloc_noio_save();

	dmc = ti->private;

	VERIFY_WARN(dmc);

	if (!argc) {
		serr("No arguments");
		goto err;
	}

	// Only one can enter here
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if (dmc->in_switching) {
		serr("Error: cache is changing to disable or enable now");
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		goto err;
	} else {
		dmc->in_switching = 1;
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	if (!strcasecmp(argv[0], "enable")) {
		// Enable
		sprint("Online enable");
		ret = flashcache_enable_or_ctr(ti, argc - 1, argv + 1);
	} else if (!strcasecmp(argv[0], "disable")) {
		// Disable
		sprint("Online disable start");

		if (2 != argc) {
			serr("Lack parameter 2");
			goto err;
		}

		if (1 != sscanf(argv[1], "%d", &disable_type)) {
			serr("Parse parameter fail");
			goto err;
		}
		sprint("Disable type = %d", disable_type);

		if ((DISABLE_NORMAL != disable_type) && (DISABLE_FORCE != disable_type)) {
			serr("Incorrect disable type = %d", disable_type);
			goto err;
		}

		ret = flashcache_disable(ti, disable_type);
		sprint("Online disable finish");

	} else {
		serr("No supported messages = %s" ,argv[0]);
	}

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	dmc->in_switching = 0;
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

err:
	memalloc_noio_restore(noio_flags);
	return ret;
}

static int flashcache_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct cache_c *dmc = (struct cache_c *)ti->private;
	int ret = 0;

	/*
	 * Refer to device_area_is_invalid()
	 * the start and len argumetns indicate the data range of a block device
	 */
	ret = fn(ti, dmc->disk_dev, 0, dmc->disk_devsize, data);
	if (ret) {
		// finish iteration
		goto end;
	}

	if (dmc->cache_dev) {
		ret = fn(ti, dmc->cache_dev, 0, to_sector(get_cache_bdev(dmc)->bd_inode->i_size), data);
	}
end:
	return ret;
}
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) && LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
/*
 * Refer to drivers/md/dm-linear.c: linear_merge
 * upstream kill merge_bvec_fn from v4.3
 * When implementing iterate_devices, we must implement the merge function,
 * otherwise the size of the bio will be limited to 4k for security reasons.
 */
static int flashcache_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct cache_c *dmc = (struct cache_c *) ti->private;
	struct block_device *disk_bdev = get_disk_bdev(dmc);
	struct request_queue *disk_queue = bdev_get_queue(disk_bdev);
	int min_bytes = 0;
	int merge_bytes = 0;
	unsigned int bio_sectors = bvm->bi_size >> SECTOR_SHIFT;

	if (disk_queue->merge_bvec_fn) {
		bvm->bi_bdev = disk_bdev;
		// For RAID 5/6, it will aligned to chunk size
		min_bytes = min(max_size, disk_queue->merge_bvec_fn(disk_queue, bvm, biovec));
	} else {
		min_bytes = max_size;
	}

	if (dmc->cache_mode != FLASHCACHE_DUMMY) {
		/*
		 * RO or RW cache: Aligned to cache block size to avoid IO splitting
		 * IO splitting is an old feature that aim to allow cache to receive big-size bio for future seq-io judgement.
		 * However, since the max bio size of btrfs is changed to 64k, this feature is not so usefully now
		 * So here we don't make bio large than cache block size anymore.
		 *
		 * Refer to blk_queue_merge_bvec() and raid5_mergeable_bvec()
		 */
		unsigned int prev_sectors = bvm->bi_sector % MAX_BIO_SIZE;

		merge_bytes = (MAX_BIO_SIZE - prev_sectors - bio_sectors) << SECTOR_SHIFT;
		if (0 > merge_bytes) {
			min_bytes = 0;
		} else {
			min_bytes = min(min_bytes, merge_bytes);
		}
	}

	sdbg_limit(DF_MERGE,"bio start=%llu size=%u min_bytes=%d", (u64)bvm->bi_sector, bio_sectors, min_bytes);

	return min_bytes;
}
#endif /* defined(MY_ABC_HERE) && LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */

static struct target_type flashcache_target = {
#ifdef MY_ABC_HERE
	.name   = SYNO_FLASHCACHE_TARGET_TYPE,
#else
	.name   = "flashcache",
#endif
	.version= {1, 0, 3},
	.module = THIS_MODULE,
	.ctr    = flashcache_enable_or_ctr,
	.dtr    = flashcache_dtr,
	.map    = flashcache_map,
#ifdef MY_ABC_HERE
	.noclone_map = flashcache_noclone_map,
#endif /* SYNO_MD_SYNO_FLASHCACHE_NOCLONE */
	.status = flashcache_status,
#ifdef MY_ABC_HERE
	.extra_ioctl 	= flashcache_ioctl,
#else
	.ioctl 	= flashcache_ioctl,
#endif
#ifdef MY_DEF_HERE
	.handle_4kn_target_support = flashcache_handle_4kn_target_support,
#endif
#ifdef MY_ABC_HERE
	.message	= flashcache_message,
	.iterate_devices = flashcache_iterate_devices,
#endif
#if defined(MY_ABC_HERE) && LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	.merge  = flashcache_merge,
#endif /* defined(MY_ABC_HERE) && LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */
#ifdef MY_ABC_HERE
	.support_noclone = flashcache_support_noclone,
#endif /* SYNO_MD_SYNO_FLASHCACHE_NOCLONE */
};

static int
sync_for_remove_can_wake_up(struct cache_c *dmc)
{
#ifdef MY_ABC_HERE
	return !atomic_read(&dmc->nr_jobs) && SYNC_STATE_STOPPED == dmc->oqf.sync_state;
#else
	return !atomic_read(&dmc->nr_jobs);
#endif
}

#ifdef MY_ABC_HERE
static void dmcg_remove_from_nqf_mgmt_sync(struct cache_c *dmc)
{
	dmc_group_t *dmcg = dmc->dmcg;

	down_write(&dmcg->rw_sem);
	if (dmcg->nqf_dmc == dmc) {
		dmcg->nqf_yielded = 1;
		up_write(&dmcg->rw_sem);
		cache_mod_delayed_work(&nqf_mgmt_work, 0);
		/* No need to lock since dmcg->nqf_dmc will never be dmc again */
		wait_event(dmcg->nqf_wqh, dmcg->nqf_dmc != dmc);
	} else if (dmc->oqf.dmcg_wb_scheduled) {
		list_del(&dmc->node->nqf_entry);
		up_write(&dmcg->rw_sem);
	} else {
		up_write(&dmcg->rw_sem);
	}
}
#endif

/* The function is called in ending phase, either remove in progress or nr_dirty == 0
 * the function makes sure the nqf loop ends */
static void stop_wb_works(struct cache_c *dmc)
{
#ifdef MY_ABC_HERE
	/* Stop detect fallow. Also stop nqf being scheduled by wb_work */
	cancel_delayed_work_sync(&dmc->oqf.wb_work);

	/* Remove from queue, on going mu won't schedule again because remove in prog or nr_fallow == 0 */
#ifdef MY_ABC_HERE
	dmcg_remove_from_nqf_mgmt_sync(dmc);
#endif

	mutex_lock(&dmc->oqf.mtx);
	while (dmc->oqf.trigger_cnt) {
		mutex_unlock(&dmc->oqf.mtx);
		wait_event(dmc->oqf.wqh, dmc->oqf.trigger_cnt == 0);
		mutex_lock(&dmc->oqf.mtx);
	}
	mutex_unlock(&dmc->oqf.mtx);

	cancel_work_sync(&dmc->plug_wb.work);
	cancel_work_sync(&dmc->plug_wb.wb_done_work);
#else
	cancel_delayed_work_sync(&dmc->delayed_clean);
#endif
}

// Do fast remove or slow remove and wait for nr_job to zero
static void
flashcache_sync_for_remove(struct cache_c *dmc)
{
	unsigned long hang_check = sysctl_hung_task_timeout_secs;
	int cur_jobs = 0;
	do {
		if (!dmc->sysctl_fast_remove) {
			atomic_set(&dmc->remove_in_prog, SLOW_REMOVE); /* Stop cleaning of sets */

			/* 
			 * Kick off cache cleaning. client_destroy will wait for cleanings
			 * to finish.
			 */
			printk(KERN_ALERT "Cleaning %d blocks please WAIT", dmc->nr_dirty);
			/* Tune up the cleaning parameters to clean very aggressively */
			dmc->max_clean_ios_total = 20;
			dmc->max_clean_ios_set = 10;
			flashcache_sync_all(dmc);
		} else {
			/* Needed to abort any in-progress cleanings, leave blocks DIRTY */
			atomic_set(&dmc->remove_in_prog, FAST_REMOVE);
			printk(KERN_ALERT "Fast flashcache remove Skipping cleaning of %d blocks", 
			       dmc->nr_dirty);
		}
		/* 
		 * We've prevented new cleanings from starting (for the fast remove case)
		 * and we will wait for all in progress cleanings to exit.
		 * Wait a few seconds for everything to quiesce before writing out the 
		 * cache metadata.
		 */
		msleep(FLASHCACHE_SYNC_REMOVE_DELAY);
		/* Wait for all the dirty blocks to get written out, and any other IOs */
		// Refer to block/blk-exec.c
		if (hang_check) {
			while (0 != (cur_jobs = atomic_read(&dmc->nr_jobs))) {
				sprint("current job is %d, do wait", cur_jobs);
				wait_event_timeout(dmc->sync_wqh,
					sync_for_remove_can_wake_up(dmc),
					hang_check * HZ / 2);
			}
		} else {
			wait_event(dmc->sync_wqh, sync_for_remove_can_wake_up(dmc));
		}
		sprint("wait event finish");
	} while (!dmc->sysctl_fast_remove && dmc->nr_dirty > 0);
	stop_wb_works(dmc);
	sprint("syncing for remove finish");
}

static int 
flashcache_notify_reboot(struct notifier_block *this,
			 unsigned long code, void *x)
{
	struct cache_c *dmc;
	int ori_enabled = 0;

	sprint("Reboot hook start")

	(void)wait_on_bit_lock_wrapper(&flashcache_control->synch_flags,
			       FLASHCACHE_UPDATE_LIST,
			       flashcache_wait_schedule, 
			       TASK_UNINTERRUPTIBLE);
	for (dmc = cache_list_head ; 
	     dmc != NULL ; 
	     dmc = dmc->next_cache) {

		if ((CACHE_WAIT_OLD_IOS_COMPLETED == dmc->cache_state)
				|| (CACHE_WAIT_DISABLE == dmc->cache_state)) {
			// If cache is disabling, wait for it
			sprint("Waiting for cache disable start");
			wait_event(dmc->wait_for_cache_disable_queue,
					 (CACHE_DISABLED == dmc->cache_state));
			sprint("Waiting for cache disable finished");
		}

		ori_enabled = (CACHE_ENABLED == dmc->cache_state)? 1 : 0;

		// Cache is ENABLED or DISABLED
		if ((CACHE_ENABLED == dmc->cache_state)
			|| (CACHE_DISABLED == dmc->cache_state)) {
			/*
			* Make proceess wait and wait until
			* no master bios to avoid call trace at removing dmc later
			*/
			sprint("Waiting master bio to zero");
			dmc->cache_state = CACHE_WAIT_OLD_IOS_COMPLETED;
			wait_event(dmc->wait_old_ios_completed_queue,
					0 == atomic_read(&dmc->num_master_io));
		}

		if ((dmc->cache_mode == FLASHCACHE_WRITE_BACK)) {
			sprint("Do fast remove and wait nr_job to zero")
			flashcache_sync_for_remove(dmc);
#ifdef MY_ABC_HERE
			sprint("Do MU flush all")
			syno_mu_flush_all_for_remove_sync(dmc);
#endif
			sprint("Save metadata")
			flashcache_writeback_md_store(dmc);
		}

#ifdef MY_ABC_HERE
		if (ori_enabled) {
			sprint("Remove cache from group")
			dmc_group_remove(dmc);
		}
#endif
	}

	clear_bit(FLASHCACHE_UPDATE_LIST, &flashcache_control->synch_flags);
	smp_mb__after_clear_bit_wrapper();
	wake_up_bit(&flashcache_control->synch_flags, FLASHCACHE_UPDATE_LIST);

	sprint("Reboot hook finish")

	return NOTIFY_DONE;
}

/*
 * The notifiers are registered in descending order of priority and
 * executed in descending order or priority. We should be run before
 * any notifiers of ssd's or other block devices. Typically, devices
 * use a priority of 0.
 * XXX - If in the future we happen to use a md device as the cache
 * block device, we have a problem because md uses a priority of 
 * INT_MAX as well. But we want to run before the md's reboot notifier !
 */
static struct notifier_block flashcache_notifier = {
	.notifier_call	= flashcache_notify_reboot,
	.next		= NULL,
	.priority	= INT_MAX, /* should be > ssd pri's and disk dev pri's */
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
struct dm_kcopyd_client *flashcache_kcp_client; /* Kcopyd client for writing back data */
#else
struct kcopyd_client *flashcache_kcp_client; /* Kcopyd client for writing back data */
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
struct dm_io_client *flashcache_io_client; /* Client memory pool*/
#endif

// In linux 4.4, ktime_t is a union
ktime_t ktime_zero;
static void flashcache_init_kcached_wq(void)
{
	int i = 0;

	for (i = 0; i < KCACHED_WQ_SIZE; i++) {
		INIT_LIST_HEAD(&kcached_wq[i].jobs);
		INIT_WORK(&kcached_wq[i].kwq_work, process_kcached_wq);
	}
	kcached_wq[PENDING].fn = flashcache_do_pending;
	kcached_wq[DO_IO].fn = flashcache_do_io;
#ifdef MY_ABC_HERE
#else
	kcached_wq[MD_IO].fn = flashcache_md_write_kickoff;
	kcached_wq[MD_COMPLETE].fn = flashcache_md_write_done;
#endif
	kcached_wq[UNCACHED_IO_COMPLETE].fn = flashcache_uncached_io_complete;
	kcached_wq[PREREAD].fn = flashcache_do_preread_io;
	kcached_wq[FREE_JOB].fn = process_free_job;
}

/*
 * Initiate a cache target.
 */
int __init 
flashcache_init(void)
{
	int r = 0;

	ktime_zero = ktime_set(0, 0);

	if (job_max_mem_count != MAX_JOB_MEM_COUNT) {
		sprint("Init driver with job_max_mem_count=%d", job_max_mem_count);
	}
	init_waitqueue_head(&job_mem_wait_queue);
	atomic_set(&nr_cache_jobs, 0);
	atomic_set(&nr_pending_jobs, 0);

	flashcache_io_client = dm_io_client_create();

	if (IS_ERR(flashcache_io_client)) {
		DMERR("flashcache_init: Failed to initialize DM IO client");
		return r;
	}

	flashcache_kcp_client = dm_kcopyd_client_create(NULL);
	if ((r = IS_ERR(flashcache_kcp_client))) {
		r = PTR_ERR(flashcache_kcp_client);
	}

	if (r) {
		dm_io_client_destroy(flashcache_io_client);
		DMERR("flashcache_init: Failed to initialize kcopyd client");
		return r;
	}

#ifdef MY_ABC_HERE
	/* allow multiple workers threads */
	cache_workqueue = alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM, 0, "ssd-cache");
#endif /* MY_ABC_HERE */

	flashcache_init_kcached_wq();

#ifdef MY_DEF_HERE
	for (r = 0 ; r < sizeof(size_hist)/sizeof(size_hist[0]) ; r++)
#else
	for (r = 0 ; r < 33 ; r++)
#endif /* MY_DEF_HERE */
		size_hist[r] = 0;
	r = dm_register_target(&flashcache_target);
	if (r < 0) {
		DMERR("cache: register failed %d", r);
	}

    sprint("%s initialized", flashcache_sw_version);

	flashcache_module_procfs_init();
	flashcache_control = (struct flashcache_control_s *)
		kmalloc(sizeof(struct flashcache_control_s), GFP_KERNEL);
	flashcache_control->synch_flags = 0;
	register_reboot_notifier(&flashcache_notifier);
	/*
	 * Initialize global bio_set (global for linux 3.2 compatible)
	 * bio_set uses similar concept as mempool
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	// BIOSET_NEED_BVECS is refer to md.c
	r = bioset_init(pcache_bio_set, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (r) {
		serr("Failed to init bio set, r = %d", r);
		goto err;
	}
#else
	pcache_bio_set = bioset_create(BIO_POOL_SIZE, 0);
	if (NULL == pcache_bio_set) {
		serr("Failed to create bioset");
		r = -ENOMEM;
		goto err;
	}
#endif

#ifdef MY_ABC_HERE
	mutex_init(&dmc_list_mtx);
#endif
#ifdef MY_ABC_HERE
	mutex_init(&dmcg_mtx);
	INIT_DELAYED_WORK(&nqf_mgmt_work, dmcg_nqf_mgmt);
	schedule_nqf_mgmt_work();
#endif
err:
	return r;
}

/*
 * Destroy a cache target.
 */
void 
flashcache_exit(void)
{
#ifdef MY_ABC_HERE
	cancel_delayed_work_sync(&nqf_mgmt_work);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	int r = dm_unregister_target(&flashcache_target);

	if (r < 0)
		DMERR("cache: unregister failed %d", r);
#else
	dm_unregister_target(&flashcache_target);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	kcopyd_client_destroy(flashcache_kcp_client);
#else
	dm_kcopyd_client_destroy(flashcache_kcp_client);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
	dm_io_client_destroy(flashcache_io_client);
#else
	dm_io_put(FLASHCACHE_ASYNC_SIZE);
#endif
	unregister_reboot_notifier(&flashcache_notifier);
#ifdef MY_ABC_HERE
#else
	flashcache_jobs_exit();
#endif
	flashcache_module_procfs_releae();
	kfree(flashcache_control);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	bioset_exit(pcache_bio_set);
#else
	if (pcache_bio_set) {
		bioset_free(pcache_bio_set);
	}
#endif
#ifdef MY_ABC_HERE
	destroy_workqueue(cache_workqueue);
#endif /* MY_ABC_HERE */
}

module_init(flashcache_init);
module_exit(flashcache_exit);

#ifdef MY_ABC_HERE
#else
EXPORT_SYMBOL(flashcache_writeback_load);
EXPORT_SYMBOL(flashcache_writeback_create);
EXPORT_SYMBOL(flashcache_writeback_md_store);
#endif

MODULE_DESCRIPTION(DM_NAME " Facebook flash cache target");
MODULE_AUTHOR("Mohan - based on code by Ming");
MODULE_LICENSE("GPL");
