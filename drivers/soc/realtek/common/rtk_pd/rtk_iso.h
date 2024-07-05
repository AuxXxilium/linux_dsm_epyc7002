/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */
#ifndef __SOC_REALTEK_ISO_H
#define __SOC_REALTEK_ISO_H

struct rtk_iso_desc {
	unsigned int iso_offset;
	unsigned int iso_bit;
	unsigned int iso_2_offset;
	unsigned int iso_2_bit;
};

#define SET_RTK_ISO_CONF(_off, _bit) \
	.iso_offset = _off,          \
	.iso_bit    = _bit

#define SET_RTK_ISO_2_CONF(_off, _bit) \
	.iso_2_offset = _off,          \
	.iso_2_bit    = _bit

struct regmap;
void rtk_iso_power_on(struct regmap *regmap, const struct rtk_iso_desc *desc);
void rtk_iso_power_off(struct regmap *regmap, const struct rtk_iso_desc *desc);
int rtk_iso_power_state(struct regmap *regmap, const struct rtk_iso_desc *desc);

#endif
