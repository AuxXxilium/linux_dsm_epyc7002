#ifndef FLASHCACHE_PIN_FILE_H
#define FLASHCACHE_PIN_FILE_H

#define PIN_SECTORS_PER_BIT 128
#define PIN_BYTES_PER_BIT (PIN_SECTORS_PER_BIT * 512)

/*
 * For worse case, each range records 4KB continous block
 * Each ioctl can handle 4MB data
 */
#define MAX_RANGES 512

#ifndef __KERNEL__
// For user space compatible
#ifndef u64
#define u64 unsigned long long
#endif
#endif

typedef struct _range {
	u64 disk_start_sector;
	u64 num_sectors;
} range_t;

typedef struct _BITMAP {
	unsigned char *data;
	// size_t is unsigned integer, so it supports 2048 TB volume (2^32 * 8 * 64KB)
	size_t sizeByte;
} BITMAP;

#endif
