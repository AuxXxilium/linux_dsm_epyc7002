#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifndef __SYNO_FLASHCACHE_FEATURE_DEFEINES_H__
#define __SYNO_FLASHCACHE_FEATURE_DEFEINES_H__

#include <linux/version.h>

/*
 * Not use this feature now, disable it for better performance
 */




/*
 * Support /proc/flashcache/node/list_set_valid_block
 */

/*
 * Don't create cache if the block device is in use (Can't be opened excludely)
 */
/*
 * Add write miss ssd, insert to flash_stat string, and append in dmsetup
 */
/*
 * Add a new mode for saving memory
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	// Not support split io to avoid unexpected cksum error due to hask bio split
#else
	/*
	 * Note: This feature requires changing __bio_clone_fast() behavior
	 * in linux kernel <= 4.4
	 * If you want to enable this feature on kernel > 4.4, you need to port
	 * old commit in __bio_clone_fast()
	 *
	 */
#endif

#if defined(CONFIG_SYNO_ALPINE)
// Alpine doesn't suppot it due to memory limitation
#else
#endif

/* Support REQ_FLUSH (barrier) */

/* Print messages when alloc memory failed */

/* dirty writeback size show KB */


/* Metadata update aggregate */
#if IS_ENABLED(CONFIG_SYNO_FS_CACHE_PROTECTION)
/*
 * Don't enable MUA since it might cause model + cache protection not work
 * in abnormal shutdown case
 */
#else
#endif



#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
/*
 * - TODO: Use seq_io to solve frame size > 1024 bytes warning
 * - Only support linux-3.10.x now
 */

/* Data rescue */
extern unsigned long long data_rescue_flags;
#define DATA_RESCUE_BLOCK_BAD_STATES (1 << 0)
typedef struct bad_states {
	unsigned long long bad_dbn_size;
	unsigned long long invalid;
	unsigned long long valid;
	unsigned long long dirty;
	unsigned long long in_prog;
	unsigned long long fallow;
	unsigned long long bitmap;
} bad_states_t;

typedef enum load_action {
	LOAD_ERROR_IGNORE = 0, /* default */
	LOAD_ERROR_CHECK,
} load_action_t;

#endif

/* Support online management */

#if defined(CONFIG_SYNO_MD_DM_DUMMY_CACHE_NOCLONE_BIO) && defined(MY_ABC_HERE)
/* Support dummy cache noclone bio */
#endif

#endif





