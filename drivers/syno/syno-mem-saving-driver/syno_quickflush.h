#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifndef __SYNO_QUICKFLUSH_H__
#define __SYNO_QUICKFLUSH_H__

#include "flashcache.h"

// REMOVE THE FILE FOR GPL RELEASE
#ifdef MY_ABC_HERE

#if defined(CONFIG_SYNO_ALPINE)
#define OQF_VB_PER_BIT 8
#else
#define OQF_VB_PER_BIT 4 // Should be power of 2
#endif
#define OQF_MARK_FALLOW_DURATION_NSEC 200000
#define OQF_MARK_FALLOW_ITVL_MSEC 10
#define OQF_FALLOW_CHECK_CNT 16384
#define OQF_BITS_PER_INT (sizeof(int) * 8)
#define QF_BLK_IDX_TO_SEC(idx) ((idx) * CACHE_BLK_SIZE_SEC)

/* wake the writeback thread up every 10 second in case the current writeback
 * stop due to error (e.g., not enough memory). */
#define OQF_WB_WORK_ITVL_SEC 10
/* Total device bytes / device byte represeted per int + 1 for remainders */
#define OQF_FALLOW_MAP_NUM_IDX(dmc) (compatible_div(get_disk_bdev(dmc)->bd_inode->i_size, \
		OQF_BITS_PER_INT * OQF_VB_PER_BIT * to_bytes(CACHE_BLK_SIZE_SEC)) + 1)

typedef struct cb_wb_param {
	int idx;
	/* Number of non-consecutive dirty blocks in a cache block
	 * During writeback we merge dirty blocks that are consecutive to each
	 * other into one IO. So this is how many writeback IO we'll send for a cb
	 * see quickflush_cacheblk_get_nr_writeback for detail */
	int nr_write;
	int job_flag; // KCACHED_JOB_FLAG
} cb_wb_param_t;

void flashcache_clean_sync_state(struct cache_c *dmc);
int quickflush_find_cacheblk_idx(struct cache_c *dmc, unsigned long long disk_blk_idx);
int quickflush_cacheblk_get_nr_writeback(struct cache_c *dmc, int index);
void quickflush_get_next_io_range(bitmap_t data_bitmap, bitmap_t *dirty_bitmap, sector_t *offset, sector_t *size);
void syno_start_sqf(struct cache_c *dmc);
void quickflush_dirty_writeback(struct cache_c *dmc, cb_wb_param_t param);
void quickflush_construct_bitmap(struct cache_c *dmc);

void quickflush_set_volume_map(struct cache_c *dmc, struct cacheblock *cb);
void quickflush_unset_volume_map(struct cache_c *dmc, unsigned long bit_idx_in_map);
void quickflush_detect_fallow(struct cache_c *dmc);

void syno_start_nqf(struct cache_c *dmc);
void quickflush_do_writeback(struct work_struct *work);

static inline int quickflush_schedule_wb_work(struct cache_c *dmc)
{
	return cache_schedule_delayed_work(&dmc->oqf.wb_work, OQF_WB_WORK_ITVL_SEC * HZ);
}

#endif /* MY_ABC_HERE */
#endif /* __SYNO_QUICKFLUSH_H__ */
