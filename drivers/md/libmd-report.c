#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#ifdef MY_ABC_HERE
#include <linux/bio.h>
#include <linux/synobios.h>
#include <linux/synolib.h>

#include <linux/raid/libmd-report.h>
#include "md.h"

int (*funcSYNOSendRaidEvent)(unsigned int, unsigned int, unsigned int, unsigned long long) = NULL;

void syno_report_faulty_device(int md_minor, struct block_device *bdev)
{
	int index  = syno_disk_get_device_index(bdev);
	char b[BDEVNAME_SIZE];

	if (bdev)
		bdevname(bdev, b);

	if (index < 0) {
		pr_warn("disk index get error, disk = %s, index = %d\n", b, index);
		return;
	}

	if (funcSYNOSendRaidEvent)
		funcSYNOSendRaidEvent(MD_FAULTY_DEVICE, md_minor, index, 0);
}
EXPORT_SYMBOL(syno_report_faulty_device);

void syno_report_bad_sector(sector_t sector, unsigned long rw,
			    int md_minor, struct block_device *bdev, const char *func_name)
{
	int index = syno_disk_get_device_index(bdev);
	char b[BDEVNAME_SIZE];

	if (bdev)
		bdevname(bdev, b);

	pr_warn("%s error, md%d, %s index [%d], sector %llu [%s]\n",
		rw ? "write" : "read", md_minor, b, index, (unsigned long long)sector, func_name);

	if (index < 0) {
		pr_warn("disk index get error, disk = %s, index = %d\n", b, index);
		return;
	}

	if (funcSYNOSendRaidEvent)
		funcSYNOSendRaidEvent((rw == WRITE) ? MD_SECTOR_WRITE_ERROR : MD_SECTOR_READ_ERROR,
				      md_minor, index, sector);
}
EXPORT_SYMBOL(syno_report_bad_sector);

void syno_report_correct_bad_sector(sector_t sector, int md_minor,
				    struct block_device *bdev, const char *func_name)
{
	int index = syno_disk_get_device_index(bdev);
	char b[BDEVNAME_SIZE];

	if (bdev)
		bdevname(bdev, b);

	pr_warn("read error corrected, md%d, %s index [%d], sector %llu [%s]\n",
		md_minor, b, index, (unsigned long long)sector, func_name);

	if (index < 0) {
		pr_warn("disk index get error, disk = %s, index = %d\n", b, index);
		return;
	}

	if (funcSYNOSendRaidEvent)
		funcSYNOSendRaidEvent(MD_SECTOR_REWRITE_OK, md_minor, index, sector);
}
EXPORT_SYMBOL(syno_report_correct_bad_sector);
EXPORT_SYMBOL(funcSYNOSendRaidEvent);

#ifdef MY_ABC_HERE
int (*funcSYNOSendAutoRemapRaidEvent)(unsigned int, unsigned long long, unsigned int) = NULL;
EXPORT_SYMBOL(funcSYNOSendAutoRemapRaidEvent);

void syno_auto_remap_report(struct mddev *mddev, sector_t sector, struct block_device *bdev)
{
	int index = syno_disk_get_device_index(bdev);
	char b[BDEVNAME_SIZE];

	if (bdev)
		bdevname(bdev, b);

	if (index < 0) {
		pr_warn("disk index get error, disk = %s, index = %d\n", b, index);
		return;
	}

	if (funcSYNOSendAutoRemapRaidEvent == NULL) {
		pr_warn("can't reference to function 'SYNOSendAutoRemapRaidEvent'\n");
	} else {
		pr_warn("report md[%d] auto-remapped sector:[%llu]\n",
			mddev->md_minor, (unsigned long long)sector);
		funcSYNOSendAutoRemapRaidEvent(mddev->md_minor, sector, (unsigned int)index);
	}
}
EXPORT_SYMBOL(syno_auto_remap_report);
#endif /* MY_ABC_HERE */
#endif /* MY_ABC_HERE */

