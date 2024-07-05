#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Data shared with original driver and tools
 */

#ifndef __SYNO_FLASHCACHE_COMMON_TOOL_H__
#define __SYNO_FLASHCACHE_COMMON_TOOL_H__

#ifdef __KERNEL__
#else
/*
 * Make user space able to detect kernel config
 */
// Macro similar to kernel
#define IS_ENABLED(option) \
    (defined(option) || defined(option##_MODULE))
#include <linux/syno_autoconf.h>
#endif

#include "syno_feature_defines.h"

#define DEV_PATHLEN	128


#define SZ_LOAD_ERROR_CHECK "error-check"
// Share to userspace (can't contain underline)
#define SYNO_FLASHCACHE_TARGET_TYPE "flashcache-syno"
// For size_hist and userspce command
#define NEW_MODE_BLOCK_SIZE 128

// For hash mappin v2
#define HASH_REGION_BLOCK_NUM (256 * 1024 * 1024 / 64) // 256 GB * 1024 * 1024 / 64 KB
// Reserve small number for origial flashcache driver
// XXX: WARNING version must be larger than 10
#define FLASHCACHE_VERSION		2
#define SYNO_FLASHCACHE_VERSION	10

#ifdef MY_ABC_HERE
#define SYNO_FLASHCACHE_VERSION_HASH_MAPPING	11
#define SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2	12
#define IS_NEW_MODE_VERSION(version) ( \
		(version) == SYNO_FLASHCACHE_VERSION || \
		(version) == SYNO_FLASHCACHE_VERSION_HASH_MAPPING || \
		(version) == SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2)
#define SUPPORT_DMC_GROUP_VERSION(version) \
	(version == SYNO_FLASHCACHE_VERSION_HASH_MAPPING_V2)
#else
#define IS_NEW_MODE_VERSION(version) ((version) >= SYNO_FLASHCACHE_VERSION)
#endif

#define ID_LEN 512
#define GROUP_ID_LEN 64

/* Cache persistence*/
#define CACHE_RELOAD_TABLE 4

typedef enum _disable_type_t {
	DISABLE_UNKNOWN = -1,
	DISABLE_NORMAL,
	DISABLE_FORCE,
} disable_type_t;

/* Cache Modes */
enum {
	FLASHCACHE_WRITE_BACK=1,
	FLASHCACHE_WRITE_THROUGH=2,
	FLASHCACHE_WRITE_AROUND=3,
	/*
	 * Note: FLASHCACHE_DUMMY is only for creating
	 * After creating, check if state is CACHE_DISABLED is sugguested
	 */
	FLASHCACHE_DUMMY=4,
};

#ifndef __KERNEL__
typedef u_int64_t sector_t;
#endif

//SYNO: default is 8 sectors
struct flash_superblock {
	sector_t size;		/* Cache size */
	u_int32_t block_size;	/* Cache block size */
	u_int32_t assoc;	/* Cache associativity */
	u_int32_t cache_sb_state;	/* Clean shutdown ? */
	char cache_devname[DEV_PATHLEN]; /* Contains dm_vdev name as of v2 modifications */
	sector_t cache_devsize;
	char disk_devname[DEV_PATHLEN]; /* underlying block device name (use UUID paths!) */
	sector_t disk_devsize;
	// SYNO: start in byte 296
	u_int32_t cache_version;
	u_int32_t md_block_size;
#ifdef MY_ABC_HERE
	u_int32_t cache_state;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	char group_uuid[GROUP_ID_LEN];
#endif
};

/* On Flash (cache metadata) Structures */
#define CACHE_MD_STATE_DIRTY		0xdeadbeef
#define CACHE_MD_STATE_CLEAN		0xfacecafe
#define CACHE_MD_STATE_FASTCLEAN	0xcafefeed
#define CACHE_MD_STATE_UNSTABLE		0xc8249756

typedef u_int16_t bitmap_t;

/* 
 * We do metadata updates only when a block trasitions from DIRTY -> CLEAN
 * or from CLEAN -> DIRTY. Consequently, on an unclean shutdown, we only
 * pick up blocks that are marked (DIRTY | CLEAN), we clean these and stick
 * them in the cache.
 * On a clean shutdown, we will sync the state for every block, and we will
 * load every block back into cache on a restart.
 * 
 * Note: When using larger flashcache metadata blocks, it is important to make 
 * sure that a flash_cacheblock does not straddle 2 sectors. This avoids
 * partial writes of a metadata slot on a powerfail/node crash. Aligning this
 * a 16b or 32b struct avoids that issue.
 * 
 * Note: If a on-ssd flash_cacheblock does not fit exactly within a 512b sector,
 * (ie. if there are any remainder runt bytes), logic in flashcache_conf.c which
 * reads and writes flashcache metadata on create/load/remove will break.
 * 
 * If changing these, make sure they remain a ^2 size !
 */
#ifdef FLASHCACHE_DO_CHECKSUMS
struct flash_cacheblock {
	sector_t 	dbn;	/* Sector number of the cached block */
	u_int64_t 	checksum;
	u_int32_t	cache_state; /* MD_POSSIBLE_STATES */
#ifdef MY_ABC_HERE
	bitmap_t	data_bitmap;
	bitmap_t	dirty_bitmap;
#endif
} __attribute__ ((aligned(32)));
#else
struct flash_cacheblock {
	sector_t 	dbn;	/* Sector number of the cached block */
	u_int32_t	cache_state; /* MD_POSSIBLE_STATES  */
#ifdef MY_ABC_HERE
	bitmap_t	data_bitmap;
	bitmap_t	dirty_bitmap;
#endif
} __attribute__ ((aligned(16)));
#endif

/*
 * States of a cache block (cache_state)
 *
 * WARNING: To change cacheblock->state, must cb_state_* functions to update
 */

/* WARNING: In current design:
 * - State & INVALID means cacheblk is going to be invalid and new IO won't
 *   be enqueued for this cacheblk
 * 	 - see flashcache_inval_block_set()
 * - State == INVALID means the cacheblk might be reclaimed after
 *   getting out of lock immediately, so don't use it anymore
 */
#define INVALID			0x0001

#define VALID			0x0002	/* Valid */
#define DISKREADINPROG		0x0004	/* Read from disk in progress */
#define DISKWRITEINPROG		0x0008	/* Write to disk in progress */
#define CACHEREADINPROG		0x0010	/* Read from cache in progress */
#define CACHEWRITEINPROG	0x0020	/* Write to cache in progress */
#define DIRTY			0x0040	/* Dirty, needs writeback to disk */
/*
 * Old and Dirty blocks are cleaned with a Clock like algorithm. The leading hand
 * marks DIRTY_FALLOW_1. 900 seconds (default) later, the trailing hand comes along and
 * marks DIRTY_FALLOW_2 if DIRTY_FALLOW_1 is already set. If the block was used in the 
 * interim, (DIRTY_FALLOW_1|DIRTY_FALLOW_2) is cleared. Any block that has both 
 * DIRTY_FALLOW_1 and DIRTY_FALLOW_2 marked is considered old and is eligible 
 * for cleaning.
 */
#define DIRTY_FALLOW_1		0x0080	
#define DIRTY_FALLOW_2		0x0100
#define PIN_FILE		0x0200

#ifdef MY_ABC_HERE
#define QFSYNCERR		0x0400	/* error in quickflush sync */
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/* SYNCED_BITS flag means the wb bio for a cb is finished but metadata haven't update
 * The data can be clean/dirty dependding on if the corresponding disk flush completed.
 * In SYNCED_BITS state, the block cannot be replaced.
 * Forced mu must be issued before invalidating a SYNCED_BITS block.
 * Each SYNCED_BITS block has a corresponding MU. However, an mu can be pending if
 * it cannot merge with the ongoing mu thus we need two flags to identify cbs.
 * SYNCED1/2 corresponds to MU_FLUSH1/2 for mu blocks */

/*
 * Don't wrap SYNDED1 / SYNOCED2 /... in MY_ABC_HERE
 * to avoid need to use MY_ABC_HERE to split many codes
 */
#endif
#define SYNCED1			0x0800
#define SYNCED2			0x1000
#define SYNCED_BITS			(SYNCED1 | SYNCED2)
#define FORCEDMU		0x2000 /* For cb request force md update (invalidating) */
#define OCCUPIED_BITS	(DIRTY | SYNCED_BITS | PIN_FILE)

#ifdef MY_ABC_HERE
#define MD_POSSIBLE_STATES (VALID | INVALID | DIRTY | PIN_FILE)
#else
#define MD_POSSIBLE_STATES (VALID | INVALID | DIRTY)
#endif

#define FALLOW_DOCLEAN		(DIRTY_FALLOW_1 | DIRTY_FALLOW_2)
#define BLOCK_IO_INPROG	(DISKREADINPROG | DISKWRITEINPROG | CACHEREADINPROG | CACHEWRITEINPROG)


#define METADATA_IO_BLOCKSIZE		(256*1024)

/*
 * SYNO:
 * 26 byte for 64KB cacheblock
 * 1GB ssd cache only need 416KB memory
 */
typedef u_int16_t cb_state_t;
struct __attribute__((__packed__)) cacheblock {
	cb_state_t	cache_state;
	int16_t 	nr_queued;	/* jobs in pending queue */
	u_int16_t	lru_prev, lru_next;
	sector_t 	dbn;	/* Sector number of the cached block */
	bitmap_t	data_bitmap;
	bitmap_t	dirty_bitmap; /* Record the dirty sub blocks */
	bitmap_t	flushing_dirty_bitmap; /* For recording partial dirty bitmap to be flushed */
	bitmap_t	io_bitmap; /* Record subblocks in IO */
	u_int8_t	nr_concurrent;
};

#endif /* __SYNO_FLASHCACHE_COMMON_TOOL_H__ */
