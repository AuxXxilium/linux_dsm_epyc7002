#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifndef __SYNO_FUNCTIONS_H__
#define __SYNO_FUNCTIONS_H__

typedef enum _param_type {
	TYPE_UNKNOWN,
	TYPE_BIO_RANGE,
	TYPE_NO_BIO,
#ifdef MY_ABC_HERE
	TYPE_NQF,		/* Normal Quickflush */
	TYPE_SQF,		/* Sync Quickflush */
#endif
	TYPE_PREREAD_CACHEBLK,	/* For cache block preread */
	TYPE_MD_IO,		/* For md io */
} param_type_t;

typedef struct _job_param {
	param_type_t type;
	struct bio* bio;
	sector_t partial_offset;
	sector_t partial_size;
} job_param_t;

typedef enum _match_t {
	MATCH_UNSET = -1,
	MATCH_ALL,
	MATCH_ONLY_PARTIAL,
	MATCH_NONE,
} match_t;
extern char *match_str[];

void job_dbg(const char *func, struct kcached_job *job);
void job_to_str(struct kcached_job *job, char *str, int len);
extern char *action_str[];
char * bio_to_str(struct bio *bio, char *str, int len);

u64 compatible_mod(u64 val, u64 mod);
unsigned long long compatible_div(unsigned long long v1, unsigned long long v2);
bitmap_t  bitmap_get(struct cacheblock *cb, struct bio *bio);
bitmap_t  bitmap_get_by_bytes(struct cacheblock *cb, struct bio *bio, unsigned long mem_data_byte);
int bitmap_is_contain(bitmap_t bitmap, bitmap_t bitmap_partial);
void bitmap_add(bitmap_t *src, bitmap_t add);
void bitmap_remove(bitmap_t *src, bitmap_t remove);
int bitmap_is_unset(bitmap_t bitmap);
int bitmap_is_all_set(bitmap_t bitmap);
match_t bitmap_get_match(bitmap_t bitmap, bitmap_t compare);

/*
 * Common
 */

void cacheblk_unset_all_data_range(struct cacheblock *cb);
void cacheblk_add_num_concurrent(struct cacheblock *cb);
void cacheblk_add_num_concurrent_by(struct cacheblock *cb, int num);
sector_t cacheblk_get_start_sector(struct cache_c *dmc, struct bio *bio);
// Return nr_concurrent
void cacheblk_dec_num_concurrent_by(struct cacheblock *cb, int num);
u_int8_t cacheblk_dec_num_concurrent_and_return(struct cacheblock *cb);

// Data bitmap
void cacheblk_add_data_range(struct cacheblock *cb, bitmap_t bio_bitmap);

void cacheblk_add_data_range_by_bio(struct cacheblock *cb, struct bio *bio);
match_t cacheblk_compare_data_range(struct cacheblock *cb, struct bio *bio);


// IO bitmap: use BLOCK_IO_INPROG to check if in io
void cacheblk_add_io_range(struct cacheblock *cb, bitmap_t bio_bitmap);
void cacheblk_add_io_range_by_bio(struct cacheblock *cb, struct bio *bio);

void cacheblk_remove_io_range(struct cacheblock *cb, bitmap_t bitmap);
void cacheblk_remove_io_range_by_bio(struct cacheblock *cb, struct bio *bio);

void cacheblk_remove_io_range_change_state(struct cache_c *dmc, struct cacheblock *cb, bitmap_t bitmap);
void cacheblk_remove_io_range_change_state_by_bio(struct cache_c *dmc, struct cacheblock *cb, struct bio *bio);

match_t cacheblk_compare_io_range(struct cacheblock *cb, struct bio *bio);
void cacheblk_set_all_io_range(struct cacheblock *cb);
void cacheblk_unset_all_io_range(struct cacheblock *cb);
void cacheblk_unset_all_io_range_change_state(struct cache_c *dmc, struct cacheblock *cb);

// For pin file
int bio_is_pin(struct bio *bio);
// Query pin file bitmap, set pin state if needed
void bio_set_pin_state(struct cache_c *dmc, struct bio *bio);
void cacheblk_set_pin_state_by_bio(struct cache_c *dmc, struct cacheblock *cb, struct bio *bio);
void cacheblk_check_unset_pin_state(struct cache_c *dmc, struct cacheblock *cb);

/*
 * Dirty bitmap
 */
void cacheblk_add_data_range_and_dirty_range(struct cacheblock *cb, bitmap_t bitmap);
int cacheblk_is_dirty_range_going_to_change(struct cacheblock *cb, bitmap_t new_added_bitmap);
void cacheblk_unset_all_dirty_range(struct cacheblock *cb);
#ifdef CONFIG_SYNO_DATA_CORRECTION
int cacheblk_is_range_dirty(struct cacheblock *cb, struct bio *bio);
void cacheblk_clear_data_range_by_bio(struct cacheblock *cb, struct bio *bio);
#endif
// Control temporary flushing dirty_bitmap for write_back_* operation
int cacheblk_is_flushing_dirty_range_unset(struct cacheblock *cb);
int cacheblk_get_lowest_part_of_flushing_dirty_range(struct cacheblock *cb, sector_t *offset, sector_t *size);
int cacheblk_unset_lowest_part_of_flushing_dirty_range(struct cacheblock *cb);

/*
 * Job create
 */
struct kcached_job *new_kcached_job_fill_cacheblk(struct cache_c *dmc, struct bio* bio, int index);
#ifdef MY_ABC_HERE
struct kcached_job *new_kcached_job_md_io(struct cache_c *dmc, mu_blk_t *mu_blk);
#endif

#ifdef MY_ABC_HERE
struct kcached_job *new_kcached_job_qf(struct cache_c *dmc, int index, sector_t offset, sector_t size, param_type_t type);
#else
struct kcached_job *new_kcached_job_no_bio(struct cache_c *dmc, int index, sector_t offset, sector_t size);
#endif

#endif /* __SYNO_FUNCTIONS_H__ */
