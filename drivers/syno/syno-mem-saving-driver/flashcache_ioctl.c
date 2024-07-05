#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/****************************************************************************
 *  flashcache_ioctl.c
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
#include <linux/kernel.h>
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
#include <linux/delay.h>

#ifdef MY_ABC_HERE
extern unsigned long hash_block(struct cache_c *dmc, sector_t dbn);
#endif

static int flashcache_find_pid_locked(struct cache_c *dmc, pid_t pid, 
				      int which_list);
static void flashcache_del_pid_locked(struct cache_c *dmc, pid_t pid, 
				      int which_list);

static int
flashcache_find_pid_locked(struct cache_c *dmc, pid_t pid, 
			   int which_list)
{
	struct flashcache_cachectl_pid *pid_list;
	
	pid_list = ((which_list == FLASHCACHE_WHITELIST) ? 
		    dmc->whitelist_head : dmc->blacklist_head);
	for ( ; pid_list != NULL ; pid_list = pid_list->next) {
		if (pid_list->pid == pid)
			return 1;
	}
	return 0;	
}

static void
flashcache_drop_pids(struct cache_c *dmc, int which_list)
{
	if (which_list == FLASHCACHE_WHITELIST) {
		while (dmc->num_whitelist_pids >= dmc->sysctl_max_pids) {
			VERIFY_WARN(dmc->whitelist_head != NULL);
			flashcache_del_pid_locked(dmc, dmc->whitelist_tail->pid,
						  which_list);
			atomic64_inc(&dmc->flashcache_stats.pid_drops);
		}
	} else {
		while (dmc->num_blacklist_pids >= dmc->sysctl_max_pids) {
			VERIFY_WARN(dmc->blacklist_head != NULL);
			flashcache_del_pid_locked(dmc, dmc->blacklist_tail->pid,
						  which_list);
			atomic64_inc(&dmc->flashcache_stats.pid_drops);
		}		
	}
}

static void
flashcache_add_pid(struct cache_c *dmc, pid_t pid, int which_list)
{
	struct flashcache_cachectl_pid *new;
 	unsigned long flags;

	new = kmalloc(sizeof(struct flashcache_cachectl_pid), GFP_KERNEL);
	if (!new) {
		serr("Failed to allocate pid when adding pid");
		return;
	}
	new->pid = pid;
	new->next = NULL;
	new->expiry = jiffies + dmc->sysctl_pid_expiry_secs * HZ;
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	if (which_list == FLASHCACHE_WHITELIST) {
		if (dmc->num_whitelist_pids > dmc->sysctl_max_pids)
			flashcache_drop_pids(dmc, which_list);
	} else {
		if (dmc->num_blacklist_pids > dmc->sysctl_max_pids)
			flashcache_drop_pids(dmc, which_list);		
	}
	if (flashcache_find_pid_locked(dmc, pid, which_list) == 0) {
		struct flashcache_cachectl_pid **head, **tail;
		
		if (which_list == FLASHCACHE_WHITELIST) {
			head = &dmc->whitelist_head;
			tail = &dmc->whitelist_tail;
		} else {
			head = &dmc->blacklist_head;
			tail = &dmc->blacklist_tail;
		}
		/* Add the new pid to the tail */
		new->prev = *tail;
		if (*head == NULL) {
			VERIFY_WARN(*tail == NULL);
			*head = new;
		} else {
			VERIFY_WARN(*tail != NULL);
			(*tail)->next = new;
		}
		*tail = new;
		if (which_list == FLASHCACHE_WHITELIST)
			dmc->num_whitelist_pids++;
		else
			dmc->num_blacklist_pids++;
		atomic64_inc(&dmc->flashcache_stats.pid_adds);
		/* When adding the first entry to list, set expiry check timeout */
		if (*head == new)
			dmc->pid_expire_check = 
				jiffies + ((dmc->sysctl_pid_expiry_secs + 1) * HZ);
	} else
		kfree(new);
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	return;
}

static void
flashcache_del_pid_locked(struct cache_c *dmc, pid_t pid, int which_list)
{
	struct flashcache_cachectl_pid *node;
	struct flashcache_cachectl_pid **head, **tail;
	
	if (which_list == FLASHCACHE_WHITELIST) {
		head = &dmc->whitelist_head;
		tail = &dmc->whitelist_tail;
	} else {
		head = &dmc->blacklist_head;
		tail = &dmc->blacklist_tail;
	}
	for (node = *tail ; node != NULL ; node = node->prev) {
		if (which_list == FLASHCACHE_WHITELIST)
			VERIFY_WARN(dmc->num_whitelist_pids > 0);
		else
			VERIFY_WARN(dmc->num_blacklist_pids > 0);
		if (node->pid == pid) {
			if (node->prev == NULL) {
				*head = node->next;
				if (node->next)
					node->next->prev = NULL;
			} else
				node->prev->next = node->next;
			if (node->next == NULL) {
				*tail = node->prev;
				if (node->prev)
					node->prev->next = NULL;
			} else
				node->next->prev = node->prev;
			kfree(node);
			atomic64_inc(&dmc->flashcache_stats.pid_dels);
			if (which_list == FLASHCACHE_WHITELIST)
				dmc->num_whitelist_pids--;
			else
				dmc->num_blacklist_pids--;
			return;
		}
	}
}

static void
flashcache_del_pid(struct cache_c *dmc, pid_t pid, int which_list)
{
	unsigned long flags;

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	flashcache_del_pid_locked(dmc, pid, which_list);
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
}

/*
 * This removes all "dead" pids. Pids that may have not cleaned up.
 */
void
flashcache_del_all_pids(struct cache_c *dmc, int which_list, int force)
{
	struct flashcache_cachectl_pid *node, **tail;
	unsigned long flags;
	
	if (which_list == FLASHCACHE_WHITELIST)
		tail = &dmc->whitelist_tail;
	else
		tail = &dmc->blacklist_tail;
	rcu_read_lock();
	spin_lock_irqsave(&dmc->cache_spin_lock, flags);
	node = *tail;
	while (node != NULL) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
		if (force == 0) {
			struct task_struct *task;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
			task = find_task_by_pid_type(PIDTYPE_PID, node->pid);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
			task = find_task_by_vpid(node->pid);
#else
			ask = pid_task(find_vpid(node->pid), PIDTYPE_PID);
#endif
			/*
			 * If that task was found, don't remove it !
			 * This prevents a rogue "delete all" from removing
			 * every thread from the list.
			 */
			if (task) {
				node = node->prev;
				continue;
			}
		}
#endif
		flashcache_del_pid_locked(dmc, node->pid, which_list);
		node = *tail;
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	rcu_read_unlock();
}

static void
flashcache_pid_expiry_list_locked(struct cache_c *dmc, int which_list)
{
	struct flashcache_cachectl_pid **head, **tail, *node, *tmp_node;
	
	if (which_list == FLASHCACHE_WHITELIST) {
		head = &dmc->whitelist_head;
		tail = &dmc->whitelist_tail;
	} else {
		head = &dmc->blacklist_head;
		tail = &dmc->blacklist_tail;
	}
	for (node = *head, tmp_node = NULL; node != NULL ; node = tmp_node) {
		tmp_node = node->next;
		if (which_list == FLASHCACHE_WHITELIST)
			VERIFY_WARN(dmc->num_whitelist_pids > 0);
		else
			VERIFY_WARN(dmc->num_blacklist_pids > 0);
		if (time_after(node->expiry, jiffies))
			continue;
		if (node->prev == NULL) {
			*head = node->next;
			if (node->next)
				node->next->prev = NULL;
		} else
			node->prev->next = node->next;
		if (node->next == NULL) {
			*tail = node->prev;
			if (node->prev)
				node->prev->next = NULL;
		} else
			node->next->prev = node->prev;
		kfree(node);
		if (which_list == FLASHCACHE_WHITELIST)
			dmc->num_whitelist_pids--;
		else
			dmc->num_blacklist_pids--;
		atomic64_inc(&dmc->flashcache_stats.expiry);
	}
}

void
flashcache_pid_expiry_all_locked(struct cache_c *dmc)
{
	if (likely(time_before(jiffies, dmc->pid_expire_check)))
		return;
	flashcache_pid_expiry_list_locked(dmc, FLASHCACHE_WHITELIST);
	flashcache_pid_expiry_list_locked(dmc, FLASHCACHE_BLACKLIST);
	dmc->pid_expire_check = jiffies + (dmc->sysctl_pid_expiry_secs + 1) * HZ;
}

/*
 * Is the IO cacheable, depending on global cacheability and the white/black
 * lists ? This function is a bit confusing because we want to support inheritance
 * of cacheability across pthreads (so we use the tgid). But when an entire thread
 * group is added to the white/black list, we want to provide for exceptions for 
 * individual threads as well.
 * The Rules (in decreasing order of priority) :
 * 1) Check the pid (thread id) against the list. 
 * 2) Check the tgid against the list, then check for exceptions within the tgid.
 * 3) Possibly don't cache sequential i/o.
 *
 * NOTE:
 *		1. XXX: DONT put this function inside cache_spin_lock
 *			since skip_sequential_io need some time to run seq gap loop that
 *			affect performance on some models
 *		2. This function will update skip_io table
 */
int
flashcache_uncacheable(struct cache_c *dmc, struct bio *bio, int *uncache_seq)
{
	int dontcache = 0;

	if (unlikely(dmc->bypass_cache)) {
		dontcache = 1;
		goto out;
	}


	if (dmc->sysctl_cache_all) {
		dontcache = 0;
#ifdef MY_ABC_HERE
		if (bio_is_pin(bio)) {
			dontcache = 0;
			goto out;
		}
#endif
#ifdef MY_ABC_HERE
#else
		/* If the tid has been blacklisted, we don't cache at all.
		   This overrides everything else */
		dontcache = flashcache_find_pid_locked(dmc, current->pid, 
						       FLASHCACHE_BLACKLIST);
		if (dontcache)
			goto out;
		/* Is the tgid in the blacklist ? */
		dontcache = flashcache_find_pid_locked(dmc, current->tgid, 
						       FLASHCACHE_BLACKLIST);
		/* 
		 * If we found the tgid in the blacklist, is there a whitelist
		 * exception entered for this thread ?
		 */
		if (dontcache) {
			if (flashcache_find_pid_locked(dmc, current->pid, 
						       FLASHCACHE_WHITELIST)) {
				dontcache = 0;
				goto out;
			}
		}
#endif /* SYNO_FLASHCACHE_BLOCKLIST */

		/* Finally, if we are neither in a whitelist or a blacklist,
		 * do a final check to see if this is sequential i/o.  If
		 * the relevant sysctl is set, we will skip it.
		 */
		*uncache_seq = skip_sequential_io(dmc, bio);
		dontcache = *uncache_seq;
	} else { /* cache nothing */
		dontcache = 1;
#ifdef MY_ABC_HERE
#else
		/* If the tid has been whitelisted, we cache 
		   This overrides everything else */
		dontcache = !flashcache_find_pid_locked(dmc, current->pid, 
							FLASHCACHE_WHITELIST);
		if (!dontcache)
			goto out;
		/* Is the tgid in the whitelist ? */
		dontcache = !flashcache_find_pid_locked(dmc, current->tgid, 
							FLASHCACHE_WHITELIST);
		/* 
		 * If we found the tgid in the whitelist, is there a black list 
		 * exception entered for this thread ?
		 */
		if (!dontcache) {
			if (flashcache_find_pid_locked(dmc, current->pid, 
						       FLASHCACHE_BLACKLIST))
				dontcache = 1;
		}
		/* No sequential handling here.  If we add to the whitelist,
		 * everything is cached, sequential or not.
  		 */
#endif
	}
out:
	return dontcache;
}

/* Below 2 functions manage the LRU cache of recent IO 'flows'.  
 * A sequential IO will only take up one slot (we keep updating the 
 * last sector seen) but random IO will quickly fill multiple slots.  
 * We allocate the LRU cache from a small fixed sized buffer at startup. 
 */
void
seq_io_remove_from_lru(struct cache_c *dmc, struct sequential_io *seqio)
{
	if (seqio->prev != NULL) 
		seqio->prev->next = seqio->next;
	else {
		VERIFY_WARN(dmc->seq_io_head == seqio);
		dmc->seq_io_head = seqio->next;
	}
	if (seqio->next != NULL)
		seqio->next->prev = seqio->prev;
	else {
		VERIFY_WARN(dmc->seq_io_tail == seqio);
		dmc->seq_io_tail = seqio->prev;
	}
}

void
seq_io_move_to_lruhead(struct cache_c *dmc, struct sequential_io *seqio)
{
	if (likely(seqio->prev != NULL || seqio->next != NULL))
		seq_io_remove_from_lru(dmc, seqio);
	/* Add it to LRU head */
	if (dmc->seq_io_head != NULL)
		dmc->seq_io_head->prev = seqio;
	seqio->next = dmc->seq_io_head;
	seqio->prev = NULL;
	dmc->seq_io_head = seqio;
}

#ifdef MY_ABC_HERE
void dump_seq_io_table(struct cache_c *dmc)
{
	struct sequential_io *seqio = NULL;
	int i = 0;

	for (seqio = dmc->seq_io_head; seqio != NULL ; seqio = seqio->next) {
		sdbg(DF_DETAIL, "seqio[%d]->most_recent_sector=%llu sequential sectors=%llu", i, (u64)seqio->most_recent_sector,
				(u64)seqio->sequential_sectors);
		i++;
	}
}
#endif
       

/* Look for and maybe skip sequential i/o.  
 *
 * Since          performance(SSD) >> performance(HDD) for random i/o,
 * but            performance(SSD) ~= performance(HDD) for sequential i/o,
 * it may be optimal to save (presumably expensive) SSD cache space for random i/o only.
 *
 * We don't know whether a single request is part of a big sequential read/write.
 * So all we can do is monitor a few requests, and try to spot if they are
 * continuations of a recent 'flow' of i/o.  After several contiguous blocks we consider
 * it sequential.
 *
 * You can tune the threshold with the sysctl skip_seq_thresh_kb (e.g. 64 = 64kb),
 * or cache all i/o (without checking whether random or sequential) with skip_seq_thresh_kb = 0.
 */
int 
skip_sequential_io(struct cache_c *dmc, struct bio *bio)
{
	struct sequential_io *seqio;
	int sequential = 0;	/* Saw > 1 in a row? */
	int skip       = 0;	/* Enough sequential to hit the threshold */
	int move_to_head_check_skip = 0;
	sector_t forward_gap_sector = 0;
	sector_t backward_gap_sector = 0;
	sector_t seqio_end_sector = 0;

	spin_lock_irq(&dmc->seq_io_spin_lock);

	/* sysctl skip sequential threshold = 0 : disable, cache all sequential and random i/o.
	 * This is the default. */	 
	if (dmc->sysctl_skip_seq_thresh_kb == 0)  {
		skip = 0;	/* Redundant, for emphasis */
		goto out;
	}

	/* Is it a continuation of recent i/o?  Try to find a match.  */
	DPRINTK("skip_sequential_io: searching for %ld", bio_bi_sector(bio));
	/* search the list in LRU order so single sequential flow hits first slot */
	for (seqio = dmc->seq_io_head; seqio != NULL && sequential == 0; seqio = seqio->next) { 

		seqio_end_sector = seqio->most_recent_sector + seqio->last_bio_sectors;
		forward_gap_sector = min(seqio->sequential_sectors, to_sector(dmc->sysctl_skip_seq_fgap_kb * 1024));

		if ((bio_bi_sector(bio) == seqio->most_recent_sector) &&
				(to_sector(bio_bi_size(bio)) == seqio->last_bio_sectors)) {

			/* Reread or write same sector again.  Ignore but move to head */
			DPRINTK("skip_sequential_io: repeat");
			sequential = 1;
			if (dmc->seq_io_head != seqio)
				seq_io_move_to_lruhead(dmc, seqio);
		}
		/* i/o to one block more than the previous i/o = sequential */	
		else if ((bio_bi_sector(bio) >= seqio_end_sector) &&
			(bio_bi_sector(bio) <= seqio_end_sector + forward_gap_sector)) {
			DPRINTK("skip_sequential_io: sequential found");
			/* Update stats.  */
			seqio->sequential_sectors +=
				to_sector(bio_bi_size(bio));
			seqio->most_recent_sector = bio_bi_sector(bio);
			seqio->last_bio_sectors = to_sector(bio_bi_size(bio));
			sequential = 1;
			move_to_head_check_skip = 1;
		} else if (dmc->sysctl_skip_seq_bgap_kb > 0) {
			backward_gap_sector = min(to_sector(dmc->sysctl_skip_seq_bgap_kb * 1024), seqio->sequential_sectors);
			if ((bio_bi_sector(bio) >= seqio_end_sector - backward_gap_sector)
				&& bio_bi_sector(bio) + to_sector(bio_bi_size(bio)) <= seqio_end_sector) {
				sequential = 1;
				move_to_head_check_skip = 1;
			}
		}

		if (move_to_head_check_skip) {
			/* And move to head, if not head already */
			if (dmc->seq_io_head != seqio) {
				seq_io_move_to_lruhead(dmc, seqio);
			}

			/* Is it now sequential enough to be sure? (threshold expressed in kb) */
			if (to_bytes(seqio->sequential_sectors) > dmc->sysctl_skip_seq_thresh_kb * 1024) {
				DPRINTK("skip_sequential_io: Sequential i/o detected, seq sectors now %llu", 
					seqio->sequential_sectors);
				/* Sufficiently sequential */
				skip = 1;
			}
		}
	}
	if (!sequential) {
		/* Record the start of some new i/o, maybe we'll spot it as 
		 * sequential soon.  */
		DPRINTK("skip_sequential_io: concluded that its random i/o");

		seqio = dmc->seq_io_tail;
		seq_io_move_to_lruhead(dmc, seqio);

		DPRINTK("skip_sequential_io: fill in data");

		/* Fill in data */
		seqio->most_recent_sector = bio_bi_sector(bio);
		seqio->last_bio_sectors = to_sector(bio_bi_size(bio));
		// Reset the vaule
		seqio->sequential_sectors = to_sector(bio_bi_size(bio));
	}
	DPRINTK("skip_sequential_io: complete.");
out:
	spin_unlock_irq(&dmc->seq_io_spin_lock);

	return skip;
}
#ifdef MY_ABC_HERE
static int set_bitmap(struct cache_c *dmc, unsigned long arg)
{
	int ret = -EFAULT;
#ifdef MY_ABC_HERE
	int get_control = 0;
	BITMAP *pbitmap = NULL;
#endif

	sdbg(DF_PIN, "Set bitmap start");

#ifdef MY_ABC_HERE
	pbitmap = kzalloc(sizeof(BITMAP), GFP_KERNEL);
	if (!pbitmap) {
		serr("Vmalloc bitmap failed");
		goto err;
	}

	bitmap_control_get(dmc);
	get_control = 1;
#endif

	if (0 == dmc->bitmap_size_byte) {
		serr("Bitmap table is not allocated");
		goto  err;
	}

	sdbg(DF_PIN, "Total bytes = %llu", dmc->bitmap_size_byte);

#ifdef MY_ABC_HERE
	if (copy_from_user(pbitmap, (void *)arg, sizeof(BITMAP))) {
		serr("Copy bitmap error");
		goto err;
	}

	/*
	 * Support online expansion so that simetimes the recorded bitmap size could be larger
	 * than the bitmap passed in
	 */
	if (pbitmap->sizeByte != dmc->bitmap_size_byte) {
		sdbg(DF_PIN, "Bitmap size change: input size = %zd cache bitmap size = %llu",
				pbitmap->sizeByte, dmc->bitmap_size_byte);
	}

	if (pbitmap->sizeByte > dmc->bitmap_size_byte) {
		serr("Input bitmap size (%zd) should not large then the cache bitmap size = %llu",
				pbitmap->sizeByte, dmc->bitmap_size_byte);
		goto err;
	}

	if (copy_from_user(dmc->pbitmap_ranges, (void *)pbitmap->data, pbitmap->sizeByte)) {
		serr("Copy bitmap error");
		goto err;
	}
#else
	if (copy_from_user(dmc->pbitmap_ranges, (void *)arg, dmc->bitmap_size_byte)) {
		serr("Copy bitmap error");
		goto err;
	}
#endif

	dmc->bitmap_is_set = 1;
	ret = 0;

err:
#ifdef MY_ABC_HERE
	if (get_control) {
		bitmap_control_put(dmc);
	}

	if (pbitmap) {
		kfree(pbitmap);
	}
#endif
	sdbg(DF_PIN, "Set bitmap finish");
	return ret ;
}

static int bitmap_is_set(struct cache_c *dmc, unsigned long arg)
{
	int ret = -EFAULT;

	if (copy_to_user((int *)arg, &dmc->bitmap_is_set, sizeof(int))) {
		serr("Can't copy data to user");
		goto err;
	}
	sdbg(DF_PIN, "Copy data to user finish. bitmap_is_set = %d", dmc->bitmap_is_set);

	ret = 0;
err:
	return ret ;
}

static void unpin_cache_block(struct cache_c *dmc, int disk_start_sector)
{
	unsigned long set_number = 0;
	int start_index = 0;
	int end_index = 0;
	int i = 0;
	unsigned long flags;

	VERIFY_WARN(dmc);

	set_number = hash_block(dmc, disk_start_sector);
	start_index = dmc->assoc * set_number;
	end_index = start_index + dmc->assoc;

	spin_lock_irqsave(&dmc->cache_spin_lock, flags);

	for (i = start_index ; i < end_index ; i++) {
		/*
		 * Search each cache block and check if bio's start sector (dbn) match a range of an cacheblk
		 */
		if (disk_start_sector >= dmc->cache[i].dbn &&
			(disk_start_sector < (dmc->cache[i].dbn + dmc->block_size)) &&
			dmc->cache[i].cache_state & VALID) {

			if (dmc->cache[i].cache_state & PIN_FILE) {
				sdbg(DF_PIN, "unpin cache block start = %llu", (unsigned long long)dmc->cache[i].dbn);
				cb_state_remove_bits_update_counts(dmc, &dmc->cache[i], PIN_FILE);
			}

			break;

		}
	}
	spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
}

static void unpin_range(struct cache_c *dmc, range_t *range)
{
	u64 next_start_sector = range->disk_start_sector;
	u64 last_sector = range->disk_start_sector + range->num_sectors - 1;

	sdbg(DF_PIN, "start sector = %llu num sectors = %llu", range->disk_start_sector, range->num_sectors);
	while (next_start_sector < last_sector) {
		// Unpin a cache block
		unpin_cache_block(dmc, next_start_sector);
		next_start_sector = compatible_div(next_start_sector + MAX_BIO_SIZE, MAX_BIO_SIZE) 
			* MAX_BIO_SIZE;
	}
}

static inline int unpin_range_reasonable(range_t *range, u64 disk_sector_num)
{
	if (range->disk_start_sector + range->num_sectors <= range->disk_start_sector) {
		return 0; // overflow
	}

	if (range->disk_start_sector + range->num_sectors > disk_sector_num) {
		return 0; // range larger than device size
	}

	return 1;
}

range_t ranges[MAX_RANGES];

int unpin_ranges(struct cache_c *dmc, unsigned long arg)
{
	int ret = -EFAULT;
	int i = 0;
	unsigned long num_uncompleted = 0;
	u64 disk_sector_num = 0;

	memset(ranges, 0, sizeof(ranges));

	num_uncompleted = copy_from_user(ranges, (void *)arg, sizeof(ranges));
	if (num_uncompleted) {
		serr("Copy ranges error. request size=%zd uncompleted=%lu", sizeof(ranges), num_uncompleted);
		goto err;
	}

	/*
	 * A new bitmap should be sent to flashcache driver before starting to unpin cache blocks
	 * However, some on-going bios might still be labeled as pin and doesn't enter the
	 * flashcache_lookup function yet
	 * So it might cause a race condition in following two paths
	 *	Path 1. Query the old bitmap -> bio labeled as pin -> flashcache_lookup
	 *			-> move the cache block to lru tail
	 *	Path 2. Unpin cache block -> move the cache block to the lru head
	 * Therefore, we add a sleep on Path 2 to wait for those bios to be completed
	 */
	msleep_interruptible(100);

	disk_sector_num = to_sector(get_disk_bdev(dmc)->bd_inode->i_size);

	while ((MAX_RANGES > i) && (0 != ranges[i].num_sectors)) {
		if (unpin_range_reasonable(&ranges[i], disk_sector_num)) {
			unpin_range(dmc, &ranges[i]);
		} else {
			serr("Unreasonable disk_sector: %llu len: %llu",
				ranges[i].disk_start_sector, ranges[i].num_sectors)
		}
		i++;
	}

	ret = 0;

err:
	sdbg(DF_PIN, "Unpin ranges finish");
	return ret ;
}

void zero_cache_bitmap(struct cache_c *dmc)
{
	bitmap_control_get(dmc);

	if (dmc->bitmap_is_set) {

		memset(dmc->pbitmap_ranges, 0, dmc->bitmap_size_byte);

		dmc->bitmap_is_set = 0;
	}

	bitmap_control_put(dmc);
}

int unpin_all_blocks(struct cache_c *dmc, unsigned long arg)
{
	int ret = -EFAULT;
	unsigned long long i = 0;
	unsigned long flags;

	sdbg(DF_PIN, "Unpin all blocks start");

	zero_cache_bitmap(dmc);

	for (i = 0; i < dmc->size; i++) {

		spin_lock_irqsave(&dmc->cache_spin_lock, flags);
		if (dmc->cache[i].cache_state & PIN_FILE) {
			cb_state_remove_bits_update_counts(dmc, &dmc->cache[i], PIN_FILE);
		}
		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	}

	ret = 0;

	sdbg(DF_PIN, "Unpin all blocks finish");
	return ret ;
}

static int is_all_blocks_unpin(struct cache_c *dmc, unsigned long arg)
{
	int ret = -EFAULT;
	unsigned long long i = 0;
	unsigned long flags;
	int all_unpin = 1;

	for (i = 0; i < dmc->size; i++) {

		spin_lock_irqsave(&dmc->cache_spin_lock, flags);

		if (dmc->cache[i].cache_state & PIN_FILE) {
			all_unpin = 0;
			spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
			break;
		}

		spin_unlock_irqrestore(&dmc->cache_spin_lock, flags);
	}

	if (copy_to_user((int *)arg, &all_unpin, sizeof(int))) {
		serr("Can't copy data to user");
		goto err;
	}


	ret = 0;
err:
	return ret;
}
#endif

#define GOTO_UNLOCK_AND_RET(val) ret = val; goto unlock_and_ret

/*
 * Add/del pids whose IOs should be non-cacheable.
 * We limit this number to 100 (arbitrary and sysctl'able).
 * We also add an expiry to each entry (defaluts at 60 sec,
 * arbitrary and sysctlable).
 * This is needed because Linux lacks an "at_exit()" hook
 * that modules can supply to do any cleanup on process 
 * exit, for cases where the process dies after marking itself
 * non-cacheable.
 */
int 
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27)
flashcache_ioctl(struct dm_target *ti, struct inode *inode,
		 struct file *filp, unsigned int cmd,
		 unsigned long arg)
#else
flashcache_ioctl(struct dm_target *ti, unsigned int cmd, unsigned long arg)
#endif
{
	int ret = -EFAULT;
	struct cache_c *dmc = (struct cache_c *) ti->private;
#ifdef MY_ABC_HERE
#else
#ifdef MY_ABC_HERE
	// Save the mode in the beginning of the system call
	fmode_t mode = get_disk_mode(dmc);
	struct block_device *bdev = get_disk_bdev(dmc);
#else
	struct block_device *bdev = dmc->disk_dev->bdev;
#endif
	struct file fake_file = {};
	struct dentry fake_dentry = {};
#endif /* MY_ABC_HERE */
	pid_t pid;

#ifdef MY_ABC_HERE
	// Check block ioctl in include/uapi/linux/fs.h
	sdbg(DF_PIN, "Get ioctl command = %d IO type=%d num=%d", cmd, _IOC_TYPE(cmd), _IOC_NR(cmd));
	sdbg(DF_PIN, "FLASHCACHE_BITMAP_SET=%d", FLASHCACHE_BITMAP_SET);
	sdbg(DF_PIN, "FLASHCACHE_UNPIN_RANGES=%d", FLASHCACHE_UNPIN_RANGES);
#endif

	down_read(&dmc->ioctl_rwsem);

	// Lock acquired, need to release before return

	if (CACHE_ENABLED != dmc->cache_state) {
		/* Do not support cache specific ioctl after disable. */
		switch (cmd) {
#ifdef MY_ABC_HERE
		case FLASHCACHE_BITMAP_SET:
		case FLASHCACHE_BITMAP_IS_SET:
		case FLASHCACHE_UNPIN_RANGES:
		case FLASHCACHE_UNPIN_ALL_BLOCKS:
		case FLASHCACHE_IS_ALL_BLOCKS_UNPIN:
#endif
		case FLASHCACHEADDBLACKLIST:
		case FLASHCACHEDELBLACKLIST:
		case FLASHCACHEDELALLBLACKLIST:
		case FLASHCACHEADDWHITELIST:
		case FLASHCACHEDELWHITELIST:
		case FLASHCACHEDELALLWHITELIST:
			GOTO_UNLOCK_AND_RET(-ENOTTY);
		default:
			goto handle_default_ioctl;
		}
	} else {
		switch(cmd) {
#ifdef MY_ABC_HERE
		case FLASHCACHE_BITMAP_SET:
			if (set_bitmap(dmc, arg)) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			GOTO_UNLOCK_AND_RET(0);

		case FLASHCACHE_BITMAP_IS_SET:
			if (bitmap_is_set(dmc, arg)) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			GOTO_UNLOCK_AND_RET(0);

		case FLASHCACHE_UNPIN_RANGES:
			sdbg(DF_PIN, "unpin ranges");
			if (unpin_ranges(dmc, arg)) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHE_UNPIN_ALL_BLOCKS:
			if (unpin_all_blocks(dmc, arg)) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHE_IS_ALL_BLOCKS_UNPIN:
			if (is_all_blocks_unpin(dmc, arg)) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			GOTO_UNLOCK_AND_RET(0);
#endif
		case FLASHCACHEADDBLACKLIST:
			if (copy_from_user(&pid, (pid_t *)arg, sizeof(pid_t))) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			flashcache_add_pid(dmc, pid, FLASHCACHE_BLACKLIST);
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHEDELBLACKLIST:
			if (copy_from_user(&pid, (pid_t *)arg, sizeof(pid_t))) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			flashcache_del_pid(dmc, pid, FLASHCACHE_BLACKLIST);
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHEDELALLBLACKLIST:
			flashcache_del_all_pids(dmc, FLASHCACHE_BLACKLIST, 0);
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHEADDWHITELIST:
			if (copy_from_user(&pid, (pid_t *)arg, sizeof(pid_t))) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			flashcache_add_pid(dmc, pid, FLASHCACHE_WHITELIST);
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHEDELWHITELIST:
			if (copy_from_user(&pid, (pid_t *)arg, sizeof(pid_t))) {
				GOTO_UNLOCK_AND_RET(-EFAULT);
			}
			flashcache_del_pid(dmc, pid, FLASHCACHE_WHITELIST);
			GOTO_UNLOCK_AND_RET(0);
		case FLASHCACHEDELALLWHITELIST:
			flashcache_del_all_pids(dmc, FLASHCACHE_WHITELIST, 0);
			GOTO_UNLOCK_AND_RET(0);
		default:
			goto handle_default_ioctl;
		}
	}

handle_default_ioctl:
#ifdef MY_ABC_HERE
	// Command is not supported
	GOTO_UNLOCK_AND_RET(1);
#else /* Not MY_ABC_HERE */
	up_read(&dmc->ioctl_rwsem);
#ifdef MY_ABC_HERE
	fake_file.f_mode = mode;
#else
	fake_file.f_mode = dmc->disk_dev->mode;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	fake_file.f_dentry = &fake_dentry;
#else
	fake_file.f_path.dentry = &fake_dentry;
#endif
	fake_dentry.d_inode = bdev->bd_inode;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27)
	return blkdev_driver_ioctl(bdev->bd_inode, &fake_file, bdev->bd_disk, cmd, arg);
#else
#ifdef MY_ABC_HERE
	return __blkdev_driver_ioctl(get_disk_bdev(dmc), mode, cmd, arg);
#else
	return __blkdev_driver_ioctl(dmc->disk_dev->bdev, dmc->disk_dev->mode, cmd, arg);
#endif
#endif
#endif /* MY_ABC_HERE */

unlock_and_ret:
	up_read(&dmc->ioctl_rwsem);
	return ret;
}
