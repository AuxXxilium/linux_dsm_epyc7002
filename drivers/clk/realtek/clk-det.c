
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include "clk-det.h"

struct clk_det_desc {
	uint32_t ctrl_rstn_bit;
	uint32_t ctrl_cnten_bit;
	uint32_t stat_ofs;
	uint32_t stat_done_bit;
	uint32_t cnt_mask;
	uint32_t cnt_shift;
};

static const struct clk_det_desc clk_det_descs[2] = {
	[CLK_DET_TYPE_CRT] = {
		0, 1, 0, 30, 0x3FFFC000, 13
	},
	[CLK_DET_TYPE_SC_WRAP] = {
		17, 16, 8, 0, 0x0001FFFE, 1
	},
};

static DEFINE_MUTEX(clk_det_lock);

static unsigned long clk_det_get_freq(struct clk_det *clkd)
{
	struct clk_regmap *clkr = &clkd->clkr;
	const struct clk_det_desc *desc = &clk_det_descs[clkd->type];
	uint32_t ctrl_mask;
	uint32_t val;
	unsigned long freq = 0;
	int ret;

	mutex_lock(&clk_det_lock);

	ctrl_mask = BIT(desc->ctrl_rstn_bit) | BIT(desc->ctrl_cnten_bit);
	clk_regmap_update(clkr, clkd->ofs, ctrl_mask, 0);
	clk_regmap_update(clkr, clkd->ofs, ctrl_mask, BIT(desc->ctrl_rstn_bit));
	clk_regmap_update(clkr, clkd->ofs, ctrl_mask, ctrl_mask);

	ret = regmap_read_poll_timeout(clkr->regmap, clkd->ofs + desc->stat_ofs, val,
			val & BIT(desc->stat_done_bit), 0, 100);
	if (!ret) {
		val = clk_regmap_read(clkr, clkd->ofs + desc->stat_ofs);
		freq = ((val & desc->cnt_mask) >> desc->cnt_shift) * 100000;
	}

	clk_regmap_update(clkr, clkd->ofs, ctrl_mask, 0);

	mutex_unlock(&clk_det_lock);

	return freq;
}

static unsigned long clk_det_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_det *clkd = to_clk_det(hw);

	if (clkd->ref && !__clk_is_enabled(clkd->ref))
		return 0;

	return clk_det_get_freq(clkd);
}

const struct clk_ops clk_det_ops = {
	.recalc_rate = clk_det_recalc_rate,
};

struct clk_det_initdata {
	const char *name;
	uint32_t ofs;
	uint32_t type;
	struct clk *ref;
};

static int of_clk_det_initdata_parse(struct device_node *np, struct clk_det_initdata *data)
{
	int ret;

	ret = of_property_read_string_index(np, "clock-output-names", 0, &data->name);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "clk-det,offset", &data->ofs);
	if (ret)
		return ret;

	if (of_property_read_u32(np, "clk-det,type", &data->type))
		data->type = 0;
	return 0;
}

static int clk_det_plat_init(struct device *dev, struct rtk_clk_data *ctlr_data, struct clk_det_initdata *data)
{
	struct clk_det *clkd;
	struct clk_init_data initdata = { .name = data->name, .ops = &clk_det_ops, .flags = CLK_GET_RATE_NOCACHE};
	struct clk_hw *hws[1];

	clkd = devm_kzalloc(dev, sizeof(*clkd), GFP_KERNEL);
	if (!clkd)
		return -ENOMEM;

	clkd->clkr.hw.init = &initdata;
	clkd->ofs  = data->ofs;
	clkd->type = data->type;
	clkd->ref  = data->ref;
	hws[0] = &clkd->clkr.hw;

	return rtk_clk_add_hws(dev, ctlr_data, hws, 1);
}

static struct regmap *of_clk_det_syscon_get(struct device_node *np)
{
	struct device_node *parent;
	struct regmap *regmap;

	if (of_find_property(np, "syscon", NULL))
		return syscon_regmap_lookup_by_phandle(np, "syscon");

	parent = of_get_parent(np);
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);

	return regmap;
}

static int clk_det_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rtk_clk_data *ctlr_data;
	struct clk_det_initdata clk_det_initdata;
	int ret;

	ctlr_data = rtk_clk_alloc_data(1);
	if (!ctlr_data)
		return -ENOMEM;

	ctlr_data->regmap = of_clk_det_syscon_get(np);
	if (IS_ERR(ctlr_data->regmap)) {
		ret = PTR_ERR(ctlr_data->regmap);
		dev_err(dev, "failed to get regmap: %d\n", ret);
		goto free_data;
	}

	clk_det_initdata.ref = clk_get(dev, 0);
	if (IS_ERR(clk_det_initdata.ref))
		clk_det_initdata.ref = NULL;

	ret = of_clk_det_initdata_parse(np, &clk_det_initdata);
	if (ret){
		dev_err(dev, "failed to parse initdata: %d\n", ret);
		goto free_data;
	}

	ret = clk_det_plat_init(dev, ctlr_data, &clk_det_initdata);
	if (ret) {
		dev_err(dev, "failed to create clk_det: %d\n", ret);
		goto free_data;
	}

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &ctlr_data->clk_data);
	if (ret)
		dev_warn(dev, "failed to add clk provider: %d\n", ret);
	return 0;

free_data:
	rtk_clk_free_data(ctlr_data);
	return ret;
}

static const struct of_device_id clk_det_match[] = {
	{ .compatible = "realtek,clk-det", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_det_match);

static struct platform_driver clk_det_driver = {
	.probe = clk_det_plat_probe,
	.driver = {
		.name = "rtk-clk-det",
		.of_match_table = of_match_ptr(clk_det_match),
	},
};
module_platform_driver(clk_det_driver);
