// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/soc/realtek/rtk_pd.h>
#include "rtk_pd_internal.h"

static inline unsigned long rtk_pd_lock(struct rtk_pd *pd)
{
	unsigned long flags = 0;

	mutex_lock(&pd->lock);
	return flags;
}

static inline void rtk_pd_unlock(struct rtk_pd *pd, unsigned long flags)
{
	mutex_unlock(&pd->lock);
}

static int sram_power_on(struct rtk_pd *pd)
{
	unsigned long flags;
	int ret;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	flags = rtk_pd_lock(pd);
	ret = rtk_sram_power_on(pd->pd_dev->regmap, &pd->sram);
	rtk_pd_unlock(pd, flags);
	return ret;
}

static int sram_power_off(struct rtk_pd *pd)
{
	unsigned long flags;
	int ret;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	flags = rtk_pd_lock(pd);
	ret = rtk_sram_power_off(pd->pd_dev->regmap, &pd->sram);
	rtk_pd_unlock(pd, flags);

	return 0;
}

static int sram_power_state(struct rtk_pd *pd)
{
	unsigned long flags;
	unsigned int val;

	flags = rtk_pd_lock(pd);
	val = rtk_sram_power_state(pd->pd_dev->regmap, &pd->sram);
	rtk_pd_unlock(pd, flags);
	return val;
}

static int rtk_pd_sram_power_on(struct generic_pm_domain *genpd)
{
	struct rtk_pd_instance *ins = genpd_to_rtk_pd_instance(genpd);
	int ret;

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_PRE_ON);

	ret = sram_power_on(rtk_pd_instance_to_rtk_pd(sram, ins));
	if (ret < 0)
		pr_warn("%s: failed to power on sram: %d\n", ins->pd.name, ret);

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_ON);

	return 0;
}

static int rtk_pd_sram_power_off(struct generic_pm_domain *genpd)
{
	struct rtk_pd_instance *ins = genpd_to_rtk_pd_instance(genpd);
	int ret;

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_PRE_OFF);

	ret = sram_power_off(rtk_pd_instance_to_rtk_pd(sram, ins));
	if (ret < 0)
		pr_warn("%s: failed to power on sram: %d\n", ins->pd.name, ret);

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_OFF);

	return 0;
}

static void iso_power_on(struct rtk_pd *pd)
{
	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	rtk_iso_power_on(pd->pd_dev->regmap, &pd->iso);
}

static void iso_power_off(struct rtk_pd *pd)
{
	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	rtk_iso_power_off(pd->pd_dev->regmap, &pd->iso);
}

static int iso_power_state(struct rtk_pd *pd)
{
	return rtk_iso_power_state(pd->pd_dev->regmap, &pd->iso);
}

static int rtk_pd_iso_power_on(struct generic_pm_domain *genpd)
{
	struct rtk_pd_instance *ins = genpd_to_rtk_pd_instance(genpd);

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_PRE_ON);

	iso_power_on(rtk_pd_instance_to_rtk_pd(iso, ins));

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_ON);

	return 0;
}

static int rtk_pd_iso_power_off(struct generic_pm_domain *genpd)
{
	struct rtk_pd_instance *ins = genpd_to_rtk_pd_instance(genpd);

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_PRE_OFF);

	iso_power_off(rtk_pd_instance_to_rtk_pd(iso, ins));

	rtk_pd_instance_notify(ins, RTK_PD_NOTIFY_OFF);

	return 0;
}

static int rtk_genpd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	pr_debug("%s: %s %s %s\n", genpd->name, __func__, dev_driver_string(dev), dev_name(dev));
	return 0;
}

static void rtk_genpd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	pr_debug("%s: %s %s %s\n", genpd->name, __func__, dev_driver_string(dev), dev_name(dev));
}

int rtk_pd_dev_pm_add_notifier(struct device *dev, struct notifier_block *nb)
{
	struct rtk_pd_instance *ins;

	if (IS_ERR_OR_NULL(dev->pm_domain))
		return -EINVAL;

	ins = genpd_to_rtk_pd_instance(pd_to_genpd(dev->pm_domain));
	if (ins->nb)
		return -EEXIST;

	ins->nb = nb;
	return raw_notifier_chain_register(&ins->power_notifiers, nb);
}
EXPORT_SYMBOL_GPL(rtk_pd_dev_pm_add_notifier);

void rtk_pd_dev_pm_remove_notifier(struct device *dev)
{
	struct rtk_pd_instance *ins;

	if (IS_ERR_OR_NULL(dev->pm_domain))
		return;

	ins = genpd_to_rtk_pd_instance(pd_to_genpd(dev->pm_domain));
	if (!ins->nb)
		return;

	raw_notifier_chain_unregister(&ins->power_notifiers, ins->nb);
	ins->nb = NULL;
}
EXPORT_SYMBOL_GPL(rtk_pd_dev_pm_remove_notifier);

int rtk_pd_init(struct rtk_pd_device *pd_dev, struct rtk_pd *pd)
{
	int st_sram, st_iso;
	struct dev_power_governor *gov = rtk_pd_get_gov(pd);
	char name[20];

	pd->pd_dev = pd_dev;
	mutex_init(&pd->lock);

	st_sram = sram_power_state(pd);
	st_iso  = iso_power_state(pd);
	pr_info("%s: %s: default power state: sram=%d iso=%d\n", rtk_pd_name(pd), __func__, st_sram, st_iso);

	pd->pd_iso.pd.attach_dev = rtk_genpd_attach_dev;
	pd->pd_iso.pd.detach_dev = rtk_genpd_detach_dev;
	pd->pd_iso.pd.power_on   = rtk_pd_iso_power_on;
	pd->pd_iso.pd.power_off  = rtk_pd_iso_power_off;
	pm_genpd_init(&pd->pd_iso.pd, gov, !st_iso);
	RAW_INIT_NOTIFIER_HEAD(&pd->pd_iso.power_notifiers);

	snprintf(name, sizeof(name), "sram_%s", rtk_pd_name(pd));
	pd->pd_sram.pd.name = kstrdup(name, GFP_KERNEL);
	pd->pd_sram.pd.flags      = pd->pd_iso.pd.flags;
	pd->pd_sram.pd.attach_dev = rtk_genpd_attach_dev;
	pd->pd_sram.pd.detach_dev = rtk_genpd_detach_dev;
	pd->pd_sram.pd.power_on   = rtk_pd_sram_power_on;
	pd->pd_sram.pd.power_off  = rtk_pd_sram_power_off;
	pm_genpd_init(&pd->pd_sram.pd, gov, !st_sram);
	RAW_INIT_NOTIFIER_HEAD(&pd->pd_sram.power_notifiers);

	pm_genpd_add_subdomain(&pd->pd_sram.pd, &pd->pd_iso.pd);

	list_add(&pd->list, &pd_dev->list);
	return 0;
}
EXPORT_SYMBOL_GPL(rtk_pd_init);

int rtk_pd_device_add_domains(struct rtk_pd_device *pd_dev, struct generic_pm_domain **domains, int num_domains)
{
	struct generic_pm_domain *domain;
	int i;
	int ret;

	pd_dev->domains = domains;
	pd_dev->num_domains = num_domains;

	for (i = 0; i < num_domains; i++) {
		domain = domains[i];
		if (!domain)
			continue;

		ret = rtk_pd_init(pd_dev, genpd_to_rtk_pd(domain));
		WARN(ret, "rtk_pd_init() returns %d\n", ret);
	}
	return 0;
}

void rtk_pd_device_show_power_state(struct rtk_pd_device *pd_dev)
{
	struct rtk_pd *p;

	dev_info(pd_dev->dev, "list power state:\n");
	list_for_each_entry(p, &pd_dev->list, list) {
		dev_info(pd_dev->dev, "  %s: sram=%d, iso=%d\n", rtk_pd_name(p), sram_power_state(p), iso_power_state(p));
	}
}

MODULE_LICENSE("GPL v2");

