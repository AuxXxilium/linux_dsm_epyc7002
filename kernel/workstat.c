// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000-2021 Synology Inc.
 */
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/ratelimit.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/hardirq.h>

#include "workqueue_internal.h"

#define KWORK_IO_STAT_UPDATE_RATE_MS       250

struct workstat {
	/* corresponding work function */
	work_func_t func;

	/**
	 * Please refer to task_io_accounting.h for the following
	 * I/O statistics
	 */
	atomic64_t read_bytes;
	atomic64_t write_bytes;
	atomic64_t cancelled_write_bytes;

	/* timer_sampled_us */
	atomic64_t timer_sampled_us;

	/* rb tree node */
	struct rb_node node;
};

struct workstat_records {
	rwlock_t lock;			/* lock for the tree and enable */
	u32 enable;			/* 1: enable. 0: disable */
	struct rb_root tree_root;
};

static struct workstat_records workstat_records = {0};

/**
 * Copy work_io_acct from task_io_accounting and init jiffies
 */
static inline void reset_work_io_acct(struct task_io_accounting *ioac,
				      struct work_io_acct *acct)
{
	acct->read_bytes = ioac->read_bytes;
	acct->write_bytes = ioac->write_bytes;
	acct->cancelled_write_bytes = ioac->cancelled_write_bytes;
	acct->last_update_jiffies = jiffies;
}

/**
 * diff = ioac - acct
 * Return true if any non-zero value in acct
 */
static inline bool diff_work_io_acct(struct work_io_acct *diff,
				     const struct task_io_accounting *ioac,
				     const struct work_io_acct *acct)
{
	diff->read_bytes = ioac->read_bytes
			 - acct->read_bytes;
	diff->write_bytes = ioac->write_bytes
			  - acct->write_bytes;
	diff->cancelled_write_bytes = ioac->cancelled_write_bytes
				    - acct->cancelled_write_bytes;
	return (diff->read_bytes || diff->write_bytes ||
		diff->cancelled_write_bytes);
}

/**
 * Allocate and init workstat.
 *
 * Return: Pointer to workstat on success and NULL on failure
 */
static inline struct workstat *alloc_workstat(work_func_t func, gfp_t gfp)
{
	struct workstat *stat = kzalloc(sizeof(struct workstat), gfp);

	if (!stat)
		return NULL;

	stat->func = func;

	return stat;
}

/**
 * Search records by func.
 *
 * Return workstat* if we found it and NULL if not found
 * CONTEXT: read_lock(workstat_records.lock)
 */
static inline struct workstat *
workstat_records_search(struct workstat_records *records, work_func_t func)
{
	struct workstat *curr;
	struct rb_node *n = records->tree_root.rb_node;

	while (n) {
		curr = rb_entry(n, struct workstat, node);
		if (func < curr->func)
			n = n->rb_left;
		else if (func > curr->func)
			n = n->rb_right;
		else
			return curr;
	}
	return NULL;
}

/**
 * CONTEXT: write_lock(workstat_records.lock)
 */
static inline struct workstat *
workstat_records_insert(struct workstat_records *records, struct workstat *ins)
{
	struct rb_node **link = &records->tree_root.rb_node;
	struct rb_node *parent = NULL;
	struct workstat *curr = NULL;

	while (*link) {
		parent = *link;
		curr = rb_entry(parent, struct workstat, node);

		if (ins->func < curr->func)
			link = &((*link)->rb_left);
		else if (ins->func > curr->func)
			link = &((*link)->rb_right);
		else
			return curr;
	}

	/* add new node and rebalance tree. */
	rb_link_node(&ins->node, parent, link);
	rb_insert_color(&ins->node, &records->tree_root);

	return NULL;
}

static inline bool try_alloc_insert_workstat(struct workstat_records *records,
					     work_func_t func, gfp_t gfp)
{
	struct workstat *stat;
	void *exist;
	unsigned long flags;

	stat = alloc_workstat(func, gfp);
	if (!stat)
		return false;

	write_lock_irqsave(&records->lock, flags);
	exist = workstat_records_insert(records, stat);
	write_unlock_irqrestore(&records->lock, flags);
	if (exist) {
		/* We may race with other insertions */
		kfree(stat);
	}
	return true;
}

/**
 * CONTEXT: write_lock(workstat_records.lock)
 */
static void workstat_records_reset(struct workstat_records *records)
{
	struct workstat *stat;
	struct rb_node *curr;

	while (!RB_EMPTY_ROOT(&records->tree_root)) {
		curr = rb_first(&records->tree_root);
		stat = rb_entry(curr, struct workstat, node);
		rb_erase(&stat->node, &records->tree_root);
		kfree(stat);
	}
}

static void update_kwork_io_stat(struct task_struct *p, gfp_t gfp)
{
	struct work_acct *acct;
	struct workstat *stat;
	struct work_io_acct io_diff;
	unsigned long flags;

	if (!p || !workstat_records.enable)
		return;

	acct = p->workacct;
	if (!acct || !diff_work_io_acct(&io_diff, &p->ioac, &acct->io_acct))
		return;

	/*
	 * We will leave some records if we are racing with disabling
	 * workstat_records. That is safe.
	 */
	read_lock_irqsave(&workstat_records.lock, flags);
	stat = workstat_records_search(&workstat_records, acct->func);
	if (likely(stat)) {
		/* accumulate workstat */
		atomic64_add(io_diff.read_bytes, &stat->read_bytes);
		atomic64_add(io_diff.write_bytes, &stat->write_bytes);
		atomic64_add(io_diff.cancelled_write_bytes, &stat->cancelled_write_bytes);
		read_unlock_irqrestore(&workstat_records.lock, flags);

		reset_work_io_acct(&p->ioac, &acct->io_acct);
	} else {
		read_unlock_irqrestore(&workstat_records.lock, flags);
		/*
		 * It's ok if we alloc memory failed.
		 * We will have another try next time.
		 */
		try_alloc_insert_workstat(&workstat_records, acct->func, gfp);
	}
}

void update_kwork_io_stat_ratelimited(struct task_struct *p, gfp_t gfp)
{
	if (!p || !p->workacct) {
		return;
	}

	if (jiffies_to_msecs(jiffies - p->workacct->io_acct.last_update_jiffies)
	    < KWORK_IO_STAT_UPDATE_RATE_MS)
		return;

	update_kwork_io_stat(p, gfp);
}
EXPORT_SYMBOL(update_kwork_io_stat_ratelimited);

void worker_run_work(struct worker *worker, struct work_struct *work)
{
	struct work_acct acct = {0};
	bool enable = workstat_records.enable;

	if (enable) {
		reset_work_io_acct(&current->ioac, &acct.io_acct);
		acct.func = worker->current_func;
		acct.wq = get_pwq_wq(worker->current_pwq);
		barrier();
		/*
		 * Saving pointer of local variable in current should be safe.
		 * Please referred to current->plug.
		 */
		current->workacct = &acct;
	}

	worker->current_func(work);

	if (enable)
		update_kwork_io_stat(current, GFP_KERNEL);

	current->workacct = NULL;
}
EXPORT_SYMBOL(worker_run_work);

void account_work_time(struct work_acct *acct, u64 us, gfp_t gfp)
{
	struct workstat *stat;
	unsigned long flags;

	if (!acct || !workstat_records.enable)
		return;

	read_lock_irqsave(&workstat_records.lock, flags);
	stat = workstat_records_search(&workstat_records, acct->func);
	if (likely(stat)) {
		/* accumulate workstat */
		atomic64_add(us, &stat->timer_sampled_us);
		read_unlock_irqrestore(&workstat_records.lock, flags);
	} else {
		read_unlock_irqrestore(&workstat_records.lock, flags);
		/*
		 * It's ok if we alloc memory failed.
		 * We will have another try next time.
		 */
		try_alloc_insert_workstat(&workstat_records, acct->func, gfp);
	}
}
EXPORT_SYMBOL(account_work_time);

/**
 * Interfaces for procfs
 */
static int workstat_stats_proc_show(struct seq_file *m, void *v)
{
	struct rb_node *n;
	struct workstat *stat;

	read_lock_irq(&workstat_records.lock);

	n = rb_first(&workstat_records.tree_root);
	while (n) {
		stat = rb_entry(n, struct workstat, node);
		seq_printf(m, "%ps: %llu %llu %llu %llu\n",
			   stat->func,
			   (u64)atomic64_read(&stat->read_bytes),
			   (u64)atomic64_read(&stat->write_bytes),
			   (u64)atomic64_read(&stat->cancelled_write_bytes),
			   (u64)atomic64_read(&stat->timer_sampled_us));
		n = rb_next(n);
	}

	read_unlock_irq(&workstat_records.lock);
	return 0;
}

static int workstat_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, workstat_stats_proc_show, NULL);
}

static const struct proc_ops workstat_stats_proc_ops = {
	.proc_open      = workstat_stats_proc_open,
	.proc_read      = seq_read,
	.proc_lseek     = seq_lseek,
	.proc_release   = single_release,
};

static int workstat_enable_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d", workstat_records.enable);
	return 0;
}

static int workstat_enable_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, workstat_enable_proc_show, NULL);
}

static ssize_t workstat_enable_proc_write(struct file *file,
					  const char __user *buf, size_t count,
					  loff_t *ppos)
{
	u32 val;
	int ret;

	if (*ppos != 0) {
		/* No partial writes. */
		ret = -EINVAL;
		goto out;
	}

	ret = kstrtou32_from_user(buf, count, 10, &val);
	if (ret < 0)
		goto out;

	ret = count;
	if (val == workstat_records.enable)
		goto out;

	write_lock_irq(&workstat_records.lock);
	workstat_records.enable = !!val;
	if (!val)
		workstat_records_reset(&workstat_records);
	write_unlock_irq(&workstat_records.lock);

out:
	return ret;
}

static const struct proc_ops workstat_enable_proc_ops = {
	.proc_open      = workstat_enable_proc_open,
	.proc_read      = seq_read,
	.proc_write     = workstat_enable_proc_write,
	.proc_lseek     = seq_lseek,
	.proc_release   = single_release,
};

static int __init proc_workstat_init(void)
{
	workstat_records.tree_root = RB_ROOT;
	rwlock_init(&workstat_records.lock);

	if (!proc_mkdir("workstat", NULL))
		return -ENOMEM;

	if (!proc_create("workstat/stats", 0, NULL, &workstat_stats_proc_ops))
		goto err;

	if (!proc_create("workstat/enable", 0, NULL, &workstat_enable_proc_ops))
		goto err;

	workstat_records.enable = 1;
	return 0;
err:
	remove_proc_subtree("workstat", NULL);
	return -ENOMEM;
}

static void __exit proc_workstat_exit(void)
{
	write_lock_irq(&workstat_records.lock);
	workstat_records.enable = 0;
	workstat_records_reset(&workstat_records);
	write_unlock_irq(&workstat_records.lock);
}

fs_initcall(proc_workstat_init);
module_exit(proc_workstat_exit);
