// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/printk.h>
#include <linux/reset.h>
#include <linux/slab.h>

struct rtk_vcpu_data {
	struct device *dev;
	struct clk *clk;
	struct reset_control *reset;

	struct devfreq_dev_profile profile;
	struct devfreq *devfreq;
};

static int rtk_vcpu_power_on( struct rtk_vcpu_data *vcpu_data)
{
	reset_control_deassert(vcpu_data->reset);
	clk_prepare_enable(vcpu_data->clk);
	return 0;
}

static int rtk_vcpu_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct rtk_vcpu_data *vcpu_data = dev_get_drvdata(dev);

	clk_disable_unprepare(vcpu_data->clk);
	clk_set_rate(vcpu_data->clk, *freq);
	clk_prepare_enable(vcpu_data->clk);
	return 0;
}

static int rtk_vcpu_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct rtk_vcpu_data *vcpu_data = dev_get_drvdata(dev);

	*freq = clk_get_rate(vcpu_data->clk);
	return 0;
}

static int rtk_vcpu_init_devfreq(struct rtk_vcpu_data *vcpu_data)
{
	struct device *dev = vcpu_data->dev;
	struct device_node *np = dev->of_node;
	int ret;

	if (!of_find_property(np, "operating-points-v2", NULL))
		return 0;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table: %d\n", ret);
		return ret;
	}

	vcpu_data->profile.get_cur_freq = rtk_vcpu_get_cur_freq;
	vcpu_data->profile.target       = rtk_vcpu_target;
	vcpu_data->profile.initial_freq = clk_get_rate(vcpu_data->clk);
	vcpu_data->devfreq = devm_devfreq_add_device(dev, &vcpu_data->profile, "userspace", NULL);
	if (!IS_ERR(vcpu_data->devfreq))
		return 0;

	ret = PTR_ERR(vcpu_data->devfreq);
	dev_err(dev, "failed to add devfreq: %d\n", ret);
	dev_pm_opp_of_remove_table(dev);
	return ret;
}

static int rtk_vcpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_vcpu_data *vcpu_data;
	int ret;

	vcpu_data = devm_kzalloc(dev, sizeof(*vcpu_data), GFP_KERNEL);
	if (!vcpu_data)
		return -ENOMEM;
	vcpu_data->dev = dev;

	vcpu_data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vcpu_data->clk)) {
		ret = PTR_ERR(vcpu_data->clk);
		dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	vcpu_data->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(vcpu_data->reset)) {
		ret = PTR_ERR(vcpu_data->reset);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	rtk_vcpu_init_devfreq(vcpu_data);

	rtk_vcpu_power_on(vcpu_data);
	platform_set_drvdata(pdev, vcpu_data);
	return 0;
}

static int rtk_vcpu_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rtk_vcpu_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id rtk_vcpu_match[] = {
	{ .compatible = "realtek,rtd1319-vcpu-firmware", },
	{}
};

static struct platform_driver rtk_vcpu_driver = {
	.probe    = rtk_vcpu_probe,
	.remove   = rtk_vcpu_remove,
	.shutdown = rtk_vcpu_shutdown,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-vcpu",
		.of_match_table = of_match_ptr(rtk_vcpu_match),
	},
};
module_platform_driver(rtk_vcpu_driver);

MODULE_DESCRIPTION("Realtek Video Firmware driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-vcpu");
