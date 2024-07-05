#include <linux/vmalloc.h>
#include "syno_kernel_functions.h"

sector_t bio_bi_sector(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return bio->bi_iter.bi_sector;
#else
	return bio->bi_sector;
#endif
}

sector_t* bio_bi_sector_ptr(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return &bio->bi_iter.bi_sector;
#else
	return &bio->bi_sector;
#endif
}


unsigned int bio_bi_size(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return bio->bi_iter.bi_size;
#else
	return bio->bi_size;
#endif
}

unsigned int* bio_bi_size_ptr(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return &bio->bi_iter.bi_size;
#else
	return &bio->bi_size;
#endif
}

unsigned int bio_bi_idx(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return bio->bi_iter.bi_idx;
#else
	return bio->bi_idx;
#endif
}

unsigned int* bio_bi_idx_ptr(struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	return &bio->bi_iter.bi_idx;
#else
	// Original type is unsigned short *
	return (unsigned int *)&bio->bi_idx;
#endif
}

// BLK_STS_IOERR currently is a bit in u8, so int is enough
void bio_endio_wrapper(struct bio *bio, int error)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	bio_status(bio) = error;
	bio_endio(bio);
#else
	bio_endio(bio, error);
#endif
}

// bio_set_dev will be conflict with kernel function
void bio_assign_dev(struct bio *bio, struct block_device *bdev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	bio_set_dev(bio, bdev);
#else
	bio->bi_bdev = bdev;
#endif
}

void bio_assign_same_dev(struct bio *des_bio, struct bio *src_bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	bio_copy_dev(des_bio, src_bio);
#else
	des_bio->bi_bdev = src_bio->bi_bdev;
#endif
}

void* vmalloc_wrapper(size_t size, gfp_t gfp_mask)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return __vmalloc(size, gfp_mask);
#else
	return __vmalloc(size, gfp_mask, PAGE_KERNEL);
#endif
}
