// Copyright (c) 2000-2020 Synology Inc. All rights reserved.

#include <linux/module.h>
#include <linux/vmalloc.h>

#include "flashcache.h"

static void
verify_is_dirty(struct cache_c *dmc, unsigned long long disk_blk_idx)
{
	int i = quickflush_find_cacheblk_idx(dmc, disk_blk_idx);

	if (0 > i) {
		serr(" dirty test FAILED!!! Block %llu is not in cache\n", disk_blk_idx);
		return;
	}

	if (DIRTY != (dmc->cache[i].cache_state & DIRTY)) {
		serr("dirty test FAILED!!! Block %llu is not dirty\n", disk_blk_idx);
		return;
	}
}

void
quickflush_test_io_count(void)
{
	int i = 0;
	int fail = 0;
	struct cache_c *dmc = NULL;
	int tmp = 0;
	int result[10] = {2, 3, 1, 3, 1, 1, 1, 2, 0, 0};

	dmc = kzalloc(sizeof(*dmc), GFP_KERNEL);
	if (!dmc) {
		serr("Failed to alloc dmc");
	}
	dmc->cache = kzalloc(10 * sizeof(struct cacheblock), GFP_KERNEL);
	if (!dmc->cache) {
		serr("Failed to alloc dmc->cache");
	}
	dmc->cache[0].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[0].data_bitmap  = (bitmap_t) 0b0001111011111000;
	dmc->cache[1].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[1].data_bitmap  = (bitmap_t) 0b0001111010111000;
	dmc->cache[2].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[2].data_bitmap  = (bitmap_t) 0b0001111111111000;
	dmc->cache[3].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[3].data_bitmap  = (bitmap_t) 0b0000100010011000;
	dmc->cache[4].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[4].data_bitmap  = (bitmap_t) 0b1111111111111111;
	dmc->cache[5].dirty_bitmap = (bitmap_t) 0b0000000000010000;
	dmc->cache[5].data_bitmap  = (bitmap_t) 0b1111111111110000;
	dmc->cache[6].dirty_bitmap = (bitmap_t) 0b1010101010101010;
	dmc->cache[6].data_bitmap  = (bitmap_t) 0b1111111111111111;
	dmc->cache[7].dirty_bitmap = (bitmap_t) 0b1010101010101010;
	dmc->cache[7].data_bitmap  = (bitmap_t) 0b1111111011111111;

	for (i = 0; i < 8; ++i) {
		tmp = quickflush_cacheblk_get_nr_writeback(dmc, i);
		if (result[i] != tmp) {
			printk(KERN_ERR "io count test %d failed, should be %d but return %d\n", i, result[i], tmp);
			fail = 1;
		}
	}

	if (0 == fail) {
		printk(KERN_ERR "io count test PASSED!\n");
	}

	kfree(dmc->cache);
	kfree(dmc);
}

void
quickflush_test_io_range(void)
{
	int i = 0;
	int j = 0;
	int fail = 0;
	struct cache_c *dmc = NULL;
	int result_offset[14] = {3, 11, 3, 7, 11, 3, 3, 7, 11, 3, 4,  1, 1, 9};
	int result_size[14]   = {5,  1, 2, 1,  1, 9, 2, 1,  1, 9, 1, 15, 7, 7};
	sector_t offset = 0;
	sector_t size = 0;
	bitmap_t dirty_bitmap = 0;

	dmc = kzalloc(sizeof(*dmc), GFP_KERNEL);
	if (!dmc) {
		serr("Failed to alloc dmc");
	}
	dmc->cache = kzalloc(10 * sizeof(struct cacheblock), GFP_KERNEL);
	if (!dmc->cache) {
		serr("Failed to alloc dmc->cache");
	}
	dmc->cache[0].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[0].data_bitmap  = (bitmap_t) 0b0001111011111000;
	dmc->cache[1].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[1].data_bitmap  = (bitmap_t) 0b0001111010111000;
	dmc->cache[2].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[2].data_bitmap  = (bitmap_t) 0b0001111111111000;
	dmc->cache[3].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[3].data_bitmap  = (bitmap_t) 0b0000100010011000;
	dmc->cache[4].dirty_bitmap = (bitmap_t) 0b0000100010011000;
	dmc->cache[4].data_bitmap  = (bitmap_t) 0b1111111111111111;
	dmc->cache[5].dirty_bitmap = (bitmap_t) 0b0000000000010000;
	dmc->cache[5].data_bitmap  = (bitmap_t) 0b1111111111110000;
	dmc->cache[6].dirty_bitmap = (bitmap_t) 0b1010101010101010;
	dmc->cache[6].data_bitmap  = (bitmap_t) 0b1111111111111111;
	dmc->cache[7].dirty_bitmap = (bitmap_t) 0b1010101010101010;
	dmc->cache[7].data_bitmap  = (bitmap_t) 0b1111111011111111;

	for (i = 0; i < 8; ++i) {
		dirty_bitmap = dmc->cache[i].dirty_bitmap;
		while (dirty_bitmap) {
			quickflush_get_next_io_range(dmc->cache[i].data_bitmap, &dirty_bitmap, &offset, &size);
			if (offset != result_offset[j] * SECTOR_PER_BIT
				|| size != result_size[j] * SECTOR_PER_BIT) {
				fail = 1;
				serr("io range test failed: i: %d j:%d offset: %lu size: %lu\n", i, j, offset, size);
			}
			j++;
			if (j >= 15) {
				serr("This should not happen\n");
				break;
			}
		}
	}

	if (!fail) {
		serr("io range test PASSED!!!\n");
	} else {
		serr("io range test FAILED!!!\n");
	}

	kfree(dmc->cache);
	kfree(dmc);
}

// Note: This function how use the bitmap and cache in dmc directly
//        Use in caution since it might messed up qf_bitmap and the index.
//        Should change to use fake data and cache.
void
quickflush_test_bitmap_correctness(struct cache_c *dmc)
{
	int seg_idx = 0;
	int unit_idx_in_seg = 0;
	int nr_unit_per_seg = 0;
	qf_unit_t *cur_unit = 0;
	unsigned long long disk_blk_idx = 0;
	unsigned long long disk_dirty_blk_idx = 0;
	unsigned long long nr_bit_per_seg = QF_KB_TO_BIT(dmc->qf_bitmap_seg_size_kb);
	unsigned long long nr_bit_per_unit = QF_BYTE_TO_BIT(sizeof(**dmc->qf_bitmap_segs));
	unsigned int right_most_bitval = 0;
	unsigned long dirty_cnt = 0;

	quickflush_build_next_bitmap(dmc);
	nr_unit_per_seg = compatible_div(QF_KB_TO_BYTE(dmc->qf_bitmap_seg_size_kb), sizeof(**dmc->qf_bitmap_segs));

	while (1) {
		while(seg_idx < dmc->qf_bitmap_seg_cnt) {
			while (unit_idx_in_seg < nr_unit_per_seg) {
				cur_unit = &dmc->qf_bitmap_segs[seg_idx][unit_idx_in_seg];
				while (*cur_unit) {
					right_most_bitval = QF_R_MOST_BITVAL(*cur_unit);
					disk_dirty_blk_idx =
						dmc->qf_bitmap_start_blk_idx
						+ seg_idx * nr_bit_per_seg
						+ unit_idx_in_seg * nr_bit_per_unit
						+ ilog2(right_most_bitval);
					*cur_unit &= ~right_most_bitval;
					verify_is_dirty(dmc, disk_dirty_blk_idx);
					dirty_cnt++;
				} /* bits in a unit */
				++unit_idx_in_seg;
				disk_blk_idx =
					dmc->qf_bitmap_start_blk_idx
					+ seg_idx * nr_bit_per_seg
					+ unit_idx_in_seg * nr_bit_per_unit;
				if (disk_blk_idx >= dmc->disk_blk_cnt) {
					goto out;
				}
			} /* units in segment */
			++seg_idx;
			unit_idx_in_seg = 0;
		} /* segments in bitmap */
		seg_idx = 0;
		quickflush_build_next_bitmap(dmc);
	} /* bitmap in volume */
out:

	printk(KERN_ERR "nr_dirty: %d dirty_cnt: %lu\n", dmc->nr_dirty, dirty_cnt);
	if (dirty_cnt == dmc->nr_dirty) {
		printk(KERN_ERR "Dirty Number Check Passed!");
	} else {
		printk(KERN_ERR "Dirty Number Check Failed!");
	}
}
