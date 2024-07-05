// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#include <linux/synolib.h>
#include <linux/time.h>
#include <linux/ktime.h>

/*
 * Draw buffer with UTC time following by "SYNO" pattern
 *
 * @param buffer [IN] page address
 * @param size   [IN] buffer size
 *
 */
void syno_draw_auto_remap_buffer(char *buffer, int size)
{
	int i;
	struct tm tm;

	for (i = 0; i <= size - 4; i += 4) {
		buffer[i] = 'S';
		buffer[i + 1] = 'Y';
		buffer[i + 2] = 'N';
		buffer[i + 3] = 'O';
	}
	time64_to_tm(get_seconds(), 0, &tm);
	snprintf(buffer, size, "UTC%04ld-%02d-%02dT%02d:%02d:%02d",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		 tm.tm_hour, tm.tm_min, tm.tm_sec);
}
