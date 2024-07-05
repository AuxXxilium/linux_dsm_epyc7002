#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/****************************************************************************
 *  flashcache_subr.c
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
#include <linux/sort.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <asm/kmap_types.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#include <linux/sched/mm.h> // memalloc_noio_* apis
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

#ifdef MY_ABC_HERE
#include "syno_quickflush.h"
#endif

static DEFINE_SPINLOCK(_job_lock);

extern mempool_t *_job_pool;
extern mempool_t *_pending_job_pool;
#ifdef MY_ABC_HERE
extern mempool_t *_job_mem_pool;

// Lock for controlling job mem allocated
extern spinlock_t job_mem_lock;
extern int job_mem_count;
extern wait_queue_head_t job_mem_wait_queue;
#endif

extern atomic_t nr_cache_jobs;
extern atomic_t nr_pending_jobs;
extern int job_max_mem_count;

static void job_mem_mempool_put(void);


struct kcached_job *
flashcache_alloc_cache_job(void)
{
	struct kcached_job *job;

	job = mempool_alloc(_job_pool, GFP_NOIO);
#ifdef MY_ABC_HERE
	if (likely(job)) {
		atomic_inc(&nr_cache_jobs);
		memset(job, 0, sizeof(*job));
	}
#ifdef MY_ABC_HERE
	else {
		if (printk_ratelimit()) {
			serr("Failed to allocate memory for cache jobs");
		}
	}
#endif
#else
	if (likely(job))
		atomic_inc(&nr_cache_jobs);
#endif
	return job;
}

/*
 * Might sleep if not in interrupt context (vfree)
 */
void
flashcache_free_cache_job(struct kcached_job *job)
{
	flashcache_free_cache_job_mem(job);

	mempool_free(job, _job_pool);
	atomic_dec(&nr_cache_jobs);
}

/*
 * Might sleep if not in interrupt context (vfree)
 */
void
flashcache_free_cache_job_mem(struct kcached_job *job)
{
#ifdef MY_ABC_HERE
	if (NULL != job->mem_addr) {
		if (job->flag & JOB_MEM_VMALLOC) {
			vfree(job->mem_addr);
			job->mem_addr = NULL;
		} else if (job->flag & JOB_MEM_MEMPOOL) {
			mempool_free(job->mem_addr, _job_mem_pool);
			job->mem_addr = NULL;
			job_mem_mempool_put();
		} else {
			serr("Free %d job memory without type", job->action);
			VERIFY_WARN(0);
		}
	}
#else
	if (NULL != job->mem_addr) {
		mempool_free(job->mem_addr, _job_mem_pool);
		job->mem_addr = NULL;
		job_mem_mempool_put();
	}
#endif
}

struct pending_job *
flashcache_alloc_pending_job(struct cache_c *dmc)
{
	struct pending_job *job;

	job = mempool_alloc(_pending_job_pool, GFP_ATOMIC);
	if (likely(job)) {
		atomic_inc(&nr_pending_jobs);
	} else {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate memory for pending jobs");
		}
#endif
	}
	return job;
}

void
flashcache_free_pending_job(struct pending_job *job)
{
	mempool_free(job, _pending_job_pool);
	atomic_dec(&nr_pending_jobs);
}

#define FLASHCACHE_PENDING_JOB_HASH(INDEX)		((INDEX) % PENDING_JOB_HASH_SIZE)

/*
 * plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
 */
void 
flashcache_enq_pending(struct cache_c *dmc, struct bio* bio,
		       int index, int action, struct pending_job *job,
		       struct io_latency *plat, enum cache_io_type io_type)
{
	struct pending_job **head;
	
	head = &dmc->pending_job_hashbuckets[FLASHCACHE_PENDING_JOB_HASH(index)];
	DPRINTK("flashcache_enq_pending: Queue to pending Q Index %d %llu",
		index, bio_bi_sector(bio));
	VERIFY_WARN(job != NULL);
	job->action = action;
	job->index = index;
	job->bio = bio;
	job->prev = NULL;
	job->next = *head;
	if (*head)
		(*head)->prev = job;
	*head = job;

	if (plat) {
		job->io_lat = *plat;
	} else {
		/* enq pending request must have bio -> have preprocess time */
		flashcache_init_io_latency(&job->io_lat,
			dmc->sysctl_syno_latency_diagnose, io_type,
			bio_get_start_jiffy(bio), 1);
	}

	dmc->cache[index].nr_queued++;
	atomic64_inc(&dmc->flashcache_stats.enqueues);
	if (INVALIDATE == job->action) {
		atomic64_inc(&dmc->flashcache_stats.pending_enqueue_inval_start);
	}
	dmc->pending_jobs_count++;
}

/*
 * Deq and move all pending jobs that match the index for this slot to list returned
 */
struct pending_job *
flashcache_deq_pending(struct cache_c *dmc, int index)
{
	struct pending_job *node, *next, *movelist = NULL;
	int moved = 0;
	struct pending_job **head;

	VERIFY_WARN(spin_is_locked(&dmc->cache_spin_lock));
	head = &dmc->pending_job_hashbuckets[FLASHCACHE_PENDING_JOB_HASH(index)];
	for (node = *head ; node != NULL ; node = next) {
		next = node->next;
		if (node->index == index) {
			/*
			 * Remove pending job from the global list of
			 * jobs and move it to the private list for freeing
			 */
			if (node->prev == NULL) {
				*head = node->next;
				if (node->next)
					node->next->prev = NULL;
			} else
				node->prev->next = node->next;
			if (node->next == NULL) {
				if (node->prev)
					node->prev->next = NULL;
			} else
				node->next->prev = node->prev;
			node->prev = NULL;
			node->next = movelist;
			flashcache_check_record_io_latency(LAT_PENDING, &node->io_lat);
			movelist = node;
			moved++;
		}
	}
	VERIFY_WARN(dmc->pending_jobs_count >= moved);
	dmc->pending_jobs_count -= moved;
	return movelist;
}

#ifdef FLASHCACHE_DO_CHECKSUMS
int
flashcache_read_compute_checksum(struct cache_c *dmc, int index, void *block)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	struct io_region where;
#else
	struct dm_io_region where;
#endif
	int error;
	u_int64_t sum = 0, *idx;
	int cnt;

	where.bdev = get_cache_bdev(dmc);
	where.sector = INDEX_TO_CACHE_ADDR(dmc, index);
	where.count = dmc->block_size;
	error = flashcache_dm_io_sync_vm(dmc, &where, COMP_DM_READ, block);
	if (error)
		return error;
	cnt = dmc->block_size * 512;
	idx = (u_int64_t *)block;
	while (cnt > 0) {
		sum += *idx++;
		cnt -= sizeof(u_int64_t);		
	}
	dmc->cache[index].checksum = sum;
	return 0;
}

u_int64_t
flashcache_compute_checksum(struct bio *bio)
{
	int i;	
	u_int64_t sum = 0, *idx;
	int cnt;
	int kmap_type;
	void *kvaddr;

	if (in_interrupt())
		kmap_type = KM_SOFTIRQ0;
	else
		kmap_type = KM_USER0;
	for (i = bio_bi_idx(bio) ; i < bio->bi_vcnt ; i++) {
		kvaddr = kmap_atomic(bio->bi_io_vec[i].bv_page, kmap_type);
		idx = (u_int64_t *)
			((char *)kvaddr + bio->bi_io_vec[i].bv_offset);
		cnt = bio->bi_io_vec[i].bv_len;
		while (cnt > 0) {
			sum += *idx++;
			cnt -= sizeof(u_int64_t);
		}
		kunmap_atomic(kvaddr, kmap_type);
	}
	return sum;
}

void
flashcache_store_checksum(struct kcached_job *job)
{
	u_int64_t sum;
	unsigned long flags;
	
	sum = flashcache_compute_checksum(job->bio);
	spin_lock_irqsave(&job->dmc->cache_spin_lock, flags);
	job->dmc->cache[job->index].checksum = sum;
	spin_unlock_irqrestore(&job->dmc->cache_spin_lock, flags);
}

int
flashcache_validate_checksum(struct kcached_job *job)
{
	u_int64_t sum;
	int retval;
	unsigned long flags;
	
	sum = flashcache_compute_checksum(job->bio);
	spin_lock_irqsave(&job->dmc->cache_spin_lock, flags);
	if (likely(job->dmc->cache[job->index].checksum == sum)) {
		atomic64_inc(&job->dmc->flashcache_stats.checksum_valid);		
		retval = 0;
	} else {
		atomic64_inc(&job->dmc->flashcache_stats.checksum_invalid);
		retval = 1;
	}
	spin_unlock_irqrestore(&job->dmc->cache_spin_lock, flags);
	return retval;
}
#endif

/*
 * Functions to push and pop a job onto the head of a given job list.
 */
struct kcached_job *
pop(struct list_head *jobs)
{
	struct kcached_job *job = NULL;
	unsigned long flags;

	spin_lock_irqsave(&_job_lock, flags);
	if (!list_empty(jobs)) {
		job = list_entry(jobs->next, struct kcached_job, list);
		list_del(&job->list);
	}
	spin_unlock_irqrestore(&_job_lock, flags);
	return job;
}

void 
push(struct list_head *jobs, struct kcached_job *job)
{
	unsigned long flags;

	spin_lock_irqsave(&_job_lock, flags);
	list_add_tail(&job->list, jobs);
	spin_unlock_irqrestore(&_job_lock, flags);
}


#ifdef MY_ABC_HERE
void process_free_job(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;

	flashcache_free_cache_job(job);

	if (atomic_dec_and_test(&dmc->nr_jobs)) {
		wake_up(&dmc->sync_wqh);
	}
}

#endif

static void
process_jobs(struct list_head *jobs,
	     void (*fn) (struct kcached_job *))
{
	int done = 0;
	struct kcached_job *job;

	while ((job = pop(jobs))) {
		(void)fn(job);

		if (++done == PROCESS_JOB_RESCHED_BATCH) {
			done = 0;
			cond_resched();
		}
	}
}

void process_kcached_wq(struct work_struct *work)
{
	struct kcached_wq *kcwq =
		container_of(work, struct kcached_wq, kwq_work);

	process_jobs(&kcwq->jobs, kcwq->fn);
}

static void job_mem_mempool_get(struct cache_c *dmc)
{
	spin_lock_irq(&job_mem_lock);

	atomic64_inc(&dmc->flashcache_stats.job_mem_get_in);
	wait_event_lock_irq(job_mem_wait_queue, job_mem_count < job_max_mem_count, job_mem_lock);
	job_mem_count++;
	atomic64_inc(&dmc->flashcache_stats.job_mem_get_out);

	spin_unlock_irq(&job_mem_lock);

	return;
}

static inline void job_mem_mempool_put(void)
{
	spin_lock_irq(&job_mem_lock);
	job_mem_count--;
	spin_unlock_irq(&job_mem_lock);

	wake_up(&job_mem_wait_queue);
}

#define JOB_MEM_GFP_FLAG (GFP_NOIO | __GFP_NORETRY)

#ifdef MY_ABC_HERE
// 0 success, -1 error
static int
job_alloc_mem_vmalloc(struct cache_c *dmc, struct kcached_job *job)
{
	int ret = -1;
	unsigned int noio_flags = memalloc_noio_save();

	job->flag |= JOB_MEM_VMALLOC;

	job->mem_addr = vmalloc_wrapper(JOB_MEM_BYTE, JOB_MEM_GFP_FLAG | VMALLOC_INT_FLAG);

	memalloc_noio_restore(noio_flags);

	if (NULL == job->mem_addr) {
		serr("vmalloc failed to allocate job memory");
		goto err;
	}

	memset(job->mem_addr, 0, JOB_MEM_BYTE);

	ret = 0;
err:
	return ret;
}
#endif

// 0 success, -1 error
static int
job_alloc_mem_mempool(struct cache_c *dmc, struct kcached_job *job)
{
#ifdef MY_ABC_HERE
	job->flag |= JOB_MEM_MEMPOOL;
#endif
	job_mem_mempool_get(dmc);

	job->mem_addr = mempool_alloc(_job_mem_pool, JOB_MEM_GFP_FLAG);
	if (NULL == job->mem_addr) {
		serr("memory allocation failed from mem pool");
		goto err;
	}

	memset(job->mem_addr, 0, JOB_MEM_BYTE);

	return 0;
err:
	job_mem_mempool_put();
	return -1;
}

/*
 * Return:
 *	-1 on error
 */
static int
job_alloc_mem_preread(struct cache_c *dmc, int index, struct kcached_job *job,
		sector_t *sectors_to_read)
{
	int ret = -1;
	unsigned long bytes = 0;
	sector_t sectors_remaining = dmc->disk_devsize - dmc->cache[index].dbn;

	job->flag |= JOB_MEM_MEMPOOL;

	// IO might read the end of disks and can't fill the cacheblk
	if (dmc->block_size > sectors_remaining) {
		// round to 4k align
		sectors_remaining = compatible_div(sectors_remaining, SECTOR_PER_BIT) * SECTOR_PER_BIT;
		*sectors_to_read = sectors_remaining;
	} else {
		*sectors_to_read = dmc->block_size;
	}

	bytes = to_bytes(*sectors_to_read);

	VERIFY_WARN((0 != bytes) && NULL == job->mem_addr);

	if (job_alloc_mem_mempool(dmc, job)) {
		goto err;
	}

	sdbg(DF_DETAIL, "job->mem_addr=%p bytes=%lu", job->mem_addr, bytes);

	job->mem_data_byte = bytes;

	ret = 0;
err:
	return ret;
}

/*
 * plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
 */
struct kcached_job *
new_kcached_job_common(struct cache_c *dmc, int index, job_param_t *param,
			struct io_latency *plat, enum cache_io_type io_type, void *ctx)
{
	struct kcached_job *job;
	struct bio* bio = NULL;
	sector_t bio_offset = 0;
	sector_t partial_offset = 0;
	sector_t partial_size = 0;
	sector_t cache_addr = 0;
	param_type_t param_type = TYPE_UNKNOWN;
	sector_t sectors_to_read = 0;
	unsigned long start_jiffy = 0;

	VERIFY_WARN(dmc && param);

	partial_offset = param->partial_offset;
	partial_size = param->partial_size;
	cache_addr = INDEX_TO_CACHE_ADDR(dmc, index);
	param_type = param->type;

	if ((TYPE_BIO_RANGE == param_type) || (TYPE_PREREAD_CACHEBLK == param_type)) {
		if (NULL == param->bio) {
			serr("bio should not be NULL!");
			VERIFY_WARN(0);
		}
		bio = param->bio;
	}


	job = flashcache_alloc_cache_job();
	if (unlikely(job == NULL)) {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
		return NULL;
	}
	job->dmc = dmc;
	job->index = index;

	/*
	 * WRITEDISK/ WRITEDISK_sync
	 *		Both regions.cache and regions.disk are used in WRITE_DISK dirty_writeback()
	 *		bio is NULL that time
	 * READCACHE/WRITECACHE/READFILL(=write to cache)
	 * 		Might just use disk or cache
	 * Uncached IO
	 *		Index= -1
	 */

	switch (param_type) {

#ifdef MY_ABC_HERE
	case TYPE_SQF:
		if (job_alloc_mem_vmalloc(dmc, job)) {
			flashcache_free_cache_job(job);
			return NULL;
		}
		break;
	case TYPE_NQF:
		if (job_alloc_mem_mempool(dmc, job)) {
			flashcache_free_cache_job(job);
			return NULL;
		}
		break;
#endif

	case TYPE_PREREAD_CACHEBLK:
		if (job_alloc_mem_preread(dmc, index, job, &sectors_to_read)) {
			flashcache_free_cache_job(job);
			return NULL;
		}
		break;

	case TYPE_BIO_RANGE:
	case TYPE_NO_BIO:
	case TYPE_MD_IO:
		break;

	default:
		serr("Unknown type=%d", param_type);
		VERIFY_WARN(0);
		break;
	}

	job->job_io_regions.cache.bdev = get_cache_bdev(dmc);
	// TODO: another type for uncached io?
	if (index != -1) {
		/*
		 * - index != -1: Not uncached I/O and Not MD IO
		 * - Also set job->action_bitmap here ...
		 *	TODO: For uncached I/O, doesn't set the action bitmap
		 */
		switch (param_type) {

		case TYPE_BIO_RANGE:
			// avoid type overflow
			bio_offset = bio_bi_sector(bio) - dmc->cache[index].dbn;
			job->job_io_regions.cache.sector = cache_addr + bio_offset;
			job->job_io_regions.cache.count = to_sector(bio_bi_size(bio));
			job->action_bitmap = bitmap_get(&dmc->cache[index], bio);
			break;

		case TYPE_NO_BIO:
#ifdef MY_ABC_HERE
		case TYPE_SQF:
		case TYPE_NQF:
#endif
			// write disk and write disk sync
			job->job_io_regions.cache.sector = cache_addr + partial_offset;
			job->job_io_regions.cache.count = partial_size;
			// io range should be as BITMAP_MASK
			job->action_bitmap = BITMAP_MASK;
			break;

		case TYPE_PREREAD_CACHEBLK:
			job->job_io_regions.cache.sector = cache_addr;
			job->job_io_regions.cache.count = sectors_to_read;
			job->action_bitmap = bitmap_get_by_bytes(&dmc->cache[index], bio, job->mem_data_byte);
			break;

		case TYPE_MD_IO:
			serr("MD IO job idx should be -1");
			VERIFY_WARN(0);
			break;

		default:
			serr("Unknown type=%d", param_type);
			VERIFY_WARN(0);
			break;
		}
	}
	job->error = 0;
	job->bio = bio;
	job->job_io_regions.disk.bdev = get_disk_bdev(dmc);
	if (index != -1) {
		// not uncached io and not MD IO
		switch (param_type) {

		case TYPE_BIO_RANGE:
			// CASE: read_miss() and read_disk_write_cache()
			// in read_disk_write_cache(), its cacheblk.dbn is NOT THE SAME to bi_sector
			job->job_io_regions.disk.sector = bio_bi_sector(bio);
			job->job_io_regions.disk.count = to_sector(bio_bi_size(bio));
			break;

		case TYPE_NO_BIO:
#ifdef MY_ABC_HERE
		case TYPE_SQF:
		case TYPE_NQF:
#endif
			// CASE: write disk and write disk sync
			job->job_io_regions.disk.sector = dmc->cache[index].dbn + partial_offset;
			job->job_io_regions.disk.count = partial_size;
			break;

		case TYPE_PREREAD_CACHEBLK:
			job->job_io_regions.disk.sector = dmc->cache[index].dbn;
			job->job_io_regions.disk.count = sectors_to_read;
			VERIFY_WARN((dmc->cache[index].dbn + sectors_to_read - 1) >= bio_last_sector(bio));
			break;

		case TYPE_MD_IO:
			serr("MD IO job idx should be -1");
			VERIFY_WARN(0);
			break;

		default:
			serr("Unknown type=%d", param_type);
			VERIFY_WARN(0);
			break;
		}
	} else if (TYPE_MD_IO != param_type) {
		// SYNO: for uncached IO
		job->job_io_regions.disk.sector = bio_bi_sector(bio);
		job->job_io_regions.disk.count = to_sector(bio_bi_size(bio));
	}

	start_jiffy = bio ? bio_get_start_jiffy(bio) : jiffies;

	job->next = NULL;
	job->md_block = NULL;
	if (dmc->sysctl_io_latency_hist)
		job->io_start_time = ktime_get();
	else {
		job->io_start_time = ktime_set(0, 0);
	}

	if (plat) {
		job->io_lat = *plat;
	} else {
		/* writeback ios (no bio) do not have preprocess stage */
		flashcache_init_io_latency(&job->io_lat,
			dmc->sysctl_syno_latency_diagnose, io_type, start_jiffy,
			bio != NULL);
	}

	INIT_LIST_HEAD(&job->list);
	job->ctx = ctx;

	return job;
}

// Use bio range to access now
// plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
struct kcached_job *
new_kcached_job(struct cache_c *dmc, struct bio* bio, int index,
		struct io_latency *plat, enum cache_io_type io_type)
{
	job_param_t param = {0};

	VERIFY_WARN(bio != NULL);

	param.type = TYPE_BIO_RANGE;
	param.bio = bio;

	return new_kcached_job_common(dmc, index, &param, plat, io_type, NULL);
}

// For cache block preread
struct kcached_job *
new_kcached_job_fill_cacheblk(struct cache_c *dmc, struct bio* bio, int index)
{

	job_param_t param = {0};

	param.type = TYPE_PREREAD_CACHEBLK;
	param.bio = bio;

	return new_kcached_job_common(dmc, index, &param, NULL, TYPE_DISK, NULL);
}


#ifdef MY_ABC_HERE
#else
// For dirty_writeback*
struct kcached_job *
new_kcached_job_no_bio(struct cache_c *dmc, int index, sector_t offset, sector_t size)
{
	job_param_t param = {0};

	param.type = TYPE_NO_BIO;
	param.partial_offset = offset;
	param.partial_size = size;

	return new_kcached_job_common(dmc, index, &param, NULL, TYPE_DISK, NULL);
}
#endif

#ifdef MY_ABC_HERE
// For md update
struct kcached_job *
new_kcached_job_md_io(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	job_param_t param = {0};

	param.type = TYPE_MD_IO;

	return new_kcached_job_common(dmc, -1, &param, NULL, TYPE_SSD, (void*)mu_blk);
}
#endif

#ifdef MY_ABC_HERE
// type should only be TYPE_NQF / TYPE_SQF
struct kcached_job *
new_kcached_job_qf(struct cache_c *dmc, int index, sector_t offset, sector_t size, param_type_t type)
{
	job_param_t param = {0};

	VERIFY_WARN(type == TYPE_NQF || type == TYPE_SQF);

	param.type = type;
	param.partial_offset = offset;
	param.partial_size = size;

	// TODO: prefix for TYPE_DISK. LAT_TYPE_DISK? LAT_CAT_DISK?
	return new_kcached_job_common(dmc, index, &param, NULL, TYPE_DISK, NULL);
}
#endif

static void
flashcache_record_latency(struct cache_c *dmc, ktime_t start)
{
	ktime_t now = ktime_get();
	int64_t us;
	
	us = ktime_us_delta(now, start);
#ifdef EC_EMULATE_U64_DIVISION
	us = div64_s64(us, IO_LATENCY_GRAN_USECS);
#else
	us /= IO_LATENCY_GRAN_USECS;	/* histogram 250us gran, scale 10ms total */
#endif
	if (us < IO_LATENCY_BUCKETS)
		/* < 10ms latency, track it */
		dmc->latency_hist[us]++;
	else
		/* else count it in 10ms+ bucket */
		dmc->latency_hist_10ms++;
}
#ifdef MY_ABC_HERE
void
handle_master_io_finish(struct cache_c *dmc)
{
	atomic_dec(&dmc->num_master_io);
	if ((0 == atomic_read(&dmc->num_master_io)) &&
		(CACHE_WAIT_OLD_IOS_COMPLETED == dmc->cache_state)) {
		wake_up(&dmc->wait_old_ios_completed_queue);
	}
}
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
void
flashcache_bio_endio_core(struct bio *bio, int error, struct cache_c *dmc,
			  ktime_t start_time, struct io_latency *plat)
{
#ifdef MY_ABC_HERE
	struct subbio_info *psubbio_info = bio->bi_private;
	struct cache_bio *cache_bio = psubbio_info->pcache_bio;
#else
	struct cache_bio *cache_bio = bio->bi_private;
#endif
	unsigned long flags = 0;
	int last_sub_bio = 0;
	char bio_str[BIO_STR_MAX] = {0};

	if (unlikely(dmc->sysctl_io_latency_hist && 
		     0 != ktime_compare(start_time, ktime_zero)))
		flashcache_record_latency(dmc, start_time);

	// Only -EIO now
	if (error && (0 == cache_bio->error)) {
		cache_bio->error = error;
	}

	spin_lock_irqsave(&dmc->endio_spin_lock, flags);
	if (atomic_inc_return(&cache_bio->num_completed_bios) == cache_bio->num_total_bios) {
		last_sub_bio = 1;
	}
	spin_unlock_irqrestore(&dmc->endio_spin_lock, flags);

	if (plat) {
		flashcache_check_process_io_latency(dmc, plat, LAT_END_BIO);
	}

	if (last_sub_bio) {
		sdbg(DF_DETAIL, "End master_bio=%s error=%d", bio_to_str(cache_bio->master_bio, bio_str, sizeof(bio_str)), cache_bio->error);
		bio_endio_wrapper(cache_bio->master_bio, cache_bio->error);
		handle_master_io_finish(dmc);
		mempool_free(cache_bio, _cache_bio_pool);
	} else {
		// Still has sub bios, just record error
		sdbg(DF_DETAIL, "bio=%p error=%d, don't end master_bio=%p", bio, cache_bio->error, cache_bio->master_bio);
	}
#ifdef MY_ABC_HERE
	// Shouled remove it
	kfree(psubbio_info);
#endif
	// Free data in sub bio
	bio_put(bio);
}

#else /* NOT MY_DEF_HERE */

void
flashcache_bio_endio_core(struct bio *bio, int error, struct cache_c *dmc,
			  ktime_t start_time, struct io_latency *plat)
{
	struct cache_bi_private *cache_bi_private = (struct cache_bi_private *)bio->bi_private;

	if (unlikely(dmc->sysctl_io_latency_hist &&
		     0 != ktime_compare(start_time, ktime_zero)))
		flashcache_record_latency(dmc, start_time);

	if (plat) {
		flashcache_check_process_io_latency(dmc, plat, LAT_END_BIO);
	}

	bio->bi_private = cache_bi_private->master_bio_private;
	mempool_free(cache_bi_private, _cache_bi_private_pool);
	bio_endio_wrapper(bio, error);
	handle_master_io_finish(dmc);
}

#endif /* MY_DEF_HERE */

void
flashcache_bio_endio(struct bio *bio, int error,
		     struct cache_c *dmc, ktime_t start_time,
		     struct io_latency *plat)
{
#ifdef CONFIG_SYNO_DATA_CORRECTION
#ifdef MY_DEF_HERE
#ifdef MY_ABC_HERE
	struct subbio_info *psubbio_info = bio->bi_private;
	struct cache_bio *cache_bio = psubbio_info->pcache_bio;
#else
	struct cache_bio *cache_bio = bio->bi_private;
#endif
#endif
	int wait_abort_bio = 0;

	if (bio->bi_flags & (1 << BIO_CORRECTION_ERR)) {
#ifdef MY_DEF_HERE
		cache_bio->master_bio->bi_flags |= 1 << BIO_CORRECTION_ERR;
#endif
		sdbg(DF_CORRECTION, "Finish IO: set cache master bio's BIO_CORRECTION_ERR"
			"flags finish, bio_sector=%llu", (u64)bio_bi_sector(bio));
	} else if (bio->bi_flags & (1 << BIO_CORRECTION_ABORT)) {
		sdbg(DF_CORRECTION, "Finish IO: correction abort flag to =%llu", (u64)bio_bi_sector(bio));
		wait_abort_bio = correction_handle_abort_io_finish(dmc, bio, error, start_time, plat);
	}
	if (likely(!wait_abort_bio)) {
		flashcache_bio_endio_core(bio, error, dmc, start_time, plat);
	}
#else
	flashcache_bio_endio_core(bio, error, dmc, start_time, plat);
#endif /* CONFIG_SYNO_DATA_CORRECTION */
}

#ifdef MY_ABC_HERE
static void
remove_cacheblk_from_lru_list(struct cache_c *dmc, int set, int start_index, struct cacheblock *cacheblk)
{
	VERIFY_WARN(dmc && cacheblk);

	/* Remove from LRU */
	if (likely((cacheblk->lru_prev != FLASHCACHE_LRU_NULL) ||
		   (cacheblk->lru_next != FLASHCACHE_LRU_NULL))) {
		// This node is in the LRU list

		if (cacheblk->lru_prev != FLASHCACHE_LRU_NULL)
			// Has an previous Node, No need to change head
			dmc->cache[cacheblk->lru_prev + start_index].lru_next =
				cacheblk->lru_next;
		else
			// This is the head node
			dmc->cache_sets[set].lru_head = cacheblk->lru_next;
		if (cacheblk->lru_next != FLASHCACHE_LRU_NULL)
			// Has an next node, No need to change tail
			dmc->cache[cacheblk->lru_next + start_index].lru_prev =
				cacheblk->lru_prev;
		else
			// The last node
			dmc->cache_sets[set].lru_tail = cacheblk->lru_prev;
	}
}

#endif
void
flashcache_reclaim_lru_movetail(struct cache_c *dmc, int index)
{
	int set = index / dmc->assoc;
	int start_index = set * dmc->assoc;
	int my_index = index - start_index;
	struct cacheblock *cacheblk = &dmc->cache[index];

#ifdef MY_ABC_HERE
	remove_cacheblk_from_lru_list(dmc, set, start_index, cacheblk);
#else
	/* Remove from LRU */
	if (likely((cacheblk->lru_prev != FLASHCACHE_LRU_NULL) ||
		   (cacheblk->lru_next != FLASHCACHE_LRU_NULL))) {
		if (cacheblk->lru_prev != FLASHCACHE_LRU_NULL)
			dmc->cache[cacheblk->lru_prev + start_index].lru_next = 
				cacheblk->lru_next;
		else
			dmc->cache_sets[set].lru_head = cacheblk->lru_next;
		if (cacheblk->lru_next != FLASHCACHE_LRU_NULL)
			dmc->cache[cacheblk->lru_next + start_index].lru_prev = 
				cacheblk->lru_prev;
		else
			dmc->cache_sets[set].lru_tail = cacheblk->lru_prev;
	}
#endif
	/* And add it to LRU Tail */
	cacheblk->lru_next = FLASHCACHE_LRU_NULL;
	cacheblk->lru_prev = dmc->cache_sets[set].lru_tail;
	if (dmc->cache_sets[set].lru_tail == FLASHCACHE_LRU_NULL)
		dmc->cache_sets[set].lru_head = my_index;
	else
		dmc->cache[dmc->cache_sets[set].lru_tail + start_index].lru_next = 
			my_index;
	dmc->cache_sets[set].lru_tail = my_index;
}

#ifdef MY_ABC_HERE
#else
static int 
cmp_dbn(const void *a, const void *b)
{
	if (((struct dbn_index_pair *)a)->dbn < ((struct dbn_index_pair *)b)->dbn)
		return -1;
	else
		return 1;
}

static void
swap_dbn_index_pair(void *a, void *b, int size)
{
	struct dbn_index_pair temp;
	
	temp = *(struct dbn_index_pair *)a;
	*(struct dbn_index_pair *)a = *(struct dbn_index_pair *)b;
	*(struct dbn_index_pair *)b = temp;
}

void
flashcache_sort_writes(struct dbn_index_pair *writes_list, int nr_writes, int set)
{
	if (unlikely(nr_writes == 0))
		return;

	// But due to max_clean_ios_set limitation, most of time it only has one write
	sort(writes_list, nr_writes, sizeof(struct dbn_index_pair),
	     cmp_dbn, swap_dbn_index_pair);
}

/*
 * We have a list of blocks to write out to disk.
 * 1) Sort the blocks by dbn.
 * 2) (sysctl'able) See if there are any other blocks in the same set
 * that are contig to any of the blocks in step 1. If so, include them
 * in our "to write" set, maintaining sorted order.
 * Has to be called under the cache spinlock !
 */
void
flashcache_merge_writes(struct cache_c *dmc, struct dbn_index_pair *writes_list, 
			int *nr_writes, int set)
{
	int start_index = set * dmc->assoc;
	int end_index = start_index + dmc->assoc;
	int old_writes = *nr_writes;
	int new_inserts = 0;
	struct dbn_index_pair *set_dirty_list = NULL;
	int ix, nr_set_dirty;
	struct cacheblock *cacheblk;

	if (unlikely(*nr_writes == 0))
		return;
	sort(writes_list, *nr_writes, sizeof(struct dbn_index_pair),
	     cmp_dbn, swap_dbn_index_pair);

	set_dirty_list = kmalloc(dmc->assoc * sizeof(struct dbn_index_pair), GFP_ATOMIC);
	if (set_dirty_list == NULL) {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate atomic memory for merge write");
		}
#endif
		goto out;
	}
	nr_set_dirty = 0;
	// SYNO: find the dirty block in this set, add to set_dirty_list
	for (ix = start_index ; ix < end_index ; ix++) {
		cacheblk = &dmc->cache[ix];
		/*
		 * Any DIRTY block in "writes_list" will be marked as 
		 * DISKWRITEINPROG already, so we'll skip over those here.
		 */
		if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
			set_dirty_list[nr_set_dirty].dbn = cacheblk->dbn;
			set_dirty_list[nr_set_dirty].index = ix;
			nr_set_dirty++;
		}
	}
	if (nr_set_dirty == 0)
		goto out;
	sort(set_dirty_list, nr_set_dirty, sizeof(struct dbn_index_pair),
	     cmp_dbn, swap_dbn_index_pair);
	// SYNO: check each dirty block in the set_dirty_list, if they can be merge to existed write_list...
	for (ix = 0 ; ix < nr_set_dirty ; ix++) {
		int back_merge, k;
		int i;

		cacheblk = &dmc->cache[set_dirty_list[ix].index];
		back_merge = -1;
		VERIFY_WARN((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY);
		for (i = 0 ; i < *nr_writes ; i++) {
			int insert;
			int j = 0;

			insert = 0;
			if (cacheblk->dbn + dmc->block_size == writes_list[i].dbn) {
				/* cacheblk to be inserted above i */
				insert = 1;
				j = i;
				back_merge = j;
			}
			if (cacheblk->dbn - dmc->block_size == writes_list[i].dbn ) {
				/* cacheblk to be inserted after i */
				insert = 1;
				j = i + 1;
			}
			VERIFY_WARN(j < dmc->assoc);
			if (insert) {
				set_wb_attrs(dmc, set_dirty_list[ix].index, 1);
				/* 
				 * Shift down everthing from j to ((*nr_writes) - 1) to
				 * make room for the new entry. And add the new entry.
				 */
				for (k = (*nr_writes) - 1 ; k >= j ; k--)
					writes_list[k + 1] = writes_list[k];
				writes_list[j].dbn = cacheblk->dbn;
				writes_list[j].index = cacheblk - &dmc->cache[0];
				(*nr_writes)++;
				VERIFY_WARN(*nr_writes <= dmc->assoc);
				new_inserts++;
				if (back_merge == -1)
					atomic64_inc(&dmc->flashcache_stats.front_merge);
				else
					atomic64_inc(&dmc->flashcache_stats.back_merge);
				VERIFY_WARN(*nr_writes <= dmc->assoc);
				break;
			}
		}
		/*
		 * If we did a back merge, we need to walk back in the set's dirty list
		 * to see if we can pick off any more contig blocks. Forward merges don't
		 * need this special treatment since we are walking the 2 lists in that 
		 * direction. It would be nice to roll this logic into the above.
		 */
		if (back_merge != -1) {
			for (k = ix - 1 ; k >= 0 ; k--) {
				int n;

				if (set_dirty_list[k].dbn + dmc->block_size != 
				    writes_list[back_merge].dbn)
					break;
				set_wb_attrs(dmc, set_dirty_list[k].index, 1);
				for (n = (*nr_writes) - 1 ; n >= back_merge ; n--)
					writes_list[n + 1] = writes_list[n];
				writes_list[back_merge].dbn = set_dirty_list[k].dbn;
				writes_list[back_merge].index = set_dirty_list[k].index;
				(*nr_writes)++;
				VERIFY_WARN(*nr_writes <= dmc->assoc);
				new_inserts++;
				atomic64_inc(&dmc->flashcache_stats.back_merge);
				VERIFY_WARN(*nr_writes <= dmc->assoc);				
			}
		}
	}
	VERIFY_WARN((*nr_writes) == (old_writes + new_inserts));
out:
	if (set_dirty_list)
		kfree(set_dirty_list);
}

#endif /* not MY_ABC_HERE */

extern struct dm_io_client *flashcache_io_client; /* Client memory pool*/

int
flashcache_dm_io_sync_vm(struct cache_c *dmc, struct dm_io_region *where, int comp_dm_op, void *data)
{
	unsigned long error_bits = 0;
	int ret = 0;
	struct dm_io_request iorq = {0};

	set_bi_op_and_flags(&iorq, comp_dm_op);

	iorq.mem.type = DM_IO_VMA,
	iorq.mem.ptr.vma = data,
	iorq.mem.offset = 0,
	iorq.notify.fn = NULL,
	iorq.client = flashcache_io_client,


#ifdef CONFIG_SYNO_DATA_CORRECTION
	/*
	 * Although this function won't get correcting IO,
	 * use same dm IO function as others for consistency
	 */
	ret = syno_dm_io(&iorq, 1, where, &error_bits, 0);
#else
	ret = dm_io(&iorq, 1, where, &error_bits);
#endif

	if (ret) {
		goto err;
	}
	if (error_bits) {
		ret = error_bits;
		goto err;
	}
err:
	return ret;
}

void
flashcache_update_sync_progress(struct cache_c *dmc)
{
	u_int64_t dirty_pct;
#ifdef MY_ABC_HERE
	static u_int64_t last_dirty_pct = -1;
#endif
	
	if (compatible_mod(atomic64_read(&dmc->flashcache_stats.cleanings), 1000))
		return;
#ifdef MY_ABC_HERE
	if (!dmc->nr_dirty || !dmc->size)
		return;
#else
	if (!dmc->nr_dirty || !dmc->size || !printk_ratelimit())
		return;
#endif

#ifdef EC_EMULATE_U64_DIVISION
	dirty_pct = div64_u64((u_int64_t)dmc->nr_dirty * 100, dmc->size);
#else
	dirty_pct = ((u_int64_t)dmc->nr_dirty * 100) / dmc->size;
#endif

#ifdef MY_ABC_HERE
	if (last_dirty_pct == dirty_pct) {
		return;
	} else {
		last_dirty_pct = dirty_pct;
	}
#endif
	printk(KERN_INFO "Flashcache: Cleaning %d Dirty blocks, Dirty Blocks pct %llu%%", 
	       dmc->nr_dirty, dirty_pct);
	printk(KERN_INFO "\r");
}

static inline unsigned int latency_level_to_ms(unsigned char level)
{
	if (200 > level) {
		return level * 5;
	} else if (219 > level) {
		return (level - 199) * 1000;
	} else if (246 > level) {
		return (level - 219) * 4000 + 20000;
	} else {
		return 128000;
	}
}

static inline unsigned char jiffy_to_latency_level(unsigned long jiffy)
{
	// XXX: change to assembly if we have performance issue?
	unsigned int ms = jiffies_to_msecs(jiffy);

	if (1000 > ms) {
		// 0 - 199
		return ms / 5;
	} else if (20000 > ms) {
		// 200 - 218
		return ms / 1000 + 199; // (ms - 1000) / 1000 + 200
	} else if (128000 > ms) {
		// 219 - 245
		return ms / 4000 + 214; // (ms - 20000) / 4000 + 219
	} else {
		return 246;
	}
}

void
flashcache_record_io_latency(enum latency_step step, struct io_latency *plat)
{
	int i = 0;
	unsigned long jiffy = jiffies;

	if (likely(plat->entry_cnt < LAT_TYPE_NUM)) {
		plat->entries[plat->entry_cnt].step = step;
		plat->entries[plat->entry_cnt].level =
			jiffy_to_latency_level(jiffy - plat->last_jiffy);
		plat->entry_cnt++;
		plat->last_jiffy = jiffy;
	} else {
		if (printk_ratelimit()) {
			serr("IO steps exceeds %d, step: %d", LAT_TYPE_NUM, step);
			for (i = 0; i < LAT_TYPE_NUM; ++i) {
				serr("step: %d level: %d", plat->entries[i].step,
					plat->entries[i].level);
			}
		}
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
void flashcache_process_lat_stats(struct timer_list *timer)
#else
void flashcache_process_lat_stats(unsigned long data)
#endif
{
	int i = 0, len = 0;
	int j = 0;
	int type_idx = 0;
	unsigned int event_cnt = 0;
	unsigned int event_percentile = 0;
	unsigned int tmp = 0;
	unsigned long lat_sum = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct cache_c *dmc = from_timer(dmc, timer, latency_timer);
#else
	struct timer_list *timer = (struct timer_list *)data;
	struct cache_c *dmc = container_of(timer, struct cache_c, latency_timer);
#endif
	struct io_latency_stats *stats_all = NULL;
	struct io_latency_stats *stats_per_type = NULL;
	unsigned long itvl_jiffy = msecs_to_jiffies(dmc->sysctl_latency_diagnose_itvl_ms);
	char output[768] = {0};
	unsigned long flags = 0;

	spin_lock_irqsave(&dmc->lat_spin_lock, flags);

	stats_all = dmc->last_itvl_lat_stats[dmc->lat_stats_idx];
	dmc->lat_stats_idx = ALT_LAT_STATS_IDX(dmc);

	spin_unlock_irqrestore(&dmc->lat_spin_lock, flags);

	for (type_idx = 0, stats_per_type = stats_all;
		type_idx < CACHE_IO_TYPE_NUM;
		++type_idx, ++stats_per_type) {

		if (stats_per_type->highest_lat_jiffy < dmc->lat_thresholds_jiffies[type_idx]) {
			continue;
		}

		for (i = 0; i < stats_per_type->highest_entry_cnt; ++i) {
			len += snprintf((char *)output + len, sizeof(output) - len, "%u:%u ",
				stats_per_type->highest_lat_entry[i].step,
				latency_level_to_ms(stats_per_type->highest_lat_entry[i].level));
		}

		len += snprintf((char *)output + len, sizeof(output) - len, "stats: ");

		for (i = 0; i < LAT_TYPE_NUM; ++i) {
			tmp = 0;
			event_cnt = 0;
			lat_sum = 0;

			for (j = 0; j < LAT_LEVEL_NUM; ++j) {
				event_cnt += stats_per_type->lat_table[i][j];
				lat_sum += latency_level_to_ms(j) * stats_per_type->lat_table[i][j];
			}
			if (0 == event_cnt) {
				continue;
			}

			event_percentile = (event_cnt * dmc->sysctl_lat_perc_ten_thousandth) / 10000;
			for (j = 0; j < LAT_LEVEL_NUM; ++j) {
				tmp += stats_per_type->lat_table[i][j];
				if (tmp >= event_percentile) {
					break;
				}
			}

			len += snprintf((char *)output + len, sizeof(output) - len, "%d:%lu:%u ",
				i, lat_sum / event_cnt, latency_level_to_ms(j));
		}

		/* If change format need to change syslog filter as well */
		sprint("[LAT_DIAGNOSE_TAG] %d over_thres: %u Lat: %u ms %s",
			type_idx, stats_per_type->over_thres_cnt,
			jiffies_to_msecs(stats_per_type->highest_lat_jiffy), output);
		memset(output, 0, sizeof(output));
		len = 0;
	}

	memset(stats_all, 0, sizeof(dmc->last_itvl_lat_stats[0]));

	timer->expires = jiffies + itvl_jiffy;
	add_timer(timer);
	return;
}

void flashcache_process_io_latency(struct cache_c *dmc, struct io_latency *plat, enum latency_step step)
{
	int i = 0;
	unsigned long lat_jiffies = 0;
	unsigned long flags = 0;
	struct io_latency_stats *stats = NULL;

	spin_lock_irqsave(&dmc->lat_spin_lock, flags);

	/* Record last latency in the lock to include the time of waiting */
	flashcache_record_io_latency(step, plat);

	lat_jiffies = plat->last_jiffy - plat->start_jiffy;

	stats = &dmc->last_itvl_lat_stats[dmc->lat_stats_idx][plat->io_type];

	if (lat_jiffies >= dmc->lat_thresholds_jiffies[plat->io_type]) {
		stats->over_thres_cnt++;
	}

	if (lat_jiffies > stats->highest_lat_jiffy) {
		stats->highest_lat_jiffy = lat_jiffies;
		stats->highest_entry_cnt = plat->entry_cnt;
		memcpy(stats->highest_lat_entry, plat->entries, sizeof(plat->entries));
	}

	for (i = 0; i < plat->entry_cnt; ++i) {
		stats->lat_table[plat->entries[i].step][plat->entries[i].level]++;
	}

	spin_unlock_irqrestore(&dmc->lat_spin_lock, flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
int
flashcache_alloc_md_bio(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	struct bio_vec *bvec = NULL;
	int ret = -1;
	struct page *page = NULL;
	unsigned long addr = 0;
	int page_allocated = 0;

	if (likely((dmc->sysctl_error_inject & MD_ALLOC_SECTOR_ERROR) == 0)) {
		// Only 1 bvec
		job->md_bio  = bio_alloc_bioset(GFP_NOIO, 1, pcache_bio_set);

	} else
		dmc->sysctl_error_inject &= ~MD_ALLOC_SECTOR_ERROR;

	// Check if md_bio is allocated
	if (unlikely(job->md_bio == NULL)) {
		atomic_inc(&job->dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate md bio");
		}
#endif
		ret = -ENOMEM;
		goto err;
	}

	/*
	 * NOTE: Original flashcache driver allows bvec to point to pages (instead of a page)
	 * However, it conflicts with bvec's propery (should point to a single page)
	 * So it will be checked here
	 */
	VERIFY_WARN(MD_BLOCK_BYTES(job->dmc) <= PAGE_SIZE);

	addr = __get_free_pages(GFP_NOIO, get_order(MD_BLOCK_BYTES(job->dmc)));
	if (addr)
		// Convert logical address to page
		page = virt_to_page(addr);

	if (unlikely(page == NULL)) {
		ret = -ENOMEM;
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate pag");
		}
#endif
		goto err;
	}

	/*
	 * Refere to md_super_write()
	 * For normal bios in block-core, those pages would be freed by kernel's bio_end_io()
	 * However, this bio is created by ourselves to pass to dm_io function. So its page should be
	 * freed in flashacache_md_bio()
	 */
	if (MD_BLOCK_BYTES(job->dmc) != bio_add_page(job->md_bio, page, MD_BLOCK_BYTES(job->dmc), 0)) {
		serr("Add page to bio failed");
		goto err;
	}
	page_allocated = 1;

	bvec = __bvec_iter_bvec(job->md_bio->bi_io_vec, job->md_bio->bi_iter);
	if (NULL == bvec) {
		serr("Get null bvec");
		goto err;
	}

	// Page to virtual address
	job->md_block = (struct flash_cacheblock *)page_address(bvec->bv_page);

	ret = 0;
err:
	if (ret) {
		if (page_allocated) {
			bio_free_pages(job->md_bio);
		} else if (addr) {
			free_pages(addr, get_order(MD_BLOCK_BYTES(job->dmc)));
		}
		if (job->md_bio) {
			bio_put(job->md_bio);
		}
	}

	return ret;
}
#else
int
flashcache_alloc_md_sector(struct kcached_job *job)
{
	struct page *page = NULL;
	struct cache_c *dmc = job->dmc;	
	
	if (likely((dmc->sysctl_error_inject & MD_ALLOC_SECTOR_ERROR) == 0)) {
		unsigned long addr;

		/* Get physically consecutive pages */
		// SYNO: NOIO won't get memory from high memory
		addr = __get_free_pages(GFP_NOIO, get_order(MD_BLOCK_BYTES(job->dmc)));
		if (addr)
			page = virt_to_page(addr);
	} else
		dmc->sysctl_error_inject &= ~MD_ALLOC_SECTOR_ERROR;
	job->md_io_bvec.bv_page = page;
	if (unlikely(page == NULL)) {
		atomic_inc(&job->dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate md sectors");
		}
#endif
		return -ENOMEM;
	}
	job->md_io_bvec.bv_len = MD_BLOCK_BYTES(job->dmc);
	job->md_io_bvec.bv_offset = 0;
	job->md_block = (struct flash_cacheblock *)page_address(page);
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
void
flashcache_free_md_bio(struct kcached_job *job)
{
	if (job->md_bio != NULL) {
		bio_free_pages(job->md_bio);
		bio_put(job->md_bio);
	}
}
#else
void
flashcache_free_md_sector(struct kcached_job *job)
{
	if (job->md_io_bvec.bv_page != NULL)
		__free_pages(job->md_io_bvec.bv_page, get_order(MD_BLOCK_BYTES(job->dmc)));
}
#endif
