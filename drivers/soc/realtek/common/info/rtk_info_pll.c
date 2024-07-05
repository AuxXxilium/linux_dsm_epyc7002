// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017,2019,2020 Realtek Semiconductor Corp.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include "rtk_info.h"

#define SYS_PLL_DIV                                0x030
#define SYS_PLL_SCPU3                              0x108

struct pll_info_device;

struct pll_info_pll_data {
	const char *name;
	int (*get_div)(struct pll_info_device *);
	struct clk *clk;
};

struct pll_info_device_desc {
	int scpu_div_version;
	int scpu_div_loc_pll_scpu : 1;
};

struct pll_info_device {
	struct device *dev;
	struct regmap *regmap;
	struct dentry *dentry;
	const struct pll_info_device_desc *desc;
};

static void pll_info_show_pll_one(struct seq_file *s,
				  struct pll_info_device *pxdev,
				  const struct pll_info_pll_data *pll_data)
{
	uint32_t freq;
	int ret;
	if (!pll_data->clk)
		return;

	seq_printf(s, "%-11s ", pll_data->name + 4);

	freq = clk_get_rate(pll_data->clk) / 1000000;
	if (!freq) {
		seq_puts(s, "  -- MHz     -     -- MHz   POWER_OFF\n");
		return;
	} else if (!pll_data->get_div) {
		seq_printf(s, "%4d MHz     -   %4d MHz\n", freq, freq);
		return;
	}

	ret = pll_data->get_div(pxdev);
	if (ret < 0)
		seq_printf(s, "%4d MHz     ?     ?? MHz   NO_PERMISSION\n",   freq);
	else if (ret == 0)
		seq_printf(s, "%4d MHz     ?     ?? MHz   INVALID_DIV\n", freq);
	else
		seq_printf(s, "%4d MHz   %3d   %4d MHz\n", freq, ret,  freq / ret);
}

#define DIV_GET_VAL(_v, _h, _l) \
	(((_v) >> (_l)) & (BIT((_h) - (_l) + 1) - 1))

static int pll_info_get_pll_bus_div(struct pll_info_device *pxdev)
{
	uint32_t div, val;

	regmap_read(pxdev->regmap, SYS_PLL_DIV, &div);
	if (div == 0xdeadbeaf || div == 0xdeaddead)
		return -EINVAL;

	val = DIV_GET_VAL(div, 1, 0);

	if (val & BIT(1))
		return (val & BIT(0)) ? 4 : 2;
	return 1;
}

static int pll_info_get_pll_dcsb_div(struct pll_info_device *pxdev)
{
	uint32_t div, val;

	regmap_read(pxdev->regmap, SYS_PLL_DIV, &div);
	if (div == 0xdeadbeaf || div == 0xdeaddead)
		return -EINVAL;

	val = DIV_GET_VAL(div, 26, 22);
	if (val & BIT(1))
		return (val & BIT(0)) ? 4 : 2;
	return 1;
}

static int pll_info_get_pll_acpu_div(struct pll_info_device *pxdev)
{
	uint32_t div, val;

	regmap_read(pxdev->regmap, SYS_PLL_DIV, &div);
	if (div == 0xdeadbeaf || div == 0xdeaddead)
		return -EINVAL;

	val = DIV_GET_VAL(div, 1, 0);
	return 1 << (ffs(~val) - 1);
}

static int __get_pll_scpu_div_v1(uint32_t val)
{
	if (val & BIT(7)) {
		val = (val & ~BIT(7)) >> 2;
		switch (val) {
		case 5: case 6: case 7: case 8: case 10: case 13:
			return val;
		default:
			return 3;
		}
	} else if (val & BIT(1)) {
		return (val & BIT(0)) ? 4 : 2;
	}
	return 1;
}

static int __get_pll_scpu_div_v2(uint32_t val)
{
	if (val < 0x80)
		return 1;
	val = (val & ~0x80) >> 2;
	return val > 16 || val < 2 ? 2 : val;
}

static int pll_info_get_pll_scpu_div(struct pll_info_device *pxdev)
{
	const struct pll_info_device_desc *desc = pxdev->desc;
	uint32_t div, val;

	if (desc->scpu_div_loc_pll_scpu) {
		regmap_read(pxdev->regmap, SYS_PLL_SCPU3, &div);
		val = DIV_GET_VAL(div, 15, 8);
	} else {
		regmap_read(pxdev->regmap,  SYS_PLL_DIV, &div);
		val = DIV_GET_VAL(div, 13, 6);
	}
	if (div == 0xdeadbeaf || div == 0xdeaddead)
		return -EINVAL;

	switch (desc->scpu_div_version) {
	default:
	case 1:
		return __get_pll_scpu_div_v1(val);
	case 2:
		return __get_pll_scpu_div_v2(val);
	}
}

static struct pll_info_pll_data pll_data_list[] = {
	{ "ref_pll_scpu", pll_info_get_pll_scpu_div, },
	{ "ref_pll_bus",  pll_info_get_pll_bus_div,  },
	{ "ref_pll_dcsb", pll_info_get_pll_dcsb_div, },
	{ "ref_pll_acpu", pll_info_get_pll_acpu_div, },
	{ "ref_pll_ddsa", },
	{ "ref_pll_ddsb", },
	{ "ref_pll_gpu",  },
	{ "ref_pll_ve1",  },
	{ "ref_pll_ve2",  },
	{ "ref_pll_npu",  },
	{ "ref_pll_hifi", },
};

static int info_pll_pll_show(struct seq_file *s, void *u)
{
	struct pll_info_device *pxdev = s->private;
	int i;

	seq_puts(s, "name        pll_freq   div   out_freq   state\n");
	seq_puts(s, "--------------------------------------------------\n");

	for (i = 0; i < ARRAY_SIZE(pll_data_list); i++)
		pll_info_show_pll_one(s, pxdev, &pll_data_list[i]);

	return 0;
}

static int info_pll_pll_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_pll_pll_show, inode->i_private);
}

static const struct file_operations info_pll_pll_fops = {
	.owner   = THIS_MODULE,
	.open    = info_pll_pll_open,
	.read    = seq_read,
	.release = single_release,
};

static
int pll_info_setup_device(struct pll_info_device *pxdev)
{
	struct device *dev = pxdev->dev;
	struct device_node *np = dev->of_node;

	pxdev->regmap = syscon_regmap_lookup_by_phandle(np, "realtek,crt");
	if (IS_ERR(pxdev->regmap))
		return PTR_ERR(pxdev->regmap);

	pxdev->desc = of_device_get_match_data(dev);

	return 0;
}

static int pll_info_setup_clks(struct device *dev)
{
	int i;
	int clk_num = 0;

	for (i = 0; i< ARRAY_SIZE(pll_data_list); i++) {
		struct pll_info_pll_data *pll_data = &pll_data_list[i];

		pll_data->clk = clk_get(NULL, pll_data->name);
		if (IS_ERR_OR_NULL(pll_data->clk)) {
			dev_info(dev, "%s: ignore clk %s\n", __func__, pll_data->name);
			pll_data->clk = NULL;
		} else
			clk_num += 1;

	}
	return clk_num ? 0 : -EINVAL;
}

static int pll_info_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pll_info_device *pxdev;
	int ret;

	pxdev = devm_kzalloc(dev, sizeof(*pxdev), GFP_KERNEL);
	if (!pxdev)
		return -ENOMEM;
	pxdev->dev = dev;

	ret = pll_info_setup_device(pxdev);
	if (ret)
		return ret;

	pxdev->dentry = debugfs_create_file("pll", 0644, rtk_info_debugfs_root,
			pxdev, &info_pll_pll_fops);
	if (!pxdev->dentry)
		return -ENOMEM;

	ret = pll_info_setup_clks(dev);
	if (ret)
		return ret;

	return 0;
}

const struct pll_info_device_desc desc_rtd1395 = {
	.scpu_div_version = 1,
};

const struct pll_info_device_desc desc_rtd1619 = {
	.scpu_div_version = 2,
};

const struct pll_info_device_desc desc_rtd1619b = {
	.scpu_div_version = 2,
	.scpu_div_loc_pll_scpu = 1,
};

static const struct of_device_id pll_info_ids[] = {
	{ .compatible = "realtek,rtd139x-pll-info", .data = &desc_rtd1395, },
	{ .compatible = "realtek,rtd161x-pll-info", .data = &desc_rtd1619, },
	{ .compatible = "realtek,rtd161xb-pll-info", .data = &desc_rtd1619b, },
	{}
};
MODULE_DEVICE_TABLE(of, pll_info_ids);

static struct platform_driver pll_info_drv = {
	.driver = {
		.name           = "rtk-pll-info",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(pll_info_ids),
	},
	.probe    = pll_info_probe,
};
module_platform_driver(pll_info_drv);

MODULE_DESCRIPTION("Realtek PLL Information driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rtk-pll-info");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
