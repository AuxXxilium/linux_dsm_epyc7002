// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#ifndef __SYNO_UNIT_TEST_H__
#define __SYNO_UNIT_TEST_H__

void quickflush_test_io_count(void);
void quickflush_test_io_range(void);
void quickflush_test_bitmap_correctness(struct cache_c *dmc);

#endif