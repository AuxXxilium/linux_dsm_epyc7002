// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/regmap.h>
#include <linux/module.h>
#include <trace/events/rtk_pm.h>
#include "rtk_sram.h"

MODULE_LICENSE("GPL v2");

#define SRAM_PWR0 0x0
#define SRAM_PWR1 0x4
#define SRAM_PWR2 0x8
#define SRAM_PWR3 0xC
#define SRAM_PWR4 0x10
#define SRAM_PWR5 0x14
#define SRAM_PWR6 0x18

static inline unsigned int pwr5_offset(const struct rtk_sram_desc *desc)
{
	return desc->pwr5_offset ?: (desc->pwr_offset + SRAM_PWR5);
}

static void sram_clear_ints(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	regmap_write(regmap, pwr5_offset(desc), 0x4);
}

static void sram_setup_l2h_delay(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	if (!desc->l2h_delay)
		return;

	regmap_write(regmap, desc->pwr_offset + SRAM_PWR0, desc->l2h_delay);
}

static void sram_setup_h2l_delay(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	if (!desc->h2l_delay)
		return;

	regmap_write(regmap, desc->pwr_offset + SRAM_PWR1, desc->h2l_delay);
}

static void sram_setup_manual_mask(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	if (!desc->manual_mask)
		return;

	regmap_write(regmap, desc->pwr_offset + SRAM_PWR3, desc->manual_mask);
}

static void sram_setup_std_delay(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	if (!desc->pwr8_offset || !desc->std_delay)
		return;

	regmap_write(regmap, desc->pwr8_offset, desc->std_delay);
}

static void sram_setup_config(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	sram_setup_l2h_delay(regmap, desc);

	sram_setup_h2l_delay(regmap, desc);

	sram_setup_manual_mask(regmap, desc);

	sram_setup_std_delay(regmap, desc);
}

static int sram_poll_ints(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	unsigned int pollval;

	return regmap_read_poll_timeout(regmap, pwr5_offset(desc),
		pollval, pollval == 0x4, 0, 500);
}

static int sram_set_power(struct regmap *regmap, const struct rtk_sram_desc *desc, int on_off)
{
	unsigned int pwr4 = desc->pwr_offset + SRAM_PWR4;
	unsigned int val = on_off ? desc->val_on : desc->val_off;
	unsigned int reg;

	sram_setup_config(regmap, desc);

	regmap_read(regmap, pwr4, &reg);
	if ((reg & 0xff) == val)
		return 1;
	val |= desc->last_sd_ch << 8;

	trace_rtk_pm_reg_set("sram", pwr4, val);

	regmap_write(regmap, pwr4, val);

	return sram_poll_ints(regmap, desc);
}

int rtk_sram_power_on(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	int ret;

	ret = sram_set_power(regmap, desc, 1);
	sram_clear_ints(regmap, desc);
	return ret;
}

int rtk_sram_power_off(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	int ret;

	ret = sram_set_power(regmap, desc, 0);
	sram_clear_ints(regmap, desc);
	return 0;
}

int rtk_sram_power_state(struct regmap *regmap, const struct rtk_sram_desc *desc)
{
	unsigned int val;
	unsigned int pwr4 = desc->pwr_offset + SRAM_PWR4;

	regmap_read(regmap, pwr4, &val);
	return (val & 0xff) == desc->val_on;
}

