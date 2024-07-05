#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0-or-later */
// Copyright (c) 2000-2021 Synology Inc. All rights reserved.
#ifndef _SYNO_MD_FAST_WAKEUP_H
#define _SYNO_MD_FAST_WAKEUP_H

#ifdef MY_ABC_HERE
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define SYNO_FAST_WAKEUP_CHECK_INTERVAL (7UL*HZ)

struct syno_md_fast_wakeup_work {
	void *mddev;
	struct work_struct work;
};

struct syno_md_fast_wakeup_info {
	bool          active;
	spinlock_t    active_lock;
	unsigned long last_req;
};

static inline void syno_md_fast_wakeup_info_init(
	struct syno_md_fast_wakeup_info *winfo)
{
	spin_lock_init(&winfo->active_lock);
	winfo->active = true;
	winfo->last_req = jiffies;
}

static inline bool syno_md_fast_wakeup_info_update(
	struct syno_md_fast_wakeup_info *winfo)
{
	bool need_wakeup = false;
	unsigned long req_jiffies = jiffies;

	if (time_after(req_jiffies,
			winfo->last_req + SYNO_FAST_WAKEUP_CHECK_INTERVAL)) {
		spin_lock(&winfo->active_lock);
		if (!winfo->active)
			need_wakeup = true;
		winfo->active = true;
		spin_unlock(&winfo->active_lock);
	}
	winfo->last_req = req_jiffies;

	return need_wakeup;
}

#endif /* MY_ABC_HERE */

#endif /* _SYNO_MD_FAST_WAKEUP_H */
