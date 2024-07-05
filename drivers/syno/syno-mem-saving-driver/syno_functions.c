#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
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

void job_dbg(const char *func, struct kcached_job *job)
{
	char job_str[JOB_STR_MAX] = {0};

	if (!(debug_flags & DF_JOB)) {
		return;
	}

	job_to_str(job, job_str, sizeof(job_str));

	sdbg(DF_JOB, "%s: %s", func, job_str);
}

char * bio_to_str(struct bio *bio, char *str, int len)
{
	VERIFY_WARN(bio && str && len);
	snprintf(str, len, "Bio (bi_sector=%llu size=%llu (sectors) direct=%s op_rw=0x%lx)",
			(u64)bio_bi_sector(bio), (u64)to_sector(bio_bi_size(bio)),
			(bio_data_dir(bio) == READ) ? "read" : "write", (unsigned long)bio_op_rw(bio));
	return str;
}

void job_to_str(struct kcached_job *job, char *str, int len)
{

	int n = 0;
	struct bio *bio = NULL;
	int index = 0;
	char bio_str[BIO_STR_MAX] = {0};

	VERIFY_WARN(job);
	VERIFY_WARN(str);

	bio = job->bio;
	index = job->index;

	if (job->bio) {
		n += snprintf(str + n , len - n, "%s ", bio_to_str(bio, bio_str, sizeof(bio_str)));
	} else {
		n += snprintf(str + n , len - n, "No bio ");
	}

	n += snprintf(str + n , len - n, "act=%s ", action_str[job->action]);

	if (index != -1) {
		n += snprintf(str + n, len - n, "[index=%d] cache.sector=%llu cache.count=%llu disk.sector=%llu disk.count=%llu ",
				index, (u64)job->job_io_regions.cache.sector, (u64) job->job_io_regions.cache.count
				, (u64)job->job_io_regions.disk.sector, (u64) job->job_io_regions.disk.count
				);
	} else {
		n += snprintf(str + n, len - n, "[uncached] disk.sector=%llu disk.count=%llu direction=%s",
				(u64)job->job_io_regions.disk.sector, (u64) job->job_io_regions.disk.count,
				(bio_data_dir(bio) == WRITE)? "write" : "read"
				);
	}

	if ((READDISKMEM == job->action) || (WRITEMEMCACHE == job->action)) {
		n += snprintf(str + n, len - n, "mem_addr=%p mem_data_byte=%lu", job->mem_addr, job->mem_data_byte);
	}
}

// Solve the alpine mod issue
// TODO: shorten function name
u64 compatible_mod(u64 val, u64 mod)
{
#ifdef EC_EMULATE_U64_DIVISION
	return mod_u64_rem64(val, mod);
#else
	return val % mod;
#endif
}

// TODO: shorten function name
unsigned long long compatible_div(unsigned long long v1, unsigned long long v2)
{
	unsigned long long ret = 0;
#ifdef EC_EMULATE_U64_DIVISION
	ret = div64_u64(v1, v2);
#else
	ret = v1 / v2;
#endif
	return ret;
}

#ifdef MY_ABC_HERE
// cb and bio are not used, just for debug
bitmap_t  bitmap_get_by_bytes(struct cacheblock *cb, struct bio *bio, unsigned long mem_data_byte)
{
	int i = 0;
	int bit_start = 0;
	int bit_end = 0;

	bitmap_t bitmap_new = 0;

	VERIFY_WARN(mem_data_byte);

	// should be 4K align, calculated in new_kcached_job...
	VERIFY_WARN(0 == (mem_data_byte % (512*SECTOR_PER_BIT)));

	bit_start = 0;
	bit_end =  compatible_div(to_sector(mem_data_byte), SECTOR_PER_BIT) - 1;

	if (bit_end > BITMAP_BITS) {
		serr("big_end=%d should not > BITMAP_BITS (%u)", bit_end, BITMAP_BITS);
		VERIFY_WARN(0);
	}

	for (i = bit_start; i <= bit_end; i++) {
		bitmap_new |= 1 << i;
	}

	return bitmap_new;
}

bitmap_t  bitmap_get(struct cacheblock *cb, struct bio *bio)
{
	int bit_start = 0;
	int bit_end = 0;
	int i = 0;
	bitmap_t bitmap_new = 0;

	if (!cb || !bio) {
		serr("Incorrect parameter cb=%p bio=%p", cb, bio);
		VERIFY_WARN(0);
	}

	// Align issue is checked in map()
	bit_start = compatible_div((bio_bi_sector(bio) - cb->dbn), SECTOR_PER_BIT);
	bit_end =  bit_start + compatible_div(to_sector(bio_bi_size(bio)), SECTOR_PER_BIT) - 1;

	if (bit_end > BITMAP_BITS) {
		serr("bi_sector=%llu cb->dbn=%llu", (u64)bio_bi_sector(bio), (u64)cb->dbn);
		serr("bit_start=%d size=%llu SECTOR_PER_BIT=%d", bit_start, (u64)to_sector(bio_bi_size(bio)), SECTOR_PER_BIT);
		serr("big_end=%d should not > BITMAP_BITS (%u)", bit_end, BITMAP_BITS);
		VERIFY_WARN(0);
	}

	for (i = bit_start; i <= bit_end; i++) {
		bitmap_new |= 1 << i;
	}

	return bitmap_new;
}

int bitmap_is_contain(bitmap_t bitmap, bitmap_t bitmap_partial)
{
	if ((bitmap & bitmap_partial) == bitmap_partial)
		return 1;
	else
		return 0;
}

match_t bitmap_get_match(bitmap_t bitmap, bitmap_t compare)
{
	bitmap_t res = bitmap & compare;
	if (res == compare) {
		return MATCH_ALL;
	} else if (res == 0) {
		return MATCH_NONE;
	} else {
		return MATCH_ONLY_PARTIAL;
	}

}

void bitmap_add(bitmap_t *src, bitmap_t add)
{
	if (!src || !add) {
		serr("Incorrect parameter");
		VERIFY_WARN(0);
	}
	*src = *src | add;
}

void bitmap_remove(bitmap_t *src, bitmap_t remove)
{
	if (!src) {
		serr("null pointer");
		VERIFY_WARN(0);
	}

	// Must contain the bits to be removed
	VERIFY_WARN(remove == (*src & remove));

	*src = *src & (~remove);
}

int bitmap_is_unset(bitmap_t bitmap)
{
	return (bitmap == 0)? 1: 0;
}


int bitmap_is_all_set(bitmap_t bitmap)
{
	return (bitmap == BITMAP_MASK)? 1: 0;
}

void cacheblk_add_data_range(struct cacheblock *cb, bitmap_t bitmap)
{
	bitmap_add(&cb->data_bitmap, bitmap);
}

void cacheblk_add_data_range_and_dirty_range(struct cacheblock *cb, bitmap_t bitmap)
{
	bitmap_add(&cb->data_bitmap, bitmap);
	bitmap_add(&cb->dirty_bitmap, bitmap);
}

#ifdef CONFIG_SYNO_DATA_CORRECTION
int cacheblk_is_range_dirty(struct cacheblock *cb, struct bio *bio)
{
	int ret = 0;
	bitmap_t bio_bitmap = bitmap_get(cb, bio);

	if (bio_bitmap & cb->dirty_bitmap) {
		ret = 1;
	}

	return ret;
}

void cacheblk_clear_data_range_by_bio(struct cacheblock *cb, struct bio *bio)
{
	bitmap_t bio_bitmap = bitmap_get(cb, bio);

	cb->data_bitmap &= ~bio_bitmap;
}
#endif

int cacheblk_is_dirty_range_going_to_change(struct cacheblock *cb, bitmap_t new_added_bitmap)
{
	int ret = 0;
	bitmap_t new_dirty_bitmap = 0;

	VERIFY_WARN(cb && new_added_bitmap);

	new_dirty_bitmap = cb->dirty_bitmap | new_added_bitmap;

	if (new_dirty_bitmap != cb->dirty_bitmap) {
		ret = 1;
	}

	return ret;
}

void cacheblk_add_data_range_by_bio(struct cacheblock *cb, struct bio *bio)
{
	bitmap_t bio_bitmap = 0;
	bio_bitmap = bitmap_get(cb, bio);
	bitmap_add(&cb->data_bitmap, bio_bitmap);
}

match_t cacheblk_compare_data_range(struct cacheblock *cb, struct bio *bio)
{
	bitmap_t bio_bitmap = 0;
	bio_bitmap = bitmap_get(cb, bio);
	return bitmap_get_match(cb->data_bitmap, bio_bitmap);
}

void cacheblk_set_all_io_range(struct cacheblock *cb)
{
	if (0 != cb->io_bitmap) {
		serr("io_bitmap should be zero!");
		VERIFY_WARN(0);
	}

	cb->io_bitmap = BITMAP_MASK;
}

void cacheblk_add_io_range(struct cacheblock *cb, bitmap_t bitmap)
{
	VERIFY_WARN(cb && bitmap);

	bitmap_add(&cb->io_bitmap, bitmap);
}

/*
 * Must be invoked after the dbn of cache block is set, ex in read_miss
 */
void cacheblk_add_io_range_by_bio(struct cacheblock *cb, struct bio *bio)
{
	bitmap_t bio_bitmap = 0;
	bio_bitmap = bitmap_get(cb, bio);
	bitmap_add(&cb->io_bitmap, bio_bitmap);
}

void cacheblk_remove_io_range(struct cacheblock *cb, bitmap_t bitmap)
{
	VERIFY_WARN(cb && bitmap);
	bitmap_remove(&cb->io_bitmap, bitmap);
}

void cacheblk_remove_io_range_by_bio(struct cacheblock *cb, struct bio *bio)
{


	bitmap_t bio_bitmap = 0;

	VERIFY_WARN(cb && bio);

	bio_bitmap = bitmap_get(cb, bio);
	VERIFY_WARN(0 != bio_bitmap);

	bitmap_remove(&cb->io_bitmap, bio_bitmap);
}

void cacheblk_remove_io_range_change_state(struct cache_c *dmc,
	struct cacheblock *cb, bitmap_t bitmap)
{

	VERIFY_WARN(cb && bitmap);

	cacheblk_remove_io_range(cb, bitmap);

	if (bitmap_is_unset(cb->io_bitmap)) {
		cb_state_remove_bits_update_counts(dmc, cb, BLOCK_IO_INPROG);
	}
}

void cacheblk_remove_io_range_change_state_by_bio(struct cache_c *dmc,
	struct cacheblock *cb, struct bio *bio)
{

	VERIFY_WARN(cb && bio);

	cacheblk_remove_io_range_by_bio(cb, bio);

	if (bitmap_is_unset(cb->io_bitmap)) {
		cb_state_remove_bits_update_counts(dmc, cb, BLOCK_IO_INPROG);
	}
}

void cacheblk_unset_all_io_range(struct cacheblock *cb)
{
	VERIFY_WARN(cb);

	VERIFY_WARN(0 != cb->io_bitmap);
	cb->io_bitmap = 0;
}

void cacheblk_unset_all_io_range_change_state(struct cache_c *dmc, struct cacheblock *cb)
{
	VERIFY_WARN(cb);

	cacheblk_unset_all_io_range(cb);

	if (0 == (cb->cache_state & BLOCK_IO_INPROG)) {
		serr("Error: Should be BLOCK_IN_PROG!");
		VERIFY_WARN(0);
	}

	cb_state_remove_bits_update_counts(dmc, cb, BLOCK_IO_INPROG);
}

match_t cacheblk_compare_io_range(struct cacheblock *cb, struct bio *bio)
{
	bitmap_t bio_bitmap = 0;
	bio_bitmap = bitmap_get(cb, bio);
	return bitmap_get_match(cb->io_bitmap, bio_bitmap);
}

int cacheblk_get_lowest_part_of_flushing_dirty_range(struct cacheblock *cb, sector_t *offset, sector_t *size)
{
	int i = 0;
	int found = 0;
	bitmap_t bitmap = cb->flushing_dirty_bitmap;

	if (0 == bitmap) {
		serr("in dirty state, bitmap should not be zero");
		VERIFY_WARN(0);
		return 0;
	}

	*offset = 0;
	*size = 0;

	for (i = 0; i < BITMAP_BITS; i++) {
		if (!found) {
			if (bitmap & 1 << i) {
				*offset = SECTOR_PER_BIT * i;
				*size += SECTOR_PER_BIT;
				found = 1;
			}
		} else {
			if (bitmap & 1 << i) {
				*size += SECTOR_PER_BIT;
			} else {
				break;
			}
		}
	}

	// offset can be 0
	VERIFY_WARN(0 != *size);

	return 0;
}

int cacheblk_unset_lowest_part_of_flushing_dirty_range(struct cacheblock *cb)
{
	int i = 0;
	int found = 0;

	VERIFY_WARN(cb);

	for (i = 0; i < BITMAP_BITS; i++) {
		if (!found) {
			if (cb->flushing_dirty_bitmap & 1 << i) {
				cb->flushing_dirty_bitmap &= ~(1<<i);
				found = 1;
			}
		} else {
			if (cb->flushing_dirty_bitmap & 1 << i) {
				cb->flushing_dirty_bitmap &= ~(1<<i);
			} else {
				break;
			}
		}
	}

	return 0;
}

int cacheblk_is_flushing_dirty_range_unset(struct cacheblock *cb)
{
	VERIFY_WARN(cb);

	return bitmap_is_unset(cb->flushing_dirty_bitmap);
}

void cacheblk_unset_all_dirty_range(struct cacheblock *cb)
{
	VERIFY_WARN(cb);
	VERIFY_WARN(0 != cb->dirty_bitmap);
	cb->dirty_bitmap = 0;
}

void cacheblk_add_num_concurrent(struct cacheblock *cb)
{
	VERIFY_WARN(cb);

	cb->nr_concurrent++;
	VERIFY_WARN(cb->nr_concurrent <= NR_CONCURRENT_MAX);
}

void cacheblk_add_num_concurrent_by(struct cacheblock *cb, int num)
{
	VERIFY_WARN(cb);

	cb->nr_concurrent += num;
	VERIFY_WARN(cb->nr_concurrent <= NR_CONCURRENT_MAX);
}

void cacheblk_dec_num_concurrent_by(struct cacheblock *cb, int num)
{
	VERIFY_WARN(cb);
	VERIFY_WARN(num <= cb->nr_concurrent);
	cb->nr_concurrent -= num;
}

// return nr_concurrent
u_int8_t cacheblk_dec_num_concurrent_and_return(struct cacheblock *cb)
{
	VERIFY_WARN(cb);
	VERIFY_WARN(0 != cb->nr_concurrent);
	cb->nr_concurrent--;
	return cb->nr_concurrent;
}

void cacheblk_unset_all_data_range(struct cacheblock *cb)
{
	VERIFY_WARN(cb);
	VERIFY_WARN(0 == cb->io_bitmap);

	if (0 != cb->nr_concurrent) {
		serr("cb->nr_concurrent=%d (Should be zero) cache_state=%x dirty_bitmap=%x"
				, cb->nr_concurrent, cb->cache_state, cb->dirty_bitmap);
		VERIFY_WARN(0);
	}
	VERIFY_WARN(0 == cb->dirty_bitmap);

	cb->data_bitmap = 0;
}

sector_t cacheblk_get_start_sector(struct cache_c *dmc, struct bio *bio)
{
	sector_t start = 0;

	VERIFY_WARN(bio && dmc);
	start = compatible_div(bio_bi_sector(bio), dmc->block_size) * dmc->block_size;

	return start;
}

#ifdef MY_ABC_HERE
// Since RO cache doesn't support pin, is_pin is always equal to the initial value 0
int bio_is_pin(struct bio *bio)
{
#ifdef MY_DEF_HERE
	struct subbio_info *psubbio_info = NULL;

	VERIFY_WARN(bio);

	psubbio_info = bio->bi_private;

	return psubbio_info->is_pin;
#else
	struct cache_bi_private *private = NULL;

	VERIFY_WARN(bio);

	private  = bio->bi_private;

	return private->is_pin;
#endif
}

/*
 * WARNING: ONLY use this function for incoming IOs
 */
void bio_set_pin_state(struct cache_c *dmc, struct bio *bio)
{
	unsigned char *pbytes = NULL;
	unsigned char byte_bitmap = 0;
	// Each sub bio should only occupy a bit due to 64KB align
	u64 bit_num = compatible_div(bio_bi_sector(bio), CACHE_BLK_SIZE_SEC);
	u64 byte_num = compatible_div(bit_num, 8);
	int bit_offset = compatible_mod(bit_num, 8);
#ifdef MY_DEF_HERE
	struct subbio_info *psubbio_info = bio->bi_private;
#else
	struct cache_bi_private *cache_bi_private = bio->bi_private;
#endif

	char bio_str[BIO_STR_MAX] = {0};

	if (FLASHCACHE_WRITE_BACK != dmc->cache_mode) {
		return;
	}

	/*
	 * No need to lock for bitmap since it is only changed when execurate "dmsetup reload"
	 * to do volume online expansion, and "dmsetup suspended" should be called before it
	 */
	VERIFY_WARN(dmc->pbitmap_ranges);
	pbytes = (unsigned char *)dmc->pbitmap_ranges;
	byte_bitmap = pbytes[byte_num];

	if (byte_bitmap & (1 << bit_offset)) {
#ifdef MY_DEF_HERE
		psubbio_info->is_pin = 1;
#else
		cache_bi_private->is_pin = 1;
#endif
		sdbg(DF_PIN, "%s is_pin=1", bio_to_str(bio, bio_str, sizeof(bio_str)));
	} else {
		sdbg(DF_PIN, "%s is_pin=0", bio_to_str(bio, bio_str, sizeof(bio_str)));
	}
}

void cacheblk_set_pin_state_by_bio(struct cache_c *dmc, struct cacheblock *cb, struct bio *bio)
{
	VERIFY_WARN(dmc && cb && bio);

	if (bio_is_pin(bio) && dmc->bitmap_is_set) {
		cb_state_add_bits_update_counts(dmc, cb, PIN_FILE);
	}
}

// When a cache block is going to be replaced or unpin
void cacheblk_check_unset_pin_state(struct cache_c *dmc, struct cacheblock *cb)
{
	VERIFY_WARN(cb);

	if (cb->cache_state & PIN_FILE) {
		cb_state_remove_bits_update_counts(dmc, cb, PIN_FILE);
	}
}
#endif /* MY_ABC_HERE */

#endif /* MY_ABC_HERE */
