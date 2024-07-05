// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>

#define TCW5ST 0x18
#define TCWRCR 0x20

struct rtk_watchdog_status_data {
	void *base;
};

static const char *name[] = {
	"gpu", "ve2", "ve1", "rng", "r2rdsc", "vo", "pcpu_otp", "pcpu_iso",
	"tpb", "main", "sce", "nag", "tmx", "scpu_iso", "scpu_otp",
	"emmc", "nf", "cp", "", "tp", "dc", "sb2", "dp",
	"rsa", "kt", "uvlo", "npu", "scpu_wrapper", "crt", "aucpu0", "",
};

static inline int get_reset_count(struct rtk_watchdog_status_data *data, int id)
{
	unsigned int val = readl(data->base + TCWRCR);

	return (val >> (id * 4)) & 0xf;
}

static inline unsigned int get_ip_assert_status(struct rtk_watchdog_status_data *data)
{
	return readl(data->base + TCW5ST);
}

static ssize_t ip_assert_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rtk_watchdog_status_data *data = dev_get_drvdata(dev);
	unsigned long val = get_ip_assert_status(data);
	ssize_t len = 0;
	int i;
	int first = 0;

	for (i = 0; i < 32; i++)
		if (test_bit(i, &val)) {
			len += snprintf(buf + len, PAGE_SIZE - len, "%s%s", first == 0 ? "" : " ", name[i]);
			first = 1;
		}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
static DEVICE_ATTR_RO(ip_assert_status);

static ssize_t reset_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rtk_watchdog_status_data *data = dev_get_drvdata(dev);
	ssize_t len = 0;
	int i;

	for (i = 0; i <= 5; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "wdt%d=%d\n", i, get_reset_count(data, i));
	return len;
}
static DEVICE_ATTR_RO(reset_count);

static struct attribute *watchdog_status_attrs[] = {
	&dev_attr_ip_assert_status.attr,
	&dev_attr_reset_count.attr,
	NULL
};

static const struct attribute_group watchdog_status_attr_group = {
	.attrs = watchdog_status_attrs,
	.name = "watchdog_status",
};

static int rtk_watchdog_status_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_watchdog_status_data *data;
	int ret;


	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = of_iomap(dev->of_node, 0);
	if (!data->base)
		return -ENOMEM;

	ret = sysfs_create_group(&dev->kobj, &watchdog_status_attr_group);
	if (ret) {
		iounmap(data->base);
		return ret;
	}

	platform_set_drvdata(pdev, data);
	return 0;
}

static int rtk_watchdog_status_remove(struct platform_device *pdev)
{
	struct rtk_watchdog_status_data *data = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	sysfs_remove_group(&pdev->dev.kobj, &watchdog_status_attr_group);
	iounmap(data->base);
	return 0;
}

static const struct of_device_id rtk_watchdog_status_match[] = {
	{ .compatible = "realtek,rtd1619b-watchdog-status", },
	{}
};

static struct platform_driver rtk_watchdog_status_driver = {
	.probe    = rtk_watchdog_status_probe,
	.remove   = rtk_watchdog_status_remove,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-wdt-st",
		.of_match_table = of_match_ptr(rtk_watchdog_status_match),
	},
};
module_platform_driver(rtk_watchdog_status_driver);

MODULE_DESCRIPTION("Realtek Watchdog Status driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-wdt-st");
