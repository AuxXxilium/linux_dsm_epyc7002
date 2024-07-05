/*
 * ve1_pm.c - ve1 power management
 *
 * Copyright (c) 2019 Realtek Semiconductor Corporation
 *
 * Author:
 *      Cheng-Yu Lee <cylee12@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/reset.h>
#include "ve1_pm.h"

struct ve_pm_data {
	struct device *dev;
	struct clk *clk_ve1;
	struct clk *clk_ve3;
	struct reset_control *rstc_ve1;
	struct reset_control *rstc_ve1_bist;
	struct reset_control *rstc_ve3;
	struct reset_control *rstc_ve3_bist;
	atomic_t power_cnt;
	int inited;
};

static struct ve_pm_data g_vpd;

int ve_pd_init(struct device *dev)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk;
	struct reset_control *rstc;

	if (vpd->inited)
		return -EINVAL;

	vpd->dev = dev;
	vpd->inited = 1;
	atomic_set(&vpd->power_cnt, 0);

	clk = devm_clk_get(dev, "clk_ve1");
	if (IS_ERR(clk)) {
		dev_warn(dev, "failed to get clk_ve1: %ld\n", PTR_ERR(clk));
		clk = NULL;
	}
	vpd->clk_ve1 = clk;

	clk = devm_clk_get(dev, "clk_ve3");
	if (IS_ERR(clk)) {
		dev_warn(dev, "failed to get clk_ve1: %ld\n", PTR_ERR(clk));
		clk = NULL;
	}
	vpd->clk_ve3 = clk;

	rstc = devm_reset_control_get_exclusive(dev, "ve1");
	if (IS_ERR(rstc)) {
		dev_warn(dev, "failed to get reset control ve1: %ld\n", PTR_ERR(rstc));
		rstc = NULL;
	}
	vpd->rstc_ve1 = rstc;

	rstc = devm_reset_control_get_optional_exclusive(dev, "ve1_bist");
	if (IS_ERR(rstc)) {
		dev_warn(dev, "failed to get reset control ve1_bist: %ld\n", PTR_ERR(rstc));
		rstc = NULL;
	}
	vpd->rstc_ve1_bist = rstc;

	rstc = devm_reset_control_get_exclusive(dev, "ve3");
	if (IS_ERR(rstc)) {
		dev_warn(dev, "failed to get reset control ve3: %ld\n", PTR_ERR(rstc));
		rstc = NULL;
	}
	vpd->rstc_ve3 = rstc;

	rstc = devm_reset_control_get_optional_exclusive(dev, "ve3_bist");
	if (IS_ERR(rstc)) {
		dev_warn(dev, "failed to get reset control ve3_bist: %ld\n", PTR_ERR(rstc));
		rstc = NULL;
	}
	vpd->rstc_ve3_bist = rstc;

	return 0;
}

void ve_pd_exit(struct device *dev)
{
}

int ve_pd_power_on(int no_reset)
{
	struct ve_pm_data *vpd = &g_vpd;

	if (atomic_inc_return(&vpd->power_cnt) != 1)
		return -EINVAL;

	dev_info(vpd->dev, "%s\n", __func__);

	reset_control_deassert(vpd->rstc_ve1_bist);
	clk_prepare_enable(vpd->clk_ve1);
	if (!no_reset)
		reset_control_reset(vpd->rstc_ve1);

	reset_control_deassert(vpd->rstc_ve3_bist);
	clk_prepare_enable(vpd->clk_ve3);
	if (!no_reset)
		reset_control_reset(vpd->rstc_ve3);

	return 0;
}

int ve_pd_power_off(void)
{
	struct ve_pm_data *vpd = &g_vpd;

	if (atomic_dec_return(&vpd->power_cnt) != 0)
		return -EINVAL;

	dev_info(vpd->dev, "%s\n", __func__);

	clk_disable_unprepare(vpd->clk_ve1);
	reset_control_assert(vpd->rstc_ve1_bist);

	clk_disable_unprepare(vpd->clk_ve3);
	reset_control_assert(vpd->rstc_ve3_bist);

	return 0;
}

int ve_pd_reset_control_reset(int idx)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk = idx ? vpd->clk_ve3 : vpd->clk_ve1;
	struct reset_control *rstc = idx ? vpd->rstc_ve3 : vpd->rstc_ve1;
	int ret;

	if (!rstc)
		return -EINVAL;

	clk_prepare_enable(clk);
	ret = reset_control_reset(rstc);
	clk_disable_unprepare(clk);
	return ret;
}

int ve_pd_clk_set_parent(int idx, const char *parent_name)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk = idx ? vpd->clk_ve3 : vpd->clk_ve1;
	struct clk *pclk;
	int ret;

	if (!parent_name)
		return -EINVAL;

	if (atomic_read(&vpd->power_cnt) != 0)
		return -EBUSY;

	pclk = clk_get(NULL, parent_name);
	if (IS_ERR_OR_NULL(pclk))
		return -EINVAL;

	dev_info(vpd->dev, "%s: %pC: parent=%s\n", __func__, clk, parent_name);

	ret = clk_set_parent(clk, pclk);
	clk_put(pclk);

	return ret;
}

int ve_pd_clk_parent_match(int idx, const char *clk_name)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk = idx ? vpd->clk_ve3 : vpd->clk_ve1;
	struct clk *rclk;
	struct clk *pclk;
	bool is_match;

	if (!clk_name)
		return -EINVAL;

	rclk = clk_get(NULL, clk_name);
	if (IS_ERR_OR_NULL(rclk))
		return -EINVAL;

	pclk = clk_get_parent(clk);

	is_match = clk_is_match(pclk, rclk);
	clk_put(rclk);

	return is_match ? 1 : 0;
}


int ve_pd_clk_set_rate(int idx, unsigned long rate)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk = idx ? vpd->clk_ve3 : vpd->clk_ve1;
	int ret;

	/* should not set rate if parent is clk_sysh */
	ret = ve_pd_clk_parent_match(idx, "clk_sysh");
	if (ret > 0)
		return -EINVAL;

	pr_debug("%s: %pC: rate=%ld\n",  __func__, clk, rate);

	return clk_set_rate(clk, rate);
}

unsigned long ve_pd_clk_get_rate(int idx)
{
	struct ve_pm_data *vpd = &g_vpd;
	struct clk *clk = idx ? vpd->clk_ve3 : vpd->clk_ve1;

	return clk_get_rate(clk);
}

