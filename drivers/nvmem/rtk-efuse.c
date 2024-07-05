// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#define pr_fmt(fmt) "rtk-efuse: " fmt

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <soc/realtek/rtk_sb2_sem.h>

#define OTP_CTRL              0x000
#define OTP_CTRL_ST           0x004
#define OTP_CRC               0x008
#define OTP_TM                0x00c
#define OTP_DBG               0x010
#define OTP_TM_ST             0x014
#define OTP_DUMMY             0x018
#define OTP_CFG               0x020
#define OTP_RINGOSC           0x024
#define OTP_CLK_DTE           0x028

struct rtk_efuse_desc {
	int size;
	int ctl_reg_sel;
	int ctl_offset;
	int enable_icg : 1;
	int writable : 1;
};

struct rtk_efuse_device {
	struct nvmem_config config;
	struct device *dev;
	struct list_head list;
	void *base;
	void *ctl_base;
	struct nvmem_device *nvmem;
	struct mutex lock;
	struct sb2_sem *hwlock;
	const struct rtk_efuse_desc *desc;
};

static unsigned long rtk_efuse_lock(struct rtk_efuse_device *edev)
{
	unsigned long flags = 0;

	mutex_lock(&edev->lock);
	if (edev->hwlock)
		sb2_sem_lock(edev->hwlock, 0);
	return flags;
}

static void rtk_efuse_unlock(struct rtk_efuse_device *edev, unsigned long flags)
{
	if (edev->hwlock)
		sb2_sem_unlock(edev->hwlock);
	mutex_unlock(&edev->lock);
}

static int rtk_efuse_reg_read_unlocked(struct rtk_efuse_device *edev, unsigned int offset, unsigned char *val, size_t bytes)
{
	int i;

	for (i = 0; i < bytes; i++)
		val[i] = readb(edev->base + offset + i);
	return 0;
}

static int rtk_efuse_reg_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rtk_efuse_device *edev = priv;
	unsigned long flags;

	dev_dbg(edev->dev, "%s: offset=%03x, size=%zd\n", __func__, offset, bytes);
	might_sleep();

	flags = rtk_efuse_lock(edev);
	rtk_efuse_reg_read_unlocked(edev, offset, val, bytes);
	rtk_efuse_unlock(edev, flags);

	return 0;
}

static int rtk_efuse_wait_write_done(struct rtk_efuse_device *edev, int timeout_us)
{
	unsigned int val;

	return readl_poll_timeout(edev->ctl_base + OTP_CTRL_ST, val, !(val & BIT(16)), 0, timeout_us);
}

static int __rtk_efuse_program(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	unsigned int cmd, tm_st;
	unsigned char cval;
	int ret;

	ret = rtk_efuse_wait_write_done(edev, 20);
	if (ret)
		return ret;

	cmd = 0x31000800 | (val << 16) | addr;
	writel(cmd, edev->ctl_base + OTP_CTRL);

	ret = rtk_efuse_wait_write_done(edev, 100);
	if (ret)
		return ret;
	udelay(250);

	tm_st = readl(edev->ctl_base + OTP_TM_ST);
	if ((tm_st & 0x300) != 0x100)
		ret = -EIO;

	rtk_efuse_reg_read_unlocked(edev, addr, &cval, 1);

	if (ret || cval != val) {
		dev_warn(edev->dev, "%s: OTP_CTRL=%08x, OTP_TM_ST=%08x, excepted=%02x, current=%02x, ret=%d\n", __func__, cmd, tm_st, val, cval, ret);
		ret = -EBUSY;
	}
	return ret;
}

static int rtk_efuse_set_bit(struct rtk_efuse_device *edev, int bit_offset)
{
	unsigned int addr = (bit_offset >> 3);
	unsigned int bit = bit_offset & 0x7;
	unsigned char val;
	unsigned long flags;
	int ret;

	flags = rtk_efuse_lock(edev);

	rtk_efuse_reg_read_unlocked(edev, addr, &val, 1);
	val = val | BIT(bit);
	ret =  __rtk_efuse_program(edev, addr, val);

	rtk_efuse_unlock(edev, flags);
	return ret;
}

static int rtk_efuse_write_byte_bit_by_bit(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	int ret;
	int j;

	dev_dbg(edev->dev, "%s: addr=%03x, val=%02x\n", __func__, addr, val);
	for (j = 0; j < 8; j++) {
		if ((val & BIT(j)) == 0)
			continue;

		ret = rtk_efuse_set_bit(edev, addr * 8 + j);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtk_efuse_write_byte_normal(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	unsigned long flags;
	unsigned char rval;
	int ret;

	dev_dbg(edev->dev, "%s: addr=%03x, val=%02x\n", __func__, addr, val);
	flags = rtk_efuse_lock(edev);

	rtk_efuse_reg_read_unlocked(edev, addr, &rval, 1);
	if (rval & ~val) {
		rtk_efuse_unlock(edev, flags);
		return -EINVAL;
	}

	val = rval | val;
	ret = __rtk_efuse_program(edev, addr, val);

	rtk_efuse_unlock(edev, flags);
	return ret;
}

static int rtk_efuse_write_byte(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	int retry = 20;
	int ret;

	if (val == 0)
		return 0;

again:
	dev_dbg(edev->dev, "%s: addr=%03x, val=%02x\n", __func__, addr, val);
	ret = rtk_efuse_write_byte_normal(edev, addr, val);

	if (ret && ret != -EBUSY)
		ret = rtk_efuse_write_byte_bit_by_bit(edev, addr, val);

	if (ret == -EBUSY && retry-- >= 0)
		goto again;
	return ret;
}

static int rtk_efuse_reg_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rtk_efuse_device *edev = priv;
	unsigned char *p = val;
	int i;
	int ret;

	dev_dbg(edev->dev, "%s: offset=%03x, size=%zu\n", __func__, offset, bytes);
	might_sleep();

	for (i = 0; i < bytes; i++) {
		ret = rtk_efuse_write_byte(edev, offset + i, p[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct rtk_efuse_desc rtd1295_efuse_desc = {
	.size = 0x400,
	.ctl_offset = 0x400,
	.enable_icg = 0,
	.writable = 0,
};

static const struct rtk_efuse_desc rtd1619_efuse_desc = {
	.size = 0x800,
	.ctl_offset = 0x800,
	.enable_icg = 1,
	.writable = 1,
};

static const struct rtk_efuse_desc rtd1619b_efuse_desc = {
	.size = 0x1000,
	.ctl_reg_sel = 1,
	.ctl_offset = 0x0,
	.enable_icg = 1,
	.writable = 1,
};

static int rtk_efuse_enable_powersaving(struct rtk_efuse_device *edev)
{
	if (!edev->desc->enable_icg)
		return 0;

	writel(0x0C00C000, edev->ctl_base + OTP_CTRL);
	return 0;
}

static int rtk_efuse_probe(struct platform_device *pdev)
{
	struct rtk_efuse_device *edev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct rtk_efuse_desc *desc;

	desc = of_device_get_match_data(dev);
	if (!desc)
		desc = &rtd1295_efuse_desc;

	edev = devm_kzalloc(dev, sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(edev->base))
		return PTR_ERR(edev->base);

	if (desc->ctl_reg_sel) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		edev->ctl_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(edev->ctl_base)) {
			dev_err(dev, "failed to get ctl_base\n");
			return PTR_ERR(edev->ctl_base);
		}

		edev->ctl_base += desc->ctl_offset;

	} else {
		edev->ctl_base = edev->base + desc->ctl_offset;
	}

	edev->dev = dev;
	edev->desc = desc;
	mutex_init(&edev->lock);

	edev->hwlock = of_sb2_sem_get(dev->of_node, 0);
	if (IS_ERR(edev->hwlock)) {
		dev_dbg(dev, "failed to get hw semaphore: %ld\n",
			PTR_ERR(edev->hwlock));
		edev->hwlock = NULL;
	}

	if (edev->hwlock)
		dev_info(dev, "use hw lock\n");

	edev->config.owner     = THIS_MODULE;
	edev->config.name      = "rtk-efuse";
	edev->config.stride    = 1;
	edev->config.word_size = 1;
	edev->config.reg_read  = rtk_efuse_reg_read;
	edev->config.reg_write = edev->desc->writable ? rtk_efuse_reg_write : NULL;
	edev->config.dev       = dev;
	edev->config.size      = desc->size;
	edev->config.priv      = edev;

	edev->nvmem = nvmem_register(&edev->config);
	if (IS_ERR(edev->nvmem))
		return PTR_ERR(edev->nvmem);

	rtk_efuse_enable_powersaving(edev);

	platform_set_drvdata(pdev, edev);
	return 0;
}

static int rtk_efuse_remove(struct platform_device *pdev)
{
	struct rtk_efuse_device *edev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	nvmem_unregister(edev->nvmem);
	return 0;
}

static const struct of_device_id rtk_efuse_of_match[] = {
	{.compatible = "realtek,efuse",       .data = &rtd1295_efuse_desc, },
	{.compatible = "realtek,rtd1619-otp", .data = &rtd1619_efuse_desc, },
	{.compatible = "realtek,rtd1619b-otp", .data = &rtd1619b_efuse_desc, },
	{},
};

static struct platform_driver rtk_efuse_drv = {
	.probe = rtk_efuse_probe,
	.remove = rtk_efuse_remove,
	.driver = {
		.name = "rtk-efuse",
		.owner = THIS_MODULE,
		.of_match_table = rtk_efuse_of_match,
	},
};

static __init int rtk_efuse_init(void)
{
	return platform_driver_register(&rtk_efuse_drv);
}
subsys_initcall(rtk_efuse_init);

MODULE_DESCRIPTION("Realtek eFuse driver");
MODULE_ALIAS("platform:rtk-efuse");
MODULE_LICENSE("GPL");
