#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/list.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include "flashcache.h"
#include "syno_md_update.h"

// REMOVE THE FILE FOR GPL RELEASE
#ifdef MY_ABC_HERE

void syno_mu_send_disk_flush(struct cache_c *dmc);
void syno_mu_send_ssd_flush(struct cache_c *dmc);
void syno_mu_send_md_ios(struct cache_c *dmc);
extern int cache_schedule_work(struct work_struct *work);

// FIXME: remove before release
#define debug(mu) // debug_queue(mu)

void debug_queue(new_mu_t *mu)
{
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;
	static int limit = 1000;
	int changed = 0;

	list_for_each(pos, &mu->disk_flush.inprog_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_DISK_FLUSH_INPROG) && limit) {
			serr("Invalid flag for disk flush inprog: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->disk_flush.pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_DISK_FLUSH_PENDING) && limit) {
			serr("Invalid flag for disk_flush pending: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->ssd_flush.pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_SSD_FLUSH_PENDING) && limit) {
			serr("Invalid flag for ssd flush pending: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->ssd_flush.inprog_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_SSD_FLUSH_INPROG) && limit) {
			serr("Invalid flag for ssd flush inprog: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->md_io_pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_MD_IO_PENDING) && limit) {
			serr("Invalid flag for md io pending: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->md_io_for_flush_pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_MD_IO_PENDING) && limit) {
			serr("Invalid flag for md io for flush pending: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	list_for_each(pos, &mu->md_io_inprog_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (((mu_blk->flags & MU_IN_QUEUE) != MU_MD_IO_INPROG) && limit) {
			serr("Invalid flag for md io for flush pending: %d %x", mu_blk->idx, mu_blk->flags);
			limit--;
			changed = 1;
		}
	}

	if (mu->ext_flush.inprog) {
		list_for_each(pos, &mu->ext_flush.md_io_inprog_queue) {
			mu_blk = container_of(pos, mu_blk_t, node);

			if (((mu_blk->flags & MU_IN_QUEUE) != MU_MD_IO_INPROG) && limit) {
				serr("Invalid flag for ext flush md io for flush pending: %d %x", mu_blk->idx, mu_blk->flags);
				limit--;
				changed = 1;
			}
		}

		list_for_each(pos, &mu->ext_flush.md_io_inprog_queue) {
			mu_blk = container_of(pos, mu_blk_t, node);

			if (((mu_blk->flags & MU_IN_QUEUE) != MU_MD_IO_INPROG) && limit) {
				serr("Invalid flag for ext flush md io inprog: %d %x", mu_blk->idx, mu_blk->flags);
				limit--;
				changed = 1;
			}
		}
	}

	if (changed) {
		dump_stack();
	}

	return;
}

static inline int job_in_mu_blk(struct cache_c *dmc, struct kcached_job *job, mu_blk_t *mu_blk)
{
	const int cb_start_idx = MD_BLOCK_TO_START_IDX(dmc, mu_blk->idx);
	return cb_start_idx <= job->index && (cb_start_idx + MD_SLOTS_PER_BLOCK(dmc)) > job->index;
}

static void set_job_fua_inprog(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	struct list_head *pos = NULL;
	struct kcached_job *job = NULL;

	list_for_each(pos, &dmc->new_mu.fua_job_list) {
		job = container_of(pos, struct kcached_job, list);

		if (job_in_mu_blk(dmc, job, mu_blk)) {
			job->flag |= JOB_FUA_MU_INPROG;
		}
	}
}

static void push_mu_blk_to_md_io_pending(new_mu_t *mu, mu_blk_t *mu_blk)
{
	struct cache_c *dmc = container_of(mu, struct cache_c, new_mu);
	mu_blk->tstamp = jiffies;
	mu_blk_set_flag(mu_blk, MU_MD_IO_PENDING);

	if (unlikely(mu_blk_flagged_any(mu_blk, MU_FUA))) {
		set_job_fua_inprog(dmc, mu_blk);
	}

	/* Must clear MU_PENDING_BEFORE_FLUSH. It's ok to queue FUA mu to
	 * ext_flush->md_io_pending_queue since it'll also be handled immediately */
	if (unlikely(mu_blk_test_clear_flag_any(mu_blk, MU_PENDING_BEFORE_FLUSH))) {
		VERIFY_WARN(mu->ext_flush.inprog);

		sdbg(DF_NEW_MU, "requeue mu pending before flush idx: %d", mu_blk->idx);

		list_add(&mu_blk->node, &mu->ext_flush.md_io_pending_queue);
		cache_schedule_work(&mu->ext_flush.send_io_work);
	} else {
		if (unlikely(mu_blk_flagged_any(mu_blk, MU_FUA))) {
			list_add(&mu_blk->node, &mu->md_io_pending_queue);
		} else {
			list_add_tail(&mu_blk->node, &mu->md_io_pending_queue);
		}

		mu->md_io_pending_queue_size++;
		atomic_inc(&dmc->dmcg->md_io_pending_no_flush);
	}
}

static inline int get_current_sync_flag(mu_blk_t *mu_blk)
{
	return mu_blk_flagged_any(mu_blk, MU_FLUSH_1) ? SYNCED1 : SYNCED2;
}

static inline int get_pending_sync_flag(mu_blk_t *mu_blk)
{
	return mu_blk_flagged_any(mu_blk, MU_FLUSH_1) ? SYNCED2 : SYNCED1;
}

/* under mu lock */
static int no_md_updates_queued(new_mu_t *mu)
{
	return list_empty(&mu->disk_flush.pending_queue) &&
		list_empty(&mu->disk_flush.inprog_queue) &&
		list_empty(&mu->ssd_flush.pending_queue) &&
		list_empty(&mu->ssd_flush.inprog_queue) &&
		list_empty(&mu->md_io_inprog_queue) &&
		list_empty(&mu->md_io_pending_queue) &&
		list_empty(&mu->md_io_for_flush_pending_queue);
}

/* Timeout for flush SYNCED_BITS blocks. If SSD crash during dirty data syncing,
 * the workqueue will be blocked here without timeout since it's not possible
 * to update metadata on SSD.
 * TODO: it's better if we can detect ssd crash. */
#define FLUSH_SYNCED_TIMEOUT_SEC 60
static inline int all_synced_flushed_or_timeout(struct cache_c *dmc, unsigned long start_jif)
{
	return (dmc->nr_synced == 0 || time_after(jiffies, start_jif + FLUSH_SYNCED_TIMEOUT_SEC * HZ));
}

void syno_mu_flush_all_synced_sync(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;
	unsigned long start_jif = jiffies;

	spin_lock_irq(&mu->lock);

	mu->sysctl_mu_delay_sec = 0;
	mu->flush_itvl_sec = 0;

	cache_mod_delayed_work(&mu->dwork, 0);

	spin_unlock_irq(&mu->lock);

	sdbg(DF_QUICKFLUSH, "waiting for synced blocks to be cleared");

	spin_lock_irq(&dmc->cache_spin_lock);
	while (!all_synced_flushed_or_timeout(dmc, start_jif)) {
		spin_unlock_irq(&dmc->cache_spin_lock);
		wait_event_timeout(mu->remove_wqh, all_synced_flushed_or_timeout(dmc, start_jif), 10 * HZ);
		spin_lock_irq(&dmc->cache_spin_lock);
	}

	if (dmc->nr_synced) {
		sdbg(DF_QUICKFLUSH, "Wait for synced blocks timeout. %d synced blocks not cleared.", dmc->nr_synced);
	} else {
		sdbg(DF_QUICKFLUSH, "wait for synced blocks cleared done");
	}
	spin_unlock_irq(&dmc->cache_spin_lock);

	spin_lock_irq(&mu->lock);
	mu->sysctl_mu_delay_sec = MU_DEFAULT_DELAY_SEC;
	mu->flush_itvl_sec = MU_DEFAULT_FLUSH_ITVL_SEC;
	spin_unlock_irq(&mu->lock);
}

/* The function blocks until all the metadata updates are finished
 * NOT atomic context*/
void syno_mu_flush_all_for_remove_sync(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;

	spin_lock_irq(&mu->lock);

	mu->sysctl_mu_delay_sec = 0;
	mu->flush_itvl_sec = 0;
	mu->remove_flush = 1;

	cache_mod_delayed_work(&mu->dwork, 0);

	serr("waiting for md update to be cleared");

	wait_event_lock_irq(mu->remove_wqh, no_md_updates_queued(mu), mu->lock);

	serr("wait for md update cleared done");

	spin_unlock_irq(&mu->lock);

	cancel_delayed_work_sync(&mu->dwork);
	VERIFY_WARN(atomic_read(&dmc->nr_jobs) == 0);

	syno_mu_disable_dmcg_throtl(dmc);
}

// in under mu lock
static int merge_all_to_force_mu(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	int i = 0;
	unsigned long flags = 0;
	int pending_sync_flag = get_pending_sync_flag(mu_blk);
	new_mu_t *mu = &dmc->new_mu;
	int cb_idx = MD_BLOCK_TO_START_IDX(dmc, mu_blk->idx);

	VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));
	VERIFY_WARN(!mu_blk_flagged_any(mu_blk, MU_MD_IO_INPROG));

	if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		/* we merge pending with forced, align SYNCED_BITS flags */
		for (i = 0; i < MD_SLOTS_PER_BLOCK(dmc) && cb_idx < dmc->size; ++i, ++cb_idx) {
			if (dmc->cache[cb_idx].cache_state & pending_sync_flag) {
				VERIFY_WARN((dmc->cache[cb_idx].cache_state & SYNCED_BITS) != SYNCED_BITS);
				/* Flip SYNCED_BITS bits */
				dmc->cache[cb_idx].cache_state ^= SYNCED_BITS;
			}
		}
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
		mu->pending_blk_num[MU_BLK_TYPE_FLUSH]--;
	}

	mu_blk->flags &= (MU_INPROG | MU_NEED_FLUSH);

	mu_blk_set_flag(mu_blk, MU_FORCED | MU_DISK_FLUSH_PENDING);
	debug(mu);
	list_add_tail(&mu_blk->node, &mu->disk_flush.pending_queue);
	debug(mu);
	mu->disk_flush.forced = 1;

	cache_mod_delayed_work(&mu->dwork, 0);
	return get_current_sync_flag(mu_blk);
}

/* NOT in ATOMIC context */
void syno_mu_queue_force_update(struct cache_c *dmc, int cb_idx)
{
	int sync_flag = 0;
	int do_pending = 0;
	unsigned long cache_flags = 0;
	unsigned long md_idx = INDEX_TO_MD_BLOCK(dmc, cb_idx);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = &mu->mu_blocks[MU_BLK_TYPE_FLUSH][md_idx];
	struct cacheblock *cb = &dmc->cache[cb_idx];

	spin_lock_irq(&mu->lock);
	if (!mu_blk_flagged_any(mu_blk, MU_INPROG)) {

		/* New md update */
		mu_blk_set_flag(mu_blk, MU_INPROG | MU_FORCED | MU_DISK_FLUSH_PENDING | MU_FLUSH_1);

		list_add_tail(&mu_blk->node, &mu->disk_flush.pending_queue);
		mu->disk_flush.forced = 1;

		sync_flag = SYNCED1; // update sync flag
		mu->inprog_blk_num[MU_BLK_TYPE_FLUSH]++;

	} else if (mu_blk_flagged_any(mu_blk, MU_FORCED)) {
		/* already have a forced update */

		if (mu_blk_flagged_any(mu_blk, MU_DISK_FLUSH_PENDING)) {
			/* No need to add counter since we merge with existing request */
			sync_flag = get_current_sync_flag(mu_blk);
		} else {
			if (!mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
				mu->pending_blk_num[MU_BLK_TYPE_FLUSH]++;
			} else {
				/* Merge with existinig pending, don't add counter */
			}

			mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ | MU_PENDING_REQ_FORCED);
			sync_flag = get_pending_sync_flag(mu_blk);
		}

	} else if (mu_blk_flagged_any(mu_blk, MU_MD_IO_INPROG)) {
		/* Need to group all updates but current one io inprog, wait
		 * Merge with existing valid, don't add counter */

		/* TODO: a possible optimization is to merge this MU with the on going force
		 *       mu if they share the same SYNCED_BITS flag. But we unlock mu lock when
		 *       calling do_pending so we cannot merge when on gonig mu is
		 *       MD_IO_INPROG with error or SSD_FLUSH_INPROG */

		if (check_new_error_inject(dmc, MU_FORCED_RESTART_TEST)) {
			sprint("Force Restart on mu_blk idx: %d", mu_blk->idx);
		}

		mu_blk_set_flag(mu_blk, MU_FORCED | MU_FORCED_RESTART);
		sync_flag = get_current_sync_flag(mu_blk);
	} else {
		/* Every other stage except for MD_IO_INPROG we start it over */
		debug(mu);
		list_del_init(&mu_blk->node); // From DFPQ, DFIQ, SFPQ, SFIQ, MIFPQ
		debug(mu);
		// block stats updated in merge_all_to_force_mu
		sync_flag = merge_all_to_force_mu(dmc, mu_blk);
	}

	spin_lock_irqsave(&dmc->cache_spin_lock, cache_flags);
	VERIFY_WARN(cb->nr_concurrent != 0);
	VERIFY_WARN(cb->io_bitmap == BITMAP_MASK);
	VERIFY_WARN(cb->cache_state & VALID);
	VERIFY_WARN(cb->cache_state & DISKWRITEINPROG);

	/* A job is allocated in the original invalidate process. Follow the
	 * original process to increase nr_job here and dec after do_pending
	 * even if we don't actually allocate a job. */
	atomic_inc(&dmc->nr_jobs);

	atomic64_inc(&dmc->flashcache_stats.md_write_clean);
	if (cb->cache_state & DIRTY) {
		cb_state_remove_bits_update_counts(dmc, cb, DIRTY);
		VERIFY_WARN(dmc->nr_dirty > 0);
		dmc->nr_dirty--;
		VERIFY_WARN(!(cb->cache_state & SYNCED_BITS));
		cb_state_add_bits_update_counts(dmc, cb, sync_flag | FORCEDMU);
		dmc->nr_synced++;
	} else if (cb->cache_state & SYNCED_BITS) {
		cb_state_remove_bits_update_counts(dmc, cb, SYNCED_BITS);
		cb_state_add_bits_update_counts(dmc, cb, sync_flag | FORCEDMU);
	} else {
		/* not forced mu already cleared the flag when out of lock,
		 * no need to update this cache block anymore.
		 * Don't change the states. */
		if (cb->nr_queued) {
			do_pending = 1;
		} else {
			VERIFY_WARN(0);
		}
	}

	spin_unlock_irqrestore(&dmc->cache_spin_lock, cache_flags);

	cache_mod_delayed_work(&mu->dwork, 0);
	spin_unlock_irq(&mu->lock);

	if (do_pending) {
		flashcache_do_pending_force_mu(dmc, cb_idx, 0);
	}

	return;
}

/*
 * Called after any fua write after flashcache_io_callback_atomic:
 *   IN ATOMIC CONTEXT, no io error. cb has partial IO range.
 */
void syno_mu_queue_no_flush_fua_update(struct cache_c *dmc, struct kcached_job *job)
{
	unsigned long flags = 0;
	unsigned long md_idx = INDEX_TO_MD_BLOCK(dmc, job->index);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = &mu->mu_blocks[MU_BLK_TYPE_NOFLUSH][md_idx];

	sdbg(DF_FUA, "Get FUA Request, CB: %d sector: %llu", job->index, (u64)bio_bi_sector(job->bio));

	spin_lock_irqsave(&mu->lock, flags);

	VERIFY_WARN(bio_has_fua_flags(job->bio));

	list_add_tail(&job->list, &mu->fua_job_list);

	/* start new md update */
	if (!mu_blk_flagged_any(mu_blk, MU_INPROG)) {
		mu_blk_set_flag(mu_blk, (MU_INPROG | MU_FUA));

		push_mu_blk_to_md_io_pending(mu, mu_blk);

		mu->inprog_blk_num[MU_BLK_TYPE_NOFLUSH]++;

		goto end_unlock;
	}

	/* merge with pending udpate */
	if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
		/* for no flush mu, has pending means MU_MD_IO_INPROG */
		VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_MD_IO_INPROG));

		/* Mark pending as FUA, so it'll be handled immediately */
		mu_blk_set_flag(mu_blk, MU_PENDING_REQ_FUA);

		goto end_unlock;
	}

	/* merge with current update or put to pending */
	if (mu_blk_flagged_any(mu_blk, MU_MD_IO_PENDING)) {
		/* merge into md io pending */
		mu_blk_set_flag(mu_blk, MU_FUA);
		set_job_fua_inprog(dmc, mu_blk);

		/* Move node to head */
		list_del(&mu_blk->node); // from MIPQ
		list_add(&mu_blk->node, &mu->md_io_pending_queue);

	} else {
		/* queue to pending mu */
		VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_MD_IO_INPROG));
		sdbg(DF_FUA, "Put FUA mu to pending cb idx: %d", job->index);

		mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ | MU_PENDING_REQ_FUA);
		mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH]++;
	}

end_unlock:
	/* As soon as we release the lock the job COULD be freed. */
	flashcache_check_record_io_latency(LAT_MD_WRITE_REG, &job->io_lat);
	spin_unlock_irqrestore(&mu->lock, flags);

	/* FUA requires immediate handling */
	cache_mod_delayed_work(&mu->dwork, 0);

	return;
}

/*
 * Called after write dirty range change in flashcache_io_callback_atomic:
 *   IN ATOMIC CONTEXT, no io error. cb has partial IO range.
 */
void syno_mu_queue_no_flush_update(struct cache_c *dmc, struct kcached_job *job)
{
	unsigned long flags = 0;
	unsigned long md_idx = INDEX_TO_MD_BLOCK(dmc, job->index);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = &mu->mu_blocks[MU_BLK_TYPE_NOFLUSH][md_idx];

	spin_lock_irqsave(&mu->lock, flags);

	/* start new md update */
	if (mu_blk_flagged_any(mu_blk, MU_INPROG)) {
		/* merge with pending udpate */
		if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
			// merge into pending
			goto end_unlock;
		} else {
			/* merge with current update or put to pending */
			if (mu_blk_flagged_any(mu_blk, MU_MD_IO_PENDING)) {
				/* merge into md io pending */
			} else {
				mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ);
				mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH]++;
			}
		}
	} else {
		mu_blk_set_flag(mu_blk, MU_INPROG);
		push_mu_blk_to_md_io_pending(mu, mu_blk);

		mu->inprog_blk_num[MU_BLK_TYPE_NOFLUSH]++;
		goto end_unlock;
	}

end_unlock:
	spin_unlock_irqrestore(&mu->lock, flags);

	flashcache_check_record_io_latency(LAT_MD_WRITE_REG, &job->io_lat);

	return;
}

/*
 * Handle MU update (e.g. send to DFPQ) and cache block information update after quick flush writeback
 *
 *   NOT in atomic context, no io error.
 *   cb in DISKWRITEINPROG & DIRTY state, has all io range
 *   DISKWRITEINPROG and io range removed, DIRTY -> SYNCED_BITS in function
 */
void syno_mu_queue_flush_update(struct cache_c *dmc, int cb_idx)
{
	int sync_flag = 0;
	int requeue_forced = 0;
	unsigned long flags = 0;
	unsigned long md_idx = INDEX_TO_MD_BLOCK(dmc, cb_idx);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = &mu->mu_blocks[MU_BLK_TYPE_FLUSH][md_idx];
	struct cacheblock *cb = &dmc->cache[cb_idx];

	spin_lock_irq(&mu->lock);

	/* start new md update */
	if (!mu_blk_flagged_any(mu_blk, MU_INPROG)) {
		mu_blk_set_flag(mu_blk, (MU_INPROG | MU_DISK_FLUSH_PENDING | MU_FLUSH_1));
		debug(mu);
		list_add_tail(&mu_blk->node, &mu->disk_flush.pending_queue);
		debug(mu);
		sync_flag = SYNCED1;

		mu->inprog_blk_num[MU_BLK_TYPE_FLUSH]++;
		goto end_unlock;
	}

	/* merge with pending udpate */
	if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
		// merge into pending
		sync_flag = get_pending_sync_flag(mu_blk);
		goto end_unlock;
	}

	/* merge with current update or put to pending */
	if (mu_blk_flagged_any(mu_blk, MU_DISK_FLUSH_PENDING)) {
		/* merge into disk flush pending queue */
		sync_flag = get_current_sync_flag(mu_blk);
	} else {
		mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ);
		sync_flag = get_pending_sync_flag(mu_blk);
		mu->pending_blk_num[MU_BLK_TYPE_FLUSH]++;
	}

end_unlock:
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if (cb->nr_queued) {
		/* someone queued when we are out of cache_spin_lock.
		 * need a forced md update. No need to cancel this flush one */
		requeue_forced = 1;
	} else {
		/* Verify states */
		VERIFY_WARN(dmc->nr_dirty > 0);
		VERIFY_WARN(cb->cache_state & VALID);
		if (!(cb->cache_state & DIRTY)) {
			serr("cb %d queue flush mu but not DIRTY, state: %x",
				cb_idx, cb->cache_state);
			VERIFY_WARN(0);
		}

		/* Unset dirty range when issuing md update
		 * Only unset dirty range if the cb block is still SYNCED
		 * If any new writes change cb state to DIRTY, we consider WB
		 * interrupted and keep the dirty range */
		atomic64_inc(&dmc->flashcache_stats.md_write_clean);
		cb_state_remove_bits_update_counts(dmc, cb, DIRTY);
		dmc->nr_dirty--;
		dmc->nr_synced++;
		cb_state_add_bits_update_counts(dmc, cb, sync_flag);
		VERIFY_WARN(!(cb->cache_state & FORCEDMU));
		VERIFY_WARN(cb->cache_state & SYNCED_BITS);
		unset_partial_cb_writeback_attrs(dmc, cb, 1);
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

	spin_unlock_irq(&mu->lock);

	if (requeue_forced) {
		syno_mu_queue_force_update(dmc, cb_idx);
	}

	return;
}

static void set_pending_before_flush(struct list_head *head)
{
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;

	list_for_each(pos, head) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
			sdbg(DF_NEW_MU, "Set pending before flush blk: %d", mu_blk->idx);
			mu_blk_set_flag(mu_blk, MU_PENDING_BEFORE_FLUSH);
		}
	}
}

static inline void clear_force_no_flush_pending_error_inject(struct cache_c *dmc)
{
	if (check_new_error_inject(dmc, FORCE_NO_FLUSH_PENDING)) {
		serr("clear force no flush pending");
		dmc->sysctl_new_error_inject = 0;
	}
}

/* Under mu lock */
static void __start_ext_flush(new_mu_t *mu)
{
	struct cache_c *dmc = container_of(mu, struct cache_c, new_mu);
	ext_flush_t *ext_flush = &mu->ext_flush;

	ext_flush->inprog = 1;

	VERIFY_WARN(list_empty(&ext_flush->flush_bios_handling));
	VERIFY_WARN(!list_empty(&ext_flush->flush_bios_pending));
	VERIFY_WARN(list_empty(&ext_flush->md_io_inprog_queue));
	VERIFY_WARN(list_empty(&ext_flush->md_io_pending_queue));

	list_splice_init(&ext_flush->flush_bios_pending, &ext_flush->flush_bios_handling);

	atomic_sub(mu->md_io_pending_queue_size, &dmc->dmcg->md_io_pending_no_flush);
	mu->md_io_pending_queue_size = 0;

	if (dmc->bypass_cache) {
		/* if cache in bypass mode, no need to send ssd flush */
		ext_flush->ssd_flush_stage = FLUSH_DONE;
	} else if (list_empty(&mu->md_io_inprog_queue) && list_empty(&mu->md_io_pending_queue)) {
		mu->ssd_flush.forced = 1;
		ext_flush->ssd_flush_stage = FLUSH_REQUESTED;
	} else {
		list_splice_init(&mu->md_io_inprog_queue, &ext_flush->md_io_inprog_queue);
		list_splice_init(&mu->md_io_pending_queue, &ext_flush->md_io_pending_queue);
		set_pending_before_flush(&ext_flush->md_io_inprog_queue);
		clear_force_no_flush_pending_error_inject(dmc);
		cache_schedule_work(&ext_flush->send_io_work);
	}

	/* Disk Flush don't have to wait for anything */
	ext_flush->disk_flush_stage = FLUSH_REQUESTED;
	mu->disk_flush.forced = 1;

	cache_mod_delayed_work(&mu->dwork, 0);
}

static inline ext_flush_bio_node_t* alloc_ext_flush_bio_list(struct cache_c *dmc)
{
	if (check_new_error_inject(dmc, EXT_FLUSH_BIO_LIST_ALLOC_FAIL)) {
		sprint("Simulate external flush bio list allocation fail");
		dmc->sysctl_new_error_inject = 0;
		return NULL;
	}

	return kzalloc(sizeof(ext_flush_bio_node_t), GFP_NOIO);
}

/* NOT atomic context */
void syno_mu_start_ext_flush(struct cache_c *dmc, struct bio *flush_bio,
	unsigned long start_jiffy)
{
	new_mu_t *mu = &dmc->new_mu;
	ext_flush_t *ext_flush = &mu->ext_flush;
	ext_flush_bio_node_t *flush_bio_node = NULL;

	sdbg(DF_NEW_MU, "Get external flush");

	flush_bio_node = alloc_ext_flush_bio_list(dmc);
	if (NULL == flush_bio_node) {
		serr("Failed to allocate memory for flush bio");
		bio_endio_wrapper(flush_bio, -ENOMEM);
		handle_master_io_finish(dmc);
		return;
	}
	flush_bio_node->bio = flush_bio;
	flashcache_init_io_latency(&flush_bio_node->io_lat,
		dmc->sysctl_syno_latency_diagnose, TYPE_DISK, start_jiffy, 1);

	spin_lock_irq(&mu->lock);
	list_add_tail(&flush_bio_node->node, &ext_flush->flush_bios_pending);
	if (ext_flush->inprog) {
		sdbg(DF_NEW_MU, "External flush queued to pending");
		spin_unlock_irq(&mu->lock);
		return;
	}

	sdbg(DF_NEW_MU, "Start new external flush");
	__start_ext_flush(mu);
	spin_unlock_irq(&mu->lock);
}

/* under lock */
void syno_mu_finish_ext_flush(new_mu_t *mu)
{
	struct cache_c *dmc = container_of(mu, struct cache_c, new_mu);
	ext_flush_t *ext_flush = &mu->ext_flush;
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	ext_flush_bio_node_t *flush_bio_node = NULL;

	VERIFY_WARN(ext_flush->inprog);
	VERIFY_WARN(ext_flush->disk_flush_stage == FLUSH_DONE);
	VERIFY_WARN(ext_flush->ssd_flush_stage == FLUSH_DONE);
	VERIFY_WARN(list_empty(&ext_flush->md_io_inprog_queue));
	VERIFY_WARN(list_empty(&ext_flush->md_io_pending_queue));

	list_for_each_safe(pos, tmp, &ext_flush->flush_bios_handling) {
		flush_bio_node = container_of(pos, ext_flush_bio_node_t, node);

		list_del(pos); // From ext flush BHQ

		flashcache_check_process_io_latency(dmc, &flush_bio_node->io_lat, LAT_FLUSH_BIO);

		bio_endio_wrapper(flush_bio_node->bio, ext_flush->error);
		handle_master_io_finish(dmc);
		kfree(flush_bio_node);
	}

	VERIFY_WARN(list_empty(&ext_flush->flush_bios_handling));

	ext_flush->disk_flush_stage = FLUSH_WAITING;
	ext_flush->ssd_flush_stage = FLUSH_WAITING;
	ext_flush->error = 0;

	if (!list_empty(&ext_flush->flush_bios_pending)) {
		__start_ext_flush(mu);
	} else {
		ext_flush->inprog = 0;
	}

}

static inline long next_flush_time_up(dev_flush_t *flush, int itvl_sec)
{
	return time_after(jiffies, flush->last_flush_jif + itvl_sec * HZ);
}

static inline int need_disk_flush(new_mu_t *mu)
{
	return (!mu->disk_flush.inprog && ((next_flush_time_up(&mu->disk_flush, mu->flush_itvl_sec)
		&& !list_empty(&mu->disk_flush.pending_queue)) || mu->disk_flush.forced));
}

static inline int need_ssd_flush(new_mu_t *mu)
{
	return (!mu->ssd_flush.inprog && ((next_flush_time_up(&mu->ssd_flush, mu->flush_itvl_sec)
		&& !list_empty(&mu->ssd_flush.pending_queue)) || mu->ssd_flush.forced));
}

void syno_mu_do_md_update(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, new_mu.dwork.work);
	new_mu_t *mu = &dmc->new_mu;

	spin_lock_irq(&mu->lock);
	if (need_disk_flush(mu)) {
		syno_mu_send_disk_flush(dmc);
		spin_lock_irq(&mu->lock);
	}

	if (need_ssd_flush(mu)) {
		syno_mu_send_ssd_flush(dmc);
	} else {
		spin_unlock_irq(&mu->lock);
	}

	syno_mu_send_md_ios(dmc);

	cache_schedule_delayed_work(&mu->dwork, mu->sysctl_mu_check_itvl_sec * HZ);
}

void syno_mu_disk_flush_callback(unsigned long error, void *context)
{
	struct cache_c *dmc = (struct cache_c *)context;
	new_mu_t *mu = &dmc->new_mu;
	ext_flush_t *ext_flush = &mu->ext_flush;
	unsigned long flags = 0;

	if (check_new_error_inject(dmc, DISK_FLUSH_ERROR)) {
		sprint("simulate disk flush error");
		dmc->sysctl_new_error_inject = 0;
		error = 1;
	}

	// XXX: will this error handling break abnormal power lost protection?
	if (error) {
		error = -EIO;
		serr("Send flush bio to disk failed");
	}
	spin_lock_irqsave(&mu->lock, flags);
	if (error && FLUSH_INPROG == ext_flush->disk_flush_stage) {
		ext_flush->error = error;
	}
	spin_unlock_irqrestore(&mu->lock, flags);

	atomic64_inc(&dmc->flashcache_stats.disk_flush_done);
	cache_schedule_work(&mu->disk_flush.complete_work);
}

static void splice_disk_flush_inprog_to_md_io_for_flush_pending(new_mu_t *mu)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	mu_blk_t *mu_blk = NULL;

	debug(mu);

	list_for_each_safe(pos, tmp, &mu->disk_flush.inprog_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		mu_blk_unset_flag(mu_blk, MU_DISK_FLUSH_INPROG);
		mu_blk_set_flag(mu_blk, MU_MD_IO_PENDING);

		list_del_init(pos); // From DFIQ

		mu_blk->tstamp = jiffies;

		if (mu_blk_flagged_any(mu_blk, MU_FORCED)) {
			list_add(pos, &mu->md_io_for_flush_pending_queue);
		} else {
			list_add_tail(pos, &mu->md_io_for_flush_pending_queue);
		}
		// no need to mod delay work, we send io immediatly after this
	}

	VERIFY_WARN(list_empty(&mu->disk_flush.inprog_queue));
	debug(mu);

	return;
}

void syno_mu_disk_flush_complete(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, new_mu.disk_flush.complete_work);
	new_mu_t *mu = &dmc->new_mu;

	sdbg(DF_NEW_MU, "disk flush complete");

	spin_lock_irq(&mu->lock);
	if (FLUSH_INPROG == mu->ext_flush.disk_flush_stage) {
		sdbg(DF_NEW_MU, "external flush disk flush done");
		mu->ext_flush.disk_flush_stage = FLUSH_DONE;
		if (FLUSH_DONE == mu->ext_flush.ssd_flush_stage) {
			syno_mu_finish_ext_flush(mu);
			sdbg(DF_NEW_MU, "Finish flush from disk flush");
		}
	}
	splice_disk_flush_inprog_to_md_io_for_flush_pending(mu);

	mu->disk_flush.inprog = 0;
	if (need_disk_flush(mu)) {
		syno_mu_send_disk_flush(dmc);
	} else {
		spin_unlock_irq(&mu->lock);
	}

	/* Forced MU need to be handled immediatly */
	syno_mu_send_md_ios(dmc);

	return;
}

static inline void splice_disk_flush_pending_to_disk_flush_inprog(new_mu_t *mu)
{
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;

	VERIFY_WARN(list_empty(&mu->disk_flush.inprog_queue));

	list_for_each(pos, &mu->disk_flush.pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);
		mu_blk_unset_flag(mu_blk, MU_DISK_FLUSH_PENDING);
		mu_blk_set_flag(mu_blk, MU_DISK_FLUSH_INPROG);
	}

	list_splice_init(&mu->disk_flush.pending_queue, &mu->disk_flush.inprog_queue);
}

/* Must in under mu spinlock. Out NOT under lock. might sleep */
void syno_mu_send_disk_flush(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;

	sdbg(DF_NEW_MU, "Sending Disk Flush");

	splice_disk_flush_pending_to_disk_flush_inprog(mu);
	mu->disk_flush.last_flush_jif = jiffies;
	mu->disk_flush.inprog = 1;
	mu->disk_flush.forced = 0;
	if (FLUSH_REQUESTED == mu->ext_flush.disk_flush_stage) {
		mu->ext_flush.disk_flush_stage = FLUSH_INPROG;
	}
	spin_unlock_irq(&mu->lock);

	atomic64_inc(&dmc->flashcache_stats.disk_flush_start);
	dm_io_async_bio_wrapper(1, &dmc->disk_region, COMP_DM_WRITE_FLUSH, mu->disk_flush.bio,
			syno_mu_disk_flush_callback, dmc, 0);
}

static void
update_cb_state_after_ssd_flush_complete(struct cache_c *dmc, mu_blk_t *mu_blk, int forced)
{
	int i = 0;
	int cb_idx = MD_BLOCK_TO_START_IDX(dmc, mu_blk->idx);
	int target_sync_flag = get_current_sync_flag(mu_blk);
	struct cacheblock *cb = NULL;

	for (i = 0; i < MD_SLOTS_PER_BLOCK(dmc) && cb_idx < dmc->size; ++i, ++cb_idx) {
		cb = &dmc->cache[cb_idx];
		if (cb->cache_state & target_sync_flag) {
			cb_state_remove_bits_update_counts(dmc, cb, target_sync_flag);
			dmc->nr_synced--;
			if (0 == dmc->nr_synced) {
				wake_up(&dmc->new_mu.remove_wqh);
			}

			if ((cb->cache_state & FORCEDMU)) {
				VERIFY_WARN(forced);
				cb_state_remove_bits_update_counts(dmc, cb, FORCEDMU);
				spin_unlock_irq(&dmc->cache_spin_lock);
				flashcache_do_pending_force_mu(dmc, cb_idx, 0);
				spin_lock_irq(&dmc->cache_spin_lock);
			}
		}
	}
}

static void
handle_pending_or_finish_after_update_cb_state(new_mu_t *mu, mu_blk_t *mu_blk, int forced)
{
	mu_blk_unset_flag(mu_blk, MU_SSD_FLUSH_INPROG);
	mu_blk_unset_flag(mu_blk, MU_FORCED);

	if (mu_blk_flagged_any(mu_blk, MU_HAS_PENDING_REQ)) {
		mu_blk_unset_flag(mu_blk, MU_HAS_PENDING_REQ);

		/* Toggle FLUSH_1/2 flag */
		mu_blk->flags ^= MU_NEED_FLUSH;

		if (mu_blk_flagged_any(mu_blk, MU_PENDING_REQ_FORCED)) {
			VERIFY_WARN(forced);
			mu_blk_unset_flag(mu_blk, MU_PENDING_REQ_FORCED);
			mu_blk_set_flag(mu_blk, MU_FORCED);
			mu->disk_flush.forced = 1;
			cache_mod_delayed_work(&mu->dwork, 0);
		}

		mu_blk_set_flag(mu_blk, MU_DISK_FLUSH_PENDING);
		debug(mu);
		list_add_tail(&mu_blk->node, &mu->disk_flush.pending_queue);
		debug(mu);
		mu->pending_blk_num[MU_BLK_TYPE_FLUSH]--;
	} else {
		/* no more pending md update */
		mu_blk_unset_flag(mu_blk, MU_INPROG);
		mu_blk_unset_flag(mu_blk, MU_NEED_FLUSH);

		mu->inprog_blk_num[MU_BLK_TYPE_FLUSH]--;
	}
}

/* In/Out not under lock */
static void handle_ssd_flush_complete_for_each_mu(struct cache_c *dmc)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = NULL;
	unsigned long flags = 0;
	int forced = 0;
	LIST_HEAD(force_queue);

	spin_lock_irq(&mu->lock);

	list_for_each_safe(pos, tmp, &mu->ssd_flush.inprog_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);
		forced = mu_blk_flagged_any(mu_blk, MU_FORCED);

		/* Need to call do pending out side spinlock, handle seperatly */
		if (forced) {
			debug(mu);
			list_del_init(pos); // from SFIQ
			debug(mu);
			list_add(pos, &force_queue);
			debug(mu);
			continue;
		}

		VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));

		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		update_cb_state_after_ssd_flush_complete(dmc, mu_blk, forced);
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);

		debug(mu);
		list_del_init(pos); // from SFIQ
		debug(mu);

		handle_pending_or_finish_after_update_cb_state(mu, mu_blk, forced);
	}

	spin_unlock_irq(&mu->lock);

	/* All mu handled below are forced */
	forced = 1;

	/* We have FORCED flag set already, safe to drop the mu lock since we won't modify it */
	spin_lock_irq(&dmc->cache_spin_lock);
	list_for_each(pos, &force_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_FORCED));

		update_cb_state_after_ssd_flush_complete(dmc, mu_blk, forced);
	}
	spin_unlock_irq(&dmc->cache_spin_lock);

	spin_lock_irq(&mu->lock);
	list_for_each_safe(pos, tmp, &force_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		debug(mu);
		list_del_init(pos); // From local queue
		debug(mu);

		handle_pending_or_finish_after_update_cb_state(mu, mu_blk, forced);
	}
	spin_unlock_irq(&mu->lock);

}

void syno_mu_ssd_flush_complete(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, new_mu.ssd_flush.complete_work);
	new_mu_t *mu = &dmc->new_mu;

	sdbg(DF_NEW_MU, "ssd flush complete");

	handle_ssd_flush_complete_for_each_mu(dmc);

	spin_lock_irq(&mu->lock);

	if (FLUSH_INPROG == mu->ext_flush.ssd_flush_stage) {
		sdbg(DF_NEW_MU, "external flush ssd flush done");
		mu->ext_flush.ssd_flush_stage = FLUSH_DONE;
		if (FLUSH_DONE == mu->ext_flush.disk_flush_stage) {
			sdbg(DF_NEW_MU, "finish flush from ssd flush done");
			syno_mu_finish_ext_flush(mu);
		}
	}

	mu->ssd_flush.inprog = 0;
	if (need_ssd_flush(mu)) {
		syno_mu_send_ssd_flush(dmc);
	} else {
		spin_unlock_irq(&mu->lock);
	}

	spin_lock_irq(&mu->lock);
	if (no_md_updates_queued(mu)) {
		wake_up(&mu->remove_wqh);
	}
	spin_unlock_irq(&mu->lock);
	return;
}

void syno_mu_ssd_flush_callback(unsigned long error, void *context)
{
	struct cache_c *dmc = (struct cache_c *)context;
	new_mu_t *mu = &dmc->new_mu;
	ext_flush_t *ext_flush = &mu->ext_flush;
	unsigned long flags = 0;

	// XXX: will this error handling break abnormal power lost protection?
	sdbg(DF_NEW_MU, "ssd flush done");


	if (check_new_error_inject(dmc, SSD_FLUSH_ERROR)) {
		sprint("Simulate ssd flush error");
		dmc->sysctl_new_error_inject = 0;
		error = 1;
	}

	if (error) {
		if (dmc->bypass_cache || is_writeback_crash_safe_set_bypass(dmc)) {
			error = 0;
			serr("In bypass mode, ignore ssd flush error");
		} else {
			error = -EIO;
			serr("Send flush bio to ssd failed");
		}
	}

	spin_lock_irqsave(&mu->lock, flags);
	if (error && FLUSH_INPROG == ext_flush->ssd_flush_stage) {
		ext_flush->error = error;
	}
	spin_unlock_irqrestore(&mu->lock, flags);

	atomic64_inc(&dmc->flashcache_stats.md_flush_done);
	cache_schedule_work(&mu->ssd_flush.complete_work);
}

static inline void splice_ssd_flush_pending_to_ssd_flush_inprog(new_mu_t *mu)
{
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;

	VERIFY_WARN(list_empty(&mu->ssd_flush.inprog_queue));

	list_for_each(pos, &mu->ssd_flush.pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);
		mu_blk_unset_flag(mu_blk, MU_SSD_FLUSH_PENDING);
		// TODO: used only for debug purpose, consider remove
		mu_blk_set_flag(mu_blk, MU_SSD_FLUSH_INPROG);
	}

	list_splice_init(&mu->ssd_flush.pending_queue, &mu->ssd_flush.inprog_queue);
}

/* Must in under mu spinlock. Out NOT under lock, might sleep */
void syno_mu_send_ssd_flush(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;

	sdbg(DF_NEW_MU, "Sending SSD Flush");

	splice_ssd_flush_pending_to_ssd_flush_inprog(mu);
	mu->ssd_flush.last_flush_jif = jiffies;
	mu->ssd_flush.inprog = 1;
	mu->ssd_flush.forced = 0;
	if (FLUSH_REQUESTED == mu->ext_flush.ssd_flush_stage) {
		sdbg(DF_NEW_MU, "Sending SSD Flush for external flush");
		mu->ext_flush.ssd_flush_stage = FLUSH_INPROG;
	}
	spin_unlock_irq(&mu->lock);

	atomic64_inc(&dmc->flashcache_stats.md_flush_start);
	dm_io_async_bio_wrapper(1, &dmc->cache_region, COMP_DM_WRITE_FLUSH,
		mu->ssd_flush.bio, syno_mu_ssd_flush_callback, dmc, 0);
	sdbg(DF_NEW_MU, "Sent SSD Flush");
}

static inline mu_blk_t *get_sibling_mu_blk(new_mu_t *mu, mu_blk_t *mu_blk)
{
	int sib_flush = (mu_blk->flags & MU_NEED_FLUSH) ? MU_BLK_TYPE_NOFLUSH : MU_BLK_TYPE_FLUSH;
	return mu->mu_blocks[sib_flush] + mu_blk->idx;
}

static inline int sib_mu_blk_io_inprog(new_mu_t *mu, mu_blk_t *mu_blk)
{
	return mu_blk_flagged_any(get_sibling_mu_blk(mu, mu_blk), MU_MD_IO_INPROG);
}

static inline int dmcg_need_throtl_for_md_io(dmc_group_t *dmcg)
{
	return atomic_read(&dmcg->md_io_pending_no_flush) > dmcg->md_io_throtl_thres;
}

static inline int md_io_above_per_cache_throtl_thres(struct cache_c *dmc)
{
	/* No need to lock to get md_io_pending_queue_size, incorrect value won't hurt */
	return dmc->new_mu.md_io_pending_queue_size > dmc->dmcg->md_io_throtl_thres / dmc->dmcg->num_dmc;
}

static inline int in_throtl_for_md_io(struct cache_c *dmc)
{
	return dmcg_need_throtl_for_md_io(dmc->dmcg) &&
		md_io_above_per_cache_throtl_thres(dmc);
}

static inline int mu_blk_expired(new_mu_t *mu, mu_blk_t *mu_blk)
{
	return time_after(jiffies, mu_blk->tstamp + mu->sysctl_mu_delay_sec * HZ);
}

static mu_blk_t *first_valid_no_flush_mu_for_md_io(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;

	list_for_each(pos, &mu->md_io_pending_queue) {
		mu_blk = container_of(pos, mu_blk_t, node);

		if (!in_throtl_for_md_io(dmc) && !mu_blk_expired(mu, mu_blk) &&
			!mu_blk_flagged_any(mu_blk, MU_FUA)) {
			return NULL;
		}

		if (sib_mu_blk_io_inprog(mu, mu_blk)) {
			/* sibling mu blk io in progress */
			continue;
		}

		return mu_blk;
	}

	return NULL;
}

static mu_blk_t *first_valid_flush_mu_for_md_io(new_mu_t *mu)
{
	struct list_head *pos = NULL;
	mu_blk_t *mu_blk = NULL;

	list_for_each(pos, &mu->md_io_for_flush_pending_queue) {

		mu_blk = container_of(pos, mu_blk_t, node);
		/* Forced mu don't care about jiffies */
		if (!mu_blk_flagged_any(mu_blk, MU_FORCED) && !mu_blk_expired(mu, mu_blk)) {
			return NULL;
		}

		if (sib_mu_blk_io_inprog(mu, mu_blk)) {
			continue;
		}

		return mu_blk;
	}

	return NULL;
}

static inline int can_send_more_md_io(new_mu_t *mu)
{
	return mu->md_io_inprog_normal < mu->sysctl_mu_ios_total;
}

/* Set mu blk state for md io and handle combo */
static mu_blk_t* setup_and_get_mu_blk_to_update(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *sib_mu_blk = get_sibling_mu_blk(mu, mu_blk);
	mu_blk_t *ret = mu_blk;

	mu_blk_set_flag(mu_blk, MU_MD_IO_INPROG);
	mu_blk_unset_flag(mu_blk, MU_MD_IO_PENDING);

	VERIFY_WARN(!mu_blk_flagged_any(sib_mu_blk, MU_MD_IO_INPROG));

	/* handle combo io */
	if (mu_blk_flagged_any(sib_mu_blk, MU_MD_IO_PENDING)) {

		VERIFY_WARN(mu_blk_flagged_any(sib_mu_blk, MU_INPROG));

		if (check_new_error_inject(dmc, MD_IO_COMBO_TEST)) {
			sprint("MD IO combo sent idx: %d", mu_blk->idx);
		}

		debug(mu);
		list_del_init(&sib_mu_blk->node); // From MIPQ, MIFPQ
		if (!mu_blk_flagged_any(sib_mu_blk, MU_NEED_FLUSH)) {
			mu->md_io_pending_queue_size--;
			atomic_dec(&dmc->dmcg->md_io_pending_no_flush);
		}
		debug(mu);
		mu_blk_set_flag(mu_blk, MU_MD_IO_COMBO);
		mu_blk_set_flag(sib_mu_blk, MU_MD_IO_COMBO);
		mu_blk_set_flag(sib_mu_blk, MU_MD_IO_INPROG);
		mu_blk_unset_flag(sib_mu_blk, MU_MD_IO_PENDING);

		/* We must send md io with need flush mu for combo mu for 2 reasons
		 * 1. we need FLUSH1/2 flag to send correct metadata for SYNCED_BITS cbs
		 * 2. We handle do pending for forced mu on error before handle COMBO. */
		if (mu_blk_flagged_any(sib_mu_blk, MU_NEED_FLUSH)) {
			ret = sib_mu_blk;
		}
	}

	/* The flag in unset when IO sent */
	if (mu_blk_flagged_any(sib_mu_blk, MU_FUA) || mu_blk_flagged_any(mu_blk, MU_FUA)) {
		mu_blk_set_flag(ret, MU_MD_IO_NEED_FUA);
	}

	return ret;
}

static void requeue_mu_io(new_mu_t *mu, mu_blk_t *mu_blk)
{
	if (mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH)) {
		debug(mu);
		mu_blk->tstamp = jiffies;
		mu_blk_set_flag(mu_blk, MU_MD_IO_PENDING);
		list_add_tail(&mu_blk->node, &mu->md_io_for_flush_pending_queue);
		debug(mu);
	} else {
		// merge pending io
		if (mu_blk_test_clear_flag_any(mu_blk, MU_HAS_PENDING_REQ)) {
			mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH]--;
		}

		if (mu_blk_test_clear_flag_any(mu_blk, MU_PENDING_REQ_FUA)) {
			mu_blk_set_flag(mu_blk, MU_FUA);
		}

		push_mu_blk_to_md_io_pending(mu, mu_blk);
	}
}

void handle_md_io_complete_for_mu(new_mu_t *mu, mu_blk_t *mu_blk, int err)
{
	if (err) {
		requeue_mu_io(mu, mu_blk);
		return;
	}

	if (mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH)) {
		mu_blk_set_flag(mu_blk, MU_SSD_FLUSH_PENDING);
		debug(mu);
		list_add(&mu_blk->node, &mu->ssd_flush.pending_queue);
		debug(mu);
	} else {
		if (mu_blk_test_clear_flag_any(mu_blk, MU_HAS_PENDING_REQ)) {
			if (mu_blk_test_clear_flag_any(mu_blk, MU_PENDING_REQ_FUA)) {
				sdbg(DF_FUA, "Handle FUA Pending mu idx: %d", mu_blk->idx);
				mu_blk_set_flag(mu_blk, MU_FUA);
			}
			push_mu_blk_to_md_io_pending(mu, mu_blk);

			mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH]--;
		} else {
			mu_blk_unset_flag(mu_blk, MU_INPROG);
			mu->inprog_blk_num[MU_BLK_TYPE_NOFLUSH]--;
		}
	}
}

// endio / handle pending
static void handle_fua_ios(struct cache_c *dmc, mu_blk_t *mu_blk, int err)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct kcached_job *job = NULL;

	sdbg(DF_FUA, "Finish FUA mu idx %d", mu_blk->idx);

	list_for_each_safe(pos, tmp, &dmc->new_mu.fua_job_list) {
		job = container_of(pos, struct kcached_job, list);

		if (job_in_mu_blk(dmc, job, mu_blk) && (job->flag & JOB_FUA_MU_INPROG)) {
			list_del(&job->list);
			job->flag &= ~(JOB_FUA_MU_INPROG);
			job->error = err;

			flashcache_check_record_io_latency(LAT_MD_WRITE_FUA, &job->io_lat);

			flashcache_bio_endio(job->bio, err, dmc, job->io_start_time, &job->io_lat);
			job->bio = NULL;

			sdbg(DF_FUA, "Finish FUA Request, CB: %d", job->index);
			flashcache_handle_job_finish(dmc, job);
		}
	}

}

static void handle_md_io_complete_for_fua_mu(new_mu_t *mu, mu_blk_t *mu_blk, int err)
{
	struct cache_c *dmc = container_of(mu, struct cache_c, new_mu);

	VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_FUA));
	VERIFY_WARN(!mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));

	handle_fua_ios(dmc, mu_blk, err);

	mu_blk_unset_flag(mu_blk, MU_FUA);

	handle_md_io_complete_for_mu(mu, mu_blk, err);
}

void handle_md_io_complete_for_forced_mu(new_mu_t *mu, mu_blk_t *mu_blk, int err)
{
	struct cache_c *dmc = container_of(mu, struct cache_c, new_mu);

	VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));

	if (mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART)) {
		// TODO: stats needed
		merge_all_to_force_mu(dmc, mu_blk);
		return;
	}

	if (err) {
		if (mu_blk_flagged_any(mu_blk, MU_PENDING_REQ_FORCED)) {
			/* Do pending for this forced IO handled befor this.
			 * It's like we have a new forced mu and an on going
			 * normal mu. Merge them */
			merge_all_to_force_mu(dmc, mu_blk);
		} else {
			/* forced handled and no force pending, remove forced flag then requeue */
			mu_blk_unset_flag(mu_blk, MU_FORCED);
			requeue_mu_io(mu, mu_blk);
		}
	} else {

		mu->ssd_flush.forced = 1;
		mu_blk_set_flag(mu_blk, MU_SSD_FLUSH_PENDING);
		debug(mu);
		list_add(&mu_blk->node, &mu->ssd_flush.pending_queue);
		debug(mu);

		cache_mod_delayed_work(&mu->dwork, 0);
	}
}

static inline
void handle_mu_io_complete_for_single_mu(new_mu_t *mu, mu_blk_t *mu_blk, int err)
{
	if (unlikely(mu_blk_flagged_any(mu_blk, MU_FORCED))) {
		handle_md_io_complete_for_forced_mu(mu, mu_blk, err);
	} else if (unlikely(mu_blk_flagged_any(mu_blk, MU_FUA))) {
		handle_md_io_complete_for_fua_mu(mu, mu_blk, err);
	} else {
		handle_md_io_complete_for_mu(mu, mu_blk, err);
	}
}

void handle_md_io_complete(struct cache_c *dmc, mu_blk_t *mu_blk, int err)
{
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *sib_mu_blk = NULL;
	struct list_head *pos = NULL;

	if (err) {
		list_for_each(pos, &mu->ext_flush.md_io_inprog_queue) {
			if (pos == &mu_blk->node) {
				mu->ext_flush.error = err;
			}
		}
	}
	list_del(&mu_blk->node); // for ext flush MIIQ or MIIQ

	switch (mu_blk->flags & MU_SENT_BY) {
	case MU_SENT_BY_EXT_FLUSH:
		mu->md_io_inprog_ext_flush--;
		mu->sent_md_io_ext_flush_cnt++;
		break;
	case MU_SENT_BY_NORMAL:
		mu->md_io_inprog_normal--;
		mu->sent_md_io_normal_cnt++;
		break;
	case MU_SENT_BY_THROTL:
		mu->md_io_inprog_throtl--;
		mu->sent_md_io_throtl_cnt++;
		break;
	case MU_SENT_BY_FORCE:
		mu->md_io_inprog_force--;
		mu->sent_md_io_force_cnt++;
		break;
	case MU_SENT_BY_FUA:
		mu->md_io_inprog_fua--;
		mu->sent_md_io_fua_cnt++;
		break;
	default:
		serr("Unexpected MU_SENT_BY flag: %x", mu_blk->flags);
		break;
	}

	mu_blk_unset_flag(mu_blk, MU_SENT_BY);
	mu_blk_unset_flag(mu_blk, MU_MD_IO_INPROG);
	if (mu_blk_flagged_any(mu_blk, MU_MD_IO_COMBO)) {
		sib_mu_blk = get_sibling_mu_blk(mu, mu_blk);
		mu_blk_unset_flag(mu_blk, MU_MD_IO_COMBO);
		mu_blk_unset_flag(sib_mu_blk, MU_MD_IO_COMBO | MU_MD_IO_INPROG);
	}

	handle_mu_io_complete_for_single_mu(mu, mu_blk, err);

	if (sib_mu_blk) {
		if (check_new_error_inject(dmc, MD_IO_COMBO_TEST)) {
			dmc->sysctl_new_error_inject = 0;
			sprint("md io combo test finishes, idx %d", mu_blk->idx);
		}
		handle_mu_io_complete_for_single_mu(mu, sib_mu_blk, err);
	}
}

/* In under cache_spin_lock */
void handle_force_mu_io_error_do_pending(struct cache_c *dmc, mu_blk_t *mu_blk, int error)
{
	int i = 0;
	int cb_idx = MD_BLOCK_TO_START_IDX(dmc, mu_blk->idx);
	int target_sync_flag = get_current_sync_flag(mu_blk);
	struct cacheblock *cb = NULL;

	VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));
	VERIFY_WARN(!mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART));

	for (i = 0; i < MD_SLOTS_PER_BLOCK(dmc) && cb_idx < dmc->size; ++i, ++cb_idx) {
		cb = &dmc->cache[cb_idx];
		if ((cb->cache_state & target_sync_flag) && (cb->cache_state & FORCEDMU)) {
			/* force md update failed, but we still need md update for this
			 * so leave the SYNCED_BITS flag */
			cb_state_remove_bits_update_counts(dmc, cb, FORCEDMU);
			spin_unlock_irq(&dmc->cache_spin_lock);
			flashcache_do_pending_force_mu(dmc, cb_idx, error);
			spin_lock_irq(&dmc->cache_spin_lock);
		}
	}

}

void handle_delayed_jobs_for_force_mu_io_error(struct cache_c *dmc, struct kcached_job *delayed_jobs)
{
	struct kcached_job *job = delayed_jobs;
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = NULL;

	if (NULL == delayed_jobs) {
		return;;
	}

	spin_lock_irq(&dmc->cache_spin_lock);
	while (job) {
		VERIFY_WARN(job->error);
		mu_blk = job->ctx;
		handle_force_mu_io_error_do_pending(dmc, mu_blk, job->error);
		job = job->next;
	}
	spin_unlock_irq(&dmc->cache_spin_lock);

	spin_lock_irq(&mu->lock);
	job = delayed_jobs;
	while (job) {
		mu_blk = job->ctx;
		handle_md_io_complete(dmc, mu_blk, job->error);

		job = job->next;
	}
	spin_unlock_irq(&mu->lock);
}

void free_jobs_for_md_io(struct kcached_job *job_list)
{
	struct kcached_job *job = NULL;

	while (job_list) {
		job = job_list;
		job_list = job_list->next;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
		flashcache_free_md_bio(job);
#else
		flashcache_free_md_sector(job);
#endif
		flashcache_free_cache_job(job);
	}
}

/* under MU lock */
static void handle_ext_flush_after_md_io(new_mu_t *mu)
{
	ext_flush_t *ext_flush = &mu->ext_flush;

	if (ext_flush->inprog) {
		if (!list_empty(&ext_flush->md_io_pending_queue)) {
			/* io left in pending queue due to sibling inprog */
			cache_schedule_work(&ext_flush->send_io_work);
		} else if (list_empty(&ext_flush->md_io_inprog_queue) &&
			FLUSH_WAITING == ext_flush->ssd_flush_stage) {

			sdbg(DF_NEW_MU, "All md io done for ext flush, request ssd flush");
			ext_flush->ssd_flush_stage = FLUSH_REQUESTED;
			mu->ssd_flush.forced = 1;
			cache_mod_delayed_work(&mu->dwork, 0);
		}
	}
}

static void
simulate_md_io_complete_errors(struct cache_c *dmc, mu_blk_t *mu_blk, struct kcached_job *job)
{
	if (check_new_error_inject(dmc, FORCE_MD_IO_ERROR)) {
		if (mu_blk_flagged_any(mu_blk, MU_FORCED) &&
			!mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART)) {
			sprint("Simulate force md io error");
			dmc->sysctl_new_error_inject = 0;
			job->error = -EIO;
		}
	}

	if (check_new_error_inject(dmc, MD_IO_ERROR)) {
		if (!mu_blk_flagged_any(mu_blk, MU_FORCED)) {
			sprint("Simulate md io error");
			dmc->sysctl_new_error_inject = 0;
			job->error = -EIO;
		}
	}

	if (check_new_error_inject(dmc, WRITE_FUA_ERROR)) {
		if (mu_blk_flagged_any(mu_blk, MU_FUA)) {
			sprint("Simulate fua error");
			dmc->sysctl_new_error_inject = 0;
			job->error = -EIO;
		}
	}
}

void syno_mu_md_io_complete(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, new_mu.md_io_complete_work);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = NULL;
	struct kcached_job *jobs = NULL;
	struct kcached_job *delayed_jobs = NULL;
	struct kcached_job *job_itr = NULL;
	struct kcached_job *prev_job = NULL;
	struct kcached_job *next_job = NULL;
	int job_cnt = 0;

	spin_lock_irq(&mu->lock);
	jobs = mu->md_io_complete_job_queue;
	mu->md_io_complete_job_queue = NULL;

	job_itr = jobs;
	prev_job = NULL;
	while (job_itr) {
		job_cnt++;
		next_job = job_itr->next;

		mu_blk = job_itr->ctx;

		simulate_md_io_complete_errors(dmc, mu_blk, job_itr);

		if (job_itr->error && mu_blk_flagged_any(mu_blk, MU_FORCED) &&
			!mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART)) {
			/* Need to call do pending, handle outside the lock later */
			if (NULL == prev_job) {
				jobs = job_itr->next;
			} else {
				prev_job->next = job_itr->next;
			}
			job_itr->next = delayed_jobs;
			delayed_jobs = job_itr;
		} else {
			prev_job = job_itr;
			handle_md_io_complete(dmc, mu_blk, job_itr->error);
		}

		job_itr = next_job;
	}
	spin_unlock_irq(&mu->lock);

	// do pending for forced mu on error and process mu_blks
	handle_delayed_jobs_for_force_mu_io_error(dmc, delayed_jobs);

	spin_lock_irq(&mu->lock);
	handle_ext_flush_after_md_io(mu);
	spin_unlock_irq(&mu->lock);

	free_jobs_for_md_io(jobs);
	free_jobs_for_md_io(delayed_jobs);

	if (atomic_sub_and_test(job_cnt, &dmc->nr_jobs)) {
		wake_up(&dmc->sync_wqh);
	}

	syno_mu_send_md_ios(dmc);

	spin_lock_irq(&mu->lock);
	if (no_md_updates_queued(mu)) {
		wake_up(&mu->remove_wqh);
	}
	spin_unlock_irq(&mu->lock);
}

void syno_mu_md_io_callback(unsigned long error, void *ctx)
{
	struct kcached_job *job = (struct kcached_job *)ctx;
	struct cache_c *dmc = job->dmc;
	new_mu_t *mu = &job->dmc->new_mu;
	unsigned long flags = 0;
	static int remove_flush_printed = 0;

	atomic_inc(&mu->md_io_callback_cnt);

	//TODO: deal with latency diagnose later

	if (unlikely(error)) {
		if (dmc->bypass_cache || is_writeback_crash_safe_set_bypass(dmc)) {
			/* We do not expect there'll be md io queueing when in bypass mode
			   md io queueing implies dirty blocks, since all dirty should
			   already been written back in bypass mode, all no flush mu should
			   also be written with the corresponding need flush mu */
			serr("md io error in bypass mode, ignore");
		} else if (mu->remove_flush) {
			if (!remove_flush_printed) {
				serr("Forced Remove detected on metadata io error, ignore error!");
				remove_flush_printed = 1;
			}
		} else {
			job->error = -EIO;
			if (job->dmc->limits.md_io_error > 0) {
				job->dmc->limits.md_io_error--;
				serr("MD IO Error");
			}
		}
	}

	spin_lock_irqsave(&mu->lock, flags);
	job->next = mu->md_io_complete_job_queue;
	mu->md_io_complete_job_queue = job;
	spin_unlock_irqrestore(&mu->lock, flags);
	cache_schedule_work(&mu->md_io_complete_work);
}

static inline int job_alloc_md_io_data(struct kcached_job *job)
{

	if (check_new_error_inject(job->dmc, MD_BIO_ALLOC_FAIL)) {
		sprint("Simulate md bio alloc fail");
		job->dmc->sysctl_new_error_inject = 0;
		return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return flashcache_alloc_md_bio(job);
#else
	return flashcache_alloc_md_sector(job);
#endif
}

static inline void handle_force_no_flush_pending_error_inject(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	if (check_new_error_inject(dmc, FORCE_NO_FLUSH_PENDING)) {
		serr("Detect force no flush pending flag, force pending idx: %d", mu_blk->idx);
		mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ);
		dmc->new_mu.pending_blk_num[MU_BLK_TYPE_NOFLUSH]++;
		while (check_new_error_inject(dmc, FORCE_NO_FLUSH_PENDING)) {
			serr("Detect force no flush pending flag, sleep 5 seconds");
			msleep(5000);
		}
		serr("Force no flush pending flag cleared, continue");
	}
}

static void syno_mu_send_md_io_from_job(struct kcached_job *job, int need_fua)
{
	int i = 0;
	int idx = 0;
	struct cache_c *dmc = job->dmc;
	mu_blk_t *mu_blk = job->ctx;
	struct flash_cacheblock *md_block = NULL;
	struct dm_io_region where = {0};
	int sync_flag = 0;
	int comp_dm_op = need_fua ? COMP_DM_WRITE_FUA : COMP_DM_WRITE;
	struct cacheblock *cb = NULL;

	if (mu_blk_flagged_any(mu_blk, MU_MD_IO_COMBO)) {
		VERIFY_WARN(mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH));
	}

	if (mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH)) {
		sync_flag = get_current_sync_flag(mu_blk);
	}

	handle_force_no_flush_pending_error_inject(dmc, mu_blk);

	atomic_inc(&dmc->new_mu.send_md_io_cnt);

	if (job_alloc_md_io_data(job)) {
		/* We have job, follow normal error handling process */
		serr("Failed to allocate md bio, mu idx: %d", mu_blk->idx);
		syno_mu_md_io_callback(1, job);
		return;
	}
	spin_lock_irq(&dmc->cache_spin_lock);

	md_block = job->md_block;
	idx = MD_BLOCK_TO_START_IDX(dmc, mu_blk->idx);

	for (i = 0; i < MD_SLOTS_PER_BLOCK(dmc) && idx < dmc->size; i++, idx++) {
		cb = &dmc->cache[idx];

		while (check_new_error_inject(dmc, MU_FORCED_RESTART_TEST) &&
			mu_blk_flagged_any(mu_blk, MU_NEED_FLUSH) && cb->cache_state & SYNCED_BITS) {
			if (mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART)) {
				dmc->sysctl_new_error_inject = 0;
				sprint("Detect Force Restart flag, continue");
				break;
			}
			sprint("Detect Force Restart Test Flag, dbn %lld valid mu_blk idx: %d",
				(unsigned long long)cb->dbn, mu_blk->idx);
			spin_unlock_irq(&dmc->cache_spin_lock);
			msleep(5000);
			spin_lock_irq(&dmc->cache_spin_lock);
		}

		md_block[i].dbn = cb->dbn;
#ifdef FLASHCACHE_DO_CHECKSUMS
		md_block[i].checksum = cb->checksum;
#endif
		md_block[i].data_bitmap = cb->data_bitmap;

		// NOTE: sync_flag will be zero for no flush mu
		if ((cb->cache_state & sync_flag) && cb->dirty_bitmap) {
			/* disk flush done, we are safe to clean dirty bitmap
			 * If we don't clean dirty bitmap, no flush mu after this
			 * might write it to disk and cause inconsistency */
			cacheblk_unset_all_dirty_range(cb);
			/* cannot clean SYNCED_BITS because it's still not applicable to be replaced */
		}
		md_block[i].dirty_bitmap = cb->dirty_bitmap;
		md_block[i].cache_state = cb->cache_state & MD_POSSIBLE_STATES;

		if (cb->cache_state & SYNCED_BITS && !(cb->cache_state & sync_flag) && cb->dirty_bitmap) {
			/* SYNCED_BITS blocks not belonged to this round and
			 * haven't issued md io (dirty_bitmap != 0) should be dirty on disk */
			md_block[i].cache_state |= DIRTY;
		}
	}

	spin_unlock_irq(&dmc->cache_spin_lock);

	where.bdev = get_cache_bdev(dmc);
	where.count = MD_SECTORS_PER_BLOCK(dmc);
	where.sector = (1 + mu_blk->idx) * MD_SECTORS_PER_BLOCK(dmc);
	atomic64_inc(&dmc->flashcache_stats.ssd_writes);
	atomic64_inc(&dmc->flashcache_stats.md_ssd_writes);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	dm_io_async_bvec(1, &where, comp_dm_op, job->md_bio, syno_mu_md_io_callback, job, 0);
#else
	dm_io_async_bvec(1, &where, comp_dm_op, &job->md_io_bvec, syno_mu_md_io_callback, job, 0);
#endif

}

static struct kcached_job*
alloc_md_io_job(struct cache_c *dmc, mu_blk_t *mu_blk)
{
	if (check_new_error_inject(dmc, MD_IO_JOB_ALLOC_FAIL)) {
		if (!mu_blk_flagged_any(mu_blk, MU_FORCED)) {
			sprint("Simulate md io job alloc fail");
			dmc->sysctl_new_error_inject = 0;
			return NULL;
		}
	}

	if (check_new_error_inject(dmc, FORCED_MD_IO_JOB_ALLOC_FAIL)) {
		if (mu_blk_flagged_any(mu_blk, MU_FORCED)) {
			sprint("Simulate forced md io job alloc fail");
			dmc->sysctl_new_error_inject = 0;
			return NULL;
		}
	}

	return new_kcached_job_md_io(dmc, mu_blk);
}

static void handle_io_inprog_delay_error_inject(struct cache_c *dmc)
{
	while (check_new_error_inject(dmc, IO_INPROG_DELAY)) {
		serr("Detect IO_INPROG_DELAY error inject, sleep 5 seconds");
		msleep(5000);
	}
}

static void send_md_ios_in_queue(struct cache_c *dmc, struct list_head *start, struct list_head *end)
{
	struct kcached_job *job = NULL;
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = NULL;

	handle_io_inprog_delay_error_inject(dmc);

	pos = start;
	while (1) {
		/* pos->next will only change AFTER pos/pos->next is submitted
		 * So it seems we don't need lock here? */
		tmp = pos->next;
		mu_blk = container_of(pos, mu_blk_t, node);
		atomic_inc(&mu->md_io_in_queue);

		job = alloc_md_io_job(dmc, mu_blk);
		if (NULL == job) {
			// TODO: stats here
			serr("Failed to allocate kcached job for md io, idx %d", mu_blk->idx);

			spin_lock_irq(&dmc->cache_spin_lock);
			/* mu should be io inprog. no one changes the state.
			 * Safe to check without lock */
			if (mu_blk_flagged_any(mu_blk, MU_FORCED) &&
				!mu_blk_flagged_any(mu_blk, MU_FORCED_RESTART)) {
				handle_force_mu_io_error_do_pending(dmc, mu_blk, -EIO);
			}
			spin_unlock_irq(&dmc->cache_spin_lock);

			spin_lock_irq(&mu->lock);
			handle_md_io_complete(dmc, mu_blk, -EIO);
			handle_ext_flush_after_md_io(mu);
			spin_unlock_irq(&mu->lock);
			if (atomic_dec_and_test(&dmc->nr_jobs)) {
				wake_up(&dmc->sync_wqh);
			}
		} else {
			syno_mu_send_md_io_from_job(job, mu_blk_test_clear_flag_any(mu_blk, MU_MD_IO_NEED_FUA));
		}

		if (pos == end) {
			break;
		} else {
			pos = tmp;
		}
	}
}

void syno_mu_send_md_ios_from_ext_flush(struct work_struct *work)
{
	struct cache_c *dmc = container_of(work, struct cache_c, new_mu.ext_flush.send_io_work);
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *mu_blk = NULL;
	struct list_head *pos1 = NULL;
	struct list_head *pos2 = NULL;
	ext_flush_t *ext_flush = &mu->ext_flush;
	LIST_HEAD(queue);

	sdbg(DF_NEW_MU, "start send md ios for ext flush");

	spin_lock_irq(&mu->lock);
	list_for_each_safe(pos1, pos2, &ext_flush->md_io_pending_queue) {
		mu_blk = container_of(pos1, mu_blk_t, node);

		if (sib_mu_blk_io_inprog(mu, mu_blk)) {
			continue;
		}

		list_del(&mu_blk->node); // from ext flush MIPQ

		mu_blk = setup_and_get_mu_blk_to_update(dmc, mu_blk);

		atomic_inc(&mu->md_io_to_queue);
		mu_blk_set_flag(mu_blk, MU_SENT_BY_EXT_FLUSH);
		mu->md_io_inprog_ext_flush++;
		atomic_inc(&dmc->nr_jobs);

		list_add_tail(&mu_blk->node, &queue);
	}

	pos1 = pos2 = NULL;
	if (!list_empty(&queue)) {
		pos1 = queue.next;
		pos2 = queue.prev;
	}
	list_splice_tail_init(&queue, &ext_flush->md_io_inprog_queue);
	spin_unlock_irq(&mu->lock);

	if (pos1) {
		send_md_ios_in_queue(dmc, pos1, pos2);
	}
}

/* When there are too many md updates we force external IO to send one MU before endio */
static void send_md_io_for_throtl_ext_io(struct cache_c *dmc) {
	mu_blk_t *mu_blk = NULL;
	new_mu_t *mu = &dmc->new_mu;

	spin_lock_irq(&mu->lock);

	mu_blk = first_valid_no_flush_mu_for_md_io(dmc);

	if (NULL == mu_blk) {
		goto out_unlock;
	}

	debug(mu);
	list_del(&mu_blk->node); // from MIPQ
	debug(mu);

	atomic_dec(&dmc->dmcg->md_io_pending_no_flush);
	mu->md_io_pending_queue_size--;

	mu_blk = setup_and_get_mu_blk_to_update(dmc, mu_blk);

	atomic_inc(&mu->md_io_to_queue);
	mu_blk_set_flag(mu_blk, MU_SENT_BY_THROTL);
	mu->md_io_inprog_throtl++;
	atomic_inc(&dmc->nr_jobs);

	debug(mu);
	list_add_tail(&mu_blk->node, &mu->md_io_inprog_queue);
	debug(mu);

out_unlock:
	spin_unlock_irq(&mu->lock);

	if (mu_blk) {
		send_md_ios_in_queue(dmc, &mu_blk->node, &mu_blk->node);
	}

	return;
}

// in/out under mu lock
static void get_force_mu_to_send(struct cache_c *dmc, struct list_head *out)
{
	new_mu_t *mu = &dmc->new_mu;
	mu_blk_t *flush_mu_blk = first_valid_flush_mu_for_md_io(mu);

	flush_mu_blk = first_valid_flush_mu_for_md_io(mu);

	while (flush_mu_blk && mu_blk_flagged_any(flush_mu_blk, MU_FORCED)) {
		debug(mu);
		list_del(&flush_mu_blk->node); // From MIFPQ
		debug(mu);

		flush_mu_blk = setup_and_get_mu_blk_to_update(dmc, flush_mu_blk);

		atomic_inc(&mu->md_io_to_queue);
		mu_blk_set_flag(flush_mu_blk, MU_SENT_BY_FORCE);
		mu->md_io_inprog_force++;
		atomic_inc(&dmc->nr_jobs);

		list_add_tail(&flush_mu_blk->node, out);

		flush_mu_blk = first_valid_flush_mu_for_md_io(mu);
	}
}

// in/out under mu lock
static void get_fua_mu_to_send(struct cache_c *dmc, struct list_head *out)
{
	new_mu_t *mu = &dmc->new_mu;
	/* Handle fua MU without throttle */
	mu_blk_t *mu_blk = first_valid_no_flush_mu_for_md_io(dmc);

	while (mu_blk && mu_blk_flagged_any(mu_blk, MU_FUA)) {
		list_del(&mu_blk->node); // From MIPQ
		atomic_dec(&dmc->dmcg->md_io_pending_no_flush);
		mu->md_io_pending_queue_size--;

		/* MU_HAS_PENDING_REQ should NEVER be set at this moment */
		if (check_new_error_inject(dmc, WRITE_FUA_TEST)) {
			dmc->sysctl_new_error_inject = 0;
			sprint("detect WRITE_FUA_TEST flag, simulate concurrent FUA writes");
			mu_blk_set_flag(mu_blk, MU_HAS_PENDING_REQ | MU_PENDING_REQ_FUA);
			mu->pending_blk_num[MU_BLK_TYPE_NOFLUSH]++;
		}

		mu_blk = setup_and_get_mu_blk_to_update(dmc, mu_blk);

		atomic_inc(&mu->md_io_to_queue);
		mu_blk_set_flag(mu_blk, MU_SENT_BY_FUA);
		mu->md_io_inprog_fua++;
		atomic_inc(&dmc->nr_jobs);

		list_add_tail(&mu_blk->node, out);

		mu_blk = first_valid_no_flush_mu_for_md_io(dmc);
	}
}

// TODO: move force to standalone function
void syno_mu_send_md_ios(struct cache_c *dmc)
{
	mu_blk_t *mu_blk = NULL;
	mu_blk_t *flush_mu_blk = NULL;
	mu_blk_t *target_mu_blk = NULL;
	new_mu_t *mu = &dmc->new_mu;
	LIST_HEAD(queue);
	struct list_head *start = NULL;
	struct list_head *end = NULL;

	spin_lock_irq(&mu->lock);

	get_force_mu_to_send(dmc, &queue);
	get_fua_mu_to_send(dmc, &queue);

	mu_blk = first_valid_no_flush_mu_for_md_io(dmc);
	flush_mu_blk = first_valid_flush_mu_for_md_io(mu);

	while ((mu_blk || flush_mu_blk) && can_send_more_md_io(mu)) {
		if (NULL == mu_blk) {
			target_mu_blk = flush_mu_blk;
		} else if (NULL == flush_mu_blk) {
			target_mu_blk = mu_blk;
		} else if (time_after(flush_mu_blk->tstamp, mu_blk->tstamp)) {
			target_mu_blk = mu_blk;
		} else {
			target_mu_blk = flush_mu_blk;
		}

		debug(mu);
		list_del(&target_mu_blk->node); // From MIPQ or MIFPQ
		debug(mu);

		if (!mu_blk_flagged_any(target_mu_blk, MU_NEED_FLUSH)) {
			atomic_dec(&dmc->dmcg->md_io_pending_no_flush);
			mu->md_io_pending_queue_size--;
		}

		target_mu_blk = setup_and_get_mu_blk_to_update(dmc, target_mu_blk);

		atomic_inc(&mu->md_io_to_queue);
		mu_blk_set_flag(target_mu_blk, MU_SENT_BY_NORMAL);
		mu->md_io_inprog_normal++;
		atomic_inc(&dmc->nr_jobs);

		debug(mu);
		list_add_tail(&target_mu_blk->node, &queue);
		debug(mu);

		mu_blk = first_valid_no_flush_mu_for_md_io(dmc);
		flush_mu_blk = first_valid_flush_mu_for_md_io(mu);
	}

	if (!list_empty(&queue)) {
		start = queue.next;
		end = queue.prev;
	}
	list_splice_tail_init(&queue, &mu->md_io_inprog_queue);

	spin_unlock_irq(&mu->lock);

	if (start) {
		send_md_ios_in_queue(dmc, start, end);
	}

	return;
}

/* In under read lock, out under read lock */
static void dmcg_throtl_rebalance(struct cache_c *dmc)
{
	dmc_group_t *dmcg = dmc->dmcg;
	dmc_node *node = NULL;
	struct list_head *pos = NULL;
	struct cache_c *throtl_dmc = NULL;
	int target_md_io_size = 0;

	up_read(&dmcg->rw_sem);
	down_write(&dmcg->rw_sem);

	atomic_inc(&dmcg->rebalance_cnt);

	list_for_each(pos, &dmcg->dmc_list) {
		node = container_of(pos, dmc_node, group_entry);

		if (!node->pdmc->new_mu.allow_throtl) {
			continue;
		}

		if (node->pdmc->new_mu.md_io_pending_queue_size > target_md_io_size) {
			throtl_dmc = node->pdmc;
			target_md_io_size = throtl_dmc->new_mu.md_io_pending_queue_size;
		}
	}

	dmcg->throtl_dmc = throtl_dmc;
	downgrade_write(&dmcg->rw_sem);
}

static inline int dmcg_throtl_need_rebalance(dmc_group_t *dmcg)
{
	return 0 == (atomic_inc_return(&dmcg->num_throtl_io) % DMCG_MU_THROTL_REBALANCE_THRES_IOS) ||
		NULL == dmcg->throtl_dmc;
}

/* MUST not be in atomic context */
void syno_mu_dmcg_throtl(struct cache_c *dmc)
{
	dmc_group_t *dmcg = dmc->dmcg;
	struct cache_c *throtl_dmc = NULL;

	/* Temporary incorrect thres value shouldn't be a problem, don't lock */
	if (!dmcg_need_throtl_for_md_io(dmcg)) {
		return;
	}

	down_read(&dmcg->rw_sem);
	if (dmcg_throtl_need_rebalance(dmcg)) {
		dmcg_throtl_rebalance(dmc);
	}

	if (NULL == dmcg->throtl_dmc) {
		goto end_unlock;
	}

	if (FLASHCACHE_WRITE_BACK == dmc->cache_mode &&
		md_io_above_per_cache_throtl_thres(dmc)) {
		/* dmc itself need throttle */
		throtl_dmc = dmc;
		atomic_inc(&dmc->new_mu.throtl_self);
	} else {
		throtl_dmc = dmcg->throtl_dmc;
		atomic_inc(&dmc->new_mu.throtl_help);
	}

	send_md_io_for_throtl_ext_io(throtl_dmc);

end_unlock:
	up_read(&dmcg->rw_sem);
}

void syno_mu_disable_dmcg_throtl(struct cache_c *dmc)
{
	new_mu_t *mu = &dmc->new_mu;

	down_write(&dmc->dmcg->rw_sem);

	if (dmc->cache_mode != FLASHCACHE_WRITE_BACK) {
		goto out_unlock;
	}

	/* Prevent other dmc from selecting this dmc as the target */
	mu->allow_throtl = 0;

	if (dmc->dmcg->throtl_dmc == dmc) {
		dmc->dmcg->throtl_dmc = NULL;
	}

out_unlock:
	up_write(&dmc->dmcg->rw_sem);
}

#endif /* MY_ABC_HERE */
