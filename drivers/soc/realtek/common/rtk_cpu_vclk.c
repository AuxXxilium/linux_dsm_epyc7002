// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <soc/realtek/rtk_cpuhp.h>

struct freq_map {
	unsigned long freq;
	unsigned long target_freq;
	int           cpuhp_num;
};

struct rtk_cpu_vclk_data {
	struct device                 *dev;
	struct clk_hw                 hw;
	struct rtk_cpuhp_qos_request  req;
	unsigned long                 cur;
	const struct freq_map         *maps;
	int                           num_maps;
	struct clk                    *clk;
};

static const struct freq_map *lookup_freq_map(struct rtk_cpu_vclk_data *data, unsigned long freq)
{
	int i;

	for (i = 0; i < data->num_maps; i++) {
		if (data->maps[i].freq == freq)
			return &data->maps[i];
	}
	return NULL;
}

static int rtk_cpu_vclk_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct rtk_cpu_vclk_data *data = container_of(hw, struct rtk_cpu_vclk_data, hw);
	const struct freq_map *map;
	int ret;
	unsigned long target_freq = rate;
	int cpuhp_num = 0;

	dev_dbg(data->dev, "%s: rate=%lu\n", __func__, rate);

	map = lookup_freq_map(data, rate);
	if (map) {
		target_freq = map->target_freq;
		cpuhp_num = map->cpuhp_num;
	}

	ret = clk_set_rate(data->clk, target_freq);
	if (ret)
		return ret;
	rtk_cpuhp_qos_update_request(&data->req, cpuhp_num);
	data->cur = rate;
	return ret;
}

static unsigned long rtk_cpu_vclk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct rtk_cpu_vclk_data *data = container_of(hw, struct rtk_cpu_vclk_data, hw);

	return data->cur ?: clk_get_rate(data->clk);
}

static long rtk_cpu_vclk_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *parent_rate)
{
	struct rtk_cpu_vclk_data *data = container_of(hw, struct rtk_cpu_vclk_data, hw);

	return clk_round_rate(data->clk, rate);
}

static const struct clk_ops rtk_cpu_vclk_ops = {
	.round_rate  = rtk_cpu_vclk_round_rate,
	.recalc_rate = rtk_cpu_vclk_recalc_rate,
	.set_rate    = rtk_cpu_vclk_set_rate,
};

static int rtk_cpu_vclk_parse_freq_maps(struct rtk_cpu_vclk_data *data)
{
	struct device_node *np = data->dev->of_node;
	const struct property *prop;
	const __be32 *val;
	int len;
	struct freq_map *maps;
	int i;

	prop = of_find_property(np, "freq-maps", NULL);
	if (!prop || !prop->value || (prop->length % 12) != 0)
		return -EINVAL;

	len = prop->length / 12;
	val = prop->value;

	maps = devm_kcalloc(data->dev, len, sizeof(*maps), GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		maps[i].freq        = be32_to_cpup(val++) * 1000;
		maps[i].target_freq = be32_to_cpup(val++) * 1000;
		maps[i].cpuhp_num   = be32_to_cpup(val++);
	}

	data->maps = maps;
	data->num_maps = len;
	return 0;
}

static void rtk_cpu_vclk_remove_of_clk_provider(void *d)
{
	struct rtk_cpu_vclk_data *data = d;

	of_clk_del_provider(data->dev->of_node);
}

static int rtk_cpu_vclk_add_clk(struct rtk_cpu_vclk_data *data)
{
	struct device *dev = data->dev;
	struct clk_init_data init_data = {
		.name         = "vclk_scpu",
		.ops          = &rtk_cpu_vclk_ops,
		.num_parents  = 0,
		.flags        = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	data->hw.init = &init_data;
	ret = devm_clk_hw_register(dev, &data->hw);
	if (ret)
		return ret;

	ret = of_clk_add_provider(dev->of_node, of_clk_src_simple_get, data->hw.clk);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, rtk_cpu_vclk_remove_of_clk_provider, data);
}

static int rtk_cpu_vclk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_cpu_vclk_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;

	data->clk = devm_clk_get(dev, NULL);
	ret = PTR_ERR_OR_ZERO(data->clk);
	if (ret) {
		if (ret == -EPROBE_DEFER)
                        dev_info(dev, "clock not ready, retry\n");
		else
			dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	ret = rtk_cpu_vclk_parse_freq_maps(data);
	if (ret) {
		dev_err(dev, "failed to parse freq map: %d\n", ret);
		return ret;
	}

	ret = rtk_cpu_vclk_add_clk(data);
	if (ret) {
		dev_err(dev, "failed to add clk: %d\n", ret);
		return ret;
	}

	ret = rtk_cpuhp_qos_add_request(&data->req, 0);
	if (ret) {
		dev_err(dev, "failed to add cpuhp qos request: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, data);
	return 0;
}

static int rtk_cpu_vclk_remove(struct platform_device *pdev)
{
	struct rtk_cpu_vclk_data *data = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	rtk_cpuhp_qos_remove_request(&data->req);
	return 0;
}

static const struct of_device_id rtk_cpu_vclk_ids[] = {
	{ .compatible = "realtek,cpu-vclk" },
	{}
};

static struct platform_driver rtk_cpu_vclk_driver = {
	.probe = rtk_cpu_vclk_probe,
	.remove = rtk_cpu_vclk_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "rtk-cpu-vclk",
		.of_match_table = rtk_cpu_vclk_ids,
	},
};
module_platform_driver(rtk_cpu_vclk_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rtk-cpu-vclk");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_DESCRIPTION("Realtek CPU Virtual Clock Controller");
