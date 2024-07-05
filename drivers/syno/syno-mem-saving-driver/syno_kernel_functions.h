#ifndef SYNO_FLASHCACHE_KERNEL_FUNCTIONS_H
#define SYNO_FLASHCACHE_KERNEL_FUNCTIONS_H
#include <linux/bio.h>
#include <linux/wait.h>
#include <linux/blk_types.h>
#include <linux/version.h>

/* Kernel porting */
sector_t bio_bi_sector(struct bio *bio);
sector_t* bio_bi_sector_ptr(struct bio *bio);
unsigned int bio_bi_size(struct bio *bio);
unsigned int* bio_bi_size_ptr(struct bio *bio);
unsigned int bio_bi_idx(struct bio *bio);
unsigned int* bio_bi_idx_ptr(struct bio *bio);
void bio_endio_wrapper(struct bio *bio, int error);
void bio_assign_dev(struct bio *bio, struct block_device *bdev);
void bio_assign_same_dev(struct bio *des_bio, struct bio *src_bio);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
typedef enum _comp_dm_op {
	/*
	 * COMP_DM_WRITE_FLUSH Must use REQ_OP_WRITE since dm async_io() only allow op 
	 * is WRITE as linux 4.4
	 */
	COMP_DM_WRITE_FLUSH = REQ_OP_WRITE | REQ_PREFLUSH,
	COMP_DM_WRITE = REQ_OP_WRITE,
	COMP_DM_READ = REQ_OP_READ,
	COMP_DM_WRITE_FUA = REQ_OP_WRITE | REQ_FUA,
} comp_dm_op_t;
#else
typedef enum _comp_dm_op {
	COMP_DM_WRITE_FLUSH = WRITE_FLUSH,
	COMP_DM_WRITE = WRITE,
	COMP_DM_READ = READ,
	COMP_DM_WRITE_FUA = WRITE_FUA,
} comp_dm_op_t;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define COMP_REQ_FLUSH REQ_PREFLUSH
#define COMP_BIO_ERROR BLK_STS_IOERR
#else
#define COMP_REQ_FLUSH REQ_FLUSH
#define COMP_BIO_ERROR -EIO
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
#define smp_mb__after_clear_bit_wrapper() smp_mb__after_atomic()
#else
#define smp_mb__after_clear_bit_wrapper() smp_mb__after_clear_bit()
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
#define wait_on_bit_lock_wrapper(...) wait_on_bit_lock_action(__VA_ARGS__)
#else
#define wait_on_bit_lock_wrapper(...) wait_on_bit_lock(__VA_ARGS__)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
/*
 * XXX: Print this value must cast to unsigned long
 *
 * Dont' cast bi_opf to unsigned long here. (it's unsigned short)
 * since this function is also used as lvalue
 */
#define bio_op_rw(bio) bio->bi_opf
#else
#define bio_op_rw(bio) bio->bi_rw
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
/*
 * Get bio (REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH) from btrfs's write_dev_flush()
 */
#define bio_has_flush_flags(bio) (REQ_PREFLUSH & bio_op_rw(bio))
#define bio_has_fua_flags(bio) (REQ_FUA & bio_op_rw(bio))
#else
#define bio_has_flush_flags(bio) (WRITE_FLUSH == bio_op_rw(bio)) // TODO: why not & REQ_FLUSH?
#define bio_has_fua_flags(bio) (WRITE_FUA == bio_op_rw(bio))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define bio_status(bio) bio->bi_status
#else
#define bio_status(bio) bio->bi_error
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define DECLARE_PROC_OPS_OPEN(name, open)\
	static struct proc_ops name = {\
	.proc_open		= open,\
	.proc_read		= seq_read,\
	.proc_lseek		= seq_lseek,\
	.proc_release	= single_release,\
};
#else
#define DECLARE_PROC_OPS_OPEN(name, func_open)\
	static struct file_operations name = {\
		.open		= func_open,\
		.read		= seq_read,\
		.llseek		= seq_lseek,\
		.release	= single_release,\
	};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define DECLARE_PROC_OPS_READ_WRITE(name, func_read, func_write)\
	struct proc_ops name = {\
	proc_read:	func_read,\
	proc_write:	func_write,\
	};
#else
#define DECLARE_PROC_OPS_READ_WRITE(name, func_read, func_write)\
	struct file_operations name = {\
		read:	func_read,\
		write:	func_write,\
	};
#endif

#ifdef CONFIG_SYNO_MD_UNUSED_HINT
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define bio_is_unused_hint(bio) (REQ_OP_UNUSED_HINT == bio_op(bio))
#else
#define bio_is_unused_hint(bio) (REQ_UNUSED_HINT & bio_op_rw(bio))
#endif
#endif /* CONFIG_SYNO_MD_UNUSED_HINT */

void* vmalloc_wrapper(size_t size, gfp_t gfp_mask);
#endif /*SYNO_FLASHCACHE_KERNEL_FUNCTIONS_H*/
