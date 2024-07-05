#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/blk_types.h>
#include <linux/wait.h>

#include "flashcache.h"
#include "syno_quickflush.h"
#ifdef MY_ABC_HERE
#include "syno_md_update.h"
#endif

extern struct dm_kcopyd_client *flashcache_kcp_client;
extern unsigned long hash_block(struct cache_c *dmc, sector_t dbn);
extern void flashcache_sync_blocks(struct cache_c *dmc);
extern struct mutex nqf_mem_mtx;

extern struct delayed_work nqf_mgmt_work;
extern struct list_head dmc_group_list;
extern struct mutex dmcg_mtx;

// REMOVE THE FILE FOR GPL RELEASE
#ifdef MY_ABC_HERE

/* Must in plug_wb lock */
static inline int
ios_can_issue_plug_wb(struct cache_c* dmc, int nr_write)
{
	return (nr_write + dmc->plug_wb.ssd_read_inprog) <= dmc->plug_wb.ssd_read_ios_limit;
}

static void inline reset_plug_wb_param_to_nqf(struct cache_c *dmc)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&dmc->plug_wb.lock, flags);
	/* SSD read is used for throttling by userspace, don't touch it */
	dmc->plug_wb.disk_write_ios_limit = PLUG_WB_DEFAULT_DISK_WRITE;
	spin_unlock_irqrestore(&dmc->plug_wb.lock, flags);
}

/* MUST use INSIDE oqf mtx */
void
flashcache_clean_sync_state(struct cache_c *dmc)
{
	VERIFY_WARN(0 == dmc->oqf.sync_trigger_cnt);
	VERIFY_WARN(dmc->oqf.sync_state == SYNC_STATE_RUNNING);

	/* only change the value after no io left in queue */
	dmc->oqf.bit_idx = 0;
	reset_plug_wb_param_to_nqf(dmc);
	dmc->oqf.sync_state = SYNC_STATE_STOPPED;
	atomic_dec(&dmc->dmcg->sqf_inprog);
	wake_up(&dmc->sync_wqh);
}

/* use INSIDE cache_spin_lock */
int
quickflush_find_cacheblk_idx(struct cache_c *dmc, unsigned long long disk_blk_idx)
{
	int i = 0;
	int ret = -1;
	sector_t sector = QF_BLK_IDX_TO_SEC(disk_blk_idx);
	unsigned long set_number = hash_block(dmc, sector);
	int start_index = dmc->assoc * set_number;

	for (i = start_index; i < start_index + dmc->assoc; ++i) {
		if (dmc->cache[i].dbn == sector && (dmc->cache[i].cache_state & VALID)) {
			ret = i;
			break;
		}
	}

	return ret;
}

/* in bitmap_rwsem w lock */
void
quickflush_construct_bitmap(struct cache_c *dmc)
{
	int i = 0;

	/* Bitmap serves as a hint, not need to lock for 100% correctnes */
	for (i = 0; i < dmc->size; ++i) {
		if ((dmc->cache[i].cache_state & DIRTY)) {
			quickflush_set_volume_map(dmc, &dmc->cache[i]);
		}
	}
}

/* MUST be in cache_spin_lock */
int
quickflush_cacheblk_get_nr_writeback(struct cache_c *dmc, int index)
{
	int nr_io = 1;
	struct cacheblock *cacheblk = &dmc->cache[index];
	bitmap_t tmp_data = cacheblk->data_bitmap;
	bitmap_t tmp_dirty = cacheblk->dirty_bitmap;
	int can_merge = 0;

	while (tmp_dirty) {
		if (tmp_dirty & 1) {
			can_merge = 1;
		}
		if (!(tmp_data & 1)) {
			if (can_merge) {
				can_merge = 0;
				nr_io++;
			}
		}
		tmp_dirty >>= 1;
		tmp_data >>= 1;
	}

	return nr_io;
}

static void
quickflush_wb_done(struct kcached_job *job)
{
	struct cache_c *dmc = job->dmc;
	int i = 0;
	int index = job->index;
	struct cacheblock *cacheblk = &dmc->cache[index];
	int offset_bit = compatible_div((job->job_io_regions.disk.sector - cacheblk->dbn), SECTOR_PER_BIT);
	int size_bit = compatible_div(job->job_io_regions.disk.count, SECTOR_PER_BIT);
	bitmap_t done_bitmap = 0;
#ifdef MY_ABC_HERE
	int sync = job->action == WRITEDISK_SYNC;
#endif

	VERIFY_WARN(!in_interrupt());
	VERIFY_WARN(job->bio == NULL);

	for (i = 0; i < size_bit; ++i) {
		done_bitmap |= 1 << (offset_bit + i);
	}

	spin_lock_irq(&dmc->cache_spin_lock);
	VERIFY_WARN((cacheblk->cache_state & (DISKWRITEINPROG | VALID | DIRTY))
		== (DISKWRITEINPROG | VALID | DIRTY));
	cacheblk->flushing_dirty_bitmap &= ~(done_bitmap);

	if (check_new_error_inject(dmc, OQF_WB_ERROR)) {
		sprint("Simulate oqf write back error");
		dmc->sysctl_new_error_inject = 0;
		job->error = -EIO;
	}

	if (job->error) {
		cb_state_add_bits_update_counts(dmc, cacheblk, QFSYNCERR);
	}

	if (cacheblk->flushing_dirty_bitmap) {
		/* Not the last sub job */
		VERIFY_WARN(0 != cacheblk_dec_num_concurrent_and_return(cacheblk));
		spin_unlock_irq(&dmc->cache_spin_lock);
		flashcache_free_cache_job(job);
		if (atomic_dec_and_test(&dmc->nr_jobs)) {
			/* Not the last job, should not enter here */
			serr("not last job but nr_job is zero");
		}
		goto end;
	}

	/* only the last sub job enter here */
	VERIFY_WARN(cacheblk->nr_concurrent == 1);

	atomic64_inc(&dmc->flashcache_stats.dirty_writeback_done);
	if (unlikely(cacheblk->cache_state & QFSYNCERR)) {
		cb_state_remove_bits_update_counts(dmc, cacheblk, QFSYNCERR);
		spin_unlock_irq(&dmc->cache_spin_lock);
		/* Now all io are -EIO, we dont need to distinguish read/write */
		job->error = -EIO;
		flashcache_do_pending(job);
		atomic64_inc(&dmc->flashcache_stats.cleanings);
		goto err;
	}

#ifdef MY_ABC_HERE
	/* If there are queued io, the block IS going to be invalidated in
	 * the writeback case, i.e., we will sent a force mu in do_pending
	 * since the block is SYNCED. Force the md update directly */
	if (cacheblk->nr_queued) {
		spin_unlock_irq(&dmc->cache_spin_lock);
		// TODO: io latency will be lost. But I guess we don't care about it?
		syno_mu_queue_force_update(dmc, index);
	} else {
		VERIFY_WARN(!(job->flag & JOB_WB_FORCED));
		spin_unlock_irq(&dmc->cache_spin_lock);

		while (check_new_error_inject(dmc, WB_MU_REQUEUE_FORCED_TEST)) {
			sprint("Detect wb mu requeue force test, wait");
			msleep(5000);
			if (cacheblk->nr_queued) {
				sprint("Detect Queued, continue");
				dmc->sysctl_new_error_inject = 0;
			}
		}

		/* States/nr_dirty set in function.
		 * Other IO might queue since we unlocked! */
		syno_mu_queue_flush_update(dmc, index);
	}

	flashcache_free_cache_job(job);
	if (atomic_dec_and_test(&dmc->nr_jobs)) {
		wake_up(&dmc->sync_wqh);
	}

	atomic64_inc(&dmc->flashcache_stats.cleanings);
	if (sync) {
		flashcache_update_sync_progress(dmc);
	}
#else
	spin_unlock_irq(&dmc->cache_spin_lock);
	// Start metadata update
	flashcache_md_write(job);
#endif

end:
err:
	return;
}

void
quickflush_get_next_io_range(bitmap_t data_bitmap, bitmap_t *dirty_bitmap, sector_t *offset, sector_t *size)
{
	int i = 1;
	int first_dirty_offset = -1;
	int last_dirty_offset = -1;

	VERIFY_WARN(*dirty_bitmap);

	for (i = 0; i < BITMAP_BITS; ++i) {
		if ((*dirty_bitmap) & (1 << i)) {
			if (-1 == first_dirty_offset) {
				first_dirty_offset = i;
			}
			last_dirty_offset = i;
			*dirty_bitmap &= ~(1 << i);
			if (!(*dirty_bitmap)) {
				break;
			}
		}

		if (-1 != first_dirty_offset) {
			if (!(data_bitmap & (1 << i))) {
				break;
			}
		}
	}

	*offset = first_dirty_offset * SECTOR_PER_BIT;
	*size = (last_dirty_offset - first_dirty_offset + 1) * SECTOR_PER_BIT;

	VERIFY_WARN(0 != *size);
}

static inline void
add_writeback_stats(struct cache_c *dmc, int nr_write, sector_t total_size, int sync)
{
	atomic64_inc(&dmc->flashcache_stats.dirty_writeback_start);
	atomic64_add(nr_write, &dmc->flashcache_stats.qf_nr_writes);
	atomic64_add(nr_write, &dmc->flashcache_stats.ssd_reads);
	atomic64_add(nr_write, &dmc->flashcache_stats.disk_writes);
	if (sync) {
		atomic64_add(total_size, &dmc->flashcache_stats.dirty_writeback_sync_sector);
	} else {
		atomic64_add(total_size, &dmc->flashcache_stats.dirty_writeback_sector);
	}
}

static void
wb_disk_write_callback(unsigned long error, void *ctx)
{
	unsigned long flags = 0;
	struct kcached_job *job = ctx;
	struct cache_c *dmc = job->dmc;
	plug_wb_t *plug_wb = &dmc->plug_wb;

	if (check_new_error_inject(dmc, WB_DISK_WRITE_ERROR)) {
		sprint("Simulate wb disk write error sync: %d", job->action == WRITEDISK_SYNC);
		dmc->sysctl_new_error_inject = 0;
		error = 1;
	}

	if (error) {
		job->error = -EIO;
		if (0 < dmc->limits.writeback_sync_err) {
			serr("plub wb write fail. block %llu",
				(u64)job->job_io_regions.disk.sector);
			dmc->limits.writeback_sync_err--;
		}
		atomic_inc(&dmc->flashcache_errors.disk_write_errors);
	}

	spin_lock_irqsave(&plug_wb->lock, flags);
	list_add_tail(&job->list, &plug_wb->complete_queue);
	plug_wb->disk_write_inprog--;
	spin_unlock_irqrestore(&plug_wb->lock, flags);

	cache_schedule_work(&plug_wb->work);
}

void
quickflush_process_wb_done(struct work_struct *wb_done_work)
{
	struct cache_c *dmc = container_of(wb_done_work, struct cache_c, plug_wb.wb_done_work);
	plug_wb_t *plug_wb = &dmc->plug_wb;
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct kcached_job *job = NULL;
	int sync_trigger = 0;
	int nqf_trigger = 0;
	LIST_HEAD(list);

	spin_lock_irq(&plug_wb->lock);
	list_splice_init(&plug_wb->wb_done_queue, &list);
	spin_unlock_irq(&plug_wb->lock);

	list_for_each_safe(pos, tmp, &list) {
		job = container_of(pos, struct kcached_job, list);

		list_del(pos);

		if (job->action == WRITEDISK_SYNC) {
			sync_trigger++;
		} else if (job->flag & JOB_WB_NQF) {
			nqf_trigger++;
		} else {
			if (!(job->flag & JOB_WB_FORCED)) {
				serr("job->action: %d job->flag %x dmc: %p", job->action, job->flag, dmc);
				VERIFY_WARN(0);
			}
		}

		quickflush_wb_done(job);
	}

	/* sync and oqf should not run in parallel */
	VERIFY_WARN(!(sync_trigger && nqf_trigger));

	mutex_lock(&dmc->oqf.mtx);
	if (nqf_trigger) {
		dmc->oqf.trigger_cnt -= (nqf_trigger - 1);
	} else if (sync_trigger) {
		dmc->oqf.sync_trigger_cnt -= (sync_trigger - 1);
	} else if (dmc->oqf.sync_trigger_cnt) {
		/* Optimization:
		 * When ssd read batch finished and sent disk write we need to
		 * send more ssd read and find new batch. Normally this is done
		 * in disk write finish callback. Trigger it manually if no disk write finish */
		sync_trigger = 1;
		dmc->oqf.sync_trigger_cnt++;
	}
	mutex_unlock(&dmc->oqf.mtx);

	if (nqf_trigger) {
		syno_start_nqf(dmc);
	} else if (sync_trigger) {
		syno_start_sqf(dmc);
	}
}

static void
process_complete_queue(plug_wb_t *plug_wb)
{
	LIST_HEAD(list);
	struct list_head *pos = NULL;
	struct kcached_job *job = NULL;

	spin_lock_irq(&plug_wb->lock);
	list_splice_init(&plug_wb->complete_queue, &list);
	spin_unlock_irq(&plug_wb->lock);

	list_for_each(pos, &list) {
		job = container_of(pos, struct kcached_job, list);

		VERIFY_WARN(job->action == WRITEDISK || job->action == WRITEDISK_SYNC);

		flashcache_free_cache_job_mem(job);
	}

	spin_lock_irq(&plug_wb->lock);
	list_splice(&list, &plug_wb->wb_done_queue);
	spin_unlock_irq(&plug_wb->lock);

	cache_schedule_work(&plug_wb->wb_done_work);
}

static inline void
wb_send_disk_write(struct list_head *list)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct kcached_job *job = NULL;
	unsigned long bi_flags = 0;

	/* Use safe version since job can be removed from list once io sent */
	list_for_each_safe(pos, tmp, list) {
		job = container_of(pos, struct kcached_job, list);
		bi_flags = job->action == WRITEDISK_SYNC ? (1 << BIO_SYNO_FULL_STRIPE_MERGE) : 0;

		dm_io_vmem(1, &job->job_io_regions.disk, WRITE, job->mem_addr,
			wb_disk_write_callback, job, bi_flags);
	}
}

static inline void
handle_error_or_append_to_disk_write_list(plug_wb_t *plug_wb, struct kcached_job *job, struct list_head *dwl)
{
	plug_wb->ssd_read_inprog--;

	if (job->error) {
		list_add_tail(&job->list, &plug_wb->complete_queue);
	} else {
		// append to disk write list
		list_add_tail(&job->list, dwl);
		plug_wb->disk_write_inprog++;
	}
}

static void
process_io_queue(plug_wb_t *plug_wb)
{
	LIST_HEAD(list);
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct kcached_job *job = NULL;

	spin_lock_irq(&plug_wb->lock);

	list_for_each_safe(pos, tmp, &plug_wb->io_queue) {
		job = container_of(pos, struct kcached_job, list);

		VERIFY_WARN(job->flag & JOB_WB_READ_DONE);

		/* FORCED job is guaranteed to be at the head of the queue
		 * No other thorttling needed for online wb / force wb */
		if (!(job->flag & JOB_WB_FORCED) &&
			plug_wb->disk_write_inprog >= plug_wb->disk_write_ios_limit) {
			/* forced job not throttled */
			break;
		}

		list_del(pos);

		handle_error_or_append_to_disk_write_list(plug_wb, job, &list);
	}

	spin_unlock_irq(&plug_wb->lock);

	wb_send_disk_write(&list);
}

static inline int
must_send_batch_under_ios_throtl(plug_wb_t *plug_wb)
{
	return 0 == plug_wb->disk_write_inprog;
}

static inline int
can_send_batch_under_ios_throtl(struct cache_c *dmc, plug_wb_t *plug_wb)
{
	return (plug_wb->disk_write_inprog + plug_wb->cur_batch_len +
		atomic_read(&dmc->num_master_io)) <= plug_wb->disk_write_ios_limit;
}

static inline int
can_send_disk_write_under_ios_throtl(struct cache_c *dmc, plug_wb_t *plug_wb)
{
	return (plug_wb->disk_write_inprog + atomic_read(&dmc->num_master_io)) <
		plug_wb->disk_write_ios_limit;
}

static void
process_sync_io_queue(plug_wb_t *plug_wb)
{
	LIST_HEAD(list);
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct kcached_job *job = NULL;
	struct cache_c *dmc = container_of(plug_wb, struct cache_c, plug_wb);

	spin_lock_irq(&plug_wb->lock);
	if (!(PLUG_WB_HAS_BATCH_AND_DISK_WRITE_READY(plug_wb))) {
		goto end_unlock;
	}

	/* The throttle rules:
	 *  1. Limit disk IO to disk_write_ios_limit, assume all external IO touches disk
	 *  2. If no disk write in progess, send a batch of io (incase no one triggers wb after that) */
	if (!must_send_batch_under_ios_throtl(plug_wb) &&
		!can_send_batch_under_ios_throtl(dmc, plug_wb)) {
		goto end_unlock;
	}

	list_for_each_safe(pos, tmp, &plug_wb->sync_io_queue) {
		job = container_of(pos, struct kcached_job, list);

		if (!(job->flag & JOB_WB_READ_DONE)) {
			break;
		}

		/* Send extra IO after batch if SSD_READ_DONE and pass throttle */
		if (0 != plug_wb->cur_batch_len) {
			/* Disk write in this batch already checked throtl cond */
			plug_wb->cur_batch_len--;
		} else if (!can_send_disk_write_under_ios_throtl(dmc, plug_wb)) {
			break;
		}

		list_del(pos);
		handle_error_or_append_to_disk_write_list(plug_wb, job, &list);
		atomic64_inc(&dmc->flashcache_stats.qf_queued_sync_io_processed);
	}

	VERIFY_WARN(0 == plug_wb->cur_batch_len);

end_unlock:
	spin_unlock_irq(&plug_wb->lock);

	wb_send_disk_write(&list);
}

/*
 * Find new batch, queue it if all io in batch are ssd read done
 */
static void sync_wb_handle_batch(plug_wb_t *plug_wb)
{
	unsigned long flags = 0;
	struct list_head *pos = NULL;
	struct kcached_job *job = NULL;

	spin_lock_irqsave(&plug_wb->lock, flags);
	if (PLUG_WB_HAS_BATCH(plug_wb) || list_empty(&plug_wb->sync_io_queue)) {
		goto end_unlock;
	}

	VERIFY_WARN(0 == plug_wb->cur_batch_len);
	VERIFY_WARN(0 == plug_wb->unfinished_read_in_batch);

	job = container_of(plug_wb->sync_io_queue.next, struct kcached_job, list);
	plug_wb->cur_batch_head = job->wb_seq;

	list_for_each(pos, &plug_wb->sync_io_queue) {
		job = container_of(pos, struct kcached_job, list);

		plug_wb->cur_batch_len++;
		if (!(job->flag & JOB_WB_READ_DONE)) {
			plug_wb->unfinished_read_in_batch++;
		}

		if (plug_wb->cur_batch_len == plug_wb->batch_size) {
			break;
		}
	}

	if (plug_wb->cur_batch_len && 0 == plug_wb->unfinished_read_in_batch) {
		/* All ready, start next writeback */
		cache_schedule_work(&plug_wb->work);
	}

end_unlock:
	spin_unlock_irqrestore(&plug_wb->lock, flags);
}

/* Main function for plug wb.
 * WARNING: The threads is responsible for releasing job mempool memory
 *          MUST NOT block trying to get memory from job mempool. (e.g., call syno_start_sqf) */
void
quickflush_process_wb_ios(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, plug_wb.work);
	plug_wb_t *plug_wb = &dmc->plug_wb;
	struct blk_plug plug;

	/* Disk writes should be flushed */
	blk_start_plug(&plug);
	process_io_queue(plug_wb);
	process_sync_io_queue(plug_wb);
	blk_finish_plug(&plug);

	/* Free memory and schedule callback work, MUST put at the end */
	process_complete_queue(plug_wb);
}

static inline int job_in_current_batch(plug_wb_t *plug_wb, struct kcached_job *job)
{
	VERIFY_WARN(job->wb_seq >= plug_wb->cur_batch_head);
	return (PLUG_WB_HAS_BATCH(plug_wb) &&
		job->wb_seq < plug_wb->cur_batch_head + plug_wb->cur_batch_len);
}

static void
wb_ssd_read_callback(unsigned long error, void *ctx)
{
	unsigned long flags;
	struct kcached_job *job = ctx;
	struct cache_c *dmc = job->dmc;
	int sync = (job->action == WRITEDISK_SYNC);
	int schedule_work = 0;
	plug_wb_t *plug_wb  = &job->dmc->plug_wb;

	spin_lock_irqsave(&plug_wb->lock, flags);

	/* no need to distinguish sync, sync/normal wb is
	 * unlikely to happen at the same time */
	if (check_new_error_inject(dmc, WB_SSD_READ_ERROR)) {
		sprint("Simulate writeback ssd read error, sync: %d", sync);
		dmc->sysctl_new_error_inject = 0;
		error = 1;
	}

	job->flag |= JOB_WB_READ_DONE;

	if (sync) {
		if (job_in_current_batch(plug_wb, job)) {
			plug_wb->unfinished_read_in_batch--;
			if (0 == plug_wb->unfinished_read_in_batch) {
				schedule_work = 1;
			}
		}
	} else {
		// TODO: use standalone queue for forced job so the order won't be reversed
		if (job->flag & JOB_WB_FORCED) {
			list_add(&job->list, &plug_wb->io_queue);
		} else {
			// TODO: will preserve perfect order for nqf help?
			//       scheduler are expected to handle that case
			list_add_tail(&job->list, &plug_wb->io_queue);
		}
		schedule_work = 1;
	}

	if (unlikely(error)) {
		if (0 < job->dmc->limits.writeback_sync_err) {
			serr("plub wb read fail. block %llu",
				(u64)job->job_io_regions.cache.sector);
			job->dmc->limits.writeback_sync_err--;
		}
		atomic_inc(&job->dmc->flashcache_errors.ssd_read_errors);
		job->error = -EIO;
	}

	spin_unlock_irqrestore(&plug_wb->lock, flags);

	if (schedule_work) {
		cache_schedule_work(&plug_wb->work);
	}
}

static void _wb_send_ssd_read_from_jobs(plug_wb_t *plug_wb, struct list_head *job_list, int sync)
{
	struct kcached_job *job = NULL;
	struct list_head *pos = NULL;
	struct list_head *start = job_list->next;
	struct list_head *end = job_list->prev;

	if (list_empty(job_list)) {
		return;
	}

	spin_lock_irq(&plug_wb->lock);

	list_for_each(pos, job_list) {
		job = container_of(pos, struct kcached_job, list);
		if (sync) {
			job->wb_seq = plug_wb->seq++;
		}
	}

	if (sync) {
		list_splice_tail(job_list, &plug_wb->sync_io_queue);
	} // not sync jobs add to io queue after read done
	spin_unlock_irq(&plug_wb->lock);

	/*
	 * No need to lock even if start/end are in sync_io_queue
	 * As long as the ssd read IO is not sent, the list of job won't be changed
	 * Record start->next before sending ssd read since 'start' may be removed after that
	 */
	while (1) {
		pos = start->next;
		job = container_of(start, struct kcached_job, list);

		dm_io_vmem(1, &job->job_io_regions.cache, READ,
			job->mem_addr, wb_ssd_read_callback, job, 0);

		if (start == end) {
			break;
		}
		start = pos;
	}
}

static inline void
wb_send_ssd_read_from_jobs(plug_wb_t *plug_wb, struct list_head *job_list)
{
	_wb_send_ssd_read_from_jobs(plug_wb, job_list, 0);
}

static inline void
wb_send_ssd_read_from_jobs_sync(plug_wb_t *plug_wb, struct list_head *job_list)
{
	_wb_send_ssd_read_from_jobs(plug_wb, job_list, 1);
}

static inline void
verify_wb_states(struct cacheblock *cb, int nr_write)
{
	VERIFY_WARN((cb->cache_state & FALLOW_DOCLEAN) == 0);
	VERIFY_WARN((cb->cache_state & BLOCK_IO_INPROG) == DISKWRITEINPROG);
	VERIFY_WARN(cb->cache_state & DIRTY);
	VERIFY_WARN(cb->flushing_dirty_bitmap == 0);
	VERIFY_WARN(cb->nr_concurrent == nr_write);
}

/* Newly allocated jobs are append to out */
static void
quickflush_wb_jobs_alloc_common(struct cache_c *dmc, cb_wb_param_t param, int sync, struct list_head *out)
{
	LIST_HEAD(list);
	struct list_head *pos = NULL;
	struct list_head *tmp_list = NULL;
	struct kcached_job *tmp_job = NULL;
	const int nr_write = param.nr_write;
	const int index = param.idx;
	int cur_write = 0;
	int device_removal = 0;
	struct cacheblock *cacheblk = &dmc->cache[index];
	bitmap_t dirty_bitmap = 0;
	sector_t offset = 0;
	sector_t size = 0;
	sector_t total_size = 0;
	plug_wb_t *plug_wb = &dmc->plug_wb;

	verify_wb_states(cacheblk, nr_write);

	/* WRITEDISK* flag set for this cb, status shouldn't be changed by
	 * others, no need to lock */
	cacheblk->flushing_dirty_bitmap = cacheblk->dirty_bitmap;

	dirty_bitmap = cacheblk->dirty_bitmap;

	while (dirty_bitmap) {
		quickflush_get_next_io_range(cacheblk->data_bitmap, &dirty_bitmap, &offset, &size);
		total_size += size;

		if (sync) {
			tmp_job = new_kcached_job_qf(dmc, index, offset, size, TYPE_SQF);
		} else {
			tmp_job = new_kcached_job_qf(dmc, index, offset, size, TYPE_NQF);
		}


		if (unlikely(dmc->sysctl_error_inject & QF_ALLOC_WRITEBACK_JOB_ERROR)) {
			dmc->sysctl_error_inject &= ~(QF_ALLOC_WRITEBACK_JOB_ERROR);
			if (tmp_job) {
				flashcache_free_cache_job(tmp_job);
				tmp_job = NULL;
			}
		}

		if (unlikely(tmp_job == NULL)) {
			serr("Failed to allocate job for quickflush writeback, block %llu",
				(u64)cacheblk->dbn);
			break;
		}

		tmp_job->bio = NULL;
		tmp_job->action = sync ? WRITEDISK_SYNC : WRITEDISK;
		tmp_job->flag |= param.job_flag;
		list_add_tail(&tmp_job->list, &list);
		cur_write++;
	}

	if (sync) {
		device_removal = (atomic_read(&dmc->remove_in_prog) == FAST_REMOVE) ? 1 : 0;
	} else {
		device_removal = atomic_read(&dmc->remove_in_prog) ? 1 : 0;
	}

	if (device_removal) {
		sprint("qucikflush writeback aborted for device removal, block %llu",
			(u64)cacheblk->dbn);
	}

	if (unlikely(cur_write != nr_write || device_removal)) {
		spin_lock_irq(&dmc->cache_spin_lock);
		flashcache_free_pending_jobs(dmc, cacheblk, -EIO);
		cacheblk->flushing_dirty_bitmap = 0;
		unset_partial_cb_writeback_attrs(dmc, cacheblk, nr_write);
		spin_unlock_irq(&dmc->cache_spin_lock);

		mutex_lock(&dmc->oqf.mtx);
		if (sync) {
			dmc->oqf.sync_trigger_cnt -= nr_write;
			/* always called by sync sorted block, trigger shouldn't be zero */
			VERIFY_WARN(0 < dmc->oqf.sync_trigger_cnt);
		} else if(param.job_flag & JOB_WB_NQF) {
			dmc->oqf.trigger_cnt -= nr_write;
			VERIFY_WARN(0 < dmc->oqf.trigger_cnt);
		}
		mutex_unlock(&dmc->oqf.mtx);

		list_for_each_safe(pos, tmp_list, &list) {
			tmp_job = container_of(pos, struct kcached_job, list);
			flashcache_free_cache_job(tmp_job);
		}

		spin_lock_irq(&plug_wb->lock);
		plug_wb->ssd_read_inprog -= nr_write;
		spin_unlock_irq(&plug_wb->lock);

		if (atomic_sub_and_test(nr_write, &dmc->nr_jobs)) {
			wake_up(&dmc->sync_wqh);
		}
	} else {
		add_writeback_stats(dmc, nr_write, total_size, sync);
		list_splice_tail(&list, out);
	}
}

static inline void
quickflush_wb_job_alloc_sync(struct cache_c *dmc, cb_wb_param_t param, struct list_head *out)
{
	return quickflush_wb_jobs_alloc_common(dmc, param, 1, out);
}

static inline void
quickflush_wb_job_alloc(struct cache_c *dmc, cb_wb_param_t param, struct list_head *out)
{
	return quickflush_wb_jobs_alloc_common(dmc, param, 0, out);
}

static void
sync_wb_send_ssd_read(struct cache_c *dmc, cb_wb_param_t *cb_wb_param_list, int nr_blk)
{
	int i = 0;
	LIST_HEAD(job_list);

	for (i = 0; i < nr_blk; ++i) {
		quickflush_wb_job_alloc_sync(dmc, cb_wb_param_list[i], &job_list);
	}

	wb_send_ssd_read_from_jobs_sync(&dmc->plug_wb, &job_list);
}

void
quickflush_dirty_writeback(struct cache_c *dmc, cb_wb_param_t wb_param)
{
	LIST_HEAD(job_list);

	/* Lock here to avoid deadlock from multiple dmc allocating from mempool
	 * since writing back a cb might requires multiple job & mem. */
	mutex_lock(&nqf_mem_mtx);
	quickflush_wb_job_alloc(dmc, wb_param, &job_list);
	mutex_unlock(&nqf_mem_mtx);

	wb_send_ssd_read_from_jobs(&dmc->plug_wb, &job_list);
}

static inline int
find_cb_check_can_fallow_wb(struct cache_c *dmc, u64 vb_idx)
{
	int cb_idx = quickflush_find_cacheblk_idx(dmc, vb_idx);

	if (-1 == cb_idx || !(dmc->cache[cb_idx].cache_state & DIRTY_FALLOW_2)) {
		return -1;
	}

	VERIFY_WARN(0 == (dmc->cache[cb_idx].cache_state & BLOCK_IO_INPROG));

	return cb_idx;
}

static inline int
find_cb_check_can_sync_wb(struct cache_c *dmc, u64 vb_idx)
{
	int cb_idx = quickflush_find_cacheblk_idx(dmc, vb_idx);

	if (-1 == cb_idx || !(dmc->cache[cb_idx].cache_state & DIRTY)
		|| dmc->cache[cb_idx].cache_state & BLOCK_IO_INPROG) {
		VERIFY_WARN(cb_idx == -1 || !(dmc->cache[cb_idx].cache_state & DIRTY_FALLOW_2));
		return -1;
	}

	return cb_idx;
}

/* return 0: can send more io, continue on next unit. 1: reach io limit. */
static int
_get_cbs_to_wb_from_bit(struct cache_c *dmc, int *nr_blk,
	cb_wb_param_t *cb_param_list, int param_list_size, int sync)
{
	int i = 0;
	int ret = 0;
	online_qf_t *oqf = &dmc->oqf;
	int cb_idx = -1;
	const int init_nr_blk = *nr_blk;
	int new_writes = 0;
	unsigned long wb_flags = 0;
	u64 vb_idx = oqf->bit_idx * OQF_VB_PER_BIT;
	int (*find_cb_check_can_wb)(struct cache_c*, u64) =
		sync ? find_cb_check_can_sync_wb : find_cb_check_can_fallow_wb;

	up_read(&oqf->bitmap_rwsem);
	down_write(&oqf->bitmap_rwsem);
	mutex_lock(&oqf->mtx);
	spin_lock_irq(&dmc->cache_spin_lock);

	for (i = 0; i < OQF_VB_PER_BIT; ++i, ++vb_idx) {

		if (-1 == (cb_idx = find_cb_check_can_wb(dmc, vb_idx))) {
			continue;
		}

		new_writes = quickflush_cacheblk_get_nr_writeback(dmc, cb_idx);
		spin_lock_irqsave(&dmc->plug_wb.lock, wb_flags);
		if (*nr_blk < param_list_size && ios_can_issue_plug_wb(dmc, new_writes)) {
			cb_param_list[*nr_blk].idx = cb_idx;
			cb_param_list[*nr_blk].nr_write = new_writes;
			if (!sync) {
				cb_param_list[*nr_blk].job_flag |= JOB_WB_NQF;
			}
			(*nr_blk)++;
			set_wb_attrs(dmc, cb_idx, new_writes);
			spin_unlock_irqrestore(&dmc->plug_wb.lock, wb_flags);

			if (sync) {
				oqf->sync_trigger_cnt += new_writes;
			} else {
				oqf->trigger_cnt += new_writes;
			}
		} else {
			spin_unlock_irqrestore(&dmc->plug_wb.lock, wb_flags);
			ret = 1;
			break;
		}

	}

	if (!ret) {
		if (init_nr_blk == *nr_blk) {
			atomic64_inc(&dmc->flashcache_stats.qf_bits_mismatch);
		}
		quickflush_unset_volume_map(dmc, oqf->bit_idx);
	}

	spin_unlock_irq(&dmc->cache_spin_lock);
	mutex_unlock(&oqf->mtx);
	downgrade_write(&oqf->bitmap_rwsem);

	return ret;
}
static int
get_cbs_to_wb_from_bit_sync(struct cache_c *dmc, int *nr_blk,
	cb_wb_param_t *cb_param_list, int param_list_size)
{
	return _get_cbs_to_wb_from_bit(dmc, nr_blk, cb_param_list, param_list_size, 1);
}

static int
get_cbs_to_wb_from_bit(struct cache_c *dmc, int *nr_blk,
	cb_wb_param_t *cb_param_list, int param_list_size)
{
	return _get_cbs_to_wb_from_bit(dmc, nr_blk, cb_param_list, param_list_size, 0);
}

static inline int
skip_sqf(struct cache_c *dmc)
{

	if ((atomic_read(&dmc->remove_in_prog) == FAST_REMOVE) || dmc->sysctl_stop_sync) {
		return 1;
	}

	return 0;
}

static inline cb_wb_param_t*
alloc_wb_param_list(struct cache_c *dmc, int list_size)
{
	cb_wb_param_t *list = NULL;

	list = kzalloc(sizeof(cb_wb_param_t) * list_size, GFP_NOIO);
	if (unlikely(dmc->sysctl_error_inject & QF_ALLOC_CACHEBLK_TO_CLEAN_ERROR)) {
		dmc->sysctl_error_inject &= ~(QF_ALLOC_CACHEBLK_TO_CLEAN_ERROR);
		if (list) {
			kfree(list);
			list = NULL;
		}
	}

	return list;
}

/*
 * Let one thread queue when another is in critical section
 * Prevent the race condition where ssd read ios limit is released by thread A
 * when thread B in critical section is about to end.
 * If we don't queue A, A exits but B might not check and use the released ios limit,
 * this might result in premature termination of the syncing process.
 */
static inline int
try_lock_sqf(online_qf_t *oqf)
{
	/* return 1 when failed to lock, already two people in function */
	if (down_trylock(&oqf->sqf_sem2)) {
		return 1;
	}

	down(&oqf->sqf_sem1);
	return 0;
}

static inline void
release_sqf_lock(online_qf_t *oqf)
{
	/* sem2 should be released first so both threads in sem2 is either in
	 * syno_start_sqf or will call into it */
	up(&oqf->sqf_sem2);
	up(&oqf->sqf_sem1);
}

void
syno_start_sqf(struct cache_c *dmc)
{
	int nr_blk = 0;
	cb_wb_param_t *cb_wb_param_list = NULL;
	// prevent oob access of cb_wb_param_list when ssd_read_ios_limit changed by sysctl
	int param_list_size = dmc->plug_wb.ssd_read_ios_limit;
	online_qf_t *oqf = &dmc->oqf;
	unsigned long map_idx = oqf->bit_idx / OQF_BITS_PER_INT;
	unsigned long bit_off_in_map_unit = oqf->bit_idx % OQF_BITS_PER_INT;

	// TODO: maybe only one wq calls this function after new metadata update?
	//       check if we can remove the semaphore
	if (try_lock_sqf(&dmc->oqf)) {
		/* Already a thread waiting, the critical section is guaranteed
		 * to run at least once, end directly */
		goto end;
	}
	/* both semaphore locked */

	/* The sync process rely on callback to this function to find
	 * new batch of disk writes to send (sync_wb_handle_batch).
	 * If sync_wb_handle_batch is skipped, then jobs in sync_io_queue won't finish.
	 * Skip after obtain the lock and handle batch before end */
	if (skip_sqf(dmc)) {
		goto end_unlock;
	}

	if (oqf->bit_idx >= oqf->bitmap_num_idx * OQF_BITS_PER_INT) {
		goto end_unlock;
	}

	cb_wb_param_list = alloc_wb_param_list(dmc, param_list_size);
	if (!cb_wb_param_list) {
		atomic_inc(&dmc->flashcache_errors.memory_alloc_errors);
		serr("Failed to allocate memory for syncing blocks");
		goto err_unlock;
	}

	down_read(&oqf->bitmap_rwsem);
	while (map_idx < oqf->bitmap_num_idx) {
		if (0 == oqf->volume_bitmap[map_idx]) {
			map_idx++;
			oqf->bit_idx = map_idx * OQF_BITS_PER_INT;
			bit_off_in_map_unit = 0;
			continue;
		}

		if (oqf->volume_bitmap[map_idx] & (1 << bit_off_in_map_unit)) {
			if (get_cbs_to_wb_from_bit_sync(dmc, &nr_blk,
				cb_wb_param_list, param_list_size)) {
				break;
			}
		}

		oqf->bit_idx++;
		map_idx = oqf->bit_idx / OQF_BITS_PER_INT;
		bit_off_in_map_unit = oqf->bit_idx % OQF_BITS_PER_INT;
	}
	up_read(&oqf->bitmap_rwsem);
	sync_wb_send_ssd_read(dmc, cb_wb_param_list, nr_blk);

end_unlock:
err_unlock:
	sync_wb_handle_batch(&dmc->plug_wb);
	release_sqf_lock(&dmc->oqf);

end:
	mutex_lock(&dmc->oqf.mtx);
	dmc->oqf.sync_trigger_cnt--;
	VERIFY_WARN(0 <= dmc->oqf.sync_trigger_cnt);
	if (0 == dmc->oqf.sync_trigger_cnt) {
		// Last sync thread
#ifdef MY_ABC_HERE
		mutex_unlock(&dmc->oqf.mtx);
		syno_mu_flush_all_synced_sync(dmc);
		mutex_lock(&dmc->oqf.mtx);
#endif
		flashcache_clean_sync_state(dmc);
	}
	mutex_unlock(&dmc->oqf.mtx);

	if(cb_wb_param_list) {
		kfree(cb_wb_param_list);
		cb_wb_param_list = NULL;
	}
}

/* In under read lock, out under read lock
 * return 1 cannot writeback more, 0 can writeback more */
static int
quickflush_writeback_by_fallow_bit(struct cache_c *dmc)
{
	int i = 0;
	int ret = 0;
	cb_wb_param_t cb_wb_param_list[OQF_VB_PER_BIT] = {0};
	int nr_cb_to_clean = 0;

	ret = get_cbs_to_wb_from_bit(dmc, &nr_cb_to_clean, cb_wb_param_list, OQF_VB_PER_BIT);

	for (i = 0; i < nr_cb_to_clean; ++i) {
		quickflush_dirty_writeback(dmc, cb_wb_param_list[i]);
	}

	return ret;
}

enum {
	DMCG_NQF_RIGHT_GRANTED, // can do nqf
	DMCG_NQF_SCHEDULED,
};

/**
 * Check if dmc has right to do nqf.
 * If dmc doesn't not own right to do nqf, queue dmc into rr queue and
 * schedule nqf management work to see if we can rotate.
 *
 * return DMCG_NQF_RIGHT_GRANTED: can writeback
 *        DMCG_NQF_SCHEDULED: put this dmc into rr queue in dmc group, schedule mgmt work
 */
static int nqf_request_right_or_schedule(struct cache_c *dmc)
{
	int can_nqf = DMCG_NQF_SCHEDULED;
	dmc_group_t *dmcg = dmc->dmcg;

	/* Most of the case */
	down_read(&dmcg->rw_sem);
	if (dmcg->nqf_dmc == dmc) {
		can_nqf = DMCG_NQF_RIGHT_GRANTED;
		up_read(&dmcg->rw_sem);
		goto end;
	} else if (dmc->oqf.dmcg_wb_scheduled) {
		up_read(&dmcg->rw_sem);
		goto end;
	}
	up_read(&dmcg->rw_sem);

	/* Should rarely happen */
	down_write(&dmcg->rw_sem);

	/* Might change between release read lock and obtain write lock, check again */
	if (dmcg->nqf_dmc == dmc) {
		can_nqf = DMCG_NQF_RIGHT_GRANTED;
		up_write(&dmcg->rw_sem);
		goto end;
	} else if (dmc->oqf.dmcg_wb_scheduled) {
		up_write(&dmcg->rw_sem);
		goto end;
	}

	/* Request nqf right */
	dmc->oqf.dmcg_wb_scheduled = 1;
	list_add_tail(&dmc->node->nqf_entry, &dmcg->nqf_pending_dmcs);
	up_write(&dmcg->rw_sem);

	cache_mod_delayed_work(&nqf_mgmt_work, 0);

end:
	return can_nqf;
}

// TODO: stats
/* When nqf trigger decreased to 0 no wb is running for this dmc
 * Give nqf right to other dmc */
static void nqf_yield_right(struct cache_c *dmc)
{
	dmc_group_t *dmcg = dmc->dmcg;

	down_read(&dmcg->rw_sem);
	if (dmcg->nqf_dmc != dmc) {
		/* Nothing to yield */
		up_read(&dmcg->rw_sem);
		return;
	}
	up_read(&dmcg->rw_sem);

	down_write(&dmcg->rw_sem);
	if (dmcg->nqf_dmc == dmc) {
		dmcg->nqf_yielded = 1;
		cache_mod_delayed_work(&nqf_mgmt_work, 0);
	}
	up_write(&dmcg->rw_sem);

	return;
}

static inline void nqf_schedule(struct cache_c *dmc)
{
	/* If dmc owns current nqf right, i.e., following func return DMCG_NQF_RIGHT_GRANTED
	 * there should be thread writing back, do nothing.
	 * Else wb request is registered and will start by mgmt thread when the time comes */
	nqf_request_right_or_schedule(dmc);
	return;
}

static inline int skip_nqf(struct cache_c *dmc) {
	return 0 == atomic_read(&dmc->oqf.nr_fallow) ||
		atomic_read(&dmc->remove_in_prog) ||
		dmc->oqf.sync_state != SYNC_STATE_STOPPED;
}

void syno_start_nqf(struct cache_c *dmc)
{
	online_qf_t *oqf = &dmc->oqf;
	unsigned long map_idx = oqf->bit_idx / OQF_BITS_PER_INT;
	unsigned long bit_off_in_map_unit = oqf->bit_idx % OQF_BITS_PER_INT;
	unsigned long init_map_idx = map_idx;
	int map_idx_increased = 0;
	int yield_nqf_right = 0;

	VERIFY_WARN(oqf->trigger_cnt > 0);

	mutex_lock(&oqf->mtx);
	if (oqf->sync_state != SYNC_STATE_STOPPED || oqf->nqf_inprog) {
		goto end_unlock;
	} else {
		oqf->nqf_inprog = 1;
	}
	mutex_unlock(&oqf->mtx);

	/* For shared cache, dmc group controls who can do online wb
	 * don't schedule if nqf skipped */
	if (skip_nqf(dmc) || DMCG_NQF_SCHEDULED == nqf_request_right_or_schedule(dmc)) {
		goto end;
	}

	/* sync state is not under lock cause it don't need to be perfectly synced */
	down_read(&oqf->bitmap_rwsem);
	while (!skip_nqf(dmc)) {

		if (map_idx_increased && map_idx == init_map_idx) {
			break;
		}

		if (0 == oqf->volume_bitmap[map_idx]) {
			map_idx++;
			map_idx_increased = 1;
			if (unlikely(map_idx == oqf->bitmap_num_idx)) {
				oqf->bit_idx = 0;
				map_idx = 0;
				bit_off_in_map_unit = 0;
			} else {
				oqf->bit_idx = map_idx * OQF_BITS_PER_INT;
				bit_off_in_map_unit = 0;
			}
			continue;
		}

		if (oqf->volume_bitmap[map_idx] & (1 << bit_off_in_map_unit)) {
			if (quickflush_writeback_by_fallow_bit(dmc)) {
				break;
			}
		}
		oqf->bit_idx++;
		if (map_idx != oqf->bit_idx / OQF_BITS_PER_INT) {
			map_idx_increased = 1;
		}
		map_idx = oqf->bit_idx / OQF_BITS_PER_INT;
		bit_off_in_map_unit = oqf->bit_idx % OQF_BITS_PER_INT;
		if (unlikely(map_idx == oqf->bitmap_num_idx)) {
			bit_off_in_map_unit = 0;
			oqf->bit_idx = 0;
			map_idx = 0;
		}

	}
	up_read(&oqf->bitmap_rwsem);

end:
	mutex_lock(&oqf->mtx);
	oqf->nqf_inprog = 0;

end_unlock:
	oqf->trigger_cnt--;
	if (0 == oqf->trigger_cnt) {
		wake_up(&oqf->wqh);
		yield_nqf_right = 1;
	}
	mutex_unlock(&oqf->mtx);

	if (yield_nqf_right) {
		nqf_yield_right(dmc);
	}

	return;
}

// in under bitmap read lock & cache spin lock
void
quickflush_set_volume_map(struct cache_c *dmc, struct cacheblock *cb)
{
	unsigned long bit_idx_in_map = ((cb->dbn / NEW_MODE_BLOCK_SIZE) / OQF_VB_PER_BIT);
	unsigned long int_idx_in_map = (bit_idx_in_map / OQF_BITS_PER_INT);
	unsigned long bit_off_in_map_unit = (bit_idx_in_map % OQF_BITS_PER_INT);

	dmc->oqf.volume_bitmap[int_idx_in_map] |= 1 << bit_off_in_map_unit;
}

// in under bitmap write lock & cache spin lock
void
quickflush_unset_volume_map(struct cache_c *dmc, unsigned long bit_idx_in_map)
{
	unsigned long int_idx_in_map = (bit_idx_in_map / OQF_BITS_PER_INT);
	unsigned long bit_off_in_map_unit = (bit_idx_in_map % OQF_BITS_PER_INT);

	atomic64_inc(&dmc->flashcache_stats.qf_fallow_bits_unset);
	dmc->oqf.volume_bitmap[int_idx_in_map] &= ~(1 << bit_off_in_map_unit);
}

static inline int
quickflush_no_need_detect_fallow(struct cache_c *dmc)
{
	if (0 == dmc->nr_dirty || atomic_read(&dmc->remove_in_prog)) {
		return 1;
	}

	return 0;
}

static inline unsigned long
next_fallow_time(struct cache_c *dmc)
{
	return dmc->oqf.last_fallow_tstamp + dmc->sysctl_fallow_delay * HZ;
}

// in without lock
void
quickflush_detect_fallow(struct cache_c *dmc)
{
	int i = 0;
	int print = 0; // for debug
	int counter = 0; // for debug
	u64 start = ktime_to_ns(ktime_get()); // FIXME: remove debug messages
	ktime_t next_sleep_time;

	if (quickflush_no_need_detect_fallow(dmc)) {
		goto end;
	}

	mutex_lock(&dmc->oqf.mtx);
	if (dmc->oqf.detect_fallow_inprog) {
		VERIFY_WARN(0);
		mutex_unlock(&dmc->oqf.mtx);
		goto end;
	}
	dmc->oqf.detect_fallow_inprog = 1;
	mutex_unlock(&dmc->oqf.mtx);

	if (dmc->sysctl_fallow_delay && time_after(jiffies, next_fallow_time(dmc))) {
		print = 1;
		next_sleep_time = ktime_add(ktime_get(), ktime_set(0, OQF_MARK_FALLOW_DURATION_NSEC));
		for (i = 0; i < dmc->size ; i++) {
			flashcache_detect_fallow(dmc, i);
			/* Scan every cacheblock takes a long time. (e.g., 2 seconds for 16T cache in 918+).
			 * To avoid holding lock for too long, sleep after running for a specific interval. */
			if (unlikely((0 == i % OQF_FALLOW_CHECK_CNT) && ktime_after(ktime_get(), next_sleep_time))) {
				counter++;
				msleep(OQF_MARK_FALLOW_ITVL_MSEC);
				if (quickflush_no_need_detect_fallow(dmc)) {
					goto end_reset_inprog;
				}
				next_sleep_time = ktime_add(ktime_get(), ktime_set(0, OQF_MARK_FALLOW_DURATION_NSEC));
			}
		}
		dmc->oqf.last_fallow_tstamp = jiffies;
	}

	// debug msg
	if (print) {
		sdbg(DF_QUICKFLUSH, "Time elasped: %llu Time sleeps: %d\n",
			ktime_to_ns(ktime_get()) - start, counter);
	}

end_reset_inprog:
	mutex_lock(&dmc->oqf.mtx);
	dmc->oqf.detect_fallow_inprog = 0;
	mutex_unlock(&dmc->oqf.mtx);
end:
	return;
}

void
quickflush_do_writeback(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, oqf.wb_work.work);

	sdbg(DF_QUICKFLUSH, "quickflush_do_writeback\n");

	quickflush_detect_fallow(dmc);

	if (!skip_nqf(dmc)) {
		nqf_schedule(dmc);
	}

	quickflush_schedule_wb_work(dmc);
}

static void inline dmcg_grant_next_nqf_right(dmc_group_t *dmcg)
{
	dmc_node *node = container_of(dmcg->nqf_pending_dmcs.next, dmc_node, nqf_entry);
	list_del(&node->nqf_entry);
	dmcg->nqf_dmc = node->pdmc;
	dmcg->nqf_expire_jiffies = jiffies + NQF_TIMESLICE_SEC * HZ;
	VERIFY_WARN(node->pdmc->oqf.dmcg_wb_scheduled);
	node->pdmc->oqf.dmcg_wb_scheduled = 0;
}

static inline int dmcg_nqf_need_release_right(dmc_group_t *dmcg)
{
	/* No need to release right if no one is waiting */
	return dmcg->nqf_yielded ||
		(dmcg->nqf_dmc &&
		time_after(jiffies, dmcg->nqf_expire_jiffies) &&
		!list_empty(&dmcg->nqf_pending_dmcs));
}

/* In under dmcg->rw_sem WRITE lock */
static void dmcg_nqf_check_rotate(dmc_group_t *dmcg)
{
	/* Stop nqf if any sqf is on going */
	if (atomic_read(&dmcg->sqf_inprog)) {
		dmcg->nqf_dmc = NULL;
		return;
	}

	if (dmcg_nqf_need_release_right(dmcg)) {
		dmcg->nqf_dmc = NULL;
		dmcg->nqf_yielded = 0;
	}

	if (NULL == dmcg->nqf_dmc && !list_empty(&dmcg->nqf_pending_dmcs)) {
		dmcg_grant_next_nqf_right(dmcg);
	}

	return;
}

/* Main function to manage nqf for all dmc group */
void dmcg_nqf_mgmt(struct work_struct *work)
{
	struct list_head *pos = NULL;
	dmc_group_t *dmcg = NULL;

	mutex_lock(&dmcg_mtx);
	list_for_each(pos, &dmc_group_list) {
		dmcg = container_of(pos, dmc_group_t, node);

		down_write(&dmcg->rw_sem);
		dmcg_nqf_check_rotate(dmcg);
		up_write(&dmcg->rw_sem);

		/* This is the only thread that will modify dmcg->nqf_dmc, skip lock */
		if (dmcg->nqf_dmc) {
			mutex_lock(&dmcg->nqf_dmc->oqf.mtx);
			dmcg->nqf_dmc->oqf.trigger_cnt++;
			mutex_unlock(&dmcg->nqf_dmc->oqf.mtx);
			syno_start_nqf(dmcg->nqf_dmc);
		}

		wake_up(&dmcg->nqf_wqh);
	}

	mutex_unlock(&dmcg_mtx);

	schedule_nqf_mgmt_work();
}

#endif /* MY_ABC_HERE */
