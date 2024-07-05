#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/****************************************************************************
 *  flashcache_main.c
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
#include <linux/pid.h>
#include <linux/vmalloc.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21)
#include <linux/device-mapper.h>
#include <linux/bio.h>
#endif
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
#include <linux/delay.h>
#endif
#ifdef MY_ABC_HERE
#include <linux/delay.h>
#endif

#ifdef MY_ABC_HERE
#include "syno_quickflush.h"
#endif

#ifdef MY_ABC_HERE
#include "syno_md_update.h"
#endif

#ifndef DM_MAPIO_SUBMITTED
#define DM_MAPIO_SUBMITTED	0
#endif

/*
 * TODO List :
 * 1) Management of non cache pids : Needs improvement. Remove registration
 * on process exits (with  a pseudo filesstem'ish approach perhaps) ?
 * 2) Breaking up the cache spinlock : Right now contention on the spinlock
 * is not a problem. Might need change in future.
 * 3) Use the standard linked list manipulation macros instead rolling our own.
 * 4) Fix a security hole : A malicious process with 'ro' access to a file can 
 * potentially corrupt file data. This can be fixed by copying the data on a
 * cache read miss.
 */

#ifdef MY_ABC_HERE

#ifdef MY_ABC_HERE
#ifdef MY_ABC_HERE
#define DEBUG_STR "-debug"
#else
#define DEBUG_STR ""
#endif
#define FLASHCACHE_SW_VERSION "flashcache-1.0-syno-"SYNO_VERSION_NAME""DEBUG_STR

#else
#define FLASHCACHE_SW_VERSION "flashcache-1.0-syno-"

#endif /* MY_ABC_HERE*/

#else
#define FLASHCACHE_SW_VERSION "flashcache-1.0"
#endif
char *flashcache_sw_version = FLASHCACHE_SW_VERSION;

static void flashcache_read_miss(struct cache_c *dmc, struct bio* bio,
				 int index);
static void flashcache_write(struct cache_c *dmc, struct bio* bio);
static int flashcache_inval_blocks(struct cache_c *dmc, struct bio *bio,
				struct io_latency *plat, enum cache_io_type io_type);
#ifdef MY_ABC_HERE
#else
static void flashcache_dirty_writeback(struct cache_c *dmc, int index);
#endif
void flashcache_sync_blocks(struct cache_c *dmc);
static void flashcache_start_uncached_io(struct cache_c *dmc, struct bio *bio,
					struct io_latency *plat, enum cache_io_type io_type);


#ifdef CONFIG_SYNO_DATA_CORRECTION
// List
struct correction_entry *correction_list_find_entry(struct cache_c *dmc, u64 bio_sector,
		lock_type_t lock_type);

// Entry
struct correction_entry *correcting_entry_alloc(u64 bio_sector);
int correction_entry_in_correcting(struct cache_c *dmc, u64 bio_sector, lock_type_t lock_type);
void correction_entry_set_abort_bitmap(struct cache_c *dmc, u64 bio_sector);
void correction_entry_record_cache_addr(struct cache_c *dmc, int index, sector_t bio_sector);

// Range
int correction_range_set_type(struct cache_c *dmc, u64 bio_sector, correcting_type_t device_type);
correcting_type_t correction_range_get_type(struct cache_c *dmc, u64 bio_sector);

// Flow
int correction_handle_retry_io_start(struct cache_c *dmc, u64 bio_sector);
void correction_send_force_abort_bios(struct work_struct *work);
struct bio* correction_alloc_abort_bio(struct cache_c *dmc, sector_t sector);
int correction_find_cache_index(struct cache_c *dmc, sector_t start_sector);
#endif /* CONFIG_SYNO_DATA_CORRECTION */

extern u_int64_t size_hist[];

#ifdef MY_ABC_HERE
char *action_str[] = {"NONE", "READ_CACHE", "WRITE_CACHE", "READ_DISK", "WRITE_DISK", "READ_FILL", "INVALIDATE", "WRITEDISK_SYNC", "READDISKMEM", "WRITEMEMCACHE"};

static void read_disk_write_cache(struct cache_c *dmc, struct cacheblock *cacheblk, struct bio *bio, int index);

sector_t bio_last_sector(struct bio *bio)
{
	VERIFY_WARN(bio);
	return bio_bi_sector(bio) + bio_sectors(bio) - 1;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
extern struct dm_kcopyd_client *flashcache_kcp_client; /* Kcopyd client for writing back data */
#else
extern struct kcopyd_client *flashcache_kcp_client; /* Kcopyd client for writing back data */
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
extern struct dm_io_client *flashcache_io_client; /* Client memory pool*/
#endif

#ifdef SYNO_FLASHCACHE_DEBUG
struct _syno_debug syno_debug;
#endif

int cache_schedule_work(struct work_struct *work)
{
	int ret = 0;
#ifdef MY_ABC_HERE
	ret = queue_work(cache_workqueue, work);
#else
	ret = schedule_work(work);
#endif /* MY_ABC_HERE */
	return ret;
}

int cache_schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	int ret = 0;
#ifdef MY_ABC_HERE
	ret = queue_delayed_work(cache_workqueue, dwork, delay);
#else
	ret = schedule_delayed_work(dwork, delay);
#endif /* MY_ABC_HERE */
	return ret;
}

#ifdef MY_ABC_HERE
int cache_mod_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return mod_delayed_work(cache_workqueue, dwork, delay);
}
#endif

#ifdef MY_ABC_HERE
/* Job memory is allocated by vmalloc to prevent the
 * PAGE_ALLOC_COSTLY_ORDER constraint in kmalloc */
int dm_io_vmem(unsigned int num_regions,
			    struct dm_io_region *where,
			    int comp_dm_op,
			    void *mem_addr, io_notify_fn fn,
			    void *context, unsigned long bi_flags)
{
	struct dm_io_request iorq = {0};
	int ret = 0;

	set_bi_op_and_flags(&iorq, comp_dm_op);

	iorq.mem.type = DM_IO_VMA;
	iorq.mem.ptr.addr = mem_addr;
	iorq.notify.fn = fn;
	iorq.notify.context = context;
	iorq.client = flashcache_io_client;

#ifdef CONFIG_SYNO_DATA_CORRECTION
	ret = syno_dm_io(&iorq, num_regions, where, NULL, bi_flags);
#else
	ret = dm_io(&iorq, num_regions, where, NULL);
#endif
	return ret;
}
#endif

/*
 * SYNO: Don't wrap as a new function in porting due to the arguments are changed
 *	For example, md_bio only existed for 3.14...
 */
int dm_io_async_bvec(unsigned int num_regions, 
		     struct dm_io_region *where, 
		     int comp_dm_op, 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
		     struct bio *bio, io_notify_fn fn,
#else
		     struct bio_vec *bvec, io_notify_fn fn,
#endif
		     void *context, unsigned long bi_flags)
{
	struct dm_io_request iorq = {0};
	int ret = 0;

	set_bi_op_and_flags(&iorq, comp_dm_op);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	iorq.mem.type = DM_IO_BIO;
	iorq.mem.ptr.bio = bio;
#else
	iorq.mem.type = DM_IO_BVEC;
	iorq.mem.ptr.bvec = bvec;
#endif
	iorq.notify.fn = fn;
	iorq.notify.context = context;
	iorq.client = flashcache_io_client;

	/*
	 * WARNING: Should not pass all bio->bi_flags directly to dm_io
	 * Since bi_flags contain extran property (e.g. pool) that should not be set to
	 * a new bio in another pool
	 */
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (bi_flags & (~((1 << BIO_CORRECTION_ABORT) | (1 << BIO_CORRECTION_RETRY)))) {
		serr("Only allow to set BIO_CORRECTION_ABORT or BIO_CORRECTION_RETRY bitmap");
		VERIFY_WARN(0);
	}
	ret = syno_dm_io(&iorq, num_regions, where, NULL, bi_flags);
#else
	if (bi_flags) {
		serr("bi_flags should be 0");
		VERIFY_WARN(0);
	}
	ret = dm_io(&iorq, num_regions, where, NULL);
#endif
	return ret;
}

int dm_io_async_bio_wrapper(unsigned int num_regions, struct dm_io_region *where,
			    int comp_dm_op, struct bio *bio, io_notify_fn fn,
			    void *context, unsigned long bi_flags)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return dm_io_async_bvec(num_regions, where, comp_dm_op, bio, fn, context, bi_flags);
#else
	return dm_io_async_bvec(num_regions, where, comp_dm_op, bio->bi_io_vec + bio_bi_idx(bio),
			fn, context, bi_flags);
#endif
}

/* 
 * A simple 2-hand clock like algorithm is used to identify dirty blocks 
 * that lie fallow in the cache and thus are candidates for cleaning. 
 * Note that we could have such fallow blocks in sets where the dirty blocks 
 * is under the configured threshold.
 * The hands are spaced fallow_delay seconds apart (one sweep runs every 
 * fallow_delay seconds).  The interval is configurable via a sysctl. 
 * Blocks are moved to DIRTY_FALLOW_1, if they are found to be in DIRTY_FALLOW_1
 * for fallow_delay seconds or more, they are moved to DIRTY_FALLOW_1 | DIRTY_FALLOW_2, 
 * at which point they are eligible for cleaning. Of course any intervening use
 * of the block within the interval turns off these 2 bits.
 * 
 * Cleaning of these blocks happens from the flashcache_clean_set() function.
 */
void
flashcache_detect_fallow(struct cache_c *dmc, int index)
{
	struct cacheblock *cacheblk = &dmc->cache[index];
#ifdef MY_ABC_HERE
#else
	struct cache_set *cacheset = &dmc->cache_sets[index / dmc->assoc];
#endif

	if (dmc->cache_mode != FLASHCACHE_WRITE_BACK)
		return;

#ifdef MY_ABC_HERE
	/* Only used as a hint. It's ok if we didn't mark a dirty block as
	 * fallow due to race condition, it will be handled next round */
	if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) != DIRTY ||
		cacheblk->cache_state & DIRTY_FALLOW_2) {
		return;
	}

	down_read(&dmc->oqf.bitmap_rwsem);
	spin_lock_irq(&dmc->cache_spin_lock);
#endif
	if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
		if ((cacheblk->cache_state & DIRTY_FALLOW_1) == 0) {
			cb_state_add_bits_update_counts(dmc, cacheblk, DIRTY_FALLOW_1);
		} else if ((cacheblk->cache_state & DIRTY_FALLOW_2) == 0) {
			cb_state_add_bits_update_counts(dmc, cacheblk, DIRTY_FALLOW_2);
#ifdef MY_ABC_HERE
			quickflush_set_volume_map(dmc, cacheblk);
			atomic_inc(&dmc->oqf.nr_fallow);
#else
			cacheset->dirty_fallow++;
#endif
		}
	}
#ifdef MY_ABC_HERE
	spin_unlock_irq(&dmc->cache_spin_lock);
	up_read(&dmc->oqf.bitmap_rwsem);
#endif
}

void
flashcache_clear_fallow(struct cache_c *dmc, int index)
{
	struct cacheblock *cacheblk = &dmc->cache[index];
#ifdef MY_ABC_HERE
#else
	struct cache_set *cacheset = &dmc->cache_sets[index / dmc->assoc];
#endif

	if (dmc->cache_mode != FLASHCACHE_WRITE_BACK)
		return;
	if (cacheblk->cache_state & FALLOW_DOCLEAN) {
		if (cacheblk->cache_state & DIRTY_FALLOW_2) {
#ifdef MY_ABC_HERE
			/* Do not unset fallow bitmap. A bit represents
			 * multiple volume block, let the writeback thread
			 * scan the volume blocks to unset it. */
			atomic_dec(&dmc->oqf.nr_fallow);
#else
			VERIFY_WARN(cacheset->dirty_fallow > 0);
			cacheset->dirty_fallow--;
#endif
		}
		cb_state_remove_bits_update_counts(dmc, cacheblk, FALLOW_DOCLEAN);
	}
}

#ifdef MY_ABC_HERE
int is_writeback_crash_safe(struct cache_c *dmc)
{
	int ret = 0;

	/* Our daemon has disable cache function and there're no dirty blocks */
	if ((FLASHCACHE_WRITE_BACK == dmc->cache_mode) && (0 == dmc->nr_dirty) &&
		(0 == dmc->sysctl_cache_all) && (0 == dmc->nr_synced)) {
		ret = 1;
	}

	return ret;
}

int is_writeback_crash_safe_set_bypass(struct cache_c *dmc)
{
	int ret = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&dmc->bypass_lock, flags);
	if (is_writeback_crash_safe(dmc)) {
		dmc->bypass_cache = 1;
		ret = 1;
	}
	spin_unlock_irqrestore(&dmc->bypass_lock, flags);

	return ret;
}
#endif


#ifdef MY_ABC_HERE
static DEFINE_SPINLOCK(flashache_kmap_lock);

/*
 * Write job->mem_addr to bio synchronously
 */
static void write_mem_to_bio_sync(void *mem_addr, sector_t sector, struct bio *bio)
{
	unsigned char *mem_start = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	struct bvec_iter iter = {0};
	struct bio_vec bvl = {0};
#else
	int iter;
	struct bio_vec *bvl = NULL;
#endif
	struct bio_vec *pbvl = NULL;
	void *bvec_page_addr = NULL;
	void *bvec_offset_addr = NULL;
	unsigned long flags;
	unsigned int handled_bytes = 0;
	sector_t offset_sectors_in_cache_block = 0;

	VERIFY_WARN(bio);
	VERIFY_WARN(mem_addr);

	VERIFY_WARN(bio_bi_sector(bio) >= sector);
	offset_sectors_in_cache_block = bio_bi_sector(bio) - sector;

	bio_for_each_segment(bvl, bio, iter) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		pbvl = &bvl;
#else
		pbvl = bvl;
#endif
		/*
		 * For alpine platform, a page will accessed by multpile I/Os
		 * So use a spin_lock here
		 */
		spin_lock_irqsave(&flashache_kmap_lock, flags);

		// Don't use page_addrss due to it is too low-level function
		bvec_page_addr = kmap_atomic(pbvl->bv_page);
		VERIFY_WARN(bvec_page_addr != NULL);

		bvec_offset_addr = bvec_page_addr + pbvl->bv_offset;

		// Here to_bytes doesn't overflow here, due to the offset is small
		mem_start = (unsigned char *)mem_addr + to_bytes(offset_sectors_in_cache_block) + handled_bytes;

		memcpy(bvec_offset_addr, mem_start, pbvl->bv_len);

		kunmap_atomic(bvec_page_addr);
		bvec_page_addr = NULL;
		spin_unlock_irqrestore(&flashache_kmap_lock, flags);

		handled_bytes += pbvl->bv_len;
	}

	VERIFY_WARN(handled_bytes == bio_bi_size(bio));
}

// Dump memory as the output of xxd command
void mem_dump(struct kcached_job *job)
{
	unsigned char *mem_addr = job->mem_addr;
	unsigned long bytes = job->mem_data_byte;
	int i = 0;
	char str[512] = {0};
	int n = 0;
	int len = sizeof(str)/sizeof(str[0]);

	sdbg(DF_DETAIL, "dump start mem_addr=%p bytes=%lu bio sector=%llu",
			job->mem_addr, bytes, (u64)bio_bi_sector(job->bio));

	for (i = 0; i < bytes; i += 2) {
		if (0 == i % 16) {
			n += snprintf(str + n, len - n, "%08x: ", i);
		}
		n += snprintf(str + n, len - n, "%02x%02x", *(mem_addr + i), *(mem_addr + i + 1));

		if (0 == ((i + 2) % 16)) {
			n += snprintf(str + n, len - n, "\n");
			printk(KERN_DEBUG "%s", str);
			n = 0;
		} else {
			n += snprintf(str + n, len - n, " ");
		}
	}

	sdbg(DF_DETAIL, "dump finish");
}
#endif

#ifdef MY_ABC_HERE
// Don't end bio for performing uncached I/O later
int no_need_to_end_bio(struct cache_c *dmc, int error, int disk_error, struct kcached_job *job)
{
	int no_need = 0;
	int ssd_error = 0;

	// For WRITEMEMCACHE, don't set disk_error and treat it as accessing SSD error
	if ((0 != error) && (0 == disk_error)) {
		ssd_error = 1;
	}

	if (!ssd_error) {
		// Must end bio
		goto end;
	}

	// When access SSD error

	if ((FLASHCACHE_WRITE_AROUND == dmc->cache_mode) ||
			(FLASHCACHE_WRITE_THROUGH == dmc->cache_mode)) {
			no_need = 1;
	} else {
		// WRITE_BACK cache
		if (is_writeback_crash_safe(dmc)) {

			// Due to cache_all = 0
			VERIFY_WARN(READFILL != job->action);
			VERIFY_WARN(WRITECACHE != job->action);

			if ((READCACHE == job->action) || (WRITEMEMCACHE == job->action)) {
				no_need = 1;
			}
		}
	}

	if (1 == no_need) {
		if (printk_ratelimit()) {
			DMERR("flashcache: Don't end this bio action=%d", job->action);
		}
	}

end:
	return no_need;
}
#endif

/* under atomic context */
void
flashcache_handle_job_finish(struct cache_c *dmc, struct kcached_job *job)
{
	unsigned long flags = 0;
	struct cacheblock *cb = &dmc->cache[job->index];

	/*
	 * The INPROG flag is still set. We cannot turn that off until all the pending requests
	 * processed. We need to loop the pending requests back to a workqueue. We have the job,
	 * add it to the pending req queue.
	 */
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);


	if (unlikely(job->error || cb->nr_queued > 0)) {
		/*
		 * SYNO:
		 * Remove io range in do_pending_xxx
		 * XXX: Must consider following two cases
		 *	Case: nr_queue > 0
		 *	Case: error
		 */
		if (!job->error) {
			// redundent for WRITECACHE
			cacheblk_add_data_range(cb, job->action_bitmap);
		}
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		push_pending(job);
		sdbg(DF_DETAIL, "act=%s push_pending - nr_queued=%d", action_str[job->action], cb->nr_queued);
		cache_schedule_pending();
	} else {
		// READDISKMEM won't go here
		if (WRITEMEMCACHE == job->action) {
			cacheblk_unset_all_io_range_change_state(dmc, cb);
		} else {
			cacheblk_remove_io_range_change_state(dmc, cb, job->action_bitmap);
		}

		if (WRITECACHE == job->action) {
			/* In WRITECACHE case without error, data range and dirty range should be
			 * set already. (legacy for WT mode?) */
			// TODO: consider remove
			VERIFY_WARN(DIRTY & cb->cache_state);
			VERIFY_WARN((cb->data_bitmap & job->action_bitmap) == job->action_bitmap);
			VERIFY_WARN((cb->dirty_bitmap & job->action_bitmap) == job->action_bitmap);
			cacheblk_add_data_range_and_dirty_range(cb, job->action_bitmap);
		} else {
			cacheblk_add_data_range(cb, job->action_bitmap);
		}
		cacheblk_dec_num_concurrent_and_return(cb);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		if (WRITECACHE == job->action) {
			atomic_dec(&dmc->num_write_cache);
			wake_up(&dmc->wait_io_done_queue);
		}
		push_to_free_jobs(job);
		cache_schedule_to_free_jobs();
		sdbg(DF_DETAIL, "act=%s finish io", action_str[job->action]);
	}
}

/*
 * SYNO: Change its suffix to _atomic to emphasize this function SHOULD NOT go sleep
 * since it might be called under spin lock
 */
void 
flashcache_io_callback_atomic(unsigned long error, void *context)
{
	struct kcached_job *job = (struct kcached_job *) context;
	struct cache_c *dmc = job->dmc;
	struct bio *bio;
	unsigned long flags;
	int index = job->index;
	struct cacheblock *cacheblk = &dmc->cache[index];
	unsigned long disk_error = 0;
#ifdef CONFIG_SYNO_DATA_CORRECTION
	int error_bits = 0;
#else
	int error_bits = error;  /* record the error bit */
#endif /* CONFIG_SYNO_DATA_CORRECTION */

	VERIFY_WARN(index != -1);
	bio = job->bio;
	VERIFY_WARN(bio != NULL);
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (error & (1 << BIO_CORRECTION_ERR)) {
		bio->bi_flags |= 1 << BIO_CORRECTION_ERR;
		// Remove extra information from DM layer
		error &= ~(1 << BIO_CORRECTION_ERR);
		sdbg(DF_CORRECTION, "remove BIO_CORRECTION_ERR from bio, error=%lu", error);
	}
	error_bits = error;
#endif /* CONFIG_SYNO_DATA_CORRECTION */
	if (unlikely(error)) {
		error = -EIO;
		if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
			/*
			 * limit the message number to avoid kernel print a lot of messages and busy
			 * When action is WRITECACHE, it writes to 2 regions (1: disk 2: ssd).
			 * So when error_bit is 2, it fails to write to SSD
			 */
			if ((job->action == WRITECACHE && error_bits == 2)
					|| (job->action == READFILL && error_bits == 1)
					|| (job->action == READCACHE && error_bits == 1)) {
				if (printk_ratelimit()) {
					DMERR("flashcache_io_callback_atomic: io error %ld block %llu action %d bypass=%d error_bits=%d",
						  error, (u64)job->job_io_regions.disk.sector, job->action, dmc->bypass_cache, error_bits);
				}
			}
		} else if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
			if (printk_ratelimit()) {
				DMERR("flashcache_io_callback_atomic: WRITE_BACK mode gets error io error %ld action=%d bypass=%d error_bits=%d", 
					error, job->action, dmc->bypass_cache, error_bits);
			}
		} else {
			DMERR("flashcache_io_callback_atomic: io error %ld block %llu action %d",
				  error, (u64)job->job_io_regions.disk.sector, job->action);
		}

		if (!dmc->bypass_cache) {
			if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
				if (is_writeback_crash_safe_set_bypass(dmc)) {
					/* when cache in degradation or recovery mode, and second SSD fail */
					DMERR("flashcache_io_callback_atomic: WRITE_BACK mode enables BYPASS");
				} else {
					if (printk_ratelimit()) {
						DMERR("flashcache_io_callback_atomic: WRITE_BACK mode fails, can't bypass data");
					}
				}
			} else {
				DMERR("flashcache_io_callback_atomic: switching %s to BYPASS mode",
					  dmc->cache_devname);
				dmc->bypass_cache = 1;
			}
		}
	}
	job->error = error;

	switch (job->action) {
	case READDISK:
		DPRINTK("flashcache_io_callback_atomic: READDISK  %d",
			index);
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		if (unlikely(dmc->sysctl_error_inject & READDISK_ERROR)) {
			error = -EIO;
			job->error = -EIO;
			dmc->sysctl_error_inject &= ~READDISK_ERROR;
		}
		VERIFY_WARN(cacheblk->cache_state & DISKREADINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_DISK_READ, &job->io_lat);

		if (likely(error == 0)) {
			/* Kick off the write to the cache */
			job_dbg("READDISK", job);
			sdbg(DF_DETAIL, "act=%s call readfill return", action_str[job->action]);
			job->action = READFILL;
			push_io(job);
			cache_schedule_io();
			return;
		} else {
			disk_error = -EIO;
			atomic_inc(&dmc->flashcache_errors.disk_read_errors);
		}
		break;
	case READDISKMEM:
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		VERIFY_WARN(cacheblk->cache_state & DISKREADINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_DISK_READ_MEM, &job->io_lat);

		if (likely(error == 0)) {
			job_dbg("READDISKMEM", job);
			sdbg(DF_DETAIL, "act=%s call WRITEMEMCACHE", action_str[job->action]);
			job->action = WRITEMEMCACHE;
			push_io(job);
			cache_schedule_io();
			return;
		} else {
			disk_error = -EIO;
			atomic_inc(&dmc->flashcache_errors.disk_read_errors);
		}

		break;

	case WRITEMEMCACHE:
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);

		if (unlikely(error))
			atomic_inc(&dmc->flashcache_errors.ssd_write_errors);

		VERIFY_WARN(cacheblk->cache_state & DISKREADINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_MEM_WRITE_SSD, &job->io_lat);

		// In flashacache, read data from disk to cache doesn't update metadata
		if (likely(0 == error)) {
			// write to bio synchronously
			write_mem_to_bio_sync(job->mem_addr, job->job_io_regions.disk.sector, job->bio);
		} else {
			if (printk_ratelimit()) {
				DPRINTK("flashcache_io_callback_atomic: WRITEMEMCACHE failed");
			}
		}
		break;
	case READCACHE:
		DPRINTK("flashcache_io_callback_atomic: READCACHE %d",
			index);
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		if (unlikely(dmc->sysctl_error_inject & READCACHE_ERROR)) {
			error = -EIO;
			job->error = -EIO;
			dmc->sysctl_error_inject &= ~READCACHE_ERROR;
		}
		VERIFY_WARN(cacheblk->cache_state & CACHEREADINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_SSD_READ, &job->io_lat);

		if (unlikely(error))
			atomic_inc(&dmc->flashcache_errors.ssd_read_errors);
#ifdef FLASHCACHE_DO_CHECKSUMS
		if (likely(error == 0)) {
			if (flashcache_validate_checksum(job)) {
				DMERR("flashcache_io_callback_atomic: Checksum mismatch at disk offset %lu", 
				      job->job_io_regions.disk.sector);
				error = -EIO;
			}
		}
#endif
		break;
	case READFILL:
		DPRINTK("flashcache_io_callback_atomic: READFILL %d",
			index);
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		if (unlikely(dmc->sysctl_error_inject & READFILL_ERROR)) {
			error = -EIO;
			job->error = -EIO;
			dmc->sysctl_error_inject &= ~READFILL_ERROR;
		}
		if (unlikely(error))
			atomic_inc(&dmc->flashcache_errors.ssd_write_errors);
		VERIFY_WARN(cacheblk->cache_state & DISKREADINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_SSD_WRITE, &job->io_lat);

		break;
	case WRITECACHE:
		DPRINTK("flashcache_io_callback_atomic: WRITECACHE %d",
			index);
		if (unlikely(dmc->sysctl_error_inject & WRITECACHE_ERROR)) {
			error = -EIO;
			job->error = -EIO;
			dmc->sysctl_error_inject &= ~WRITECACHE_ERROR;
		}
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		VERIFY_WARN(cacheblk->cache_state & CACHEWRITEINPROG);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		flashcache_check_record_io_latency(LAT_SSD_WRITE, &job->io_lat);

		if (likely(error == 0)) {
			if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {

				spin_lock_irqsave(&dmc->cache_spin_lock, flags);
#ifdef MY_ABC_HERE
				// Set shared data in critical region
				atomic64_inc(&dmc->flashcache_stats.md_write_dirty);
				if ((cacheblk->cache_state & DIRTY) == 0) {
					dmc->nr_dirty++;
				}
				cb_state_add_bits_update_counts(dmc, cacheblk, DIRTY);
				if ((cacheblk->cache_state & SYNCED_BITS)) {
					cb_state_remove_bits_update_counts(dmc, cacheblk, SYNCED_BITS);
					dmc->nr_synced--;
					if (0 == dmc->nr_synced) {
						wake_up(&dmc->new_mu.remove_wqh);
					}
				}
#endif
#ifdef FLASHCACHE_DO_CHECKSUMS
				spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
				atomic64_inc(&dmc->flashcache_stats.checksum_store);
				flashcache_store_checksum(job);
				// Shared data should be in critical region
				sdbg(DF_DETAIL, "act=%s index=%d finish return", action_str[job->action], job->index);
				/*
				 * We need to update the metadata on a DIRTY->DIRTY as well 
				 * since we save the checksums.
				 */
#ifdef MY_ABC_HERE
				/* Have to queue md update BEFORE endio. So following flush requests
				* got correct md updates. */
				syno_mu_queue_no_flush_update(dmc, job);
#else
				spin_lock_irqsave(&dmc->cache_spin_lock, flags);
				// Should add dta bitmap here for mdblock update
				cacheblk_add_data_range_and_dirty_range(cacheblk, job->action_bitmap);
				spin_unlock_irqsrestore(&dmc->cache_spin_lock, flags);
				flashcache_md_write(job);
#endif
#else // END OF (#ifdef FLASHCACHE_DO_CHECKSUMS)

				/*
				 * Don't use data bitmap to check if metadata update is needed
				 * Due to read opertion also set it
				 *
				 * FUA always requires md update, since we don't know if the dirty bitmap
				 * has been written to SSD.
				 */
				if (cacheblk_is_dirty_range_going_to_change(cacheblk, job->action_bitmap)
					|| bio_has_fua_flags(job->bio)) {
					// Should add data_bitmap before updating metadata blocks
					cacheblk_add_data_range_and_dirty_range(cacheblk, job->action_bitmap);
					spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
#ifdef MY_ABC_HERE

					/* Have to queue md update BEFORE endio. So following flush requests
					* got correct md updates. */
					if (bio_has_fua_flags(job->bio)) {
						syno_mu_queue_no_flush_fua_update(dmc, job);
						return;
					} else {
						syno_mu_queue_no_flush_update(dmc, job);
					}
#else
					flashcache_md_write(job);
					sdbg(DF_DETAIL, "act=%s index=%d finish return", action_str[job->action], job->index);
					return;
#endif
				} else {
					spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
				}
#endif
			} else { /* cache_mode == WRITE_THROUGH */
				/* Writs to both disk and cache completed */
				VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_THROUGH);
#ifdef FLASHCACHE_DO_CHECKSUMS
				flashcache_store_checksum(job);
				atomic64_inc(&job->dmc->flashcache_stats.checksum_store);
#endif
			}
		} else {
			/* job error */
			if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH) {
				if (2 == error_bits) {
					atomic_inc(&dmc->flashcache_errors.ssd_write_errors);
				} else if (1 == error_bits) {
					disk_error = -EIO;
					atomic_inc(&dmc->flashcache_errors.disk_write_errors);
				}
			} else {
				atomic_inc(&dmc->flashcache_errors.ssd_write_errors);
			}
		}
		break;
	}
        /*
         * If we get an error in write through || write around modes,
         * we try the disk directly, after invalidating the cached block.
         * see flashcache_do_pending_error().
         * XXX - We can do the same for writeback as well. But that is more
         * work. (a) we cannot fall back to disk when a ssd read of a dirty
         * cacheblock fails (b) we'd need to handle ssd metadata write
	 * failures as well and fall back to disk in those cases as well.
	 * 
	 * We track disk errors separately. If we get a disk error (in 
	 * writethru or writearound modes) end the IO right here.
         */

	/*
	 * Go Here in normal state
	 *  WRITECACHE / READFILL / READCACHE / WRITECACHE (WT mode) / WRITEMEMCACHE
	 *
	 * Go Here only when error
	 *  READDISK
	 *  READDISKMEM
	 */

	if (no_need_to_end_bio(dmc, error, disk_error, job)) {
		/*
		 * Don't end bio for performing uncached I/O in error handing
		 * - WRITE_AROUND & WRITE_THROUGH: Any access SSD error
		 * - WRITE_BACK: In crash save mode with read SSD error
		 */
	} else {
		/*
		 * End bio in following cases (Set bio = NULL)
		 *	1. READ / Write disk with error => do_pendig_error()
		 *  2. No error happen => do_pending_no_error()
		 */
		flashcache_bio_endio(bio, error, dmc, job->io_start_time, &job->io_lat);
		job->bio = NULL;
	}

	flashcache_handle_job_finish(dmc, job);
}

void
flashcache_free_pending_jobs(struct cache_c *dmc, struct cacheblock *cacheblk, 
			     int error)
{
	struct pending_job *pending_job, *freelist = NULL;

	VERIFY_WARN(spin_is_locked(&dmc->cache_spin_lock));
	freelist = flashcache_deq_pending(dmc, cacheblk - &dmc->cache[0]);
	while (freelist != NULL) {
		pending_job = freelist;
		freelist = pending_job->next;
		VERIFY_WARN(cacheblk->nr_queued > 0);
		cacheblk->nr_queued--;
		flashcache_bio_endio(pending_job->bio, error, dmc, ktime_zero, &pending_job->io_lat);
		flashcache_free_pending_job(pending_job);
	}
	VERIFY_WARN(cacheblk->nr_queued == 0);
}

typedef struct do_pending_params {
	struct cache_c *dmc;
	struct bio *bio;
	int index;
	int error;
	int action;
	bitmap_t action_bitmap;
	struct io_latency *plat;
	ktime_t *io_start_time;
	void *mem_addr;
	sector_t sector;
} dp_params_t;

/*
 * Common error handling for everything.
 * 1) If the block isn't dirty, invalidate it.
 * 2) De-link all pending IOs that totally or partly overlap this block.
 * 3) If it was an SSD error (bio != NULL), issue the invalidated block IO and other de-linked pending IOs uncached to disk. 
 * 4) Free the job.
 * SYNO:
 *	Handing following cases:
 *	- Access SSD error (bio != 0) , perform uncached_io for this job and all pending jobs
 *	  (bio != 0 because it's still possbile to access from the disks for workaround)
 *	- Access Disk error (bio = 0), free all pending job with errror
 */
static void
flashcache_do_pending_error(dp_params_t *params)
{
	struct cache_c *dmc = params->dmc;
	struct bio *bio = params->bio;
	struct cacheblock *cacheblk = &dmc->cache[params->index];
	struct pending_job *pjob_list = NULL, *pjob = NULL;
	int write_back_crash_safe = 0;
	int pending_jobs_handled = 0;
	u_int8_t nr_concurrent = 0;
	int is_last_job = 1;
#ifdef MY_ABC_HERE
#else
	struct cache_set *cacheset = &dmc->cache_sets[params->index / dmc->assoc];
#endif


	if ((is_writeback_crash_safe_set_bypass(dmc))) {
		/*
		 * Write back cache has degraded before and we already disabled cache and flush all
		 * dirty blocks
		 */
		write_back_crash_safe = 1;
		// In some paths (e.g. dirty_writeback), the bypass_cache is not set yet, so set here
	}

	spin_lock_irq(&dmc->cache_spin_lock);

	// All the WRITEDISK_SYNC jobs that has errors go here
	if (WRITEDISK_SYNC == params->action) {
		atomic_inc(&dmc->flashcache_errors.write_disk_sync_errors);
	}

	if (!dmc->bypass_cache) {
		if (0 < dmc->limits.do_pending_err) {
			DMERR("flashcache_do_pending_error: error %d block %llu action %d",
				params->error, (u64)params->sector, params->action);
			dmc->limits.do_pending_err--;
		}
	}

	VERIFY_WARN(cacheblk->cache_state & VALID);

	nr_concurrent = cacheblk_dec_num_concurrent_and_return(cacheblk);

	if (0 != nr_concurrent) {
		is_last_job = 0;
	}

	// Only invalidate the cache block when the job is the last job
	if (is_last_job) {
		/* Invalidate block if possible */
#ifdef MY_ABC_HERE
		/* SYNCED_BITS block can not be invalidated */
		if ((cacheblk->cache_state & DIRTY) == 0 && (cacheblk->cache_state & SYNCED_BITS) == 0) {
			/* in new mu we set dirty bitmap along with states */
			VERIFY_WARN(0 == cacheblk->dirty_bitmap);
#else
		if ((cacheblk->cache_state & DIRTY) == 0) {
			/*
			* For our implementation, the dirty_bitmap might be set before an error happens
			* At the same time, DIRTY is still 0
			*/
			if (0 != cacheblk->dirty_bitmap) {
				cacheblk_unset_all_dirty_range(cacheblk);
			}
#endif
			atomic64_dec(&dmc->cached_blocks);
			atomic64_inc(&dmc->flashcache_stats.pending_inval);
			cb_state_remove_bits_update_counts(dmc, cacheblk, VALID);
#ifdef MY_ABC_HERE
			cacheblk_check_unset_pin_state(dmc, cacheblk);
#endif
			cb_state_add_bits_update_counts(dmc, cacheblk, INVALID);

		} else if ((WRITEDISK_SYNC == params->action) && (ATTR_FORCE_REMOVE == dmc->attribute)) {
			/*
			* WRITEDISK_SYNC: Remove cache or do_sync command
			*/
			if (0 < dmc->limits.dirty_inval) {
				serr("Forcibly remove cacheblock [index=%d dbn=%llu error],"
					"invalidate it\n", params->index, (u64)cacheblk->dbn);
				dmc->limits.dirty_inval--;
			}

			if (cacheblk->cache_state & DIRTY) {
				dmc->nr_dirty--;
#ifdef MY_ABC_HERE
#else
				cacheset->nr_dirty--;
#endif
				cb_state_remove_bits_update_counts(dmc, cacheblk, DIRTY);
			}


#ifdef MY_ABC_HERE
			if (cacheblk->cache_state & SYNCED_BITS) {
				cb_state_remove_bits_update_counts(dmc, cacheblk, SYNCED_BITS);
				dmc->nr_synced--;
				if (0 == dmc->nr_synced) {
					wake_up(&dmc->new_mu.remove_wqh);
				}
			}
#endif

			if (0 != cacheblk->dirty_bitmap) {
				cacheblk_unset_all_dirty_range(cacheblk);
			}

			atomic64_dec(&dmc->cached_blocks);
			atomic64_inc(&dmc->flashcache_stats.pending_inval);
			cb_state_remove_bits_update_counts(dmc, cacheblk, VALID);
#ifdef MY_ABC_HERE
			cacheblk_check_unset_pin_state(dmc, cacheblk);
#endif
			cb_state_add_bits_update_counts(dmc, cacheblk, INVALID);
		} else {
			// In other cases, if the cache block is dirty, just keep it in SSDs
			VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_BACK);
		}

	} // if (is_last_job)

	/*
	 * Check io_callback and md_write_done
	 * io_range are not removed before this function if an error happens
	 * (See kcopyd_callback as a example)
	 * bio might be NULL shometimes, so don't use bio here
	 */
	if (READDISKMEM == params->action || WRITEMEMCACHE == params->action
			|| WRITEDISK == params->action || WRITEDISK_SYNC == params->action) {
		cacheblk_unset_all_io_range_change_state(dmc, cacheblk);
	} else {
		cacheblk_remove_io_range_change_state(dmc, cacheblk, params->action_bitmap);
	}

	if (is_last_job) {
		VERIFY_WARN(0 == cacheblk->io_bitmap);
	}

	if (!is_last_job) {
		spin_unlock_irq(&dmc->cache_spin_lock);
		goto handle_single_job;
	}

	/*
	 * In case of an error in writethrough or writearound modes, if there
	 * are pending jobs, de-link them from the cacheblock so we can issue disk 
	 * IOs below.
	 */
	if (bio != NULL) {
		// SYNO: Access SSD error
		/*
		 * If is_writeback_crash_safe is true && bypass mode, the mode can be write back
		 * Note: By default, in io_callback the bio should be set to NULL in write-back mode
		 * but we change the behavior !
		 */
		if (!(is_writeback_crash_safe(dmc) && dmc->bypass_cache)) {
			VERIFY_WARN(dmc->cache_mode != FLASHCACHE_WRITE_BACK);
		}

		// SYNO: just to reduce the nr_queue count
		pjob_list = flashcache_deq_pending(dmc, cacheblk - &dmc->cache[0]);
		for (pjob = pjob_list ; pjob != NULL ; pjob = pjob->next) {
			VERIFY_WARN(cacheblk->nr_queued > 0);
			cacheblk->nr_queued--;
		}
		VERIFY_WARN(cacheblk->nr_queued == 0);
	} else {
		/*
		 * SYNO: Normal disk IO or dirty block writeback IO error
		 * Free each pending jobs (return error)
		 */
		flashcache_free_pending_jobs(dmc, cacheblk, params->error);
	}
	spin_unlock_irq(&dmc->cache_spin_lock);

handle_single_job:

	if (bio != NULL) {
		/*
		 * SYNO: Write back mode also go here and only be invalidated in some cases
		 *
		 * Cache (read/write) error in write through or write around
		 * mode. Issue the IO directly to disk. We've already invalidated
		 * the cache block above.
		 */
		if (dmc->cache_mode == FLASHCACHE_WRITE_THROUGH && (params->action == WRITECACHE || params->action == READFILL)) {
			/*
			 * Writing to SSD fails in WRITECACHE or READFILL mode
			 * We discard this bio
			 */
			params->error = 0;
			flashcache_bio_endio(bio, params->error, dmc, *params->io_start_time, params->plat);

		} else if ((FLASHCACHE_WRITE_BACK == dmc->cache_mode) && (0 == write_back_crash_safe)) {
			// TODO: Seems it's a dead logic, if a bio is presented here in writeback mode then it must be
			//       write_back_crash_safe. Replace This logic with VERIFY_WARN(0)
			VERIFY_WARN(0);

			// Should return the error to warn the file system as soon as possible
			flashcache_bio_endio(bio, params->error, dmc, *params->io_start_time, params->plat);
			flashcache_free_pending_jobs(dmc, cacheblk, params->error);
			pending_jobs_handled = 1;
		} else {
			/*
			 * Other cases enter here:
			 * - Write through mode: read SSD failed
			 * - Write back mode (crash save state): read / write SSD failed
			 * - Write around mode: read SSD failed
			 */
			spin_lock_irq(&dmc->cache_spin_lock);
			if (0 < dmc->limits.uncached_io) {
				DMERR("%s: Start to perform uncached io\n", __func__);
				dmc->limits.uncached_io--;
			}
			spin_unlock_irq(&dmc->cache_spin_lock);
			VERIFY_WARN(params->plat);
			flashcache_start_uncached_io(dmc, bio, params->plat, TYPE_DONT_CARE);
		}

		if (is_last_job && (!pending_jobs_handled)) {
			while (pjob_list != NULL) {
				pjob = pjob_list;
				pjob_list = pjob->next;
				flashcache_start_uncached_io(dmc, pjob->bio,
					&pjob->io_lat, TYPE_DONT_CARE);
				flashcache_free_pending_job(pjob);
			}
		}
	} // SYNO: if bio != NULL
}


// Must all pending job is READCACHE and no pending job are INVALIDATE
static int
check_can_pending_preread(int action, struct pending_job *freelist)
{
	struct pending_job *pending_job = NULL;
	int can_pending_preread = 0;

	VERIFY_WARN(freelist);

	if (WRITEMEMCACHE != action) {
		sdbg(DF_DETAIL, "action is not WRITEMEMCACHE");
		return 0;
	}

	pending_job = freelist;

	for (pending_job = freelist; pending_job != NULL; pending_job = pending_job->next) {

		if ((READCACHE != pending_job->action) || (INVALIDATE == pending_job->action)) {
			goto end;
		}
	}


	can_pending_preread = 1;

end:
	return can_pending_preread;
}

#ifdef MY_ABC_HERE
/* It's only a helper function used by do_pending_noerror/invalid_block_set
 * in under cache_spin_lock, out NOT under lock */
static void start_force_mu_to_invalid(struct cache_c *dmc, int index)
{
	struct cacheblock *cb = &dmc->cache[index];

	VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_BACK);
	VERIFY_WARN(0 == cb->io_bitmap);
	VERIFY_WARN(0 != cb->data_bitmap);
	VERIFY_WARN(!(cb->cache_state & BLOCK_IO_INPROG));
	VERIFY_WARN(!(cb->cache_state & FALLOW_DOCLEAN));

	cb_state_add_bits_update_counts(dmc, cb, DISKWRITEINPROG);
	cacheblk_set_all_io_range(cb);

	cacheblk_add_num_concurrent(cb);
	spin_unlock_irq(&dmc->cache_spin_lock);

	while (check_new_error_inject(dmc, SYNCED_STATE_CHANGE_WHEN_FORCE_MU)) {
		sprint("Detect sync state change when force mu flag, wait, dbn %lld",
			(u64)cb->dbn);
		msleep(5000);
		if (!(cb->cache_state & SYNCED_BITS)) {
			sprint("State change from SYNCED_BITS to valid, continue");
			break;
		}
	}

	/* The status might change from SYNCED_BITS to VALID here */
	syno_mu_queue_force_update(dmc, index);
}
#endif

/* It's only a helper function used by do_pending_noerror/invalid_block_set
 * in under cache_spin_lock, out NOT under lock */
static void start_force_wb_to_invalid(struct cache_c *dmc, int index)
{
	struct cacheblock *cb = &dmc->cache[index];
	int nr_write = 1;
#ifdef MY_ABC_HERE
	cb_wb_param_t wb_param = {0};
	unsigned long wb_flags = 0;
#endif

	VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_BACK);
	VERIFY_WARN(0 == cb->io_bitmap);
	VERIFY_WARN(0 != cb->data_bitmap);
	VERIFY_WARN(!(cb->cache_state & BLOCK_IO_INPROG));

#ifdef MY_ABC_HERE
	nr_write = quickflush_cacheblk_get_nr_writeback(dmc, index);
	spin_lock_irqsave(&dmc->plug_wb.lock, wb_flags);
	set_wb_attrs(dmc, index, nr_write);
	spin_unlock_irqrestore(&dmc->plug_wb.lock, wb_flags);
#else
	set_wb_attrs(dmc, index, nr_write);
#endif

	spin_unlock_irq(&dmc->cache_spin_lock);
#ifdef MY_ABC_HERE
	wb_param.idx = index;
	wb_param.nr_write = nr_write;
	wb_param.job_flag |= JOB_WB_FORCED;
	quickflush_dirty_writeback(dmc, wb_param);
#else
	flashcache_dirty_writeback(dmc, index);
#endif
}

static void
flashcache_do_pending_noerror(dp_params_t *params)
{
	struct cache_c *dmc = params->dmc;
	int index = params->index;
	struct pending_job *pending_job, *freelist;
	int queued;
	struct cacheblock *cacheblk = &dmc->cache[index];
	int can_pending_preread = 0;
	u_int8_t nr_concurrent = 0;

	spin_lock_irq(&dmc->cache_spin_lock);

	/*
	 * For partial access, we wait for the last one job (nr_concurrent = 1) to perform this
	 * It is OK to clear some io_bitmap, due to nr->queue > 0, no one will access anymore
	 * READFILL / READCACHE / WRITE_DISK(WT mode) enter this function
	 */

	nr_concurrent = cacheblk_dec_num_concurrent_and_return(cacheblk);

	// If it can be preread, its io_bitmap will be added back later
	if (params->action == WRITEMEMCACHE)  {
		cacheblk_unset_all_io_range(&dmc->cache[index]);
		VERIFY_WARN(0 == nr_concurrent);
	} else {
		cacheblk_remove_io_range(&dmc->cache[index], params->action_bitmap);
	}

	if (0 != nr_concurrent) {
		sdbg(DF_DETAIL, "index=%d In io, nr_concurrent=%d act=%s io_bitmap=0x%x cache_state=0x%x ",
				index, nr_concurrent, action_str[params->action], cacheblk->io_bitmap, cacheblk->cache_state);
		spin_unlock_irq(&dmc->cache_spin_lock);
		goto out;
	}

	// nr_concurrent is  0
	sdbg(DF_DETAIL, "index=%d Clean pending queue, nr_concurrent=%d act=%s io_bitmap=0x%x cache_state=0x%x ",
			index, nr_concurrent, action_str[params->action], cacheblk->io_bitmap, cacheblk->cache_state);

#ifdef MY_ABC_HERE
	if (cacheblk->cache_state & SYNCED_BITS) {
		VERIFY_WARN(!(cacheblk->cache_state & FORCEDMU));
		/* Unlocked in function */
		cb_state_remove_bits_update_counts(dmc, cacheblk, BLOCK_IO_INPROG);
		start_force_mu_to_invalid(dmc, index);
		goto out;
	}
#endif

	if (cacheblk->cache_state & DIRTY) {
		/* Unlocked in function */
		cb_state_remove_bits_update_counts(dmc, cacheblk, BLOCK_IO_INPROG);
		atomic64_inc(&dmc->flashcache_stats.do_pending_no_error_drity_writeback);
		start_force_wb_to_invalid(dmc, index);
		goto out;
	}

	DPRINTK("flashcache_do_pending: Index %d %lx",
		index, cacheblk->cache_state);
	VERIFY_WARN(cacheblk->cache_state & VALID);
	sdbg(DF_DETAIL, "try to deq and invalidate blocks...");

	freelist = flashcache_deq_pending(dmc, index);

	// This job is WRITEMEMCACHE (preread)
	can_pending_preread = check_can_pending_preread(params->action, freelist);

	// reclaim V won't be triggered Because BLOCK_IN_PROG is still set
	if (can_pending_preread) {
		sdbg(DF_DETAIL, "index=%d can_pending_preread", index);
		VERIFY_WARN(cacheblk->io_bitmap == 0);
		// don't set to INVALID
		cacheblk_set_all_io_range(cacheblk);
	} else {
#ifdef CONFIG_SYNO_DATA_CORRECTION
		sdbg(DF_CORRECTION, "Set abort bitmap if needed");
		correction_entry_record_cache_addr(dmc, index, cacheblk->dbn);
		correction_entry_set_abort_bitmap(dmc, cacheblk->dbn);
#endif
		atomic64_dec(&dmc->cached_blocks);
		atomic64_inc(&dmc->flashcache_stats.pending_inval);
		cb_state_remove_bits_update_counts(dmc, cacheblk, VALID);
#ifdef MY_ABC_HERE
		cacheblk_check_unset_pin_state(dmc, cacheblk);
		// Here the BLOCK_IN_PROG is still set
#endif
		cb_state_add_bits_update_counts(dmc, cacheblk, INVALID);
	}

	/*
	 * Already get freelist in above code
	 * No need to use "while" freelist, due to we set INVALID for this cacheblock
	 * No one can get it VALID and enqueue anymore
	 * XXX: However, it might cause the new I/O will access disks when there pending job
	 * are performing uncached I/O ?
	 */
	while (freelist != NULL) {
		VERIFY_WARN(!(cacheblk->cache_state & DIRTY));
		VERIFY_WARN(!(cacheblk->cache_state & SYNCED_BITS));
		pending_job = freelist;
		freelist = pending_job->next;
		VERIFY_WARN(cacheblk->nr_queued > 0);
		cacheblk->nr_queued--;
		if (pending_job->action == INVALIDATE) {
			// job wait for range invalidate, follwoing value should not be set
			VERIFY_WARN(0 == can_pending_preread);
			DPRINTK("flashcache_do_pending: INVALIDATE  %llu",
				bio_bi_sector(pending_job->bio));
			VERIFY_WARN(pending_job->bio != NULL);
			queued = flashcache_inval_blocks(dmc, pending_job->bio,
					&pending_job->io_lat, TYPE_DONT_CARE);
			/* The invalidate here might queue becuase below code in
			 * the loop unlock cache spinlock, external
			 * IO might come in and create a valid block. */
			if (queued) {
				atomic64_inc(&dmc->flashcache_stats.do_pending_no_error_inval);
				if (unlikely(queued < 0)) {
					/*
					* Memory allocation failure inside inval_blocks.
					* Fail this io.
					*/
					flashcache_bio_endio(pending_job->bio,
						-EIO, dmc, ktime_zero, &pending_job->io_lat);
				}
				flashcache_free_pending_job(pending_job);
				atomic64_inc(&dmc->flashcache_stats.pending_enqueue_inval_done);
				continue;
			}
		}
		if (can_pending_preread) {
			// don't unlock irq to avoid new I/O being queue that wont' be handled
			write_mem_to_bio_sync(params->mem_addr, params->sector, pending_job->bio);
			// Just make io_start_time as NULL
			flashcache_bio_endio(pending_job->bio, 0, dmc, ktime_zero, &pending_job->io_lat);
			flashcache_free_pending_job(pending_job);
			atomic64_inc(&dmc->flashcache_stats.pending_preread);
			atomic64_inc(&dmc->flashcache_stats.read_hits);
		} else {
			/*
				* Original logic
				* XXX: If unlocking here, new I/O whould be added to the queue list ?
				*/
			if (INVALIDATE == pending_job->action) {
				atomic64_inc(&dmc->flashcache_stats.pending_enqueue_inval_done);
			}
			spin_unlock_irq(&dmc->cache_spin_lock);
			DPRINTK("flashcache_do_pending: Sending down IO %llu",
				bio_bi_sector(pending_job->bio));
			/* Start uncached IO */
			flashcache_start_uncached_io(dmc, pending_job->bio,
				&pending_job->io_lat, TYPE_DONT_CARE);
			flashcache_free_pending_job(pending_job);
			spin_lock_irq(&dmc->cache_spin_lock);

		}

	}
	VERIFY_WARN(cacheblk->nr_queued == 0);

	if (can_pending_preread) {
		cacheblk_unset_all_io_range_change_state(dmc, cacheblk);
	} else {
		// SYNO: only the last partial access I/O will enter here
		VERIFY_WARN(0 == cacheblk->io_bitmap);

		if (global_tester == TEST_OQF_INVALIDATE_DELAY) {
			spin_unlock_irq(&dmc->cache_spin_lock);
			while (global_tester == TEST_OQF_INVALIDATE_DELAY) {
				serr("Get invalidate delay for %d", index);
				msleep(5000);
			}
			serr("invalidate delay done");
			spin_lock_irq(&dmc->cache_spin_lock);
		}

		cb_state_remove_bits_update_counts(dmc, cacheblk, BLOCK_IO_INPROG);
	}

	spin_unlock_irq(&dmc->cache_spin_lock);
out:

	sdbg(DF_DETAIL, "finish");
}

/*
 * Call this function when job->error is set or cacheblk->nr_queue > 0
 *
 * Note: do_pending* only called from non-atomic context
 */
void
flashcache_do_pending(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	dp_params_t params = {
		.dmc = job->dmc,
		.bio = job->bio,
		.index = job->index,
		.error = job->error,
		.action = job->action,
		.action_bitmap = job->action_bitmap,
		.plat = &job->io_lat,
		.io_start_time = &job->io_start_time,
		.mem_addr = job->mem_addr,
		.sector = job->job_io_regions.disk.sector,
	};

	if (job->error) {
		flashcache_do_pending_error(&params);
	} else {
		flashcache_do_pending_noerror(&params);
	}

	if (WRITECACHE == job->action) {
		atomic_dec(&dmc->num_write_cache);
		wake_up(&dmc->wait_io_done_queue);
	}

	flashcache_free_cache_job(job);
	if (atomic_dec_and_test(&dmc->nr_jobs)) {
		wake_up(&dmc->sync_wqh);
	}
}

#ifdef MY_ABC_HERE
void flashcache_do_pending_force_mu(struct cache_c *dmc, int idx, int error) {
	dp_params_t params = {
		.dmc = dmc,
		.bio = NULL,
		.index = idx,
		.error = error,
		.action = WRITEDISK_SYNC, // to allow force remove
		.action_bitmap = BITMAP_MASK,
		.plat = NULL,
		.io_start_time = NULL,
		.mem_addr = NULL,
		.sector = 0,
	};

	if (error) {
		flashcache_do_pending_error(&params);
	} else {
		flashcache_do_pending_noerror(&params);
	}

	if (atomic_dec_and_test(&dmc->nr_jobs)) {
		wake_up(&dmc->sync_wqh);
	}
}
#endif

void
flashcache_do_io(struct kcached_job *job)
{
	struct bio *bio = job->bio;
	int r = 0;

	VERIFY_WARN(job->action == READFILL || job->action == WRITEMEMCACHE);
#ifdef FLASHCACHE_DO_CHECKSUMS
	flashcache_store_checksum(job);
	atomic64_inc(&job->dmc->flashcache_stats.checksum_store);
#endif
	/* Write to cache device */
	atomic64_inc(&job->dmc->flashcache_stats.ssd_writes);

	job_dbg(__func__, job);

	flashcache_check_record_io_latency(LAT_DO_IO_WQ, &job->io_lat);

	if (READFILL == job->action) {
		r = dm_io_async_bio_wrapper(1, &job->job_io_regions.cache, COMP_DM_WRITE,
			bio, flashcache_io_callback_atomic, job, 0);
	} else {
		// WRITEMEMCACHE
		r = dm_io_vmem(1, &job->job_io_regions.cache, COMP_DM_WRITE, job->mem_addr,
					 flashcache_io_callback_atomic, job, 0);
	}
	VERIFY_WARN(r == 0);
	/* In our case, dm_io_async_bvec() must always return 0 */
}

#ifdef MY_ABC_HERE
// Refer to include/linux/hash.h, copy here to prevent hash function change
static __always_inline u64 hash_64_cache(u64 val, unsigned int bits)
{
	u64 hash = val;

	/*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
	u64 n = hash;
	n <<= 18;
	hash -= n;
	n <<= 33;
	hash -= n;
	n <<= 3;
	hash += n;
	n <<= 3;
	hash -= n;
	n <<= 4;
	hash += n;
	n <<= 2;
	hash += n;

	/* High bits are more random, so use them. */
	return hash >> (64 - bits);
}
#endif

/*
 * Map a block from the source device to a block in the cache device.
 */
unsigned long
hash_block(struct cache_c *dmc, sector_t dbn)
{
#ifdef MY_ABC_HERE
	unsigned long hash_res = 0;
	unsigned long block_number = 0;
	unsigned long region_idx = 0;
#endif
	unsigned long set_number = 0, value = 0;

#ifdef MY_ABC_HERE
	switch (dmc->hash_mapping) {
	case HASH_MAPPING_V1:
		block_number = dbn >> dmc->block_shift;
		// Hash value to 64 bit to make it more randomly distributed
		hash_res = hash_64_cache(block_number, 64);
		set_number = hash_res % dmc->num_sets;
		break;
	case HASH_MAPPING_V2:
		/* Mod cache size to get offset in cache for region calculating.
		 * Blocks seperate from each other by exact a cache size on volume
		 * will map to same set in the same region */
		block_number = compatible_mod(dbn >> dmc->block_shift, dmc->size);
		region_idx = compatible_div(block_number, HASH_REGION_BLOCK_NUM);
		hash_res = hash_64_cache(block_number, 64);
		if (region_idx >= dmc->tail_region_idx) {
			/* if tail is merged with last full region, the region index will
			 * be larger than tail_region_idx */
			set_number = compatible_mod(hash_res, dmc->tail_region_num_sets) +
				dmc->tail_region_idx * dmc->region_num_sets;
		} else {
			set_number = compatible_mod(hash_res, dmc->region_num_sets) +
				region_idx * dmc->region_num_sets;
		}
		break;
	case HASH_MAPPING_DISABLE:
#endif
		value = (unsigned long)
			(dbn >> (dmc->block_shift + dmc->assoc_shift));
		set_number = value % dmc->num_sets;
#ifdef MY_ABC_HERE
		break;
	}
#endif
	DPRINTK("Hash: %llu(%lu)->%lu", dbn, value, set_number);
	return set_number;
}

static void
/*
 * Find a cacheblock [i] that match the dbn (start sector of the bio) in a set
 *
 * SYNO: IN: under spin_lock_irq
 *
 * Return value:
 *	valid = i (!= -1): cacheblock[i] is VALID and its dbn match the dbn and state is VALID
 * 	invalid = i (!=-1):  cacheblock[i] is INVALID
 * 	invalid = -1: Can't find any invalid cacheblock
 */
find_valid_dbn(struct cache_c *dmc, struct bio *bio,
	       int start_index, int *valid, int *invalid)
{
	int i;
	int end_index = start_index + dmc->assoc;
	sector_t bi_start_sector = bio_bi_sector(bio);
	sector_t bi_size_sector = bio_sectors(bio);

	if (FLASHCACHE_LRU != dmc->sysctl_reclaim_policy) {
		serr("flashcache_syno only support LRU algorithm");
		VERIFY_WARN(0);
	}

	*valid = *invalid = -1;
	for (i = start_index ; i < end_index ; i++) {
		/*
		 * Search each cache block and check if bio's start sector (dbn) match a range of an cacheblk
		 */
		if (bi_start_sector >= dmc->cache[i].dbn &&
			(bi_start_sector < (dmc->cache[i].dbn + dmc->block_size)) &&
			dmc->cache[i].cache_state & VALID) {

			if ((bi_start_sector + bi_size_sector) > (dmc->cache[i].dbn + dmc->block_size)) {
				// should not enter here
				sdbg(DF_DETAIL, "dbn + size is over the cache's range Bio [sector=%llu size=%llu] \
						cacheblk.dbn=%llu\n",
						(u64)bi_start_sector, (u64)bi_size_sector, (u64)dmc->cache[i].dbn);
				VERIFY_WARN(0);
			} else {
				sdbg(DF_DETAIL, "find valid index=%d bi_start_sector=%llu cache dbn=%llu",
						i, (u64)bi_start_sector, (u64)dmc->cache[i].dbn);
				*valid = i;

				if (dmc->sysctl_reclaim_policy == FLASHCACHE_LRU &&
					((dmc->cache[i].cache_state & BLOCK_IO_INPROG) == 0)) {
					flashcache_reclaim_lru_movetail(dmc, i);
				}
				/*
				* If the block was DIRTY and earmarked for cleaning because it was old, make 
				* the block young again.
				*/
				flashcache_clear_fallow(dmc, i);
				return;
			}

		}

		if (*invalid == -1 && dmc->cache[i].cache_state == INVALID) {
			VERIFY_WARN((dmc->cache[i].cache_state & FALLOW_DOCLEAN) == 0);
			*invalid = i;
		}
	}

	if (*valid == -1 && *invalid != -1)
		if (dmc->sysctl_reclaim_policy == FLASHCACHE_LRU)
			flashcache_reclaim_lru_movetail(dmc, *invalid);
}

/* Search for a slot that we can reclaim */
static void
#ifdef MY_ABC_HERE
/*
 * Doesn't set index while there's no cache block to be reclaimed
 * SYNO: IN: Under lock
 */
find_reclaim_dbn(struct cache_c *dmc, int start_index, int *index, struct bio *bio)
#else
find_reclaim_dbn(struct cache_c *dmc, int start_index, int *index)
#endif
{
	int set = start_index / dmc->assoc;
	struct cache_set *cache_set = &dmc->cache_sets[set];
	struct cacheblock *cacheblk;
#ifdef MY_ABC_HERE
	int is_pin = 0;
	int first_pin_block_index = -1;
	int found_reclaim = 0;

	is_pin = bio_is_pin(bio);
#endif

	if (dmc->sysctl_reclaim_policy == FLASHCACHE_FIFO) {
		int end_index = start_index + dmc->assoc;
		int slots_searched = 0;
		int i;

		i = cache_set->set_fifo_next;
		while (slots_searched < dmc->assoc) {
			VERIFY_WARN(i >= start_index);
			VERIFY_WARN(i < end_index);
			if (dmc->cache[i].cache_state == VALID) {
				*index = i;
				VERIFY_WARN((dmc->cache[*index].cache_state & FALLOW_DOCLEAN) == 0);
				break;
			}
			slots_searched++;
			i++;
			if (i == end_index)
				i = start_index;
		}
		i++;
		if (i == end_index)
			i = start_index;
		cache_set->set_fifo_next = i;
	} else { /* reclaim_policy == FLASHCACHE_LRU */
		int lru_rel_index;

		lru_rel_index = cache_set->lru_head;
		while (lru_rel_index != FLASHCACHE_LRU_NULL) {
			cacheblk = &dmc->cache[lru_rel_index + start_index];

#ifdef CONFIG_SYNO_DATA_CORRECTION
			if (correction_entry_in_correcting(dmc, (u64)cacheblk->dbn, DO_LOCK)) {
				lru_rel_index = cacheblk->lru_next;
				continue;
			}
#endif
			if (cacheblk->cache_state == VALID) {


				VERIFY_WARN((cacheblk - &dmc->cache[0]) ==
				       (lru_rel_index + start_index));
				/*
				 * SYNO: cache block's DIRTY is not set here
				 * and it is going to be reclaimed
				 */
				*index = cacheblk - &dmc->cache[0];
#ifdef MY_ABC_HERE
				found_reclaim = 1;
#endif
				VERIFY_WARN((dmc->cache[*index].cache_state & FALLOW_DOCLEAN) == 0);
				flashcache_reclaim_lru_movetail(dmc, *index);
				break;
#ifdef MY_ABC_HERE
			} else if ((-1 == first_pin_block_index) &&
					((VALID | PIN_FILE) == cacheblk->cache_state)) {
				// Record the first pin block
				first_pin_block_index = cacheblk - &dmc->cache[0];
#endif
			}
			lru_rel_index = cacheblk->lru_next;
		} // SYNO: end of while

#ifdef MY_ABC_HERE
		if (is_pin && !found_reclaim && (-1 != first_pin_block_index)) {
			/*
			 * Bio is gonna be pinned and we can't find a VALID cache block
			 * So we replace the first pinned cache block
			 */
			*index = first_pin_block_index;
			VERIFY_WARN((dmc->cache[*index].cache_state & FALLOW_DOCLEAN) == 0);
			flashcache_reclaim_lru_movetail(dmc, *index);
		}

#endif
	}
}

/* 
 * dbn is the starting sector, io_size is the number of sectors.
 * SYNO:
 *  Also return VALID if io match the partical cache block range
 *  Return:
 *		VALID 2
 *		INVALID 1
 *		-1 (Doesn't find any cache block)
 *  IN: under lock (spin_lock_irq)
 */
static int 
flashcache_lookup(struct cache_c *dmc, struct bio *bio, int *index)
{
	sector_t dbn = bio_bi_sector(bio);
#if DMC_DEBUG
	int io_size = to_sector(bio_bi_size(bio));
#endif
	unsigned long set_number = hash_block(dmc, dbn);
	int invalid, oldest_clean = -1;
	int start_index;

	start_index = dmc->assoc * set_number;
	DPRINTK("Cache lookup : dbn %llu(%lu), set = %d",
		dbn, io_size, set_number);
	// use bio as parameter
	find_valid_dbn(dmc, bio, start_index, index, &invalid);

	if (*index >= 0) {
		DPRINTK("Cache lookup HIT: Block %llu(%lu): VALID index %d",
			     dbn, io_size, *index);
		/* We found the exact range of blocks we are looking for */
		return VALID;
	}
	if (invalid == -1) {
		/* We didn't find an invalid entry, search for oldest valid entry */
#ifdef MY_ABC_HERE
		find_reclaim_dbn(dmc, start_index, &oldest_clean, bio);
#else
		find_reclaim_dbn(dmc, start_index, &oldest_clean);
#endif
	}
	/* 
	 * Cache miss :
	 * We can't choose an entry marked INPROG, but choose the oldest
	 * INVALID or the oldest VALID entry.
	 */
	*index = start_index + dmc->assoc;
	if (invalid != -1) {
		DPRINTK("Cache lookup MISS (INVALID): dbn %llu(%lu), set = %d, index = %d, start_index = %d",
			     dbn, io_size, set_number, invalid, start_index);
		// SYNO: get invalid
		*index = invalid;
	} else if (oldest_clean != -1) {
		DPRINTK("Cache lookup MISS (VALID): dbn %llu(%lu), set = %d, index = %d, start_index = %d",
			     dbn, io_size, set_number, oldest_clean, start_index);
		// SYNO: get oldest valid
		*index = oldest_clean;
	} else {
		DPRINTK_LITE("Cache read lookup MISS (NOROOM): dbn %llu(%lu), set = %d",
			dbn, io_size, set_number);
	}
	if (*index < (start_index + dmc->assoc))
		return INVALID;
	else {
		atomic64_inc(&dmc->flashcache_stats.noroom);
		return -1;
	}
}

#ifdef MY_ABC_HERE
#else
/*
 * Cache Metadata Update functions
 */
void
flashcache_md_write_callback(unsigned long error, void *context)
{
	struct kcached_job *job = NULL;
	struct kcached_job *orig_job = (struct kcached_job *)context;
	struct cache_c *dmc = orig_job->dmc;
	struct cache_md_block_head *md_block_head =
		&dmc->md_blocks_buf[INDEX_TO_MD_BLOCK(dmc, orig_job->index)];

	flashcache_check_record_io_latency(LAT_MD_WRITE_SSD, &orig_job->io_lat);

	for (job = md_block_head->md_io_inprog; job != NULL; job = job->next) {
		flashcache_check_record_io_latency(LAT_MD_WRITE_SSD, &job->io_lat);
	}

	if (unlikely(error))
		orig_job->error = -EIO;
	else
		orig_job->error = 0;
	push_md_complete(orig_job);
	cache_schedule_md_complete();
}

#ifdef MY_ABC_HERE

void
disk_flush_pending_queue_add(struct cache_c *dmc, job_node *job_node)
{
	list_add_tail(&job_node->list, dmc->disk_flush_pending_queue);
}

void
flush_disk_complete(unsigned long error, void *context)
{
	struct cache_c *dmc = (struct cache_c *)context;

	if (error & 1) {
		serr("Send flush bio to disk failed");
	}

	atomic64_inc(&dmc->flashcache_stats.disk_flush_done);
	cache_schedule_work(&dmc->disk_flush_io_queue_work);
}

void
send_disk_flush_bio(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, flush_work);

	atomic64_inc(&dmc->flashcache_stats.disk_flush_start);

	dm_io_async_bio_wrapper(1, &dmc->disk_region, COMP_DM_WRITE_FLUSH,
		dmc->flush_bio, flush_disk_complete, dmc, 0);
}

static void
md_write_no_flush_callback(unsigned long error, void *context)
{
	struct kcached_job *job = (struct kcached_job *) context;
	struct cache_c *dmc = job->dmc;

	if (unlikely(error)) {
		job->error = -EIO;
	} else {
		job->error = 0;
	}

	if (atomic_dec_and_test(&dmc->inflight_md_write_for_flush)) {
		cache_schedule_work(&dmc->md_flush_work);
	}
}

/* Without lock */
static void start_md_writes_no_flush(struct cache_c *dmc)
{
	struct kcached_job *next_job = NULL;
	struct dm_io_region where;
	job_node *cur = NULL;
	int job_cnt = 0;

	if (unlikely(dmc->sysctl_error_inject & MD_PREWRITE_DELAY)) {
		dmc->sysctl_error_inject &= ~MD_PREWRITE_DELAY;
		serr("Cache Metadata Pre Write Delay 10 Sec");
		mdelay(10000);
	}

	VERIFY_WARN(0 == atomic_read(&dmc->inflight_md_write_for_flush));
	where.bdev = get_cache_bdev(dmc);
	where.count = MD_SECTORS_PER_BLOCK(dmc);

	list_for_each_entry(cur, dmc->md_flush_io_queue, list) {
		job_cnt++;
	}

	atomic_add(job_cnt, &dmc->inflight_md_write_for_flush);

	list_for_each_entry(cur, dmc->md_flush_io_queue, list) {
		next_job = cur->job;
		// Update metadata
		where.sector = (1 + INDEX_TO_MD_BLOCK(dmc, next_job->index)) * MD_SECTORS_PER_BLOCK(dmc);
		atomic64_inc(&dmc->flashcache_stats.ssd_writes);
		atomic64_inc(&dmc->flashcache_stats.md_ssd_writes);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
		dm_io_async_bvec(1, &where, COMP_DM_WRITE,
				 next_job->md_bio,
				 md_write_no_flush_callback, next_job, 0);
#else
		dm_io_async_bvec(1, &where, COMP_DM_WRITE,
				 &next_job->md_io_bvec,
				 md_write_no_flush_callback, next_job, 0);
#endif
	}
}

void
process_md_flush_io_queue(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, md_flush_io_queue_work);
	unsigned long flags = 0;
	job_node *cur = NULL;
	job_node *next = NULL;

	if (unlikely(dmc->sysctl_error_inject & MD_POSTFLUSH_DELAY)) {
		dmc->sysctl_error_inject &= ~MD_POSTFLUSH_DELAY;
		serr("Cache Metadata Post Flush Delay 10 Sec");
		mdelay(10000);
	}

	list_for_each_entry_safe(cur, next, dmc->md_flush_io_queue, list) {
		push_md_complete(cur->job);
		// free resource
		list_del(&cur->list);
		kfree(cur);
		cur = NULL;
	}
	cache_schedule_md_complete();

	VERIFY_WARN(list_empty(dmc->md_flush_io_queue));
	spin_lock_irqsave(&dmc->md_flush_lock, flags);
	if (!list_empty(dmc->md_flush_pending_queue)) {
		dmc->md_flush_unused_queue = dmc->md_flush_io_queue;
		dmc->md_flush_io_queue = dmc->md_flush_pending_queue;
		dmc->md_flush_pending_queue = dmc->md_flush_unused_queue;
		dmc->md_flush_unused_queue = NULL;
		spin_unlock_irqrestore(&dmc->md_flush_lock, flags);
		start_md_writes_no_flush(dmc);
	} else {
		dmc->md_flush_unused_queue = dmc->md_flush_io_queue;
		dmc->md_flush_io_queue = NULL;
		dmc->md_flushing = 0;
		spin_unlock_irqrestore(&dmc->md_flush_lock, flags);
	}
}

void
flush_md_complete(unsigned long error, void *context)
{
	struct cache_c *dmc = (struct cache_c *) context;

	if (error & 1) {
		serr("Send flush bio to cache failed");
	}

	atomic64_inc(&dmc->flashcache_stats.md_flush_done);
	cache_schedule_work(&dmc->md_flush_io_queue_work);
}

void
send_md_flush_bio(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, md_flush_work);

	if (unlikely(dmc->sysctl_error_inject & MD_PREFLUSH_DELAY)) {
		dmc->sysctl_error_inject &= ~MD_PREFLUSH_DELAY;
		serr("Cache Metadata Pre Flush Delay 10 Sec");
		mdelay(10000);
	}

	atomic64_inc(&dmc->flashcache_stats.md_flush_start);

	dm_io_async_bio_wrapper(1, &dmc->cache_region, COMP_DM_WRITE_FLUSH,
		dmc->md_flush_bio, flush_md_complete, dmc, 0);
}

void
process_disk_flush_io_queue(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, disk_flush_io_queue_work);
	unsigned long flags = 0;

	spin_lock_irqsave(&dmc->md_flush_lock, flags);
	list_splice_tail_init(dmc->disk_flush_io_queue, dmc->md_flush_pending_queue);
	VERIFY_WARN(!list_empty(dmc->md_flush_pending_queue));
	VERIFY_WARN(list_empty(dmc->disk_flush_io_queue));
	if (!dmc->md_flushing) {
		dmc->md_flushing = 1;
		dmc->md_flush_io_queue = dmc->md_flush_pending_queue;
		dmc->md_flush_pending_queue = dmc->md_flush_unused_queue;
		dmc->md_flush_unused_queue = NULL;
		spin_unlock_irqrestore(&dmc->md_flush_lock, flags);
		start_md_writes_no_flush(dmc);
	} else {
		spin_unlock_irqrestore(&dmc->md_flush_lock, flags);
	}

	VERIFY_WARN(list_empty(dmc->disk_flush_io_queue));

	spin_lock_irqsave(&dmc->disk_flush_lock, flags);

	// set back to unused queue
	dmc->disk_flush_unused_queue = dmc->disk_flush_io_queue;
	dmc->disk_flush_io_queue = NULL;

	// This thread can only stop when no jobs in pending queue while in_flush = 1
	if (!list_empty(dmc->disk_flush_pending_queue)) {
		// Change queue
		dmc->disk_flush_io_queue = dmc->disk_flush_pending_queue;
		dmc->disk_flush_pending_queue = dmc->disk_flush_unused_queue;
		spin_unlock_irqrestore(&dmc->disk_flush_lock, flags);
		cache_schedule_work(&dmc->flush_work);

	} else {
		dmc->disk_flushing = 0;
		spin_unlock_irqrestore(&dmc->disk_flush_lock, flags);
	}
}


job_node * alloc_init_job_node(void)
{
	static int print = 0;
	job_node *node = NULL;

	while (1) {
		node = kzalloc(sizeof(job_node), GFP_NOIO);
		if (node) {
			break;
		} else {
			if (!print) {
				serr("retry to get memory for job node");
				print = 1;
			}
		}
	}

	return node;
}

void
handle_md_write_with_flush(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	unsigned long flags = 0;
	job_node *job_node = NULL;

	job_node = alloc_init_job_node();
	job_node->job = job;

	spin_lock_irqsave(&dmc->disk_flush_lock, flags);

	disk_flush_pending_queue_add(dmc, job_node);

	if (dmc->disk_flushing) {
		spin_unlock_irqrestore(&dmc->disk_flush_lock, flags);
	} else {
		dmc->disk_flushing = 1;
		// Change queue
		dmc->disk_flush_io_queue = dmc->disk_flush_pending_queue;
		dmc->disk_flush_pending_queue = dmc->disk_flush_unused_queue;
		dmc->disk_flush_unused_queue = NULL;
		spin_unlock_irqrestore(&dmc->disk_flush_lock, flags);
		cache_schedule_work(&dmc->flush_work);
	}
}


#endif /* MY_ABC_HERE */

void
flashcache_md_write_kickoff(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;	
	struct flash_cacheblock *md_block;
	int md_block_ix;
	struct dm_io_region where;
	int i;
	struct cache_md_block_head *md_block_head;
	struct kcached_job *orig_job = job;
	unsigned long flags;
	int need_flush = 0;

	flashcache_check_record_io_latency(job->io_lat.next_step, &job->io_lat);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	if (flashcache_alloc_md_bio(job)) {
#else
	if (flashcache_alloc_md_sector(job)) {
#endif
		DMERR("flashcache: %d: Cache metadata write failed, cannot alloc page ! block %llu",
			job->action, (u64)job->job_io_regions.disk.sector);
		flashcache_md_write_callback(-EIO, job);
		return;
	}
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	/*
	 * Transfer whatever is on the pending queue to the md_io_inprog queue.
	 */
	md_block_head = &dmc->md_blocks_buf[INDEX_TO_MD_BLOCK(dmc, job->index)];
	md_block_head->md_io_inprog = md_block_head->queued_updates;
	md_block_head->queued_updates = NULL;
	md_block = job->md_block;
	// SYNO: Get the start cache[index] of the md block
	md_block_ix = INDEX_TO_MD_BLOCK(dmc, job->index) * MD_SLOTS_PER_BLOCK(dmc);

	/* First copy out the entire md block */
	for (i = 0 ; 
	     i < MD_SLOTS_PER_BLOCK(dmc) && md_block_ix < dmc->size ; 
	     i++, md_block_ix++) {
		md_block[i].dbn = dmc->cache[md_block_ix].dbn;
#ifdef FLASHCACHE_DO_CHECKSUMS
		md_block[i].checksum = dmc->cache[md_block_ix].checksum;
#endif
		md_block[i].data_bitmap = dmc->cache[md_block_ix].data_bitmap;
		md_block[i].dirty_bitmap = dmc->cache[md_block_ix].dirty_bitmap;
#ifdef MY_ABC_HERE
		md_block[i].cache_state =
			dmc->cache[md_block_ix].cache_state & (VALID | INVALID | DIRTY | PIN_FILE);
#else
		md_block[i].cache_state =
			dmc->cache[md_block_ix].cache_state & (VALID | INVALID | DIRTY);
#endif
	}
	/* Then set/clear the DIRTY bit for the "current" index */
	if (job->action == WRITECACHE) {
		/* DIRTY the cache block */
		md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state = 
			(VALID | DIRTY);
	} else { /* job->action == WRITEDISK* */
		/* un-DIRTY the cache block */
		md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state = VALID;
		need_flush = 1;
	}
#ifdef MY_ABC_HERE
	// Set Pin_File flag if needed
	if (dmc->cache[job->index].cache_state & PIN_FILE) {
		md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state |= PIN_FILE;
	}
#endif

	for (job = md_block_head->md_io_inprog ; 
	     job != NULL ;
	     job = job->next) {
		atomic64_inc(&dmc->flashcache_stats.md_write_batch);
		flashcache_check_record_io_latency(LAT_MD_WRITE_INPROG_WAIT, &job->io_lat);

		if (job->action == WRITECACHE) {
			/* DIRTY the cache block */
			md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state = 
				(VALID | DIRTY);
		} else { /* job->action == WRITEDISK* */
			/* un-DIRTY the cache block */
			md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state = VALID;
			need_flush = 1;
		}

#ifdef MY_ABC_HERE
		// Set Pin_File flag if needed
		if (dmc->cache[job->index].cache_state & PIN_FILE) {
			md_block[INDEX_TO_MD_BLOCK_OFFSET(dmc, job->index)].cache_state |= PIN_FILE;
		}
#endif

	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	if (need_flush) {
		handle_md_write_with_flush(orig_job);
		return;
	}

	where.bdev = get_cache_bdev(dmc);
	where.count = MD_SECTORS_PER_BLOCK(dmc);
	where.sector = (1 + INDEX_TO_MD_BLOCK(dmc, orig_job->index)) * MD_SECTORS_PER_BLOCK(dmc);
	atomic64_inc(&dmc->flashcache_stats.ssd_writes);
	atomic64_inc(&dmc->flashcache_stats.md_ssd_writes);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	dm_io_async_bvec(1, &where, COMP_DM_WRITE,
			 orig_job->md_bio,
			 flashcache_md_write_callback, orig_job, 0);
#else
	dm_io_async_bvec(1, &where, COMP_DM_WRITE,
			 &orig_job->md_io_bvec,
			 flashcache_md_write_callback, orig_job, 0);
#endif
}

void
flashcache_md_write_done(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	struct cache_md_block_head *md_block_head;
	int index;
	unsigned long flags;
	struct kcached_job *job_list;
	int error = job->error;
	struct kcached_job *next;
	struct cacheblock *cacheblk;
	struct cache_set *cacheset = NULL;
	int oqf_wb = 0;

	VERIFY_WARN(!in_interrupt());
	VERIFY_WARN(job->action == WRITEDISK || job->action == WRITECACHE || 
	       job->action == WRITEDISK_SYNC);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	flashcache_free_md_bio(job);
#else
	flashcache_free_md_sector(job);
#endif
	job->md_block = NULL;
	/*
	 * SYNO: Might shared by multiple IOs in the same cache block
	 * Use nr_in_prog as flag in critial section
	 */
	md_block_head = &dmc->md_blocks_buf[INDEX_TO_MD_BLOCK(dmc, job->index)];
	job_list = job;
	job->next = md_block_head->md_io_inprog;
	md_block_head->md_io_inprog = NULL;
	for (job = job_list ; job != NULL ; job = next) {
		next = job->next;
		job->error = error;
		index = job->index;
		cacheblk = &dmc->cache[index];
		cacheset = &dmc->cache_sets[index / dmc->assoc];
		oqf_wb = (job->flag & JOB_WB_NQF);

		flashcache_check_record_io_latency(LAT_MD_WRITE_CALLBACK_WQ, &job->io_lat);

		spin_lock_irqsave(&dmc->cache_spin_lock, flags);

		if (job->action == WRITECACHE) {
			if (unlikely(dmc->sysctl_error_inject & WRITECACHE_MD_ERROR)) {
				job->error = -EIO;
				dmc->sysctl_error_inject &= ~WRITECACHE_MD_ERROR;
			}
			if (likely(job->error == 0)) {
				if ((cacheblk->cache_state & DIRTY) == 0) {
#ifdef MY_ABC_HERE
#else
					cacheset->nr_dirty++;
#endif
					dmc->nr_dirty++;
					cb_state_add_bits_update_counts(dmc, cacheblk, DIRTY);
				}
				atomic64_inc(&dmc->flashcache_stats.md_write_dirty);

				if (0 == cacheblk->data_bitmap) {
					serr("Set cache[%d] to DIRTY, data bitmap should not be 0!", index);
					VERIFY_WARN(0);
				}
			} else {
				atomic_inc(&dmc->flashcache_errors.ssd_write_errors);
			}

			flashcache_bio_endio(job->bio, job->error, dmc, job->io_start_time, &job->io_lat);

			// Solve do_pending_error call trace
			job->bio = NULL;
			if (job->error || cacheblk->nr_queued > 0) {

				/*
				 * SYNO: Add data range in io_callback for md_block update
				 * Remove IO range in do_pending_xxx
				 */
				if (job->error) {
					// printk_ratelimit start
					if (printk_ratelimit()) {
						DMERR("flashcache: WRITE: Cache metadata write failed ! error %d block %llu",
							job->error, (u64)cacheblk->dbn);
					}
					// printk_ratelimit end
				}

				spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
				flashcache_do_pending(job);
			} else {
				// SYNO: Add data range in io_callback for md_block update
				cacheblk_remove_io_range_change_state(dmc, cacheblk, job->action_bitmap);
				cacheblk_dec_num_concurrent_and_return(cacheblk);

				spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

				atomic_dec(&dmc->num_write_cache);
				wake_up(&dmc->wait_io_done_queue);

				flashcache_free_cache_job(job);
				if (atomic_dec_and_test(&dmc->nr_jobs))
					wake_up(&dmc->sync_wqh);
			}
		} else {
			/* SYNO: WRITE DISK, WRITE_DISK_SYNC */
			int action = job->action;

			if (unlikely(dmc->sysctl_error_inject & WRITEDISK_MD_ERROR)) {
				job->error = -EIO;
				dmc->sysctl_error_inject &= ~WRITEDISK_MD_ERROR;
			}
			/*
			 * If we have an error on a WRITEDISK*, no choice but to preserve the 
			 * dirty block in cache. Fail any IOs for this block that occurred while
			 * the block was being cleaned.
			 */
			if (likely(job->error == 0)) {
				atomic64_inc(&dmc->flashcache_stats.md_write_clean);

				VERIFY_WARN(0 == cacheblk->flushing_dirty_bitmap);
				cacheblk_unset_all_dirty_range(cacheblk);

				cb_state_remove_bits_update_counts(dmc, cacheblk, DIRTY);
#ifdef MY_ABC_HERE
#else
				VERIFY_WARN(cacheset->nr_dirty > 0);
				cacheset->nr_dirty--;
#endif
				VERIFY_WARN(dmc->nr_dirty > 0);
				dmc->nr_dirty--;
			} else
				// SYNO: Don't unset DIRTY here for updating metadata again ...
				atomic_inc(&dmc->flashcache_errors.ssd_write_errors);

#ifdef MY_ABC_HERE
#else
			dec_clean_inprog(dmc, cacheset, 1);
#endif

			if (job->error || cacheblk->nr_queued > 0) {
				if (job->error) {
				// printk_ratelimit start
					if (printk_ratelimit()) {
						DMERR("flashcache: CLEAN: Cache metadata write failed ! error %d block %llu",
						job->error, (u64)cacheblk->dbn);
					}
					// printk_ratelimit end
				}
				/*
				 * SYNO:
				 * Don't remove BLOCK_IN_PROG here
				 * BLOCK_IN_PROG should be removed in do_pending_xxx (as it default behavoir)
				 */
				spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
				flashcache_do_pending(job);
			} else {
				unset_partial_cb_writeback_attrs(dmc, cacheblk, 1);

				spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
				flashcache_free_cache_job(job);
				if (atomic_dec_and_test(&dmc->nr_jobs))
					wake_up(&dmc->sync_wqh);
			}
			/* Kick off more cleanings */
			if (action == WRITEDISK) {
#ifdef MY_ABC_HERE
#else
				flashcache_clean_set(dmc, index / dmc->assoc);
#endif
			} else {
#ifdef MY_ABC_HERE
#else
				flashcache_sync_blocks(dmc);
#endif
			}
			atomic64_inc(&dmc->flashcache_stats.cleanings);
			if (action == WRITEDISK_SYNC)
				flashcache_update_sync_progress(dmc);
		}
	}

	// SYNO: WRITE_CACHE / WRITE_DISK / WRITE_DISK_SYNC go here

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if (md_block_head->queued_updates != NULL) {
		/* peel off the first job from the pending queue and kick that off */
		job = md_block_head->queued_updates;
		md_block_head->queued_updates = job->next;
		job->next = NULL;
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		VERIFY_WARN(job->action == WRITEDISK || job->action == WRITECACHE ||
			job->action == WRITEDISK_SYNC);
		flashcache_md_write_kickoff(job);
	} else {
		md_block_head->nr_in_prog = 0;
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	}
}

/* 
 * Kick off a cache metadata update (called from workqueue).
 * Cache metadata update IOs to a given metadata sector are serialized using the 
 * nr_in_prog bit in the md sector bufhead.
 * If a metadata IO is already in progress, we queue up incoming metadata updates
 * on the pending_jobs list of the md sector bufhead. When kicking off an IO, we
 * cluster all these pending updates and do all of them as 1 flash write (that 
 * logic is in md_write_kickoff), where it switches out the entire pending_jobs
 * list and does all of those updates as 1 ssd write.
 */
void
flashcache_md_write(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	struct cache_md_block_head *md_block_head;
	unsigned long flags;

	VERIFY_WARN(job->action == WRITEDISK || job->action == WRITECACHE ||
	       job->action == WRITEDISK_SYNC);
	md_block_head = &dmc->md_blocks_buf[INDEX_TO_MD_BLOCK(dmc, job->index)];
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	/* If a write is in progress for this metadata sector, queue this update up */
	if (md_block_head->nr_in_prog != 0) {
		struct kcached_job **nodepp;

		/* A MD update is already in progress, queue this one up for later */
		nodepp = &md_block_head->queued_updates;
		while (*nodepp != NULL)
			nodepp = &((*nodepp)->next);
		job->io_lat.next_step = LAT_MD_WRITE_INPROG_WAIT;
		job->next = NULL;
		*nodepp = job;
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	} else {
		md_block_head->nr_in_prog = 1;
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		/*
		 * Always push to a worker thread. If the driver has
		 * a completion thread, we could end up deadlocking even
		 * if the context would be safe enough to write from.
		 * This could be executed from the context of an IO
		 * completion thread. Kicking off the write from that
		 * context could result in the IO completion thread
		 * blocking (eg on memory allocation). That can easily
		 * deadlock.
		 */
		job->io_lat.next_step = LAT_MD_WRITE_START_WQ;
		push_md_io(job);
		cache_schedule_md_io();
	}
}
#endif /* SYNO_FLASHCACHQ_MD_UPDATE_AGGR */

#ifdef MY_ABC_HERE
#else
static void 
flashcache_kcopyd_callback(int read_err, unsigned int write_err, void *context)
{
	struct kcached_job *job = (struct kcached_job *)context;
	struct cache_c *dmc = job->dmc;
	int index = job->index;
	unsigned long flags;
#ifdef MY_ABC_HERE
	struct cacheblock *cacheblk = &dmc->cache[index];

	job_dbg(__func__, job);
#endif

	VERIFY_WARN(!in_interrupt());
	DPRINTK("kcopyd_callback: Index %d", index);
	VERIFY_WARN(job->bio == NULL);
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	VERIFY_WARN(dmc->cache[index].cache_state & (DISKWRITEINPROG | VALID | DIRTY));
	if (unlikely(dmc->sysctl_error_inject & KCOPYD_CALLBACK_ERROR)) {
		read_err = -EIO;
		dmc->sysctl_error_inject &= ~KCOPYD_CALLBACK_ERROR;
	}
	if (likely(read_err == 0 && write_err == 0)) {
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

#ifdef MY_ABC_HERE
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		cacheblk_unset_lowest_part_of_flushing_dirty_range(cacheblk);

		if (cacheblk_is_flushing_dirty_range_unset(cacheblk)) {
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

#ifdef MY_ABC_HERE
			atomic64_inc(&dmc->flashcache_stats.dirty_writeback_done);
#endif
			flashcache_md_write(job);
		} else {
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
			flashcache_free_cache_job(job);

			/*
			 * Don't decrease nr_jog and wake_up here
			 * Trigger again
			 */
			flashcache_dirty_writeback(dmc, index);
		}
#else
		flashcache_md_write(job);
#endif
	} else {
		if (read_err)
			read_err = -EIO;
		if (write_err)
			write_err = -EIO;
		/* Disk write failed. We can not purge this block from flash */
#ifdef MY_ABC_HERE
		if (0 < dmc->limits.writeback_err) {
#endif
		DMERR("flashcache: Disk writeback failed ! read error %d write error %d block %llu",
		      -read_err, -write_err, (u64)job->job_io_regions.disk.sector);
#ifdef MY_ABC_HERE
			dmc->limits.writeback_err--;
		}
#endif
		dec_clean_inprog(dmc, &dmc->cache_sets[index / dmc->assoc], 1);
		cacheblk->flushing_dirty_bitmap = 0;
		// io_range whould be unset in do_pending_xxx

		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		/* Set the error in the job and let do_pending() handle the error */
		if (read_err) {
			atomic_inc(&dmc->flashcache_errors.ssd_read_errors);
			job->error = read_err;
		} else {
			atomic_inc(&dmc->flashcache_errors.disk_write_errors);
			job->error = write_err;
		}
		flashcache_do_pending(job);
		flashcache_clean_set(dmc, index / dmc->assoc); /* Kick off more cleanings */
		atomic64_inc(&dmc->flashcache_stats.cleanings);
	}
}

static void
flashcache_dirty_writeback(struct cache_c *dmc, int index)
{
	struct kcached_job *job;
	unsigned long flags;
	struct cacheblock *cacheblk = &dmc->cache[index];
	struct cache_set *cacheset = &dmc->cache_sets[index / dmc->assoc];
	int device_removal = 0;
	int first_time_flush = 0;
	sector_t offset = 0;
	sector_t size = 0;

	DPRINTK("flashcache_dirty_writeback: Index %d", index);

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	VERIFY_WARN((cacheblk->cache_state & BLOCK_IO_INPROG) == DISKWRITEINPROG);
	VERIFY_WARN(cacheblk->cache_state & DIRTY);

	// Check data_bitmap inside spin_lock
	if (0 == cacheblk->data_bitmap) {
		serr("cb index=%d data_bitmap=0", index);
		VERIFY_WARN(0);
	}

	if (0 == cacheblk->flushing_dirty_bitmap) {
		cacheblk->flushing_dirty_bitmap = cacheblk->dirty_bitmap;
		first_time_flush = 1;
	}

	// Access inside the lock
	cacheblk_get_lowest_part_of_flushing_dirty_range(cacheblk, &offset, &size);

	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	job = new_kcached_job_no_bio(dmc, index, offset, size);
	if (unlikely(dmc->sysctl_error_inject & DIRTY_WRITEBACK_JOB_ALLOC_FAIL)) {
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		dmc->sysctl_error_inject &= ~DIRTY_WRITEBACK_JOB_ALLOC_FAIL;
	}

	if (!first_time_flush && (unlikely(dmc->sysctl_error_inject & SIMULATE_DIRTY_WRITEBACK_LONG_TIME))) {
		sprint("Detect sysctl SIMULATE_DIRTY_WRITEBACK_LONG_TIME is set, sleep 15s");
		msleep(15*1000);
		dmc->sysctl_error_inject &= ~SIMULATE_DIRTY_WRITEBACK_LONG_TIME;
	}

	/*
	 * If the device is being removed, do not kick off any more cleanings.
	 */
	if (unlikely(atomic_read(&dmc->remove_in_prog))) {
		DMERR("flashcache: Dirty Writeback (for set cleaning) aborted for device removal, block %llu",
			(u64)cacheblk->dbn);
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		device_removal = 1;
	}
	if (unlikely(job == NULL)) {
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		dec_clean_inprog(dmc, cacheset, 1);
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
		cacheblk->flushing_dirty_bitmap = 0;
		unset_partial_cb_writeback_attrs(dmc, cacheblk, 1);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		if (device_removal == 0)
			DMERR("flashcache: Dirty Writeback (for set cleaning) failed ! Can't allocate memory, block %llu", (u64)cacheblk->dbn);

		if (atomic_dec_and_test(&dmc->nr_jobs)) {
			wake_up(&dmc->sync_wqh);
		}
	} else {
		job->bio = NULL;
		job->action = WRITEDISK;
		if (first_time_flush) {
			atomic64_inc(&dmc->flashcache_stats.dirty_writeback_start);
		}

		job_dbg(__func__, job);
		atomic64_inc(&dmc->flashcache_stats.ssd_reads);
		atomic64_inc(&dmc->flashcache_stats.disk_writes);
#ifdef MY_ABC_HERE
		atomic64_add(size, &dmc->flashcache_stats.dirty_writeback_sector);
#endif
		dm_kcopyd_copy(flashcache_kcp_client, &job->job_io_regions.cache, 1, &job->job_io_regions.disk, 0, 
			       (dm_kcopyd_notify_fn) flashcache_kcopyd_callback, 
			       (void *)job);
	}
}


/*
 * This function encodes the background disk cleaning logic.
 * Background disk cleaning is triggered for 2 reasons.
 A) Dirty blocks are lying fallow in the set, making them good 
    candidates for being cleaned.
 B) This set has dirty blocks over the configured threshold 
    for a set.
 * (A) takes precedence over (B). Fallow dirty blocks are cleaned
 * first.
 * The cleaning of disk blocks is subject to the write limits per
 * set and across the cache, which this function enforces.
 *
 * 1) Select the n blocks that we want to clean (choosing whatever policy), 
 *    sort them.
 * 2) Then sweep the entire set looking for other DIRTY blocks that can be 
 *    tacked onto any of these blocks to form larger contigous writes. 
 *    The idea here is that if you are going to do a write anyway, then we 
 *    might as well opportunistically write out any contigous blocks for 
 *    free.
 */

/* Are we under the limits for disk cleaning ? */
static inline int
flashcache_can_clean(struct cache_c *dmc,
		     struct cache_set *cache_set)
{
	return (cache_set->clean_inprog < dmc->max_clean_ios_set &&
		dmc->clean_inprog < dmc->max_clean_ios_total);
}

/*
 * SYNO: Just sync dirty block to disks and make its state to VALID
 */
void
flashcache_clean_set(struct cache_c *dmc, int set)
{
	unsigned long flags;
	int threshold_clean = 0;
	struct dbn_index_pair *writes_list;
	int nr_writes = 0, i;
	int start_index = set * dmc->assoc; 
	int end_index = start_index + dmc->assoc;
	struct cache_set *cache_set = &dmc->cache_sets[set];
	struct cacheblock *cacheblk;
	int do_delayed_clean = 0;

	if (dmc->cache_mode != FLASHCACHE_WRITE_BACK)
		return;

	/* 
	 * If a removal of this device is in progress, don't kick off 
	 * any more cleanings. This isn't sufficient though. We still need to
	 * stop cleanings inside flashcache_dirty_writeback() because we could
	 * have started a device remove after tested this here.
	 */
	if (atomic_read(&dmc->remove_in_prog))
		return;
	writes_list = kmalloc(dmc->assoc * sizeof(struct dbn_index_pair), GFP_NOIO);
	if (unlikely(dmc->sysctl_error_inject & WRITES_LIST_ALLOC_FAIL)) {
		if (writes_list)
			kfree(writes_list);
		writes_list = NULL;
		dmc->sysctl_error_inject &= ~WRITES_LIST_ALLOC_FAIL;
	}
	if (writes_list == NULL) {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate write list for clean set");
		}
#endif
		return;
	}
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	/* 
	 * Before we try to clean any blocks, check the last time the fallow block
	 * detection was done. If it has been more than "fallow_delay" seconds, make 
	 * a sweep through the set to detect (mark) fallow blocks.
	 */
	if (dmc->sysctl_fallow_delay && time_after(jiffies, cache_set->fallow_tstamp)) {
		for (i = start_index ; i < end_index ; i++)
			flashcache_detect_fallow(dmc, i);
		cache_set->fallow_tstamp = jiffies + dmc->sysctl_fallow_delay * HZ;
	}
	/* If there are any dirty fallow blocks, clean them first */
	for (i = start_index ; 
	     (dmc->sysctl_fallow_delay > 0 &&
	      cache_set->dirty_fallow > 0 &&
	      time_after(jiffies, cache_set->fallow_next_cleaning) &&
	      i < end_index) ; 
	     i++) {
		cacheblk = &dmc->cache[i];
		if (!(cacheblk->cache_state & DIRTY_FALLOW_2))
			continue;
		if (!flashcache_can_clean(dmc, cache_set)) {
			/*
			 * There are fallow blocks that need cleaning, but we 
			 * can't clean them this pass, schedule delayed cleaning 
			 * later.
			 */
			do_delayed_clean = 1;
			goto out;
		}
		VERIFY_WARN(cacheblk->cache_state & DIRTY);
		VERIFY_WARN((cacheblk->cache_state & BLOCK_IO_INPROG) == 0);
		set_wb_attrs(dmc, i, 1);
		writes_list[nr_writes].dbn = cacheblk->dbn;
		writes_list[nr_writes].index = i;
		atomic64_inc(&dmc->flashcache_stats.fallow_cleanings);
		nr_writes++;
	}
	if (nr_writes > 0)
		cache_set->fallow_next_cleaning = jiffies + HZ / dmc->sysctl_fallow_clean_speed;
	if (cache_set->nr_dirty < dmc->dirty_thresh_set ||
	    !flashcache_can_clean(dmc, cache_set))
		goto out;
	/*
	 * We picked up all the dirty fallow blocks we can. We can still clean more to 
	 * remain under the dirty threshold. Clean some more blocks.
	 */
	threshold_clean = cache_set->nr_dirty - dmc->dirty_thresh_set;
	if (dmc->sysctl_reclaim_policy == FLASHCACHE_FIFO) {
		int scanned;
		
		scanned = 0;
		i = cache_set->set_clean_next;
		DPRINTK("flashcache_clean_set: Set %d", set);
		while (scanned < dmc->assoc &&
		       flashcache_can_clean(dmc, cache_set) &&
		       nr_writes < threshold_clean) {
			cacheblk = &dmc->cache[i];
			if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {	
				set_wb_attrs(dmc, i, 1);
				atomic64_inc(&dmc->flashcache_stats.cleanings_over_threshold);
				writes_list[nr_writes].dbn = cacheblk->dbn;
				writes_list[nr_writes].index = i;
				nr_writes++;
			}
			scanned++;
			i++;
			if (i == end_index)
				i = start_index;
		}
		cache_set->set_clean_next = i;
	} else { /* reclaim_policy == FLASHCACHE_LRU */
		int lru_rel_index;

		lru_rel_index = cache_set->lru_head;
		while (lru_rel_index != FLASHCACHE_LRU_NULL && 
		       flashcache_can_clean(dmc, cache_set) &&
		       nr_writes < threshold_clean) {
			cacheblk = &dmc->cache[lru_rel_index + start_index];
			if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
				set_wb_attrs(dmc, lru_rel_index + start_index, 1);
				atomic64_inc(&dmc->flashcache_stats.cleanings_over_threshold);
				writes_list[nr_writes].dbn = cacheblk->dbn;
				writes_list[nr_writes].index = cacheblk - &dmc->cache[0];
				nr_writes++;
			}
			lru_rel_index = cacheblk->lru_next;
		}
	}
out:
	if (nr_writes > 0) {
#ifdef MY_ABC_HERE
		/*
		 * Don't invoke flashcache_merge_writes for new mode
		 * Or the merge write will be too mush because new cache blocks
		 * are too easy to be DIRTY.
		 */
		flashcache_sort_writes(writes_list, nr_writes, set);
#else
		flashcache_merge_writes(dmc, writes_list, &nr_writes, set);
#endif
		atomic64_add(nr_writes, &dmc->flashcache_stats.clean_set_ios);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		for (i = 0 ; i < nr_writes ; i++)
			flashcache_dirty_writeback(dmc, writes_list[i].index);
	} else {
		if (cache_set->nr_dirty > dmc->dirty_thresh_set)
			do_delayed_clean = 1;
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		if (do_delayed_clean)
			cache_schedule_delayed_work(&dmc->delayed_clean, 1*HZ);
	}
	kfree(writes_list);
}
#endif /* MY_ABC_HERE */

// match none of the bitmap, read from disk and write to cache
/*
 * Use in read hit (match none of the data bitmap) or match partial and no need to invalidate
 * IN: under lock
 * OUT: release lock
 */
static void
read_disk_write_cache(struct cache_c *dmc, struct cacheblock *cacheblk, struct bio *bio, int index)
{

	struct kcached_job *job = NULL;
	unsigned long flags = 0;
	char job_title[JOB_STR_MAX] = {0};

	VERIFY_WARN(dmc && cacheblk && bio);
	VERIFY_WARN(spin_is_locked(&dmc->cache_spin_lock));

	cb_state_add_bits_update_counts(dmc, cacheblk, DISKREADINPROG);
	// This is read hit case, dbn is already set !

	cacheblk_add_io_range_by_bio(cacheblk, bio);
	cacheblk_add_num_concurrent(cacheblk);

	spin_unlock_irq(&dmc->cache_spin_lock);

	job = new_kcached_job(dmc, bio, index, NULL, TYPE_DISK);

	if (unlikely(dmc->sysctl_error_inject & READ_MISS_JOB_ALLOC_FAIL)) {
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		dmc->sysctl_error_inject &= ~READ_MISS_JOB_ALLOC_FAIL;
	}

	if (unlikely(job == NULL)) {
		/*
		 * Refer to read miss
		 * Can't allocate a job.
		 * Since we dropped the spinlock, we have to drain any
		 * pending jobs.
		 */
		DMERR("flashcache: Read from disk (Not in bitmap )failed ! Can't allocate memory, block %llu",
			(u64)cacheblk->dbn);
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		// don't set cache block to INVALID due to someone might access it now
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
		cacheblk_remove_io_range_change_state_by_bio(dmc, cacheblk, bio);
		flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	} else {
		job->action = READDISK; /* Fetch data from the source device */
		atomic_inc(&dmc->nr_jobs);
		atomic64_inc(&dmc->flashcache_stats.disk_reads);
		atomic64_inc(&dmc->flashcache_stats.read_hit_match_none_read_disk);
		snprintf(job_title, sizeof(job_title), "Read hit match (none/partial) => %s", __func__);
		job_dbg(job_title, job);

		dm_io_async_bio_wrapper(1, &job->job_io_regions.disk, COMP_DM_READ,
			bio, flashcache_io_callback_atomic, job, 0);
		// No need to clean set due to no new cache block is occupied
	}
}

/*
 * A wrapper of the original read_disk_write_cache because we have to check
 * if cache_all is on before writing the SSD.
 *
 * IN: Under lock
 * OUT: Release lock
 */
static void do_read_disk_write_cache(struct cache_c *dmc, struct cacheblock *cacheblk, struct bio *bio, int index)
{
	int queued = 0;
	atomic64_inc(&dmc->flashcache_stats.read_hit_check_write_ssd);
	if (dmc->bypass_cache) {
		queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_DISK);
		if (queued) {
			if (unlikely(queued < 0)) {
				flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
			}
			spin_unlock_irq(&dmc->cache_spin_lock);
			goto end;
		}

		spin_unlock_irq(&dmc->cache_spin_lock);
		flashcache_start_uncached_io(dmc, bio, NULL, TYPE_DISK);
	} else {
		read_disk_write_cache(dmc, cacheblk, bio, index);
	}

end:
	return;
}

/*
 * Handle the case that Read I/O request only match partial data in the cache block
 * IN: Under lock
 * Out: Should release lock
 */
static void
read_hit_match_partial(struct cache_c *dmc, struct cacheblock *cacheblk, struct bio *bio, int index)
{
	int queued;
#ifdef MY_ABC_HERE
	char bio_str[BIO_STR_MAX] = {0};
#endif

	atomic64_inc(&dmc->flashcache_stats.read_hit_match_partial);

	/*
	 * Fix issue that actually the bio WON'T be queued if the cache block is not in IO or dirty
	 */
	if (!(cacheblk->cache_state & (BLOCK_IO_INPROG | DIRTY)) &&
		(cacheblk->nr_queued == 0)) {

		sdbg(DF_DETAIL, "Don't invalidate cache block. Invoke do_read_disk_write_cache");
		do_read_disk_write_cache(dmc, cacheblk, bio, index);

	} else {
		queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_DISK);

		spin_unlock_irq(&dmc->cache_spin_lock);
		if (queued) {
			// Normal queued case (cache block is dirty or in IO)
			sdbg(DF_DETAIL, "queued, %s cacheblk index=%d dbn=%llu data_bitmap=0x%x",
					bio_to_str(bio, bio_str, sizeof(bio_str)), index, (u64)cacheblk->dbn, cacheblk->data_bitmap);
			if (unlikely(queued < 0))
				flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
		} else {
			serr("Should be queue: %s cacheblk index=%d dbn=%llu data_bitmap=0x%x",
					bio_to_str(bio, bio_str, sizeof(bio_str)), index, (u64)cacheblk->dbn,
					cacheblk->data_bitmap);
			VERIFY_WARN(0);
		}

	}
}
char *match_str[] = {"all", "only_partial", "none"};

void inject_read_hit_normal_io_wait(struct cache_c *dmc, unsigned long corr_bi_flags, struct bio *bio)
{
	if (unlikely(dmc->sysctl_error_inject & READ_HIT_NORMAL_IO_WAIT) && !corr_bi_flags) {
		do {
			sprint("Normal IO (start=%llu) is waiting", (u64)bio_bi_sector(bio));
			msleep(5000);
		} while (dmc->sysctl_error_inject & READ_HIT_NORMAL_IO_WAIT);
	}
}

void inject_read_hit_corr_io_wait(struct cache_c *dmc, unsigned long corr_bi_flags, struct bio *bio)
{
	if (unlikely(dmc->sysctl_error_inject & READ_HIT_CORR_IO_WAIT) && corr_bi_flags) {
		do {
			sprint("Correction IO (start=%llu) is waiting", (u64)bio_bi_sector(bio));
			msleep(5000);
		} while (dmc->sysctl_error_inject & READ_HIT_CORR_IO_WAIT);
	}
}

// SYNO: In: Under lock
static void
flashcache_read_hit(struct cache_c *dmc, struct bio* bio, int index)
{
	struct cacheblock *cacheblk;
	struct pending_job *pjob;
	unsigned long corr_bi_flags = 0;
#ifdef CONFIG_SYNO_DATA_CORRECTION
	int is_range_dirty = 0;
	correcting_type_t correcting_type = NO_CORRECTING;
#endif
	u64 bio_start_sector = (u64)bio_bi_sector(bio);

	match_t match_data_range = MATCH_UNSET;
	match_t match_io_range = MATCH_UNSET;

	cacheblk = &dmc->cache[index];
#ifdef CONFIG_SYNO_DATA_CORRECTION
	is_range_dirty = cacheblk_is_range_dirty(cacheblk, bio);
	correcting_type = correction_range_get_type(dmc, bio_start_sector);
	corr_bi_flags = bio->bi_flags & ((1 << BIO_CORRECTION_RETRY) | (1 << BIO_CORRECTION_ABORT));
#endif

	match_data_range = cacheblk_compare_data_range(cacheblk, bio);
	match_io_range = cacheblk_compare_io_range(cacheblk, bio);

#ifdef MY_ABC_HERE
	cacheblk_set_pin_state_by_bio(dmc, cacheblk, bio);
#endif

	// allow read concurrent start
	if ((MATCH_NONE == match_io_range) && (cacheblk->nr_queued == 0) &&
			((MATCH_ALL == match_data_range) || (MATCH_NONE == match_data_range))) {

		if (MATCH_ALL == match_data_range) {
			// Case: Match all
			struct kcached_job *job;
#ifdef CONFIG_SYNO_DATA_CORRECTION
			if (corr_bi_flags) {
				if (is_range_dirty) {
					if (TYPE_UNINITED == correcting_type) {
						if (-1 == correction_range_set_type(dmc, bio_start_sector, SSD_CORRECTING)) {
							serr("Fail to get correction device SSD type: correction entry is null");
						}
					}
					sdbg(DF_CORRECTION, "Correcting bio_sector=%llu dirty, set SSD_CORRECTING",
							bio_start_sector);
				} else if (SSD_CORRECTING == correcting_type) {
					sdbg(DF_CORRECTION, "Correcting bio_sector=%llu is already SSD_CORRECTING",
							bio_start_sector);
				} else {
					if (TYPE_UNINITED == correcting_type) {
						if (-1 == correction_range_set_type(dmc, bio_start_sector, DISK_CORRECTING)) {
							serr("Fail to get correction device disk type: correction entry is null");
						}
					}

					cacheblk_clear_data_range_by_bio(cacheblk, bio);
					spin_unlock_irq(&dmc->cache_spin_lock);
					sdbg(DF_CORRECTION, "Correcting bio_sector=%llu non-dirty ", bio_start_sector);
					flashcache_start_uncached_io(dmc, bio, NULL, TYPE_DISK);
					goto finish;
				}
			}
#endif
			cb_state_add_bits_update_counts(dmc, cacheblk, CACHEREADINPROG);
			cacheblk_add_io_range_by_bio(cacheblk, bio);
			cacheblk_add_num_concurrent(cacheblk);
			atomic64_inc(&dmc->flashcache_stats.read_hits);
			spin_unlock_irq(&dmc->cache_spin_lock);

			if (unlikely(dmc->sysctl_error_inject & READ_HIT_DELAY)) {
				dmc->sysctl_error_inject &= ~(READ_HIT_DELAY);
				sprint("Read Hit Delayed dbn: %llu\n", bio_start_sector);
				msleep(30000);
			}

			inject_read_hit_normal_io_wait(dmc, corr_bi_flags, bio);
			inject_read_hit_corr_io_wait(dmc, corr_bi_flags, bio);

			DPRINTK("Cache read: Block %llu(%lu), index = %d:%s",
				bio_start_sector, bio_bi_size(bio), index, "CACHE HIT");
			job = new_kcached_job(dmc, bio, index, NULL, TYPE_SSD);
			if (unlikely(dmc->sysctl_error_inject & READ_HIT_JOB_ALLOC_FAIL)) {
				if (job)
					flashcache_free_cache_job(job);
				job = NULL;
				dmc->sysctl_error_inject &= ~READ_HIT_JOB_ALLOC_FAIL;
			}
			if (unlikely(job == NULL)) {
				/*
				* We have a read hit, and can't allocate a job.
				* Since we dropped the spinlock, we have to drain any
				* pending jobs.
				*/
				DMERR("flashcache: Read (hit) failed ! Can't allocate memory for cache IO, block %llu",
					(u64)cacheblk->dbn);
				spin_lock_irq(&dmc->cache_spin_lock);
				flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
				cacheblk_remove_io_range_change_state_by_bio(dmc, cacheblk, bio);
				cacheblk_dec_num_concurrent_and_return(cacheblk);
				flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
				spin_unlock_irq(&dmc->cache_spin_lock);
			} else {
				job->action = READCACHE; /* Fetch data from cache */
				atomic_inc(&dmc->nr_jobs);
				atomic64_inc(&dmc->flashcache_stats.ssd_reads);
				job_dbg(__func__, job);

				dm_io_async_bio_wrapper(1, &job->job_io_regions.cache, COMP_DM_READ,
					bio, flashcache_io_callback_atomic, job, corr_bi_flags);
			}
			// End of (MATCH_ALL == match_data_range)
		} else {
			// Case: Match none of data range
#ifdef CONFIG_SYNO_DATA_CORRECTION
			if (corr_bi_flags) {
				sdbg(DF_CORRECTION, "RW_cache: bio_sector=%llu empty ", bio_start_sector);
				spin_unlock_irq(&dmc->cache_spin_lock);

				if ((TYPE_UNINITED == correcting_type) &&
					 (-1 == correction_range_set_type(dmc, bio_start_sector, DISK_CORRECTING))) {
						serr("Fail to get correction device SSD type: correction entry is null");

				}

				flashcache_start_uncached_io(dmc, bio, NULL, TYPE_DISK);
				goto finish;
			}
#endif
			// under lock
			do_read_disk_write_cache(dmc, cacheblk, bio, index);
		}
	} else {
		if ((MATCH_NONE != match_io_range) || (cacheblk->nr_queued != 0)) {
			// Case: Match partial io range or nr_queue >0  start

			if (MATCH_NONE != match_io_range) {
				atomic64_inc(&dmc->flashcache_stats.read_hit_busy_in_io);
			} else {
				atomic64_inc(&dmc->flashcache_stats.read_hit_busy_wait_queue);
			}
			pjob = flashcache_alloc_pending_job(dmc);
			if (unlikely(dmc->sysctl_error_inject & READ_HIT_PENDING_JOB_ALLOC_FAIL)) {
				if (pjob) {
					flashcache_free_pending_job(pjob);
					pjob = NULL;
				}
				dmc->sysctl_error_inject &= ~READ_HIT_PENDING_JOB_ALLOC_FAIL;
			}
			if (pjob == NULL)
				flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
			else {
				if (MATCH_NONE != match_io_range) {
					sdbg(DF_DETAIL, "match io ranges (io=0x%x bio=0x%x): enq_pending index=%d",
							cacheblk->io_bitmap, bitmap_get(cacheblk, bio), index);
				} else {
					sdbg(DF_DETAIL, "nr_queue != 0: enq_pending index=%d", index);
				}
				// flush this cache and do uncached io
				flashcache_enq_pending(dmc, bio, index, READCACHE, pjob, NULL,
					MATCH_ALL == match_data_range ? TYPE_COULD_SSD : TYPE_DISK);
			}

			spin_unlock_irq(&dmc->cache_spin_lock);
			// Case: Match partial io range or nr_queue >0  end
		} else {
			// Case: Match partial data range (But doesn't match any IO range and nr_queue = 0)
			// Data Correction will not enter here. (range always 4k)
			read_hit_match_partial(dmc, cacheblk, bio, index);
		}
	}
#ifdef CONFIG_SYNO_DATA_CORRECTION
finish:
	return;
#endif
}

/*
 * SYNO: In: no lock
 */
static void
flashcache_read_miss(struct cache_c *dmc, struct bio* bio,
		     int index)
{
	struct kcached_job *job;
	struct cacheblock *cacheblk = &dmc->cache[index];

	job = new_kcached_job_fill_cacheblk(dmc, bio, index);
	if (unlikely(dmc->sysctl_error_inject & READ_MISS_JOB_ALLOC_FAIL)) {
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		dmc->sysctl_error_inject &= ~READ_MISS_JOB_ALLOC_FAIL;
	}
	if (unlikely(job == NULL)) {
		/* 
		 * We have a read miss, and can't allocate a job.
		 * Since we dropped the spinlock, we have to drain any 
		 * pending jobs.
		 */
		DMERR("flashcache: Read (miss) failed ! Can't allocate memory for cache IO, block %llu",
			(u64)cacheblk->dbn);

		spin_lock_irq(&dmc->cache_spin_lock);

		atomic64_dec(&dmc->cached_blocks);
		cb_state_remove_bits_update_counts(dmc, cacheblk, VALID);
#ifdef MY_ABC_HERE
		cacheblk_check_unset_pin_state(dmc, cacheblk);
#endif
		cb_state_add_bits_update_counts(dmc, cacheblk, INVALID);
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
		cacheblk_remove_io_range_change_state_by_bio(dmc, cacheblk, bio);
		cacheblk_dec_num_concurrent_and_return(cacheblk);
		flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);

		spin_unlock_irq(&dmc->cache_spin_lock);
	} else {
		/*
		 * Read whole data to memory first, so later we can get partial data from memory
		 * to this bio
		 */
		job->action = READDISKMEM;
		atomic_inc(&dmc->nr_jobs);
		atomic64_inc(&dmc->flashcache_stats.disk_reads);

		push_preread_io(job);
		cache_schedule_preread_io();
		atomic64_inc(&dmc->flashcache_stats.prereads);
#ifdef MY_ABC_HERE
#else
		flashcache_clean_set(dmc, index / dmc->assoc);
#endif
	}
}

void
flashcache_do_preread_io(struct kcached_job *job)
{
	job_dbg(__FUNCTION__, job);

	flashcache_check_record_io_latency(LAT_DO_PREREAD_WQ, &job->io_lat);

	dm_io_vmem(1, &job->job_io_regions.disk, COMP_DM_READ,
			 job->mem_addr,
			 flashcache_io_callback_atomic, job, 0);
}

static void
flashcache_read(struct cache_c *dmc, struct bio *bio, int uncacheable, int uncache_seq)
{
	int index;
	int res;
	struct cacheblock *cacheblk = NULL;
	int queued;

#ifdef CONFIG_SYNO_DATA_CORRECTION
	unsigned long corr_bi_flags = 0;
	int entry_in_correcting = 0;

	correcting_type_t correcting_type = NO_CORRECTING;

	entry_in_correcting = correction_entry_in_correcting(dmc, bio_bi_sector(bio), DO_LOCK);
	correcting_type = correction_range_get_type(dmc, bio_bi_sector(bio));
	corr_bi_flags = bio->bi_flags & ((1 << BIO_CORRECTION_RETRY) | (1 << BIO_CORRECTION_ABORT));
#endif

	DPRINTK("Got a %s for %llu (%u bytes)",
	        (bio_rw(bio) == READ ? "READ":"READA"), 
		bio_bi_sector(bio), bio_bi_size(bio));

	spin_lock_irq(&dmc->cache_spin_lock);
	res = flashcache_lookup(dmc, bio, &index);
	/* Cache Read Hit case */
#ifdef CONFIG_SYNO_DATA_CORRECTION
	/* For RO, data on SSD/HDD SHOULD be aligned. Data on SSD MAY be corrupted
	 * So always do DISK_CORRECTING */
	if ((res > 0) && !(entry_in_correcting && (FLASHCACHE_WRITE_AROUND == dmc->cache_mode))) {
#else
	if (res > 0) {
#endif /* CONFIG_SYNO_DATA_CORRECTION */
		cacheblk = &dmc->cache[index];
		/*
		 * Three cases go here
		 * 1 VALID with matched  cacheblk (Cache state is VALID)
		 * 2 INVALID with state==INVALID cacheblk
		 * 3 INVALID with old VALID cacheblk (Cache state == VALID) (reclaim_oldest_clean!)
		 * To distiguish 1 & 3, the old way is to check if cacheblk->dbn match !
		 */
		if ((VALID == res) && (cacheblk->cache_state & VALID)) {
			flashcache_read_hit(dmc, bio, index);
			return;
		}
	}
	/*
	 * In all cases except for a cache hit (and VALID), test for potential 
	 * invalidations that we need to do.
	 */
	queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_DISK);
	if (queued) {
		// wait for invalidate
		sdbg(DF_DETAIL, "queued for invalidate");

		if (unlikely(queued < 0))
			flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);

		spin_unlock_irq(&dmc->cache_spin_lock);
		return;
	}
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (res == -1 || uncacheable || entry_in_correcting) {
		// If any ranges of entry are correcting, should keep doing disk IO
		if (corr_bi_flags && (TYPE_UNINITED == correcting_type)) {
			if (-1 == correction_range_set_type(dmc, bio_bi_sector(bio), DISK_CORRECTING)) {
				serr("Fail to set correction device DISK type on uncache");
			}
		} else if (SSD_CORRECTING == correcting_type) {
			sdbg(DF_CORRECTION, "Range is already in SSD_CORRECTING state");
		}
#else
	if (res == -1 || uncacheable) {
#endif /* CONFIG_SYNO_DATA_CORRECTION */

		if (uncache_seq) {
			atomic64_inc(&dmc->flashcache_stats.uncached_sequential_reads);
		}

		/* No room , non-cacheable or sequential i/o means not wanted in cache */
		spin_unlock_irq(&dmc->cache_spin_lock);
		DPRINTK("Cache read: Block %llu(%lu):%s",
			bio_bi_sector(bio), bio_bi_size(bio), "CACHE MISS & NO ROOM");
#ifdef MY_ABC_HERE
#else
		if (res == -1) {
			flashcache_clean_set(dmc, hash_block(dmc, bio_bi_sector(bio)));
		}
#endif
		sdbg(DF_DETAIL, "start uncached IO");

		/* Start uncached IO */
		flashcache_start_uncached_io(dmc, bio, NULL, TYPE_DISK);
		return;
	}
	/* 
	 * (res == INVALID) Cache Miss 
	 * And we found cache blocks to replace
	 * Claim the cache blocks before giving up the spinlock
	 */
	if (dmc->cache[index].cache_state & VALID) {
		atomic64_inc(&dmc->flashcache_stats.replace);
	} else {
		atomic64_inc(&dmc->cached_blocks);
	}
	cb_state_assign_update_counts(dmc, &dmc->cache[index], VALID | DISKREADINPROG);
#ifdef MY_ABC_HERE
	cacheblk_set_pin_state_by_bio(dmc, &dmc->cache[index], bio);
#endif
	cacheblk_unset_all_data_range(&dmc->cache[index]);
	dmc->cache[index].dbn = cacheblk_get_start_sector(dmc, bio);
	cacheblk_set_all_io_range(&dmc->cache[index]);
	cacheblk_add_num_concurrent(&dmc->cache[index]);
	spin_unlock_irq(&dmc->cache_spin_lock);

	DPRINTK("Cache read: Block %llu(%lu), index = %d:%s",
		bio_bi_sector(bio), bio_bi_size(bio), index, "CACHE MISS & REPLACE");
	flashcache_read_miss(dmc, bio, index);
}

/*
 * Invalidate any colliding blocks if they are !BUSY and !DIRTY. If the colliding
 * block is DIRTY, we need to kick off a write. In both cases, we need to wait 
 * until the underlying IO is finished, and then proceed with the invalidation.
 * SYNO:
 *	Under lock
 *	plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
 */
static int
flashcache_inval_block_set(struct cache_c *dmc, int set, struct bio *bio, int rw,
			   struct pending_job *pjob, struct io_latency *plat,
			   enum cache_io_type io_type)
{
	sector_t io_start = cacheblk_get_start_sector(dmc, bio);
	// Assume the bio doesn't across two cacheblocks, checked in flashcache_map
	sector_t io_end = io_start + (dmc->block_size - 1);
	int start_index = 0, end_index = 0, i = 0;
	struct cacheblock *cacheblk = NULL;
	sector_t start_dbn = 0;
	sector_t end_dbn = 0;

	VERIFY_WARN(io_end >= bio_last_sector(bio));

	start_index = dmc->assoc * set;
	end_index = start_index + dmc->assoc;
	for (i = start_index ; i < end_index ; i++) {
		start_dbn = dmc->cache[i].dbn;
		end_dbn = start_dbn + dmc->block_size;

		cacheblk = &dmc->cache[i];
		if (cacheblk->cache_state & INVALID)
			continue;
		if (io_start >= start_dbn && io_start < end_dbn) {
			break;
		}
	}
	if (i == end_index) {
		return 0;
	}

	/* We have a match */
	if (rw == WRITE)
		atomic64_inc(&dmc->flashcache_stats.wr_invalidates);
	else {
		sdbg(DF_DETAIL, "bi_sector=%llu bi_size=%llu io_start=%llu"
				"io_end=%llu start_dbn=%llu end_dbn=%llu",
				(u64)bio_bi_sector(bio), (u64)bio_sectors(bio), (u64)io_start,
				(u64)io_end, (u64)start_dbn, (u64)end_dbn);
		atomic64_inc(&dmc->flashcache_stats.rd_invalidates);
	}

	if (!(cacheblk->cache_state & (BLOCK_IO_INPROG | DIRTY | SYNCED_BITS)) &&
		(cacheblk->nr_queued == 0)) {
#ifdef CONFIG_SYNO_DATA_CORRECTION
		sdbg(DF_CORRECTION, "Check if abort correction");
		correction_entry_record_cache_addr(dmc, i, bio_bi_sector(bio));
		correction_entry_set_abort_bitmap(dmc, bio_bi_sector(bio));
#endif
		atomic64_dec(&dmc->cached_blocks);
		cb_state_assign_update_counts(dmc, cacheblk, INVALID);
		return 0;
	}
	/*
	 * The conflicting block has either IO in progress or is
	 * Dirty. In all cases, we need to add ourselves to the
	 * pending queue. Then if the block is dirty, we kick off
	 * an IO to clean the block.
	 * Note that if the block is dirty and IO is in progress
	 * on it, the do_pending handler will clean the block
	 * and then process the pending queue.
	 */
	flashcache_enq_pending(dmc, bio, i, INVALIDATE, pjob, plat, io_type);
	if (!(cacheblk->cache_state & BLOCK_IO_INPROG)) {
		if (cacheblk->cache_state & DIRTY) {
			/*
			* Kick off block write.
			* We can't kick off the write under the spinlock.
			* Instead, we mark the slot DISKWRITEINPROG, drop
			* the spinlock and kick off the write. A block marked
			* DISKWRITEINPROG cannot change underneath us.
			* to enqueue ourselves onto it's pending queue.
			*
			* XXX - The dropping of the lock here can be avoided if
			* we punt the cleaning of the block to the worker thread,
			* at the cost of a context switch.
			*/
			atomic64_inc(&dmc->flashcache_stats.inval_dirty_writeback);
			start_force_wb_to_invalid(dmc, i);
			spin_lock_irq(&dmc->cache_spin_lock);
		} else if (cacheblk->cache_state & SYNCED_BITS) {
			VERIFY_WARN(!(cacheblk->cache_state & FORCEDMU));
#ifdef MY_ABC_HERE
			start_force_mu_to_invalid(dmc, i);
#endif
			spin_lock_irq(&dmc->cache_spin_lock);
		}
	}
	// SYNO: queued
	return 1;
}

/* 
 * Since md will break up IO into blocksize pieces, we only really need to check 
 * the start set and the end set for overlaps.
 * SYNO:
 *	Under lock
 *	plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
 *	Return: 1 (queued)
 *	Return: 0 (not queued)
 *	Return < 0 on error
 */
static int
flashcache_inval_blocks(struct cache_c *dmc, struct bio *bio, struct io_latency *plat,
	enum cache_io_type io_type)
{
	// same as invalidate_block_set
	sector_t io_start = cacheblk_get_start_sector(dmc, bio);
	// Assume the bio won't across two cacheblocks, check it in flashcache_map
	int start_set;
	int queued;
	struct pending_job *pjob = NULL;

	pjob = flashcache_alloc_pending_job(dmc);
	if (unlikely(dmc->sysctl_error_inject & INVAL_PENDING_JOB_ALLOC_FAIL)) {
		if (pjob) {
			flashcache_free_pending_job(pjob);
			pjob = NULL;
		}
		dmc->sysctl_error_inject &= ~INVAL_PENDING_JOB_ALLOC_FAIL;
	}
	if (pjob == NULL) {
		queued = -ENOMEM;
		goto out;
	}

	start_set = hash_block(dmc, io_start);
	queued = flashcache_inval_block_set(dmc, start_set, bio,
					    bio_data_dir(bio), pjob, plat, io_type);
	if (queued) {
		goto out;
	} else {
		flashcache_free_pending_job(pjob);
	}
out:
	return queued;
}

// SYNO: Under lock
static void
flashcache_write_miss(struct cache_c *dmc, struct bio *bio, int index)
{
	struct cacheblock *cacheblk;
	struct kcached_job *job;
	int queued;
	int comp_dm_op = bio_has_fua_flags(bio) ? COMP_DM_WRITE_FUA : COMP_DM_WRITE;

	cacheblk = &dmc->cache[index];
	// TODO: maybe can remove, added probably because bio in
	//       old 4k driver can cross multiple cacheblk
	queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_COULD_SSD);
	if (queued) {
		VERIFY_WARN(0);
		atomic64_inc(&dmc->flashcache_stats.write_miss_inval);
		if (unlikely(queued < 0)) {
			flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
		}
		sdbg(DF_DETAIL, "enq_pending finish");
		spin_unlock_irq(&dmc->cache_spin_lock);
		return;
	}
#ifdef MY_ABC_HERE
	atomic64_inc(&dmc->flashcache_stats.wr_miss_ssd);
#endif

	if (cacheblk->cache_state & VALID) {
		atomic64_inc(&dmc->flashcache_stats.wr_replace);
	} else {
		atomic64_inc(&dmc->cached_blocks);
	}
	cb_state_assign_update_counts(dmc, cacheblk, VALID | CACHEWRITEINPROG);
#ifdef MY_ABC_HERE
	cacheblk_set_pin_state_by_bio(dmc, cacheblk, bio);
#endif
	cacheblk_unset_all_data_range(cacheblk);
	cacheblk->dbn = cacheblk_get_start_sector(dmc, bio);
	cacheblk_add_io_range_by_bio(cacheblk, bio);
	cacheblk_add_num_concurrent(cacheblk);

	if (global_tester == TEST_OQF_INVALIDATE_DELAY) {
		serr("write miss idx: %d", index);
	}

	spin_unlock_irq(&dmc->cache_spin_lock);
	job = new_kcached_job(dmc, bio, index, NULL, TYPE_SSD);
	if (unlikely(dmc->sysctl_error_inject & WRITE_MISS_JOB_ALLOC_FAIL)) {
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		dmc->sysctl_error_inject &= ~WRITE_MISS_JOB_ALLOC_FAIL;
	}
	if (unlikely(job == NULL)) {
		/* 
		 * We have a write miss, and can't allocate a job.
		 * Since we dropped the spinlock, we have to drain any 
		 * pending jobs.
		 */
		DMERR("flashcache: Write (miss) failed ! Can't allocate memory for cache IO, block %llu",
				(u64)cacheblk->dbn);

		spin_lock_irq(&dmc->cache_spin_lock);
		atomic64_dec(&dmc->cached_blocks);
		cb_state_remove_bits_update_counts(dmc, cacheblk, VALID);

#ifdef MY_ABC_HERE
		cacheblk_check_unset_pin_state(dmc, cacheblk);
#endif
		cb_state_add_bits_update_counts(dmc, cacheblk, INVALID);
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);

		cacheblk_remove_io_range_change_state_by_bio(dmc, cacheblk, bio);
		cacheblk_dec_num_concurrent_and_return(cacheblk);
		flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);

		spin_unlock_irq(&dmc->cache_spin_lock);
	} else {
		atomic_inc(&dmc->nr_jobs);
		atomic64_inc(&dmc->flashcache_stats.ssd_writes);
		job->action = WRITECACHE; 
		atomic_inc(&dmc->num_write_cache);
		if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
			/* Write data to the cache */
			job_dbg(__func__, job);
			dm_io_async_bio_wrapper(1, &job->job_io_regions.cache, comp_dm_op,
					 bio, flashcache_io_callback_atomic, job, 0);
		} else {
			atomic64_inc(&dmc->flashcache_stats.disk_writes);

			VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_THROUGH);
			/* Write data to both disk and cache */
			dm_io_async_bio_wrapper(2, (struct dm_io_region*)&job->job_io_regions,
				comp_dm_op, bio, flashcache_io_callback_atomic, job, 0);
		}
#ifdef MY_ABC_HERE
#else
		flashcache_clean_set(dmc, index / dmc->assoc);
#endif
	}
}

static void
flashcache_write_hit(struct cache_c *dmc, struct bio *bio, int index)
{
	struct cacheblock *cacheblk;
	struct pending_job *pjob;
	struct kcached_job *job;
	match_t match_io_range = MATCH_UNSET;
	unsigned long bi_flags = 0;
	int comp_dm_op = bio_has_fua_flags(bio) ? COMP_DM_WRITE_FUA : COMP_DM_WRITE;

	cacheblk = &dmc->cache[index];
#ifdef MY_ABC_HERE
	cacheblk_set_pin_state_by_bio(dmc, cacheblk, bio);
#endif

#ifdef CONFIG_SYNO_DATA_CORRECTION
	bi_flags = bio->bi_flags & ((1 << BIO_CORRECTION_RETRY) | (1 << BIO_CORRECTION_ABORT));
#endif

	match_io_range = cacheblk_compare_io_range(cacheblk, bio);

	if ((MATCH_NONE == match_io_range) && (cacheblk->nr_queued == 0)) {
		/*
		 * Cases:
		 *  All in data range / Partial in data range / NONE in data range
		 */
		if (cacheblk->cache_state & DIRTY) {
			atomic64_inc(&dmc->flashcache_stats.dirty_write_hits);
		}
		atomic64_inc(&dmc->flashcache_stats.write_hits);
		cb_state_add_bits_update_counts(dmc, cacheblk, CACHEWRITEINPROG);

		cacheblk_add_io_range_by_bio(cacheblk, bio);
		cacheblk_add_num_concurrent(cacheblk);

		spin_unlock_irq(&dmc->cache_spin_lock);
		job = new_kcached_job(dmc, bio, index, NULL, TYPE_SSD);
		if (unlikely(dmc->sysctl_error_inject & WRITE_HIT_JOB_ALLOC_FAIL)) {
			if (job)
				flashcache_free_cache_job(job);
			job = NULL;
			dmc->sysctl_error_inject &= ~WRITE_HIT_JOB_ALLOC_FAIL;
		}
		if (unlikely(job == NULL)) {
			/* 
			 * We have a write hit, and can't allocate a job.
			 * Since we dropped the spinlock, we have to drain any 
			 * pending jobs.
			 */
			DMERR("flashcache: Write (hit) failed ! Can't allocate memory for cache IO, block %llu",
				(u64)cacheblk->dbn);

			spin_lock_irq(&dmc->cache_spin_lock);
			flashcache_free_pending_jobs(dmc, cacheblk, -EIO);

			cacheblk_remove_io_range_change_state_by_bio(dmc, cacheblk, bio);
			cacheblk_dec_num_concurrent_and_return(cacheblk);
			flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);

			spin_unlock_irq(&dmc->cache_spin_lock);
		} else {
			DPRINTK("Queue job for %llu", bio_bi_sector(bio));
			atomic_inc(&dmc->nr_jobs);
			atomic64_inc(&dmc->flashcache_stats.ssd_writes);
			job->action = WRITECACHE;
			if (dmc->cache_mode == FLASHCACHE_WRITE_BACK) {
				/* Write data to the cache */
				job_dbg(__func__, job);
				atomic_inc(&dmc->num_write_cache);
				dm_io_async_bio_wrapper(1, &job->job_io_regions.cache, comp_dm_op,
					bio, flashcache_io_callback_atomic, job, bi_flags);

#ifdef MY_ABC_HERE
#else
				flashcache_clean_set(dmc, index / dmc->assoc);
#endif
			} else {
				VERIFY_WARN(dmc->cache_mode == FLASHCACHE_WRITE_THROUGH);
				/* Write data to both disk and cache */
				atomic64_inc(&dmc->flashcache_stats.disk_writes);
				dm_io_async_bio_wrapper(2, (struct dm_io_region*)&job->job_io_regions,
					comp_dm_op, bio, flashcache_io_callback_atomic,
					job, bi_flags);
			}
		}
	} else {
		pjob = flashcache_alloc_pending_job(dmc);
		if (unlikely(dmc->sysctl_error_inject & WRITE_HIT_PENDING_JOB_ALLOC_FAIL)) {
			if (pjob) {
				flashcache_free_pending_job(pjob);
				pjob = NULL;
			}
			dmc->sysctl_error_inject &= ~WRITE_HIT_PENDING_JOB_ALLOC_FAIL;
		}
		atomic64_inc(&dmc->flashcache_stats.write_hit_busy_inval);

		// add braces here
		if (unlikely(pjob == NULL)) {
			flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
		} else {
			/*
			 * Do not add pending WRITE_CACHE job to num_write_cache since
			 * later it would be done by uncached IO
			 */
			sdbg(DF_DETAIL, "enq_pending");
			flashcache_enq_pending(dmc, bio, index, WRITECACHE, pjob,
				NULL, TYPE_COULD_SSD);
		}

		spin_unlock_irq(&dmc->cache_spin_lock);
	}
}

static void
flashcache_write(struct cache_c *dmc, struct bio *bio)
{
	int index;
	int res;
	struct cacheblock *cacheblk;
	int queued;
#ifdef CONFIG_SYNO_DATA_CORRECTION
	correcting_type_t correcting_type = NO_CORRECTING;
	int entry_in_correcting = 0;

	correcting_type = correction_range_get_type(dmc, bio_bi_sector(bio));
	entry_in_correcting = correction_entry_in_correcting(dmc, bio_bi_sector(bio), DO_LOCK);
#endif

	spin_lock_irq(&dmc->cache_spin_lock);
	res = flashcache_lookup(dmc, bio, &index);

#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (res != -1 && DISK_CORRECTING != correcting_type) {
#else
	if (res != -1) {
#endif
		/* Cache Hit */
		cacheblk = &dmc->cache[index];		
        /*
         * Three cases go here
         * 1 VALID with matched  cacheblk (Cache state is VALID)
         * 2 INVALID with state==INVALID cacheblk
         * 3 INVALID with old VALID cacheblk (Cache state == VALID) (reclaim_oldest_clean!)
         * To distiguish 1 & 3, the old way is to check if cacheblk->dbn match !
         */
		if ((VALID == res) && (cacheblk->cache_state & VALID)) {
			if (bio_bi_sector(bio) < cacheblk->dbn) {
				serr("compare_io_range: index=%d cacheblk.dbn=%llu bi_sector=%llu",
						index, (u64)cacheblk->dbn, (u64)bio_bi_sector(bio));
				VERIFY_WARN(0);
			}
			/* Cache Hit */
			flashcache_write_hit(dmc, bio, index);
		} else {
#ifdef CONFIG_SYNO_DATA_CORRECTION
			if (entry_in_correcting) {
				sdbg(DF_CORRECTION, "correcting is now on disk, should not write ssd.");
				goto UNCACHE_WRITE;
			}
#endif
			/* Cache Miss, found block to recycle */
			flashcache_write_miss(dmc, bio, index);
		}
		return;
	} // check res

	sdbg(DF_DETAIL, "write: noroom");
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (DISK_CORRECTING == correcting_type) {
		sdbg(DF_DETAIL, "write: correcting on disk");
	}

UNCACHE_WRITE:
#endif
	/*
	 * No room in the set. We cannot write to the cache and have to 
	 * send the request to disk. Before we do that, we must check 
	 * for potential invalidations !
	 */
	queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_COULD_SSD);
	spin_unlock_irq(&dmc->cache_spin_lock);
	if (queued) {
		if (unlikely(queued < 0))
			flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
		return;
	}
	/* Start uncached IO */
	flashcache_start_uncached_io(dmc, bio, NULL, TYPE_COULD_SSD);
#ifdef MY_ABC_HERE
#else
	flashcache_clean_set(dmc, index / dmc->assoc);
#endif
}

#define bio_barrier(bio)        (bio_op_rw(bio) & COMP_REQ_FLUSH)

#ifdef MY_ABC_HERE
/*
 *  Assume a bio won't across two cache block
 *	Due to we set spilt_io to 4KB
 */
int is_across_two_cacheblk(struct cache_c *dmc, struct bio *bio)
{
	int ret = 0;
	char bio_str[BIO_STR_MAX] = {0};
	sector_t cacheblk_start_sector = 0;
	sector_t cacheblk_end_secotr = 0;

	VERIFY_WARN(dmc && bio);
	cacheblk_start_sector = cacheblk_get_start_sector(dmc, bio);
	cacheblk_end_secotr = cacheblk_start_sector + dmc->block_size - 1;

	if (bio_last_sector(bio) > cacheblk_end_secotr) {
		serr("Should not across two cacheblock %s", bio_to_str(bio, bio_str, sizeof(bio_str)));
		VERIFY_WARN(0);
		ret = 1;
	}

	return ret;
}

#ifdef MY_DEF_HERE
// Also update cache_bio->next_bv_idx
static void init_subbio_bvec_fields(struct shared_data *shared_data, struct subbio *subbio)
{
	struct bio *master_bio = shared_data->master_bio;
	struct bio_vec *bvec = NULL;
	int found_first_bv = 0;
	int found_last_bv = 0;
	int i = 0;
	unsigned int first_bv_offset_advance = 0;
	int bv_len_occupy = 0;

	u64 bio_offset_byte = to_bytes(subbio->start_sector - bio_bi_sector(master_bio));
	u64 bio_size_bytes = to_bytes(subbio->num_sectors);

	/*
	 * For kernel 4.4, we hack __bio_clone_fast() to make bi_vcnt of master_bio
	 * is not zero
	 */
	VERIFY_WARN(master_bio->bi_vcnt);

	VERIFY_WARN(shared_data && subbio && master_bio);

	for (i = shared_data->next_bv_idx; i < master_bio->bi_vcnt; i++) {

		bvec = &master_bio->bi_io_vec[i];

		if (!found_first_bv) {

			if ((shared_data->bv_len_sum + bvec->bv_len) > bio_offset_byte) {
				// Got first bvec
				first_bv_offset_advance = bio_offset_byte - shared_data->bv_len_sum;
				subbio->first_bv_offset = bvec->bv_offset + first_bv_offset_advance;
				subbio->first_bv_idx = i;
				subbio->first_bv_len = bvec->bv_len - first_bv_offset_advance;
				found_first_bv = 1;

				bv_len_occupy = subbio->first_bv_len;

				if (bv_len_occupy >= bio_size_bytes) {
					// Also the last bv
					subbio->first_bv_len = bio_size_bytes;
					subbio->last_bv_len = bio_size_bytes;
					subbio->last_bv_idx = i;
					sdbg(DF_DETAIL, "found last bv");
					found_last_bv = 1;
					break;
				}
			}
			shared_data->bv_len_sum += bvec->bv_len;
		} else {
			// Already found the first bv
			if ((bv_len_occupy + bvec->bv_len) >= bio_size_bytes) {
				subbio->last_bv_len = bio_size_bytes - bv_len_occupy;
				subbio->last_bv_idx = i;
				found_last_bv = 1;
				break;
			} else {
				bv_len_occupy += bvec->bv_len;
				shared_data->bv_len_sum += bvec->bv_len;
			}
		}
	}

	VERIFY_WARN(found_first_bv);
	VERIFY_WARN(found_last_bv);

	shared_data->next_bv_idx = subbio->last_bv_idx;
	sdbg(DF_DETAIL, "subbio bi_sector=%llu first_bv_len=%u fisrt_bv_offset=%u first_bv_idx=%hu last_bv_len=%u \
			last_bv_idx=%hu",
			(u64)subbio->start_sector, subbio->first_bv_len, subbio->first_bv_offset,
			subbio->first_bv_idx, subbio->last_bv_len, subbio->last_bv_idx);
}

/*
 *	Return 1 when there's no sub-bios
 */
int cache_bio_get_next_bio_info(struct shared_data *shared_data, struct subbio *subbio)
{

	sector_t sectors_to_previous_algin = 0;
	sector_t sectors_to_next_align = 0;
	sector_t sectors_to_handle = 0;
	sector_t unhandled_sectors = 0;

	VERIFY_WARN(shared_data && subbio);

	unhandled_sectors = shared_data->unhandled_sectors;

	if (0 == unhandled_sectors) {
		sdbg(DF_DETAIL, "No more sub-bios in the cache bio");
		return 1;
	}

	subbio->start_sector = shared_data->next_bio_start;
	sectors_to_previous_algin = compatible_mod(shared_data->next_bio_start, MAX_BIO_SIZE);
	sectors_to_next_align = MAX_BIO_SIZE - sectors_to_previous_algin;
	sectors_to_handle = (sectors_to_next_align < unhandled_sectors) ? sectors_to_next_align : unhandled_sectors;
	subbio->num_sectors = sectors_to_handle;

	// Update shared data information
	shared_data->next_bio_start += sectors_to_handle;
	shared_data->unhandled_sectors -= sectors_to_handle;

	init_subbio_bvec_fields(shared_data, subbio);

	return 0;
}

typedef enum __tag_SUB_BIO_STATUS {
	SUB_BIO_CLONE_ERROR = -2,
	SUB_BIO_NONE = -1,
	SUB_BIO_NORMAL
} SUB_BIO_STATUS;

struct bio* cache_bio_alloc_subbio(struct shared_data *shared_data, struct subbio *subbio)
{
	struct bio *master_bio = shared_data->master_bio;
	struct bio *bio = NULL;
	unsigned short bv_count = 0;

	bv_count = subbio->last_bv_idx - subbio->first_bv_idx + 1;

	bio  = bio_alloc_bioset(GFP_NOIO, bv_count, pcache_bio_set);
	if (!bio) {
		serr("blo_alloc_bioset failed");
		return NULL;
	}

	/*
	 * In original bio_clone_bioset() , it deals with bio_integrity property.
	 * However, this property seems to be reserved for file system to implement
	 * data integrity and no one use it yet. So we doesn't support it now
	 */
	if (bio_integrity(bio)) {
		serr("Doesn't support bio_integrity");
		VERIFY_WARN(0);
	}

	// Refer to bio_clone and md_trim_bio
	*bio_bi_sector_ptr(bio) = subbio->start_sector;
	*bio_bi_size_ptr(bio) = to_bytes(subbio->num_sectors);
	bio_assign_same_dev(bio, master_bio);

	// Set BIO_CLONE to indicate that it doesn't own data
	bio->bi_flags |= 1 << BIO_CLONED;
	bio_op_rw(bio) = bio_op_rw(master_bio);

	memcpy(bio->bi_io_vec, master_bio->bi_io_vec + subbio->first_bv_idx,
			bv_count * sizeof(struct bio_vec));

	*bio_bi_idx_ptr(bio) = 0;
	bio->bi_vcnt = bv_count;

	// Change the first bv
	bio->bi_io_vec[0].bv_offset = subbio->first_bv_offset;
	bio->bi_io_vec[0].bv_len = subbio->first_bv_len;

	// Change the last bv
	bio->bi_io_vec[bv_count - 1].bv_len = subbio->last_bv_len;

	// Drop BIO_SEG_VALID since we change the bi_idx and bi_vcnt (refer to md_trim_bio())
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	bio_clear_flag(bio, BIO_SEG_VALID);
#else
	clear_bit(BIO_SEG_VALID, &bio->bi_flags);
#endif

	sdbg(DF_DETAIL, "bi_sector=%llu bi_size=%u bi_idx=%hu bi_vcnt=%hu", (u64)bio_bi_sector(bio),
			bio_bi_size(bio), bio_bi_idx(bio), bio->bi_vcnt);
	sdbg(DF_DETAIL, "first bv[0]: offset=%u len=%u", bio->bi_io_vec[0].bv_offset, bio->bi_io_vec[0].bv_len);
	sdbg(DF_DETAIL, "last bv[%hu]: len=%u", bv_count - 1, bio->bi_io_vec[bv_count - 1].bv_len);

	return bio;
}

void cache_bio_set_num_total_bios(struct cache_bio *cache_bio, struct bio *master_bio)
{
	sector_t next_start_sector = 0;
	int unhandled_sectors = 0;
	// Sectors to the previos MAX_BIO_SIZE alignment
	int sectors_to_previous_algin = 0;
	int sectors_to_next_align = 0;
	int next_bio_sectors = 0;
	char bio_str[BIO_STR_MAX] = {0};

	VERIFY_WARN(cache_bio && master_bio);

	sdbg_in(DF_DETAIL, "master_bio=%s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));

	unhandled_sectors = bio_sectors(master_bio);
	next_start_sector = bio_bi_sector(master_bio);

	while (1) {
		sectors_to_previous_algin = compatible_mod(next_start_sector, MAX_BIO_SIZE);
		sectors_to_next_align = MAX_BIO_SIZE - sectors_to_previous_algin;
		next_bio_sectors = (sectors_to_next_align < unhandled_sectors) ? sectors_to_next_align : unhandled_sectors; 
		unhandled_sectors -= next_bio_sectors;
		cache_bio->num_total_bios++;

		if (0 == unhandled_sectors) {
			break;
		}
		next_start_sector += next_bio_sectors;
	}

	sdbg_out(DF_DETAIL, "master_bio=%s num_total_bios=%d", bio_to_str(master_bio, bio_str, sizeof(bio_str)), cache_bio->num_total_bios);

}

static struct cache_bio * cache_bio_alloc_init(struct bio *master_bio, unsigned long start_jiffy)
{
	struct cache_bio *cache_bio = NULL;
	char bio_str[BIO_STR_MAX] = {0};

	VERIFY_WARN(master_bio);

	sdbg_in(DF_DETAIL, "master_bio=%s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));

	cache_bio = mempool_alloc(_cache_bio_pool, GFP_NOIO);
	if (NULL == cache_bio) {
		serr("Can't allocate cache_bio");
		goto err;
	}

	memset(cache_bio, 0, sizeof(struct cache_bio));

	cache_bio->master_bio = master_bio;
	cache_bio_set_num_total_bios(cache_bio, master_bio);
	atomic_set(&cache_bio->num_completed_bios, 0);

	cache_bio->start_jiffy = start_jiffy;

	sdbg_out(DF_DETAIL, "master_bio=%s num_total_bios=%d", bio_to_str(master_bio, bio_str, sizeof(bio_str)), cache_bio->num_total_bios); 
err:
	return cache_bio;
}

/*
 * Return:
 * SUB_BIO_CLONE_ERROR: on error
 * SUB_BIO_NORMAL: Still has next subbio
 * SUB_BIO_NONE: This is the final subbio
 */
static SUB_BIO_STATUS cache_bio_get_next_bio(struct cache_bio *cache_bio,
		struct shared_data *shared_data, struct bio **ppbio, struct cache_c *dmc)
{
	SUB_BIO_STATUS status = SUB_BIO_CLONE_ERROR;
	struct subbio subbio = {0};
#ifdef MY_ABC_HERE
	struct subbio_info *psubbio_info = NULL;
	static int print_retry = 0;

	while (1) {
		psubbio_info = kzalloc(sizeof(struct subbio_info), GFP_NOIO);
		if (psubbio_info) {
			break;
		} else {
			if (!print_retry) {
				serr("Retry to allocate subbio info!");
				print_retry = 1;
			}
			msleep(100);
		}
	}
#endif

	VERIFY_WARN(shared_data && ppbio && dmc);

	if (cache_bio_get_next_bio_info(shared_data, &subbio)) {
		// No subbio
		status = SUB_BIO_NONE;
		goto end;
	}

	*ppbio = cache_bio_alloc_subbio(shared_data, &subbio);
	if (NULL == *ppbio) {
		serr("Failed to clone cache bio");
		goto end;
	}

#ifdef MY_ABC_HERE
	psubbio_info->pcache_bio = cache_bio;
	psubbio_info->is_pin = 0;
	(*ppbio)->bi_private = psubbio_info;
#else
	(*ppbio)->bi_private = cache_bio;
#endif
	status = SUB_BIO_NORMAL;
	sdbg(DF_DETAIL, "next bio=%p bi_sector=%llu size=%llu", *ppbio, (u64)bio_bi_sector(*ppbio), (u64)to_sector(bio_bi_size(*ppbio)));
	VERIFY_WARN(0 != bio_bi_size(*ppbio));

end:
#ifdef MY_ABC_HERE
	if (status != SUB_BIO_NORMAL) {
		if (psubbio_info) {
			kfree(psubbio_info);
		}
	}
#endif
	return status;

}

/*
 * Return NULL on error
 * Note: Can't be called under lock
 */

struct bio ** cache_bio_get_bios(struct cache_bio *cache_bio, struct cache_c *dmc)
{
	int i = 0;
	struct bio **ppbios = NULL;
	struct bio *master_bio = cache_bio->master_bio;
	struct shared_data shared_data = {0};
	int size = 0;
	char bio_str[BIO_STR_MAX] = {0};
	SUB_BIO_STATUS sub_bio_status = SUB_BIO_CLONE_ERROR;

	VERIFY_WARN(cache_bio && dmc);

	sdbg_in(DF_DETAIL, "master bio: %s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));

	size = cache_bio->num_total_bios * sizeof(struct bio *);

	ppbios = kmalloc(size, GFP_NOIO);
	if (NULL == ppbios) {
		serr("Can't allocate subbio arrsy!");
		return NULL;
	}

	memset(ppbios, 0, size);

	shared_data.master_bio = master_bio;
	shared_data.next_bio_start = bio_bi_sector(master_bio);
	shared_data.unhandled_sectors = bio_sectors(master_bio);
	shared_data.next_bv_idx = bio_bi_idx(master_bio);

	for (i = 0; i < cache_bio->num_total_bios ; i++) {
		sub_bio_status = cache_bio_get_next_bio(cache_bio, &shared_data, &ppbios[i], dmc);
		if (SUB_BIO_CLONE_ERROR == sub_bio_status) {
			serr("Get next subbio failed");
			goto err;
		}

		// Should get each subbio
		VERIFY_WARN(SUB_BIO_NONE != sub_bio_status);
	}

	sdbg_out(DF_DETAIL, "master_bio: %s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));

	return ppbios;

err:
	for (i = 0; i < cache_bio->num_total_bios; i++) {
		if (NULL != ppbios[i]) {
			// For bio_alloc
			bio_put(ppbios[i]);
		}
	}

	if (ppbios) {
		kfree(ppbios);
	}

	sdbg_out(DF_DETAIL, "master_bio: %s", bio_to_str(cache_bio->master_bio, bio_str, sizeof(bio_str)));

	return NULL;
}

#else /* NOT MY_DEF_HERE */

static struct cache_bi_private *
cache_bi_private_alloc_init(struct bio *master_bio, unsigned long start_jiffy)
{
	struct cache_bi_private *private = NULL;
	int err = 0;

	VERIFY_WARN(master_bio);

	private = mempool_alloc(_cache_bi_private_pool, GFP_NOIO);
	if (NULL == private) {
		serr("Can't allocate cache bi private data");
		err = 1;
		goto end;
	}

	memset(private, 0, sizeof(struct cache_bi_private));
	private->master_bio_private = master_bio->bi_private;
	private->start_jiffy = start_jiffy;
	private->is_pin = 0;

end:
	if (err && private) {
		mempool_free(private, _cache_bi_private_pool);
		private = NULL;
	}
	return private;
}
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
#else
#ifdef MY_ABC_HERE
void
flush_bios_complete(unsigned long error, void *context)
{
	struct flush_data *data = (struct flush_data *)context;
	struct cache_c *dmc = data->dmc;
	struct bio *master_bio = data->master_bio;
	int bio_error = 0;

	if (dmc->sysctl_error_inject & FLUSH_BIO_ERROR) {
		sprint("Detect flag, simulate flush error");
		dmc->sysctl_error_inject &= ~FLUSH_BIO_ERROR;
		error = 3;
	}

	// Bit 0
	if (error & (1 << 0)) {
		if (printk_ratelimit()) {
			serr("Send COMP_DM_WRITE_FLUSH to Disk failed");
		}
		bio_error = COMP_BIO_ERROR;
	}

	// Bit 1
	if (error & (1 << 1)) {
		if (dmc->bypass_cache) {
			if (printk_ratelimit()) {
				serr("Bypass mode, ignore the error of sending COMP_DM_WRITE_FLUSH to SSD");
			}
		} else if ((FLASHCACHE_WRITE_AROUND == dmc->cache_mode)) {
			// First get error
			if (printk_ratelimit()) {
				serr("Send COMP_DM_WRITE_FLUSH failed. set to bypass mode");
			}
			dmc->bypass_cache = 1;
		} else if (is_writeback_crash_safe_set_bypass(dmc)) {
			if (printk_ratelimit()) {
				serr("Send WRITE_FLUSH failed. set to bypass mode");
			}
		} else {
			if (printk_ratelimit()) {
				serr("Send COMP_DM_WRITE_FLUSH to SSD failed");
			}
			bio_error = COMP_BIO_ERROR;
		}
	}

	VERIFY_WARN(master_bio != NULL);

	flashcache_check_process_io_latency(dmc, &data->io_lat, LAT_FLUSH_BIO);

	/*
	 * Note: Traced dm.c, found __map_bio->dec_pending() does not return REQ_FLUSH error to
	 * the upper layer. It just removes REQ_FLUSH and resends the bio ...
	 */
	bio_endio_wrapper(master_bio, bio_error);
#ifdef MY_ABC_HERE
	handle_master_io_finish(dmc);
#endif /* MY_ABC_HERE */
	kfree(data);
}

struct flush_data *
alloc_flush_data(void)
{
	static int print = 0;
	struct flush_data *data = NULL;

	while (1) {
		data = kzalloc(sizeof(struct flush_data), GFP_NOIO);
		if (data) {
			break;
		} else {
			if (!print) {
				serr("Retry to allocate flush_data");
				print = 1;
			}
			msleep(100);
		}
	}

	return data;
}

void
send_flush_bios(struct cache_c *dmc, struct bio *master_bio, unsigned long start_jiffy)
{
	struct dm_io_region io_regions[2];
	struct flush_data *data = NULL;
	int send_region = 0;

	memset(io_regions, 0, sizeof(io_regions));

	atomic_inc(&dmc->in_flush);

	wait_event(dmc->wait_io_done_queue, (!atomic_read(&dmc->num_uncached_write))
				&& (!atomic_read(&dmc->num_write_cache)));

	data = alloc_flush_data();
	data->master_bio = master_bio;
	data->dmc = dmc;
	/* Has preprocess time */
	flashcache_init_io_latency(&data->io_lat,
		dmc->sysctl_syno_latency_diagnose, TYPE_DISK, start_jiffy, 1);

	io_regions[0].bdev = get_disk_bdev(dmc);
	io_regions[0].sector = 0;
	io_regions[0].count = 0;

	io_regions[1].bdev = get_cache_bdev(dmc);
	io_regions[1].sector = 0;
	io_regions[1].count = 0;

	if (dmc->bypass_cache) {
		// SSD RAID crash, only send to disks
		send_region = 1;
	} else {
		send_region = 2;
	}

	dm_io_async_bio_wrapper(send_region, io_regions, COMP_DM_WRITE_FLUSH, master_bio,
		flush_bios_complete, data, 0);

	atomic_inc(&dmc->num_flush_bio);
	atomic_dec(&dmc->in_flush);
	wake_up(&dmc->wait_flush_queue);
}
#endif /* MY_ABC_HERE */
#endif /* ! MY_ABC_HERE */

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int flashcache_map_core(struct dm_target *ti, struct bio *master_bio, int enable_noclone);
int flashcache_map(struct dm_target *ti, struct bio *master_bio)
{
	return flashcache_map_core(ti, master_bio, 0);
}
int flashcache_noclone_map(struct dm_target *ti, struct bio *master_bio)
{
	return flashcache_map_core(ti, master_bio, 1);
}
#endif /* MY_ABC_HERE */

void
process_single_bio(struct cache_c *dmc, struct bio *master_bio, struct bio *bio)
{
	int queued = 0;
	int uncacheable = 0;
	int uncache_seq = 0;

#ifdef CONFIG_SYNO_DATA_CORRECTION
	if ((master_bio->bi_flags & (1 << BIO_CORRECTION_RETRY))) {
		bio->bi_flags |= 1 << BIO_CORRECTION_RETRY;
		sdbg(DF_CORRECTION, "cache: bio_sector=%llu set BIO_CORRECTION_RETRY", (u64)bio_bi_sector(bio));
		correction_handle_retry_io_start(dmc, bio_bi_sector(bio));
	} else if (master_bio->bi_flags & (1 << BIO_CORRECTION_ABORT)) {
		bio->bi_flags |= 1 << BIO_CORRECTION_ABORT;
		sdbg(DF_CORRECTION, "cache: bio_sector=%llu set BIO_CORRECTION_ABORT", (u64)bio_bi_sector(bio));
	}
#endif /* CONFIG_SYNO_DATA_CORRECTION */

#ifdef MY_ABC_HERE
	bio_set_pin_state(dmc, bio);
#endif

	if (bio_data_dir(bio) == READ)
		atomic64_inc(&dmc->flashcache_stats.reads);
	else
		atomic64_inc(&dmc->flashcache_stats.writes);

	uncacheable = flashcache_uncacheable(dmc, bio, &uncache_seq);

	spin_lock_irq(&dmc->cache_spin_lock);
#ifdef MY_ABC_HERE
#else
	if (unlikely(dmc->sysctl_pid_do_expiry && 
		     (dmc->whitelist_head || dmc->blacklist_head)))
		flashcache_pid_expiry_all_locked(dmc);
#endif

	if (unlikely(dmc->bypass_cache) ||
		compatible_mod(bio_bi_sector(bio), ALIGNED_SECTORS) ||
		compatible_mod(bio_sectors(bio), ALIGNED_SECTORS) ||
		is_across_two_cacheblk(dmc, bio) ||
		(bio_data_dir(bio) == WRITE && 
		(dmc->cache_mode == FLASHCACHE_WRITE_AROUND || uncacheable))) {

		if (compatible_mod(bio_bi_sector(bio), ALIGNED_SECTORS) ||
			compatible_mod(bio_sectors(bio), ALIGNED_SECTORS)) {
			sdbg(DF_DETAIL, "bi_sector=%llu bi_size=%llu is not 4kb align",
					(u64)bio_bi_sector(bio), (u64)bio_sectors(bio));
			atomic64_inc(&dmc->flashcache_stats.skip_unaligned_io);

		} else if (is_across_two_cacheblk(dmc, bio)) {
			serr("across two cacheblks");
			VERIFY_WARN(0);
		} else if (uncacheable) {
			sdbg(DF_DETAIL, "bio is uncacheable");
		} else if (dmc->bypass_cache) {
			sdbg(DF_DETAIL, "bypass_cache is set !");
		}  else {
			sdbg(DF_DETAIL, "others reason???");
		}

		if (bio_data_dir(bio) == WRITE && uncache_seq) {
			atomic64_inc(&dmc->flashcache_stats.uncached_sequential_writes);
		}

		queued = flashcache_inval_blocks(dmc, bio, NULL, TYPE_DISK);

		spin_unlock_irq(&dmc->cache_spin_lock);
		if (queued) {
			atomic64_inc(&dmc->flashcache_stats.map_inval);
			if (uncacheable) {
				atomic64_inc(&dmc->flashcache_stats.uncacheable_inval);
			}
			if (unlikely(queued < 0)) {
				serr("unable to queue");
				flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, NULL);
			}
		} else {
			/* Start uncached IO */
			flashcache_start_uncached_io(dmc, bio, NULL, TYPE_DISK);
		}
	} else {
		spin_unlock_irq(&dmc->cache_spin_lock);		
		if (bio_data_dir(bio) == READ) {
			flashcache_read(dmc, bio, uncacheable, uncache_seq);
		} else {
			flashcache_write(dmc, bio);
		}
	}
}

/*
 * Decide the mapping and perform necessary cache operations for a bio request.
 */

int
// Use master_bio to record the original bio
#ifdef MY_ABC_HERE
static flashcache_map_core(struct dm_target *ti, struct bio *master_bio, int enable_noclone)
#else
flashcache_map(struct dm_target *ti, struct bio *master_bio)
#endif /* MY_ABC_HERE */
{
	struct cache_c *dmc = (struct cache_c *) ti->private;

	// Record subbio
	struct bio *bio = NULL;
	char bio_str[BIO_STR_MAX] = {0};
	int sectors = to_sector(bio_bi_size(master_bio));
	static int print = 0;
	unsigned long flags;
	int go_wait = 0;
	unsigned long start_jiffy = jiffies;
#ifdef MY_DEF_HERE
	struct bio **ppbios = NULL;
	struct cache_bio *cache_bio = NULL;
	int i = 0;
	int num_total_bios = 0;
#else
	struct cache_bi_private *pcache_bi_private = NULL;
#endif
	int err = 0;

	sdbg(DF_DETAIL, "master_bio: %s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));

#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (master_bio->bi_flags & 1 << BIO_CORRECTION_RETRY) {
		sdbg(DF_CORRECTION, "cache: get retry bio: %s:", bio_to_str(master_bio, bio_str,
					sizeof(bio_str)));
	}
	if (master_bio->bi_flags & 1 << BIO_CORRECTION_ABORT) {
		sdbg(DF_CORRECTION, "cache: get abort bio: %s:", bio_to_str(master_bio, bio_str,
				sizeof(bio_str)));
	}
#endif /* CONFIG_SYNO_DATA_CORRECTION */

#ifdef CONFIG_SYNO_MD_UNUSED_HINT
	if (bio_is_unused_hint(master_bio)) {
		bio_assign_dev(master_bio, get_disk_bdev(dmc));
		return DM_MAPIO_REMAPPED;
	}
#endif /* CONFIG_SYNO_MD_UNUSED_HINT */

#ifdef MY_ABC_HERE
	if (enable_noclone) {
		bio_assign_dev(master_bio, get_disk_bdev(dmc));
		return DM_MAPIO_REMAPPED;
	}
#endif /* MY_ABC_HERE */

	if (sectors <= MAX_HIST_SECTOR)
		size_hist[sectors]++;

	// Use spin lock to make sure there're no another IOs enter here
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if ((CACHE_WAIT_OLD_IOS_COMPLETED == dmc->cache_state)
			|| (CACHE_WAIT_DISABLE == dmc->cache_state)) {
		go_wait = 1;
	} else {
		atomic_inc(&dmc->num_master_io);
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	if (go_wait) {
		wait_event(dmc->wait_for_cache_disable_queue,
				 (CACHE_DISABLED == dmc->cache_state));

		atomic_inc(&dmc->num_master_io);
	}

	if (CACHE_DISABLED == dmc->cache_state) {
		bio_assign_dev(master_bio, get_disk_bdev(dmc));
		/*
		 * Processes in cache disable queue is waiting for cache to be disable,
		 * So when cache in disable mode, there is no process in queue
		 */
		atomic_dec(&dmc->num_master_io);
		return DM_MAPIO_REMAPPED;
	}

	if (bio_barrier(master_bio)) {
		if ((0 == bio_bi_sector(master_bio))
				&& (0 == bio_bi_size(master_bio))
				&& (bio_has_flush_flags(master_bio))) {
			// REQ_FLUSH bio
			sdbg(DF_FLUSH_BIO, "flush bio: %s", bio_to_str(master_bio, bio_str, sizeof(bio_str)));
			if (FLASHCACHE_WRITE_BACK == dmc->cache_mode) {
#ifdef MY_ABC_HERE
				syno_mu_start_ext_flush(dmc, master_bio, start_jiffy);
#else
				send_flush_bios(dmc, master_bio, start_jiffy);
#endif
				goto end;
			} else {
				bio_assign_dev(master_bio, get_disk_bdev(dmc));
				atomic_dec(&dmc->num_master_io);
				return DM_MAPIO_REMAPPED;
			}
		} else {
			if (!print) {
				serr("Get unsupported flush bio: %s", bio_to_str(master_bio, bio_str,
							sizeof(bio_str)));
				print = 1;
			}
			handle_master_io_finish(dmc);
			return -EOPNOTSUPP;
		}
	} else {
		wait_event(dmc->wait_flush_queue, !atomic_read(&dmc->in_flush));
	}

#ifdef MY_ABC_HERE
	/* Force external IO to help send one md io when throttle triggered */
	syno_mu_dmcg_throtl(dmc);
#endif

#ifdef MY_DEF_HERE
	cache_bio = cache_bio_alloc_init(master_bio, start_jiffy);
	if (NULL == cache_bio) {
		serr("Cache bio can't be allocated");
		// Refer to read_hit allocate memory failed
		bio_endio_wrapper(master_bio, -EIO);
		handle_master_io_finish(dmc);
		err = 1;
		goto end;
	}

	if (NULL == (ppbios = cache_bio_get_bios(cache_bio, dmc))) {
		serr("Can't get subbios");
		bio_endio_wrapper(master_bio, -EIO);
		handle_master_io_finish(dmc);
		err = 1;
		goto end;
	}

	/*
	 * SYNO: cache_bio will be be freed by callback function
	 * in the following loop. We should keep num_total_bios
	 * in a local variable to avoid accessing freed memory
	 */
	num_total_bios = cache_bio->num_total_bios;

	while (i < num_total_bios) {
		bio = ppbios[i++];
		process_single_bio(dmc, master_bio, bio);
	}

#else /* NOT MY_DEF_HERE */

	if (bio_sectors(master_bio) > MAX_BIO_SIZE) {
		serr("Error: bio sectors %d > %d", bio_sectors(master_bio),
				MAX_BIO_SIZE);
		err = 1;
		goto end;
	}

	pcache_bi_private = cache_bi_private_alloc_init(master_bio, start_jiffy);
	if (!pcache_bi_private) {
		serr("Failed to allocate cache bio info");
		err = 1;
		goto end;
	}

	bio = master_bio;
	bio->bi_private = pcache_bi_private;
	process_single_bio(dmc, master_bio, bio);

#endif /* MY_DEF_HERE */

end:
#ifdef MY_DEF_HERE
	if (ppbios) {
		kfree(ppbios);
	}
#endif

	if (err) {
#ifndef MY_DEF_HERE
		if (pcache_bi_private) {
			mempool_free(pcache_bi_private, _cache_bi_private_pool);
		}
#endif
	}

	sdbg(DF_DETAIL, "return, master_bio: %s", bio_str);

	return DM_MAPIO_SUBMITTED;
}

#ifdef MY_ABC_HERE
#else
// for flashcache_kcopyd_callback_sync to invoke
static void flashcache_dirty_writeback_sync(struct cache_c *dmc, int index);

/* Block sync support functions */
static void 
flashcache_kcopyd_callback_sync(int read_err, unsigned int write_err, void *context)
{
	struct kcached_job *job = (struct kcached_job *)context;
	struct cache_c *dmc = job->dmc;
	int index = job->index;
	unsigned long flags;
#ifdef MY_ABC_HERE
	struct cacheblock *cacheblk = &dmc->cache[index];
	struct cache_set *cacheset = &dmc->cache_sets[index / dmc->assoc];
#endif

	VERIFY_WARN(!in_interrupt());
	DPRINTK("kcopyd_callback_sync: Index %d", index);
	VERIFY_WARN(job->bio == NULL);
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	VERIFY_WARN(dmc->cache[index].cache_state & (DISKWRITEINPROG | VALID | DIRTY));
	if (likely(read_err == 0 && write_err == 0)) {
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
#ifdef MY_ABC_HERE
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		cacheblk_unset_lowest_part_of_flushing_dirty_range(cacheblk);

		if (cacheblk_is_flushing_dirty_range_unset(cacheblk)) {
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
			flashcache_md_write(job);
		} else {
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
			flashcache_free_cache_job(job);

			/*
			 * Don't decrease nr_job due to we don't add it twice in partial access
			 * Trigger again
			 */
			flashcache_dirty_writeback_sync(dmc, index);
		}
#else
		flashcache_md_write(job);
#endif
	} else {
		if (read_err)
			read_err = -EIO;
		if (write_err)
			write_err = -EIO;
		/* Disk write failed. We can not purge this cache from flash */
#ifdef MY_ABC_HERE
		if (0 < dmc->limits.writeback_sync_err) {
#endif
		DMERR("flashcache: Disk writeback failed ! read error %d write error %d block %llu",
		      -read_err, -write_err, (u64)job->job_io_regions.disk.sector);
#ifdef MY_ABC_HERE
			dmc->limits.writeback_sync_err--;
		}
#endif
		dec_clean_inprog(dmc, cacheset, 1);

		cacheblk->flushing_dirty_bitmap = 0;
		// io_range whould be unset in do_pending_xxx

		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		/* Set the error in the job and let do_pending() handle the error */
		if (read_err) {
			atomic_inc(&dmc->flashcache_errors.ssd_read_errors);
			job->error = read_err;
		} else {
			atomic_inc(&dmc->flashcache_errors.disk_write_errors);
			job->error = write_err;
		}
		flashcache_do_pending(job);
		flashcache_sync_blocks(dmc);  /* Kick off more cleanings */
		atomic64_inc(&dmc->flashcache_stats.cleanings);
	}
}

static void
flashcache_dirty_writeback_sync(struct cache_c *dmc, int index)
{
	struct kcached_job *job;
	unsigned long flags;
	struct cacheblock *cacheblk = &dmc->cache[index];
	struct cache_set *cacheset = &dmc->cache_sets[index / dmc->assoc];
	int device_removal = 0;
	int first_time_flush = 0;
	sector_t offset = 0;
	sector_t size = 0;
	int wakeup_sync_wqh = 0;

	VERIFY_WARN((cacheblk->cache_state & FALLOW_DOCLEAN) == 0);
	DPRINTK("flashcache_dirty_writeback_sync: Index %d", index);
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	VERIFY_WARN((cacheblk->cache_state & BLOCK_IO_INPROG) == DISKWRITEINPROG);
	VERIFY_WARN(cacheblk->cache_state & DIRTY);

	// Debug for call trace
	if (0 == cacheblk->data_bitmap) {
		serr("cb index=%d data_bitmap=0", index);
		VERIFY_WARN(0);
	}

	if (0 == cacheblk->flushing_dirty_bitmap) {
		cacheblk->flushing_dirty_bitmap = cacheblk->dirty_bitmap;
		first_time_flush = 1;
	}

	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	cacheblk_get_lowest_part_of_flushing_dirty_range(cacheblk, &offset, &size);
	job = new_kcached_job_no_bio(dmc, index, offset, size);
	VERIFY_WARN(NULL != job);
	/*
	 * If the device is being (fast) removed, do not kick off any more cleanings.
	 */
	if (unlikely(atomic_read(&dmc->remove_in_prog) == FAST_REMOVE)) {
		DMERR("flashcache: Dirty Writeback (for set cleaning) aborted for device removal, block %llu", (u64)cacheblk->dbn);
		if (job)
			flashcache_free_cache_job(job);
		job = NULL;
		device_removal = 1;
	}
#ifdef MY_ABC_HERE
	if (unlikely(dmc->sysctl_error_inject & WRITEBACK_JOB_ALLOC_ERROR)) {
		dmc->sysctl_error_inject &= ~(WRITEBACK_JOB_ALLOC_ERROR);
		if (job) {
			flashcache_free_cache_job(job);
		}
		job = NULL;
	}
#endif
	if (unlikely(job == NULL)) {
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		dec_clean_inprog(dmc, cacheset, 1);
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
		cacheblk->flushing_dirty_bitmap = 0;
		unset_partial_cb_writeback_attrs(dmc, cacheblk, 1);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		// cancel in the middle of partial flush
		if (atomic_dec_and_test(&dmc->nr_jobs)) {
			wakeup_sync_wqh = 1;
		}

		if (device_removal == 0)
			DMERR("flashcache: Dirty Writeback (for sync) failed ! Can't allocate memory, block %llu",
				(u64)cacheblk->dbn);

		if (wakeup_sync_wqh) {
			wake_up(&dmc->sync_wqh);
		}
	} else {
		job->bio = NULL;
		job->action = WRITEDISK_SYNC;

		atomic64_inc(&dmc->flashcache_stats.ssd_reads);
		atomic64_inc(&dmc->flashcache_stats.disk_writes);
#ifdef MY_ABC_HERE
		atomic64_add(size, &dmc->flashcache_stats.dirty_writeback_sync_sector);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
		kcopyd_copy(flashcache_kcp_client, &job->job_io_regions.cache, 1, &job->job_io_regions.disk, 0, 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
			    flashcache_kcopyd_callback_sync,
#else
			    (kcopyd_notify_fn) flashcache_kcopyd_callback_sync, 
#endif
			    job);
#else
		dm_kcopyd_copy(flashcache_kcp_client, &job->job_io_regions.cache, 1, &job->job_io_regions.disk, 0, 
			       (dm_kcopyd_notify_fn)flashcache_kcopyd_callback_sync, 
			       (void *)job);
#endif
	}
}

/* 
 * Sync all dirty blocks. We pick off dirty blocks, sort them, merge them with 
 * any contigous blocks we can within the set and fire off the writes.
 *
 * NOT atomic context
 */
void
flashcache_sync_blocks(struct cache_c *dmc)
{
	unsigned long flags;
	int index;
	struct dbn_index_pair *writes_list;
	int nr_writes;
	int i, set;
	struct cacheblock *cacheblk;

	/* 
	 * If a (fast) removal of this device is in progress, don't kick off 
	 * any more cleanings. This isn't sufficient though. We still need to
	 * stop cleanings inside flashcache_dirty_writeback_sync() because we could
	 * have started a device remove after tested this here.
	 */
	if ((atomic_read(&dmc->remove_in_prog) == FAST_REMOVE) || dmc->sysctl_stop_sync)
		return;

	writes_list = kmalloc(dmc->assoc * sizeof(struct dbn_index_pair), GFP_NOIO);
	if (writes_list == NULL) {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
#ifdef MY_ABC_HERE
		if (printk_ratelimit()) {
			serr("Failed to allocate atomic memory for syncing blocks");
		}
#endif
		return;
	}
	nr_writes = 0;
	set = -1;
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	index = dmc->sync_index;
	while (index < dmc->size && dmc->clean_inprog < dmc->max_clean_ios_total) {
		VERIFY_WARN(nr_writes <= dmc->assoc);
		if (((index % dmc->assoc) == 0) && (nr_writes > 0)) {
			/*
			 * Crossing a set, sort/merge all the IOs collected so
			 * far and issue the writes.
			 */
			VERIFY_WARN(set != -1);
			flashcache_merge_writes(dmc, writes_list, &nr_writes, set);
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

			// SYNO: Set all io_range for drity_writeback_sync in below and merge_write
			for (i = 0 ; i < nr_writes ; i++)
				flashcache_dirty_writeback_sync(dmc, writes_list[i].index);
			nr_writes = 0;
			set = -1;
			spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		}
		cacheblk = &dmc->cache[index];
		// SYNO: If BLOCK_IO_INPROG, it won't be flushed
		if ((cacheblk->cache_state & (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
			set_wb_attrs(dmc, index, 1);
			set = index / dmc->assoc;
			writes_list[nr_writes].dbn = cacheblk->dbn;
			writes_list[nr_writes].index = index;
			nr_writes++;
		}
		index++;
	}

	dmc->sync_index = index;
	if (nr_writes > 0) {
		VERIFY_WARN(set != -1);
		flashcache_merge_writes(dmc, writes_list, &nr_writes, set);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		for (i = 0 ; i < nr_writes ; i++)
			flashcache_dirty_writeback_sync(dmc, writes_list[i].index);
	} else
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	kfree(writes_list);
}
#endif /* ! MY_ABC_HERE */

#ifdef MY_ABC_HERE
/*
 * ssd_read_ios_limit is controlled by userspace.
 * batch size should not be touched except for debug/testing purpose
 */
static inline void update_wb_param_on_sync(plug_wb_t *plug_wb)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&plug_wb->lock, flags);
	plug_wb->disk_write_ios_limit = PLUG_WB_SYNC_DISK_WRITE;
	spin_unlock_irqrestore(&plug_wb->lock, flags);
}
#endif

void
flashcache_sync_all(struct cache_c *dmc)
{
	if (dmc->cache_mode != FLASHCACHE_WRITE_BACK)
		return;
	dmc->sysctl_stop_sync = 0;
	spin_lock_irq(&dmc->cache_spin_lock);
#ifdef MY_ABC_HERE
	if (!dmc->start_sync_all) {
		dmc->start_sync_all = 1;
		dmc->init_nr_dirty = dmc->nr_dirty;
	}
#endif

#ifdef MY_ABC_HERE
	if (0 == dmc->nr_dirty) {
		spin_unlock_irq(&dmc->cache_spin_lock);
		return;
	}
	spin_unlock_irq(&dmc->cache_spin_lock);

	mutex_lock(&dmc->oqf.mtx);
	if (dmc->oqf.sync_state == SYNC_STATE_RUNNING) {
		mutex_unlock(&dmc->oqf.mtx);
		return;
	}
	atomic_inc(&dmc->dmcg->sqf_inprog);
	dmc->oqf.sync_state = SYNC_STATE_RUNNING;

	/* Once the trigger cnt reach zero there won't be new writebacks, even
	 * if workqueue and increase trigger cnt it won't send IO */
	while (dmc->oqf.trigger_cnt) {
		mutex_unlock(&dmc->oqf.mtx);
		wait_event(dmc->oqf.wqh, dmc->oqf.trigger_cnt == 0);
		mutex_lock(&dmc->oqf.mtx);
	}

	VERIFY_WARN(dmc->oqf.sync_trigger_cnt == 0);

	update_wb_param_on_sync(&dmc->plug_wb);
	dmc->oqf.bit_idx = 0;

	mutex_unlock(&dmc->oqf.mtx);

	down_write(&dmc->oqf.bitmap_rwsem);
	quickflush_construct_bitmap(dmc);
	up_write(&dmc->oqf.bitmap_rwsem);

	mutex_lock(&dmc->oqf.mtx);
	sdbg(DF_QUICKFLUSH, "Quick Flush");

	/* WARNING: Finish all preperation before increasing this,
	 * or syno_start_sqf can be called from plugged wb wq */
	dmc->oqf.sync_trigger_cnt++;
	mutex_unlock(&dmc->oqf.mtx);
	syno_start_sqf(dmc);
	/* TODO: Refine and add Unit tests */
#else
	dmc->sync_index = 0;
	spin_unlock_irq(&dmc->cache_spin_lock);	
	flashcache_sync_blocks(dmc);
#endif /* MY_ABC_HERE */
}

/*
 * We handle uncached IOs ourselves to deal with the problem of out of ordered
 * IOs corrupting the cache. Consider the case where we get 2 concurent IOs
 * for the same block Write-Read (or a Write-Write). Consider the case where
 * the first Write is uncacheable and the second IO is cacheable. If the 
 * 2 IOs are out-of-ordered below flashcache, then we will cache inconsistent
 * data in flashcache (persistently).
 * 
 * We do invalidations before launching uncacheable IOs to disk. But in case
 * of out of ordering the invalidations before launching the IOs does not help.
 * We need to invalidate after the IO completes.
 * 
 * Doing invalidations after the completion of an uncacheable IO will cause 
 * any overlapping dirty blocks in the cache to be written out and the IO 
 * relaunched. If the overlapping blocks are busy, the IO is relaunched to 
 * disk also (post invalidation). In these 2 cases, we will end up sending
 * 2 disk IOs for the block. But this is a rare case.
 * 
 * When 2 IOs for the same block are sent down (by un co-operating processes)
 * the storage stack is allowed to re-order the IOs at will. So the applications
 * cannot expect any ordering at all.
 * 
 * What we try to avoid here is inconsistencies between disk and the ssd cache.
 */
void 
flashcache_uncached_io_complete(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	unsigned long flags;
	int queued;
	int error = job->error;
	int is_write = (bio_data_dir(job->bio) == WRITE);

	flashcache_check_record_io_latency(LAT_UNCACHED_IO_COMPLETE_WQ, &job->io_lat);

	if (unlikely(error)) {
		if (printk_ratelimit()) {
			DMERR("flashcache uncached disk IO error: io error %d block %llu R/w %s",
				  error, (u64)job->job_io_regions.disk.sector,
				  (bio_data_dir(job->bio) == WRITE) ? "WRITE" : "READ");
		}
		if (bio_data_dir(job->bio) == WRITE)
			atomic_inc(&dmc->flashcache_errors.disk_write_errors);
		else
			atomic_inc(&dmc->flashcache_errors.disk_read_errors);
	}

	job_dbg(__func__, job);

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
#ifdef CONFIG_SYNO_DATA_CORRECTION
	if (job->bio->bi_flags & ((1 << BIO_CORRECTION_RETRY) | (1 << BIO_CORRECTION_ABORT))) {
		//If this is a correction bio, we should not invalidate it.
		sdbg(DF_CORRECTION, "Get correcting bio_sector=%llu nerver invalidate", (u64)bio_bi_sector(job->bio));
		queued = 0;
	} else {
		queued = flashcache_inval_blocks(dmc, job->bio, &job->io_lat, TYPE_DONT_CARE);
	}
#else
	queued = flashcache_inval_blocks(dmc, job->bio, &job->io_lat, TYPE_DONT_CARE);
#endif
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	if (queued) {
		if (unlikely(queued < 0))
			flashcache_bio_endio(job->bio, -EIO, dmc, ktime_zero, &job->io_lat);
		/* 
		 * The IO will be re-executed.
		 * The do_pending logic will re-launch the 
		 * disk IO post-invalidation calling start_uncached_io.
		 * This should be a rare occurrence.
		 */
		atomic64_inc(&dmc->flashcache_stats.uncached_io_requeue);
	} else {
		flashcache_bio_endio(job->bio, error, dmc, job->io_start_time, &job->io_lat);
	}

	if (is_write) {
		atomic_dec(&dmc->num_uncached_write);
		wake_up(&dmc->wait_io_done_queue);
	}

	flashcache_free_cache_job(job);
	if (atomic_dec_and_test(&dmc->nr_jobs))
		wake_up(&dmc->sync_wqh);
}

static void 
flashcache_uncached_io_callback(unsigned long error, void *context)
{
	struct kcached_job *job = (struct kcached_job *) context;
	struct bio *bio = job->bio;
	VERIFY_WARN(bio != NULL);

	flashcache_check_record_io_latency(
		bio_data_dir(job->bio) == WRITE ? LAT_UNCACHED_WRITE : LAT_UNCACHED_READ,
		&job->io_lat);

#ifdef CONFIG_SYNO_DATA_CORRECTION

	if (error & (1 << BIO_CORRECTION_ERR)) {
		bio->bi_flags |= 1 << BIO_CORRECTION_ERR;
		error &= ~(1 << BIO_CORRECTION_ERR);
		sdbg(DF_CORRECTION, "remove BIO_CORRECTION_ERR from non-cache bio, error=%lu, bio_sector=%llu", error, (u64)bio_bi_sector(bio));
	}
#endif
	VERIFY_WARN(job->index == -1);
	if (unlikely(error))
		job->error = -EIO;
	else
		job->error = 0;
	push_uncached_io_complete(job);
	cache_schedule_uncached_io_complete();
}

static inline int get_comp_dm_op_flag(struct cache_c *dmc, struct bio *bio, int is_write)
{
	if (unlikely(bio_has_fua_flags(bio))) {
		if (is_write) {
			return COMP_DM_WRITE_FUA;
		} else {
			return COMP_DM_READ;
		}
	} else {
		return (is_write ? COMP_DM_WRITE : COMP_DM_READ);
	}
}

/*
 * plat and io_type are mutual exclusive. Given plat, we use plat->io_type.
 */
static void
flashcache_start_uncached_io(struct cache_c *dmc, struct bio *bio,
	struct io_latency *plat, enum cache_io_type io_type)
{
	int is_write = (bio_data_dir(bio) == WRITE);
	int comp_dm_op = 0;
	struct kcached_job *job;
	unsigned long bi_flags = 0;

#ifdef CONFIG_SYNO_DATA_CORRECTION
	bi_flags = bio->bi_flags & ((1 << BIO_CORRECTION_ABORT) | (1 << BIO_CORRECTION_RETRY));
#endif

	if (is_write) {
		atomic64_inc(&dmc->flashcache_stats.uncached_writes);
		atomic64_inc(&dmc->flashcache_stats.disk_writes);
	} else {
		atomic64_inc(&dmc->flashcache_stats.uncached_reads);
		atomic64_inc(&dmc->flashcache_stats.disk_reads);
	}
	job = new_kcached_job(dmc, bio, -1, plat, io_type);

	if (global_tester == TEST_OQF_INVALIDATE_DELAY) {
		flashcache_free_cache_job(job);
		job = NULL;
	}

	if (unlikely(job == NULL)) {
		flashcache_bio_endio(bio, -EIO, dmc, ktime_zero, plat);
		return;
	}
	atomic_inc(&dmc->nr_jobs);
	job_dbg(__func__, job);
	if (is_write) {
		atomic_inc(&dmc->num_uncached_write);
	}

	comp_dm_op = get_comp_dm_op_flag(dmc, bio, is_write);

	dm_io_async_bio_wrapper(1, &job->job_io_regions.disk, comp_dm_op,
		bio, flashcache_uncached_io_callback, job, bi_flags);
}

#ifdef CONFIG_SYNO_DATA_CORRECTION
// Return 0 on success. Otherwise return -1
int correction_handle_retry_io_start(struct cache_c *dmc, u64 bio_sector)
{
	int ret = -1;
	struct correction_entry *entry = NULL;
	unsigned long irq_flags = 0;
	int exist = 0;

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);
	if (entry) {
		exist = 1;
	} else {
		entry = correcting_entry_alloc(bio_sector);
		if (!entry) {
			sdbg(DF_CORRECTION, "Failed to allocate new entry\n");
			goto err;
		}
		list_add_tail(&entry->link, &dmc->correction_list->link);
	}

	entry->correcting_bitmap |= 1 << ((bio_sector - entry->start_sector) / ALIGNED_SECTORS);
	sdbg(DF_CORRECTION, "Entry exist=%d correcting bitmap is updated to 0x%x",
			exist, entry->correcting_bitmap);

	ret = 0;
err:
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);

	return ret;
}

int correction_find_cache_index(struct cache_c *dmc, sector_t start_sector)
{
	unsigned long set_number = 0;
	int start_index = 0;
	int end_index = 0;
	int i =0;
	int ret = -1;

	set_number = hash_block(dmc, start_sector);
	start_index = dmc->assoc * set_number;
	end_index = start_index + dmc->assoc;

	for (i = start_index ; i < end_index ; i++) {
		if ((start_sector >= dmc->cache[i].dbn) &&
			(start_sector < (dmc->cache[i].dbn + dmc->block_size))) {
			ret = i;
		}
	}

	return ret;
}

/* Return 0 no abort bio work queued, 1 abort bio work queued */
static int inline queue_force_abort_bio_work(struct cache_c *dmc, struct bio *bio,
	int error, ktime_t start_time, struct io_latency *plat,
	struct correction_entry *entry)
{
	int ret = 0;
	struct send_force_abort_bio_task *task = NULL;

	task = kzalloc(sizeof(*task), GFP_ATOMIC);
	if (unlikely(NULL == task)) {
		serr("Failed to allocate send force abort bio, entry_sector: "
			"%llu need_abort_bitmap: %u cache_addr: %llu",
			(u64)entry->start_sector, entry->need_abort_bitmap, (u64)entry->cache_addr);
	} else {
		task->dmc = dmc;
		task->entry_start_sector = entry->start_sector;
		task->need_abort_bitmap = entry->need_abort_bitmap;
		task->cache_addr = entry->cache_addr;
		task->bio = bio;
		task->error = error;
		task->start_time = start_time;
		task->io_lat = *plat;
		sdbg(DF_CORRECTION, "need abort bitmap: %d", task->need_abort_bitmap);

		INIT_WORK(&task->work, correction_send_force_abort_bios);
		cache_schedule_work(&task->work);
		ret = 1;
	}

	return ret;
}

/* Return 0 no need to wait for abort bio, 1 need to wait for abort bio */
int correction_handle_abort_io_finish(struct cache_c *dmc,
	struct bio *bio, int error, ktime_t start_time, struct io_latency *plat)
{
	int ret = 0;
	struct correction_entry *entry = NULL;
	unsigned long irq_flags = 0;
	int index = 0;
	sector_t bio_sector = bio_bi_sector(bio);

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);
	if (NULL == entry) {
		serr("BIO (bio_sector:%llu) has been aborted", (u64)bio_sector);
		goto end;
	}

	VERIFY_WARN(0 != entry->correcting_bitmap);

	sdbg(DF_CORRECTION, "Current correcting bitmap=0x%x", entry->correcting_bitmap);

	index = (bio_sector - entry->start_sector) / ALIGNED_SECTORS;
	entry->correcting_bitmap &= ~(1 << index);
	entry->correcting_types[index] = TYPE_UNINITED;

	if (0 == entry->correcting_bitmap) {
		list_del(&entry->link);
		if (unlikely(entry->need_abort_bitmap)) {
			ret = queue_force_abort_bio_work(dmc, bio, error, start_time, plat, entry);
		}

		sdbg(DF_CORRECTION, "Free entry (start sector=%llu)", entry->start_sector);
		kfree(entry);
		entry = NULL;
	}

end:
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);

	return ret;
}

struct correction_entry *correcting_entry_alloc(u64 bio_sector)
{
	struct correction_entry *new_entry = NULL;

	new_entry = kzalloc(sizeof(struct correction_entry), GFP_ATOMIC);
	if (!new_entry) {
		serr("Fail to alloc new correction entry");
		goto err;
	}
	new_entry->start_sector = bio_sector / MAX_BIO_SIZE * MAX_BIO_SIZE;
err:
	return new_entry;
}

struct correction_entry *correction_list_find_entry(struct cache_c *dmc, u64 bio_sector, lock_type_t lock_type)
{
	struct correction_entry *entry = NULL;
	struct correction_entry *ret_entry = NULL;
	struct correction_entry *list = NULL;
	unsigned long irq_flags = 0;

	if (DO_LOCK == lock_type) {
		spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);
	}

	list = dmc->correction_list;
	list_for_each_entry(entry, &list->link, link){
		if ((bio_sector < (entry->start_sector + MAX_BIO_SIZE))
				&& (bio_sector >= entry->start_sector)) {
			ret_entry = entry;
			goto end;
		}
	}
end:
	if (DO_LOCK == lock_type) {
		spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);
	}

	return ret_entry;
}

int correction_entry_in_correcting(struct cache_c *dmc, u64 bio_sector, lock_type_t lock_type)
{
	int ret = 0;

	if (correction_list_find_entry(dmc, bio_sector, lock_type)) {
		ret = 1;
	}

	return ret;
}

// Get a correctio type of a 4KB range
correcting_type_t correction_range_get_type(struct cache_c *dmc, u64 bio_sector)
{
	correcting_type_t ret = NO_CORRECTING;
	struct correction_entry *entry = NULL;
	unsigned long irq_flags = 0;
	int index = 0;

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);

	if (NULL == entry) {
		goto err;
	}

	index = (bio_sector - entry->start_sector) / ALIGNED_SECTORS;
	ret = entry->correcting_types[index];
err:
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);
	return ret;
}

int correction_range_set_type(struct cache_c *dmc, u64 bio_sector, correcting_type_t type)
{
	int ret = -1;
	struct correction_entry *entry = NULL;
	unsigned long irq_flags = 0;
	int index = 0;

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);

	if (NULL == entry) {
		serr("Can't found entry");
		goto err;
	}

	index = (bio_sector - entry->start_sector) / ALIGNED_SECTORS;
	if (TYPE_UNINITED == entry->correcting_types[index]) {
		entry->correcting_types[index] = type;
		sdbg(DF_CORRECTION, "Update entry (start sector=%llu) correcing type [%d] to %d",
				entry->start_sector, index, type);
	} else {
		sdbg(DF_CORRECTION, "Warning : type has been set!!!");
	}
	ret = 0;
err:
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);
	return ret;
}

void correction_entry_set_abort_bitmap(struct cache_c *dmc, u64 bio_sector)
{
	struct correction_entry *entry = NULL;
	int i = 0;
	unsigned long irq_flags = 0;

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	sdbg(DF_CORRECTION, "Find entry for bio start=%llu", bio_sector);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);
	if (NULL != entry) {
		for (i = 0; i < NUM_SUB_BLOCK; i++) {
			if (SSD_CORRECTING == entry->correcting_types[i]) {
				entry->need_abort_bitmap |= (1 << i);
			}
		}
		sdbg(DF_CORRECTION, "Update entry (start=%llu) need_abort_bitmap to 0x%x",
				entry->start_sector, entry->need_abort_bitmap);
	}
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);
}

void flashcache_correction_abort_callback(unsigned long error, void *context)
{
	struct bio *bio = context;
	struct send_force_abort_bio_task *task = bio->bi_private;

	if (error) {
		serr ("force abort bio fail error :%lx", error);
	}

	sdbg(DF_CORRECTION, "force aborted finish:bi_sector:%llu", (u64)bio_bi_sector(bio));
	__free_page(bio->bi_io_vec[0].bv_page);
	bio_put(bio);

	if (atomic_dec_and_test(&task->bio_count)) {
		sdbg(DF_CORRECTION, "bio count reach zero end io and release task");
		flashcache_bio_endio_core(task->bio, task->error,
			task->dmc, task->start_time, &task->io_lat);
		kfree(task);
	}
}

struct bio* correction_alloc_abort_bio(struct cache_c *dmc, sector_t sector)
{
	struct bio *bio = NULL;
	struct page *page = NULL;

	bio  = bio_alloc_bioset(GFP_NOIO, 1, pcache_bio_set);
	if (!bio) {
		serr("alloc_aborbio:blo_alloc_bioset failed");
		goto exit;
	}

	*bio_bi_sector_ptr(bio) = sector;
	*bio_bi_size_ptr(bio) = CORRECTION_BIO_BYTES;
	*bio_bi_idx_ptr(bio) = 0;
	bio->bi_vcnt = 1;

	page = alloc_page(GFP_ATOMIC);
	if (unlikely(!page)) {
		serr("alloc_aborbio:alloc_page failed");
		goto free_bio;
	}

	bio->bi_io_vec[0].bv_page = page;
	bio->bi_io_vec[0].bv_len = CORRECTION_BIO_BYTES;
	bio->bi_io_vec[0].bv_offset = 0;

	bio->bi_flags |= (1 << BIO_CORRECTION_ABORT);

	goto exit;

free_bio:
	bio_put(bio);
exit:
	return bio;
}

void correction_send_force_abort_bios(struct work_struct *work)
{
	struct send_force_abort_bio_task *task = container_of(work, struct send_force_abort_bio_task, work);
	int i = 0;
	int bio_count = 0;
	struct bio *bio = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
        struct dm_io_region where = {0};
#else
        struct io_region where = {0};
#endif
	where.bdev = get_cache_bdev(task->dmc);

	if (check_new_error_inject(task->dmc, FORCE_ABORT_BIO_WQ_DELAY)) {
		sprint("Simulate force abort bio workqueue delay, sleep 60 sec");
		task->dmc->sysctl_new_error_inject = 0;
		msleep(60000);
	}

	/* Make there multiple entries in need abort bitmap so we can test the
	 * case in which some abort bio allocate successfully and some failed.
	 * Always try to set the last bit, if it's already set, print and ignore */
	if (check_new_error_inject(task->dmc, FORCE_ABORT_BIO_ALLOC_FAIL)) {
		sprint("Simulate multiple bit set in abort bitmap");
		if (task->need_abort_bitmap & 1) {
			sprint("LSB already set in force abort bitmap");
		} else {
			task->need_abort_bitmap |= 1;
		}
	}

	for (i = 0; i < NUM_SUB_BLOCK; ++i) {
		if (task->need_abort_bitmap & (1 << i)) {
			bio_count++;
		}
	}
	atomic_set(&task->bio_count, bio_count);

	for (i = 0; i < NUM_SUB_BLOCK; i++) {
		if (task->need_abort_bitmap & (1 << i)) {
			bio = correction_alloc_abort_bio(task->dmc, task->cache_addr + (i * ALIGNED_SECTORS));
			if (check_new_error_inject(task->dmc, FORCE_ABORT_BIO_ALLOC_FAIL)) {
				sprint("Simulate force abort bio alloc fail");
				task->dmc->sysctl_new_error_inject = 0;
				if (bio) {
					bio_put(bio);
				}
				bio = NULL;
			}
			if (bio) {
				where.sector = bio_bi_sector(bio);
				where.count = to_sector(bio_bi_size(bio));
				bio->bi_private = task;

				sdbg(DF_CORRECTION, "Send abort bio start sector=%llu size=%llu",
						(u64)bio_bi_sector(bio), (u64)to_sector(bio_bi_size(bio)));

				dm_io_async_bio_wrapper(1, &where, COMP_DM_READ, bio,
					flashcache_correction_abort_callback, bio,
					bio->bi_flags & (1 << BIO_CORRECTION_ABORT));
			} else {
				serr("Failed to allocate force abort bio for %llu",
					(u64)(task->cache_addr + (i * ALIGNED_SECTORS)));
				if (atomic_dec_and_test(&task->bio_count)) {
					sdbg(DF_CORRECTION, "bio count reach zero end io and release task");
					flashcache_bio_endio_core(task->bio, task->error,
						task->dmc, task->start_time, &task->io_lat);
					kfree(task); // must be last bio
					goto end;
				}
			}
			bio = NULL;
		}
	}
end:
	return;
}

void correction_entry_record_cache_addr(struct cache_c *dmc, int index, sector_t bio_sector)
{
	struct correction_entry *entry = NULL;
	unsigned long irq_flags = 0;

	spin_lock_irqsave(&dmc->corretion_spin_lock, irq_flags);

	entry = correction_list_find_entry(dmc, bio_sector, NO_LOCK);

	if (entry && 0 == entry->cache_addr) {
		entry->cache_addr = INDEX_TO_CACHE_ADDR(dmc, index);
		sdbg(DF_CORRECTION, "record cache_addr: bio_sector=%llu, cache_addr:%llu ", (u64)bio_sector, (u64)entry->cache_addr);
	}
	spin_unlock_irqrestore(&dmc->corretion_spin_lock, irq_flags);
}
#endif /* CONFIG_SYNO_DATA_CORRECTION */
