#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Data shared with original driver and tools
 */

#ifndef __SYNO_FLASHCACHE_COMMON_4K_DRIVER_H__
#define __SYNO_FLASHCACHE_COMMON_4K_DRIVER_H__

// Share proc and sysctl entries with original driver
extern int syno_flashcache_proc_count;
extern struct semaphore syno_flashcache_proc_mutex;

/*
 * Print Functions
 */
// For old 4k driver, this global variable can only change in compiling time
extern long debug_flags;
// Error: must prink

#ifdef MY_ABC_HERE
#define TITLE "flashcache_syno: "
#else
#define TITLE "flashcache: "
#endif
#define serr(fmt, args...) printk(KERN_ERR TITLE"%s [%d]: "fmt"\n", __FUNCTION__, __LINE__, ##args);
#define sprint(fmt, args...) printk(KERN_ERR TITLE fmt"\n", ##args);

typedef enum _DEBUG_FLAG {
	DF_CORRECTION	= 1 << 0,
	DF_RELOAD_TABLE	= 1 << 1, /* reload table */
	DF_ONLINE		= 1 << 2, /* online managment */
	DF_MERGE		= 1 << 3, /* support merge function */
	DF_PIN			= 1 << 4, /* pin file */
	DF_JOB			= 1 << 5, /* debug job */
	DF_LOC			= 1 << 6, /* location for data verifier */
	DF_JOB_POOL_INIT	= 1 << 7, /* simulate job pool initilize slow */
	DF_QUICKFLUSH		= 1 << 8, /* quick flush */
	DF_WORKQUEUE		= 1 << 9, /* workqueue */
	DF_FLUSH_BIO		= 1 << 10, /* print flush bio infomation */
	DF_NEW_MU		= 1 << 11, /* new metadata update */
	DF_FUA			= 1 << 12, /* fua */
	DF_DMCG			= 1 << 13, /* dmcg */
	DF_DETAIL		= 1 << 31, /* WARNING: print hung IO information, don't set it in user's device */
} DEBUG_FLAG;
#ifdef MY_ABC_HERE

#define sdbg_limit(flags, fmt, args...) do { \
	if ((flags & debug_flags) && printk_ratelimit()) { \
		printk(KERN_ERR TITLE"%s [%d]: "fmt"\n", __FUNCTION__, __LINE__, ##args); \
	} \
	} while (0)

#define sdbg(flags, fmt, args...) do { \
	if (flags & debug_flags) { \
		printk(KERN_ERR TITLE"%s [%d]: "fmt"\n", __FUNCTION__, __LINE__, ##args); \
	} \
	} while (0)

#define sdbg_in(flags, fmt, args...) do { \
	if (flags & debug_flags) { \
		printk(KERN_ERR TITLE"++++ %s [%d]: "fmt"\n", __FUNCTION__, __LINE__, ##args); \
	} \
	} while (0)

#define sdbg_out(flags, fmt, args...) do { \
	if (flags & debug_flags) { \
		printk(KERN_ERR TITLE"---- %s [%d]: "fmt"\n", __FUNCTION__, __LINE__, ##args); \
	} \
} while (0)

#else
#define sdbgl(flags, fmt, args...) 
#define sdbg(flags, fmt, args...) 
#define sdbg_in(flags, fmt, args...) 
#define sdbg_out(flags, fmt, args...) 
#endif
#endif
