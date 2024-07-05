// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/regmap.h>
#include <linux/module.h>
#include <trace/events/rtk_pm.h>
#include "rtk_iso.h"

MODULE_LICENSE("GPL v2");

static void update_iso_bit(struct regmap *regmap, int offset, int bit, int is_set)
{
	unsigned int mask, val;

	mask = BIT(bit);
	val  = is_set ? mask : 0;

	trace_rtk_pm_reg_update_bits("iso", offset, mask, val);

	regmap_update_bits(regmap, offset, mask, val);
}

void rtk_iso_power_on(struct regmap *regmap, const struct rtk_iso_desc *desc)
{
	update_iso_bit(regmap, desc->iso_offset, desc->iso_bit, 0);

	if (!desc->iso_2_offset)
		return;

	update_iso_bit(regmap, desc->iso_2_offset, desc->iso_2_bit, 0);
}

void rtk_iso_power_off(struct regmap *regmap, const struct rtk_iso_desc *desc)
{
	update_iso_bit(regmap, desc->iso_offset, desc->iso_bit, 1);

	if (!desc->iso_2_offset)
		return;
	update_iso_bit(regmap, desc->iso_2_offset, desc->iso_2_bit, 1);
}

int rtk_iso_power_state(struct regmap *regmap, const struct rtk_iso_desc *desc)
{
	unsigned int val;
	unsigned int pwr;

	regmap_read(regmap, desc->iso_offset, &val);
	pwr = (val & BIT(desc->iso_bit)) == 0 ? 1 : 0;

	if (!desc->iso_2_offset)
		return pwr;

	regmap_read(regmap, desc->iso_2_offset, &val);
	pwr |= (val & BIT(desc->iso_2_bit)) == 0 ? 2 : 0;
	return pwr;
}
