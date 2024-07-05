/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */
#ifndef __SOC_REALTEK_PD_INTERNAL_H
#define __SOC_REALTEK_PD_INTERNAL_H

#include <linux/clk.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/notifier.h>

#include "rtk_sram.h"
#include "rtk_iso.h"

struct rtk_pd_device {
	struct regmap *regmap;
	struct list_head list;
	struct device *dev;

	struct generic_pm_domain **domains;
	int num_domains;
	struct genpd_onecell_data of_provider_data;
};

static inline int rtk_pd_device_reg_read(struct rtk_pd_device *pd_dev, unsigned int offset, unsigned int *val)
{
	return regmap_read(pd_dev->regmap, offset, val);
}

static inline int rtk_pd_device_reg_write(struct rtk_pd_device *pd_dev, unsigned int offset, unsigned int val)
{
	return regmap_write(pd_dev->regmap, offset, val);
}

static inline int rtk_pd_device_reg_update_bits(struct rtk_pd_device *pd_dev,
	unsigned int offset, unsigned int mask, unsigned int val)
{
	return regmap_update_bits(pd_dev->regmap, offset, mask, val);
}

extern const struct dev_pm_ops rtk_pd_generic_pm_ops;
struct device;

struct rtk_pd_instance {
	struct generic_pm_domain pd;
	struct raw_notifier_head power_notifiers;
	struct notifier_block *nb;
};

#define genpd_to_rtk_pd_instance(_genpd) container_of(_genpd, struct rtk_pd_instance, pd)

static inline void rtk_pd_instance_notify(struct rtk_pd_instance *ins, long event)
{
	raw_notifier_call_chain(&ins->power_notifiers, event, NULL);
}

struct rtk_pd {
	struct rtk_pd_instance pd_iso;
	struct rtk_pd_instance pd_sram;

	struct list_head list;
	struct rtk_pd_device *pd_dev;
	struct device *dev;
	struct mutex lock;

	struct rtk_sram_desc sram;
	struct rtk_iso_desc iso;
};

static inline struct dev_power_governor *rtk_pd_get_gov(struct rtk_pd *pd)
{
	return NULL;
}

#define rtk_pd_name(_pd) ((_pd)->pd_iso.pd.name)
#define rtk_pd_to_genpd(_pd) (&((_pd)->pd_iso.pd))
#define genpd_to_rtk_pd(_genpd) container_of(_genpd, struct rtk_pd, pd_iso.pd)
#define rtk_pd_instance_to_rtk_pd(_n, _ins) container_of(_ins, struct rtk_pd, pd_ ## _n)

int rtk_pd_init(struct rtk_pd_device *, struct rtk_pd *);
int rtk_pd_device_add_domains(struct rtk_pd_device *pd_dev, struct generic_pm_domain **domains, int num_domains);
void rtk_pd_device_show_power_state(struct rtk_pd_device *pd_dev);

#define SET_PD_NAME(_name) \
	.pd_iso.pd.name = _name

#define SET_RTK_PD_SRAM_CONF_COMM(_off, _off_pwr5, _ch, _on_val, _off_val) \
	.sram = { \
		SET_RTK_SRAM_CONF_COMM(_off, _off_pwr5, _ch, _on_val, _off_val) \
	}

#define SET_RTK_PD_SRAM_CONF(_off, _ch) \
	SET_RTK_PD_SRAM_CONF_COMM(_off, 0, _ch, 0, 1)

#define SET_RTK_PD_SRAM_CONF_PWR5(_off, _off_pwr5, _ch) \
	SET_RTK_PD_SRAM_CONF_COMM(_off, _off_pwr5, _ch, 0, 1)

#define SET_RTK_PD_SRAM_CONF_MANUAL_MASK(_mask) \
	.sram.manual_mask = (_mask)

#define SET_RTK_PD_ISO_CONF(_off, _bit) \
	.iso.iso_offset = _off,             \
	.iso.iso_bit    = _bit

#define SET_RTK_PD_ISO_2_CONF(_off, _bit) \
	.iso.iso_2_offset = _off,             \
	.iso.iso_2_bit    = _bit

#define SET_RTK_PD_SRAM_DELAY(_l2h, _h2l) \
	.sram.l2h_delay = (_l2h),              \
	.sram.h2l_delay = (_h2l)

#define SET_RTK_PD_SRAM_STD_DELAY(_off, _std) \
	.sram.pwr8_offset = (_off),                \
	.sram.std_delay = (_std)

#endif
