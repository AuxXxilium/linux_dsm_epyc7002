// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/types.h>

enum clk_output_sel {
	CLK_OUTPUT_DIRECT = 0,
	CLK_OUTPUT_VSFC,
};

struct vsfc_ctrl_data {
	struct device         *dev;
	struct clk_hw         hw;
	struct regmap         *crt;
	struct regmap         *sc_wrap;
	struct clk            *clk;
	struct regulator      *supply;
	enum clk_output_sel   output;
	unsigned long         threshold_freq;
	int                   threshold_volt;
};

/* crt register offsets */
#define SYS_PLL_SCPU1       0x100
#define SYS_PLL_SCPU3       0x108

/* scpu wrapper register offsets */
#define FSS_CTRL0           0xb70
#define FSS_CTRL1           0xb74
#define FSS_CTRL2           0xb78
#define FSS_CTRL3           0xb7c

#define VSFC_CTRL           0xf00
#define VSFC_STATUS         0xf04
#define VSFC_TRIG_CTRL      0xf08
#define VSFC_RECV_CTRL0     0xf0c
#define VSFC_RECV_CTRL1     0xf10
#define VSFC_PROF_CTRL0     0xf24
#define VSFC_PROF_INTST     0xf3c
#define VSFC_PROF_INTEN     0xf40

static void vsfc_ctrl_enable_fss(struct vsfc_ctrl_data *data)
{
	regmap_write(data->sc_wrap, FSS_CTRL0, 0x00000000);
	udelay(200);

	regmap_write(data->sc_wrap, FSS_CTRL0, 0x3f000000);
	udelay(200);

	regmap_write(data->sc_wrap, FSS_CTRL1, 0x00eeeeee);
	regmap_write(data->sc_wrap, FSS_CTRL2, 0x00ffffff);
	regmap_write(data->sc_wrap, FSS_CTRL3, 0x00eeeeee);

	regmap_write(data->sc_wrap, FSS_CTRL0, 0x3f00003f);
}

static void vsfc_ctrl_disable_fss(struct vsfc_ctrl_data *data)
{
	regmap_write(data->sc_wrap, FSS_CTRL0, 0);
}

static void vsfc_ctrl_set_output_vsfc(struct vsfc_ctrl_data *data)
{
	regmap_update_bits(data->crt, SYS_PLL_SCPU1, 0x43000000, 0x40000000);
	regmap_update_bits(data->crt, SYS_PLL_SCPU3, 0x00380000, 0x00280000);
}

static void vsfc_ctrl_set_output_direct(struct vsfc_ctrl_data *data)
{
	regmap_update_bits(data->crt, SYS_PLL_SCPU1, 0x43000000, 0x00000000);
	regmap_update_bits(data->crt, SYS_PLL_SCPU3, 0x00380000, 0x00180000);
}

static void vsfc_ctrl_setup_vsfc_freq(struct vsfc_ctrl_data *data, uint32_t val)
{
	regmap_write(data->sc_wrap, VSFC_CTRL,       0x00000400);
	regmap_write(data->sc_wrap, VSFC_TRIG_CTRL,  0x00000000);
	regmap_write(data->sc_wrap, VSFC_RECV_CTRL1, val);
	regmap_write(data->sc_wrap, VSFC_RECV_CTRL0, 0x00000002);
	regmap_write(data->sc_wrap, VSFC_CTRL,       0x00000401);
}

static void vsfc_ctrl_update_vsfc_freq(struct vsfc_ctrl_data *data, uint32_t val)
{
	regmap_write(data->sc_wrap, VSFC_CTRL,       0x00000400);
	regmap_write(data->sc_wrap, VSFC_RECV_CTRL1, val);
	regmap_write(data->sc_wrap, VSFC_CTRL,       0x00000401);
}

static void vsfc_ctrl_takedown_vsfc(struct vsfc_ctrl_data *data)
{
	regmap_write(data->sc_wrap, VSFC_CTRL, 0x00000000);
}

struct freq_lowf {
	unsigned long  freq;
	uint32_t       lowf;
};

static const struct freq_lowf vsfc_freq_table[] = {
	{ 624000000, 0x49490010 },
	{ 806400000, 0x81010010 },
	{ 0, 0 }
};

static uint32_t freq_to_lowf(unsigned long target)
{
	int i;

	for (i = 0; vsfc_freq_table[i].freq != 0; i++)
		if (vsfc_freq_table[i].freq == target)
			return vsfc_freq_table[i].lowf;
	return 0;
}

static unsigned long lowf_to_freq(uint32_t target)
{
	int i;

	for (i = 0; vsfc_freq_table[i].freq != 0; i++)
		if (vsfc_freq_table[i].lowf == target)
			return vsfc_freq_table[i].freq;
	return 0;
}

static unsigned long freq_round(unsigned long target)
{
	int i;
	unsigned long closest = 0;

	for (i = 0; vsfc_freq_table[i].freq != 0; i++) {

		if (vsfc_freq_table[i].freq == target)
			return target;

		if (vsfc_freq_table[i].freq < target && (target - vsfc_freq_table[i].freq) < (target - closest))
			closest = vsfc_freq_table[i].freq;
	}

	return closest;
}

static int vsfc_ctrl_update_output(struct vsfc_ctrl_data *data, enum clk_output_sel next)
{
	int ret = 0;

	if (data->output != next)
		ret = 1;
	data->output = next;
	return ret;
}

static int vsfc_ctrl_output_is_direct(struct vsfc_ctrl_data *data)
{
	return data->output == CLK_OUTPUT_DIRECT;
}

static int vsfc_ctrl_set_vfsc_freq(struct vsfc_ctrl_data *data, unsigned long target)
{
	uint32_t val = freq_to_lowf(target);

	if (!val)
		return -EINVAL;

	if (!vsfc_ctrl_update_output(data, CLK_OUTPUT_VSFC)) {
		vsfc_ctrl_update_vsfc_freq(data, val);
		return 0;
	}

	clk_set_rate(data->clk, data->threshold_freq);

	regulator_set_voltage(data->supply, data->threshold_volt, data->threshold_volt);

	vsfc_ctrl_enable_fss(data);

	vsfc_ctrl_set_output_vsfc(data);

	vsfc_ctrl_setup_vsfc_freq(data, val);

	return 0;
}

static int vsfc_ctrl_set_pll_freq(struct vsfc_ctrl_data *data, unsigned long target)
{
	if (vsfc_ctrl_update_output(data, CLK_OUTPUT_DIRECT)) {
		vsfc_ctrl_takedown_vsfc(data);

		vsfc_ctrl_set_output_direct(data);

		vsfc_ctrl_disable_fss(data);
	}

	return clk_set_rate(data->clk, target);
}

static unsigned long vsfc_ctrl_get_current_freq(struct vsfc_ctrl_data *data)
{
	uint32_t val;

	regmap_read(data->sc_wrap, VSFC_RECV_CTRL1, &val);

	return lowf_to_freq(val);
}

static int vsfc_ctrl_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct vsfc_ctrl_data *data = container_of(hw, struct vsfc_ctrl_data, hw);
	int ret;

	dev_dbg(data->dev, "%s: rate=%lu\n", __func__, rate);

	if (rate >= data->threshold_freq)
		ret = vsfc_ctrl_set_pll_freq(data, rate);
	else
		ret = vsfc_ctrl_set_vfsc_freq(data, rate);
	return ret;
}

static unsigned long vsfc_ctrl_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct vsfc_ctrl_data *data = container_of(hw, struct vsfc_ctrl_data, hw);

	if (vsfc_ctrl_output_is_direct(data))
		return clk_get_rate(data->clk);
	else
		return vsfc_ctrl_get_current_freq(data);
}

static long vsfc_ctrl_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *parent_rate)
{
	struct vsfc_ctrl_data *data = container_of(hw, struct vsfc_ctrl_data, hw);

	if (rate >= data->threshold_freq)
		return clk_round_rate(data->clk, rate);
	else
		return freq_round(rate);
}

static const struct clk_ops vsfc_ctrl_clk_ops = {
	.round_rate  = vsfc_ctrl_round_rate,
	.recalc_rate = vsfc_ctrl_recalc_rate,
	.set_rate    = vsfc_ctrl_set_rate,
};

static void vsfc_ctrl_remove_of_clk_provider(void *d)
{
	struct vsfc_ctrl_data *data = d;

	of_clk_del_provider(data->dev->of_node);
}

static int vsfc_ctrl_add_clk(struct vsfc_ctrl_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	struct clk_init_data init_data = {
		.name         = "vsfc",
		.ops          = &vsfc_ctrl_clk_ops,
		.num_parents  = 0,
		.flags        = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	data->hw.init = &init_data;
	ret = devm_clk_hw_register(dev, &data->hw);
	if (ret)
		return ret;

	ret = of_clk_add_provider(np, of_clk_src_simple_get, data->hw.clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, vsfc_ctrl_remove_of_clk_provider, data);
	if (ret)
		return ret;

	return 0;
}

static int vsfc_ctrl_regulator_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	struct vsfc_ctrl_data *data = rdev_get_drvdata(rdev);

	return regulator_list_voltage(data->supply, selector);
}

static int vsfc_ctrl_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct vsfc_ctrl_data *data = rdev_get_drvdata(rdev);

	return regulator_get_voltage(data->supply);
}

static int vsfc_ctrl_regulator_set_voltage(struct regulator_dev *rdev,
	int min_uV, int max_uV, unsigned *selector)
{
	struct vsfc_ctrl_data *data = rdev_get_drvdata(rdev);

	dev_dbg(data->dev, "%s: voltage=(%d, %d)\n", __func__, min_uV, max_uV);
	return regulator_set_voltage(data->supply, min_uV, max_uV);
}

static const struct regulator_ops vsfc_ctrl_regulator_ops = {
	.get_voltage  = vsfc_ctrl_regulator_get_voltage,
	.set_voltage  = vsfc_ctrl_regulator_set_voltage,
	.list_voltage = vsfc_ctrl_regulator_list_voltage,
};

static struct regulator_desc vsfc_ctrl_supply_desc = {
	 .owner          = THIS_MODULE,
	 .ops            = &vsfc_ctrl_regulator_ops,
	 .type           = REGULATOR_VOLTAGE,
	 .name           = "vsfc",
};

static int vsfc_ctrl_add_supply(struct vsfc_ctrl_data *data)
{
	struct regulator_config config = {
		.dev         = data->dev,
		.of_node     = data->dev->of_node,
		.driver_data = data,
	};

	vsfc_ctrl_supply_desc.n_voltages = regulator_count_voltages(data->supply);

	config.init_data = of_get_regulator_init_data(data->dev, data->dev->of_node,
			&vsfc_ctrl_supply_desc);

	if (!config.init_data)
		return -ENOMEM;

	return PTR_ERR_OR_ZERO(devm_regulator_register(data->dev, &vsfc_ctrl_supply_desc, &config));
}

static int vsfc_ctrl_add_suppliers(struct vsfc_ctrl_data *data)
{
	int ret;

	ret = vsfc_ctrl_add_clk(data);
	if (ret) {
		dev_err(data->dev, "failed to add clk: %d\n", ret);
		return ret;
	}

	ret = vsfc_ctrl_add_supply(data);
	if (ret) {
		dev_err(data->dev, "failed to add supply: %d\n", ret);
		return ret;
	}

	return 0;
}

static int vsfc_ctrl_of_parse_threshold(struct device_node *np, struct vsfc_ctrl_data *data)
{
	struct device_node *opp_np;
	uint64_t f;
	uint32_t v;
	int ret;

	opp_np = of_parse_phandle(np, "threshold-opp", 0);
	if (!opp_np)
		return -EINVAL;

	ret = of_property_read_u64(opp_np, "opp-hz", &f);
	if (ret)
		return ret;

	ret = of_property_read_u32(opp_np, "opp-microvolt-fss", &v);
	if (ret || v == 0)
		ret = of_property_read_u32(opp_np, "opp-microvolt", &v);
	if (ret)
		return ret;

	data->threshold_freq = f;
	data->threshold_volt = v;
	return 0;
}

static int vsfc_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct vsfc_ctrl_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;

	ret = vsfc_ctrl_of_parse_threshold(np, data);
	if (ret) {
		dev_err(dev, "failed to parse threshold: %d\n", ret);
		return ret;
	}

	data->crt = syscon_regmap_lookup_by_phandle(np, "realtek,crt");
	if (IS_ERR(data->crt)) {
		ret = PTR_ERR(data->crt);
		dev_err(dev, "failed to get crt syscon: %d\n", ret);
		return ret;
	}

	data->sc_wrap = syscon_regmap_lookup_by_phandle(np, "realtek,sc-wrap");
	if (IS_ERR(data->sc_wrap)) {
		ret = PTR_ERR(data->sc_wrap);
		dev_err(dev, "failed to get sc_wrap syscon: %d\n", ret);
		return ret;
	}

	data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	data->supply = devm_regulator_get(dev, "cpu");
	if (IS_ERR(data->supply)) {
		ret = PTR_ERR(data->supply);
		dev_err(dev, "failed to get supply: %d\n", ret);
		return ret;
	}

	return vsfc_ctrl_add_suppliers(data);
}

static const struct of_device_id vsfc_ctrl_match[] = {
	{ .compatible = "realtek,vsfc-ctrl", },
	{}
};

static struct platform_driver vsfc_ctrl_driver = {
	.probe    = vsfc_ctrl_probe,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-vsfc-ctrl",
		.of_match_table = of_match_ptr(vsfc_ctrl_match),
	},
};
module_platform_driver(vsfc_ctrl_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rtk-vsfc-ctrl");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_DESCRIPTION("Realtek VSFC controller");
