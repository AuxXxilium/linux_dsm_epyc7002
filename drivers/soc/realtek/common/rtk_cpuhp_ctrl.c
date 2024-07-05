// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/bitmap.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <soc/realtek/rtk_cpuhp.h>

struct rtk_cpuhp_ctrl_data {
	struct device                 *dev;
	struct completion             complete;
	struct task_struct            *task;
	struct notifier_block         cpuhp_nb;
	int                           dyn_state;
};

static struct rtk_cpuhp_ctrl_data *ctrl_data;

static void set_cpu_device_offline(int cpu_id)
{
	struct device *cpu_dev = get_cpu_device(cpu_id);

	if (WARN_ON(cpu_id == 0))
		return;

	device_offline(cpu_dev);
}

static void set_cpu_device_online(int cpu_id)
{
	struct device *cpu_dev = get_cpu_device(cpu_id);

	device_online(cpu_dev);
}

static int next_cpu_to_online(void)
{
	struct cpumask v;

	cpumask_xor(&v, cpu_possible_mask, cpu_online_mask);
	return cpumask_first(&v);
}

static int next_cpu_to_offline(void)
{
	return find_last_bit(cpumask_bits(cpu_online_mask), nr_cpumask_bits);
}

static int get_offline_cpu_num(struct rtk_cpuhp_ctrl_data *c)
{
	int v;

	get_online_cpus();
	v =  num_online_cpus();
	put_online_cpus();
	return num_possible_cpus() - v;
}

static int rtk_cpuhp_ctrl_do_task(void *data)
{
	struct rtk_cpuhp_ctrl_data *c = data;
	int ret;
	unsigned int n_off, excepted;
	int cpu;

	for (;;) {
		ret = wait_for_completion_timeout(&c->complete, 2 * HZ);
		if (kthread_should_stop()) {
			pr_debug("stop %s\n", __func__);
			break;
		}

		reinit_completion(&c->complete);
		if (ret == 0)
			continue;

		excepted = rtk_cpuhp_qos_read_value();
		n_off = get_offline_cpu_num(c);

		pr_debug("%s: cpu_num=%d, excepted=%d\n", __func__,
			n_off, excepted);
		if (n_off == excepted)
			continue;

		if (n_off > excepted) {
			cpu = next_cpu_to_online();
			pr_debug("set cpu%d online\n", cpu);
			set_cpu_device_online(cpu);
		} else {
			cpu = next_cpu_to_offline();
			pr_debug("set cpu%d offline\n", cpu);
			set_cpu_device_offline(cpu);
		}

		msleep(500);
	}
	return 0;
}

static int rtk_cpuhp_ctrl_cpu_online(unsigned int cpu)
{
	struct rtk_cpuhp_ctrl_data *c = ctrl_data;

	complete(&c->complete);
	pr_debug("start task from %s", __func__);
	return 0;
}

static int rtk_cpuhp_ctrl_cpu_offline(unsigned int cpu)
{
	struct rtk_cpuhp_ctrl_data *c = ctrl_data;

	complete(&c->complete);
	pr_debug("start task from %s", __func__);
	return 0;
}

static int rtk_cpuhp_ctrl_callback(struct notifier_block *nb, unsigned long action, void *data)
{
	struct rtk_cpuhp_ctrl_data *c = container_of(nb, struct rtk_cpuhp_ctrl_data, cpuhp_nb);

	complete(&c->complete);
	pr_debug("start task from %s", __func__);
	return NOTIFY_OK;
}

static __init int rtk_cpuhp_ctrl_init(void)
{
	struct rtk_cpuhp_ctrl_data *c;
	int ret;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	init_completion(&c->complete);

	c->task = kthread_create(rtk_cpuhp_ctrl_do_task, c, "cpuhp_ctrl_task");
	if (IS_ERR(c->task)) {
		ret = PTR_ERR(c->task);
		pr_err("failed to create kthread: %d\n", ret);
		return ret;
	}
	kthread_bind(c->task, 0);
	wake_up_process(c->task);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "cpuhp_ctrl_data:online",
		rtk_cpuhp_ctrl_cpu_online, rtk_cpuhp_ctrl_cpu_offline);
	if (ret < 0) {
		pr_err("failed to steup cpuhp state: %d\n", ret);
		goto remove_kthread;
	}
	c->dyn_state = ret;

	c->cpuhp_nb.notifier_call = rtk_cpuhp_ctrl_callback;
	ret = rtk_cpuhp_qos_add_notifier(&c->cpuhp_nb);
	if (ret) {
		pr_err("failed add cpuhp qos notifier: %d\n", ret);
		goto remove_cpuhp_state;
	}

	ctrl_data = c;
	return 0;

remove_cpuhp_state:
	cpuhp_remove_state_nocalls(c->dyn_state);
remove_kthread:
	kthread_stop(c->task);
	kfree(c);
	return ret;
}
module_init(rtk_cpuhp_ctrl_init);

static __exit void rtk_cpuhp_ctrl_exit(void)
{
	struct rtk_cpuhp_ctrl_data *c = ctrl_data;

	rtk_cpuhp_qos_remove_notifier(&c->cpuhp_nb);
	cpuhp_remove_state_nocalls(c->dyn_state);
	kthread_stop(c->task);

	kfree(c);
	ctrl_data = NULL;
}
module_exit(rtk_cpuhp_ctrl_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_DESCRIPTION("Realtek CPU Hotplug Controller");

