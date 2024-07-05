// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek Generic Power Controller
 *
 * Copyright (C) 2021 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <trace/events/rtk_pm.h>

#include "rtk_sram.h"
#include "rtk_iso.h"

struct rtk_power_desc {
	const char             *name;
	struct rtk_sram_desc   sram;
	struct rtk_iso_desc    iso;
	int                    no_suppliers;
	int                    rst_only_reset;
};

#define SET_PWR(_name, _ofs, _iso_bit) \
	.name = _name, \
	.sram = { SET_RTK_SRAM_CONF(_ofs, 0xf), }, \
	.iso = { SET_RTK_ISO_CONF(0xfd0, _iso_bit), }

struct rtk_power_data {
	struct device                *dev;
	struct generic_pm_domain     genpd;
	const struct rtk_power_desc  *desc;
	struct regmap                *regmap;
	struct clk                   *clk;
	struct reset_control         *rstn;
	struct reset_control         *rstn_bist;
	struct reset_controller_dev  rcdev;
};

static struct rtk_power_desc desc_ve1_rtd1319 = {
	SET_PWR("ve1", 0xb00, 0),
	.rst_only_reset = 1
};

static struct rtk_power_desc desc_ve2_rtd1319 = {
	SET_PWR("ve2", 0xb20, 1),
};

static struct rtk_power_desc desc_gpu = {
	SET_PWR("gpu", 0xb60, 3),
	.no_suppliers = 1,
};

static struct rtk_power_desc desc_ve3_rtd1319 = {
	SET_PWR("ve3", 0x290, 10),
	.rst_only_reset = 1
};

static void rtk_power_setup_suppliers(struct rtk_power_data *pd, int already_power_on)
{
	if (pd->desc->no_suppliers)
		return;

	clk_prepare_enable(pd->clk);

	if (pd->desc->rst_only_reset) {
		if (!already_power_on) {
			reset_control_assert(pd->rstn_bist);
			reset_control_reset(pd->rstn);
		}
		reset_control_deassert(pd->rstn_bist);
	} else {
		if (!already_power_on)
			reset_control_assert(pd->rstn_bist);
		reset_control_deassert(pd->rstn);
		reset_control_deassert(pd->rstn_bist);
	}
}

static void rtk_power_shutdown_suppliers(struct rtk_power_data *pd)
{
	if (pd->desc->no_suppliers)
		return;

	if (!pd->desc->rst_only_reset)
		reset_control_assert(pd->rstn);
	clk_disable_unprepare(pd->clk);
}

static int rtk_power_is_on(struct rtk_power_data *pd)
{
	return rtk_sram_power_state(pd->regmap, &pd->desc->sram);
}

static int rtk_power_genpd_power_on(struct generic_pm_domain *genpd)
{
	struct rtk_power_data *pd = container_of(genpd, struct rtk_power_data, genpd);
	int ret;

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_power_on");

	ret = rtk_sram_power_on(pd->regmap, &pd->desc->sram);

	rtk_power_setup_suppliers(pd, ret > 0);

	rtk_iso_power_on(pd->regmap, &pd->desc->iso);

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_power_on_completed");
	return 0;
}

static int rtk_power_genpd_power_off(struct generic_pm_domain *genpd)
{
	struct rtk_power_data *pd = container_of(genpd, struct rtk_power_data, genpd);

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_power_off");

	rtk_iso_power_off(pd->regmap, &pd->desc->iso);

	rtk_power_shutdown_suppliers(pd);

	rtk_sram_power_off(pd->regmap, &pd->desc->sram);

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_power_off_completed");
	return 0;
}

static int rtk_power_genpd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	struct rtk_power_data *pd = container_of(genpd, struct rtk_power_data, genpd);

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_attach_dev");

	pr_debug("%s: %s %s %s\n", genpd->name, __func__, dev_driver_string(dev), dev_name(dev));

	return 0;
}

static void rtk_power_genpd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	struct rtk_power_data *pd = container_of(genpd, struct rtk_power_data, genpd);

	trace_rtk_pm_event(dev_name(pd->dev), "genpd_detach_dev");

	pr_debug("%s: %s %s %s\n", genpd->name, __func__, dev_driver_string(dev), dev_name(dev));
}

static int rtk_power_reset_reset(struct reset_controller_dev *rcdev, unsigned long idx)
{
	struct rtk_power_data *pd = container_of(rcdev, struct rtk_power_data, rcdev);

	trace_rtk_pm_event(dev_name(pd->dev), "reset_reset");
	return reset_control_reset(pd->rstn);
}

static const struct reset_control_ops rtk_power_reset_ops = {
	.reset = rtk_power_reset_reset,
};

static int rtk_power_reset_of_xlate(struct reset_controller_dev *rcdev,
				    const struct of_phandle_args *reset_spec)
{
        if (WARN_ON(reset_spec->args_count != 0))
                return -EINVAL;

        return 0;
}


static int rtk_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rtk_power_data *pd;
	int ret;
	int power_off;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->dev = dev;
	pd->desc = of_device_get_match_data(dev);
	if (!pd->desc) {
		dev_err(dev, "no match data\n");
		return -EINVAL;
	}

	pd->regmap = syscon_node_to_regmap(np->parent);
        if (IS_ERR(pd->regmap)) {
                ret = PTR_ERR(pd->regmap);
		dev_err(dev, "failed to get syscon regmap from parent: %d\n", ret);
		return ret;
	}

	if (pd->desc->no_suppliers)
		goto skip_init_suppliers;

	pd->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pd->clk)) {
		ret = PTR_ERR(pd->clk);
		if (ret == -EPROBE_DEFER)
			dev_err(dev, "clk not ready, retry\n");
		else
			dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	pd->rstn = devm_reset_control_get_exclusive(dev, "reset");
	if (IS_ERR(pd->rstn)) {
		ret = PTR_ERR(pd->rstn);
		if (ret == -EPROBE_DEFER)
			dev_err(dev, "rstn not ready, retry\n");
		else
			dev_err(dev, "failed to get rstn: %d\n", ret);
		return ret;
	}

	pd->rstn_bist = devm_reset_control_get_optional_exclusive(dev, "bist");
	if (IS_ERR(pd->rstn_bist)) {
		ret = PTR_ERR(pd->rstn_bist);
		if (ret == -EPROBE_DEFER)
			dev_err(dev, "rstn_bist not ready, retry\n");
		else
			dev_err(dev, "failed to get rstn_bist: %d\n", ret);
		return ret;
	}

	if (pd->rstn) {
		pd->rcdev.owner            = THIS_MODULE;
		pd->rcdev.ops              = &rtk_power_reset_ops;
		pd->rcdev.nr_resets        = 1,
		pd->rcdev.of_node          = dev->of_node;
		pd->rcdev.of_reset_n_cells = 0;
		pd->rcdev.of_xlate         = rtk_power_reset_of_xlate;

		ret = devm_reset_controller_register(dev, &pd->rcdev);
		if (ret) {
			dev_err(dev, "failed to register reset_controller: %d\n", ret);
			return ret;
		}
	}

skip_init_suppliers:

	trace_rtk_pm_event(dev_name(dev), "init");

	power_off = !rtk_power_is_on(pd);
	if (!power_off)
		rtk_power_setup_suppliers(pd, 1);

	trace_rtk_pm_event(dev_name(dev), "init_completed");

	pd->genpd.name       = dev_name(dev);
	pd->genpd.power_on   = rtk_power_genpd_power_on;
	pd->genpd.power_off  = rtk_power_genpd_power_off;
	pd->genpd.attach_dev = rtk_power_genpd_attach_dev;
	pd->genpd.detach_dev = rtk_power_genpd_detach_dev;
	ret = pm_genpd_init(&pd->genpd, NULL, power_off);
	if (ret) {
		dev_err(dev, "failed to init genpd: %d\n", ret);
		return ret;
	}

	ret = of_genpd_add_provider_simple(np, &pd->genpd);
	if (ret) {
		dev_err(dev, "failed to add genpd of provider: %d\n", ret);
		pm_genpd_remove(&pd->genpd);
	}
	return ret;
}

static const struct of_device_id rtk_power_match[] = {
	{ .compatible = "realtek,gpu-power", .data = &desc_gpu, },
	{ .compatible = "realtek,rtd1319-ve1-power", .data = &desc_ve1_rtd1319, },
	{ .compatible = "realtek,rtd1319-ve2-power", .data = &desc_ve2_rtd1319, },
	{ .compatible = "realtek,rtd1319-ve3-power", .data = &desc_ve3_rtd1319, },
	{}
};

static struct platform_driver rtk_power_driver = {
	.probe = rtk_power_probe,
	.driver = {
		.name = "rtk-gpc",
		.of_match_table = of_match_ptr(rtk_power_match),
	},
};

static int __init rtk_power_init(void)
{
	return platform_driver_register(&rtk_power_driver);
}
fs_initcall(rtk_power_init);

MODULE_DESCRIPTION("Realtek Generic Power Controller");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
