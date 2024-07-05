/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */
#ifndef __SOC_REALTEK_SRAM_H
#define __SOC_REALTEK_SRAM_H

struct rtk_sram_desc {
	unsigned int pwr_offset;
	unsigned int pwr5_offset;
	unsigned int pwr8_offset;
	unsigned int last_sd_ch;
	unsigned int val_on;
	unsigned int val_off;
	unsigned int l2h_delay;
	unsigned int h2l_delay;
	unsigned int manual_mask;
	unsigned int std_delay;
};

#define SET_RTK_SRAM_CONF_COMM(_off, _off_pwr5, _ch, _on_val, _off_val) \
	.pwr_offset = _off, \
	.pwr5_offset = _off_pwr5, \
	.last_sd_ch = _ch, \
	.val_on = _on_val, \
	.val_off = _off_val

#define SET_RTK_SRAM_CONF(_off, _ch) \
	SET_RTK_SRAM_CONF_COMM(_off, 0, _ch, 0, 1)

#define SET_RTK_SRAM_CONF_PWR5(_off, _off_pwr5, _ch) \
	SET_RTK_SRAM_CONF_COMM(_off, _off_pwr5, _ch, 0, 1)

#define SET_RTK_SRAM_CONF_MANUAL_MASK(_mask) \
	.manual_mask = (_mask)

#define SET_RTK_SRAM_DELAY(_l2h, _h2l) \
	.l2h_delay = (_l2h),              \
	.h2l_delay = (_h2l)

#define SET_RTK_SRAM_STD_DELAY(_off, _std) \
	.pwr8_offset = (_off),                \
	.std_delay = (_std)

struct regmap;
int rtk_sram_power_on(struct regmap *regmap, const struct rtk_sram_desc *desc);
int rtk_sram_power_off(struct regmap *regmap, const struct rtk_sram_desc *desc);
int rtk_sram_power_state(struct regmap *regmap, const struct rtk_sram_desc *desc);

#endif
