// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek FSS Scan Driver
 *
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/iopoll.h>
#include <linux/workqueue.h>
#include <linux/sort.h>

#define SC_WRAP_DVFS_FSS_CTRL0                     0x00
#define SC_WRAP_DVFS_FSS_CTRL1                     0x04
#define SC_WRAP_DVFS_FSS_ST0                       0x14
#define SC_WRAP_DVFS_FSS_ST2                       0x1c
#define SC_WRAP_DVFS_FSS_ST3                       0x20

#define SC_WRAP_DVFS_FSS_ST0_CAL_DONE_MASK         0x1f000000

struct fss_fv_desc {
	int freq;
	int volt_init;
	int volt_min;
	int volt_max;
	int scan_num;
};

struct fss_device {
	struct device             *dev;
	void                      *base;

	struct clk                *clk;
	struct regulator          *supply;
	int                       num_volts;
	int                       *volts;
	const struct fss_fv_desc  *descs;
	int                       num_descs;
	const int                 *targets;
	int                       num_targets;

	unsigned long             saved_freq;
	int                       saved_volt;
	int                       *results;
	struct work_struct        work;

};

static int fss_set_freq_volt(struct fss_device *fdev, int freq, int volt)
{
	long old_freq = clk_get_rate(fdev->clk);

	/* always set the voltage */
	if (freq >= old_freq)
		regulator_set_voltage(fdev->supply, volt, volt);

	clk_set_rate(fdev->clk, freq);

	if (freq < old_freq)
		regulator_set_voltage(fdev->supply, volt, volt);
	return 0;
}

static int fss_reg_read(struct fss_device *fdev, unsigned int ofsset,
			unsigned int *val)
{
	*val = readl(fdev->base + ofsset);
	return 0;
}

static int fss_reg_write(struct fss_device *fdev, unsigned int ofsset,
			 unsigned int val)
{
	writel(val, fdev->base + ofsset);
	return 0;
}

static inline int fss_check_cal_done(unsigned int val)
{
	const unsigned int mask = SC_WRAP_DVFS_FSS_ST0_CAL_DONE_MASK;

	return (val & mask) == mask;
}

static int fss_wait_cal_done(struct fss_device *fdev)
{
	unsigned int cal_done;

	return readl_poll_timeout_atomic(fdev->base + SC_WRAP_DVFS_FSS_ST0,
		cal_done, fss_check_cal_done(cal_done), 0, 1);
}

static int fss_get_cal_min_cdl1(struct fss_device *fdev,
				unsigned int *cal_min_cdl1)
{
	int ret;

	fss_reg_write(fdev, SC_WRAP_DVFS_FSS_CTRL0, 0x1F000000);
	fss_reg_write(fdev, SC_WRAP_DVFS_FSS_CTRL1, 0x00077777);
	fss_reg_write(fdev, SC_WRAP_DVFS_FSS_CTRL0, 0x1F1F0000);

	ret = fss_wait_cal_done(fdev);
	if (ret)
		return ret;

	mdelay(10);
	fss_reg_read(fdev, SC_WRAP_DVFS_FSS_ST3, cal_min_cdl1);
	fss_reg_write(fdev, SC_WRAP_DVFS_FSS_CTRL0, 0x00000000);

	return 0;
}

static int min_cdl_compare(unsigned int min_cdl, unsigned int target)
{
	while (target != 0) {

		if ((min_cdl & 0xf) < (target & 0xf))
			return 0;

		min_cdl >>= 4;
		target >>= 4;
	}
	return 1;
}

static int fss_scan_check_voltage(struct fss_device *fdev,
				  int volt, int target)
{
	int cal_min_cdl1;
	int ret;

	dev_info(fdev->dev, "set volt to %d\n", volt);
	ret = regulator_set_voltage(fdev->supply, volt, volt);
	if (ret) {
		dev_err(fdev->dev, "faied to set voltage: %d\n", ret);
		return 0;
	}

	/* must wait more time, when lowering voltage */
	msleep(20);

	ret = fss_get_cal_min_cdl1(fdev, &cal_min_cdl1);
	if (ret) {
		dev_err(fdev->dev, "faied to get cal_min_cdl1: %d\n", ret);
		return 0;
	}

	dev_info(fdev->dev, "cal_min_cdl1 is %08x\n", cal_min_cdl1);
	return min_cdl_compare(cal_min_cdl1, target);
}

static int fss_voltage_to_idx(struct fss_device *fdev, int volt)
{
	int i;
	int closest = 0;
	int min = volt;
	int v;

	if (volt >= fdev->volts[fdev->num_volts - 1])
		return fdev->num_volts - 1;

	if (volt <= fdev->volts[0])
		return 0;

	for (i = 0; i < fdev->num_volts; i++) {
		if (fdev->volts[i] == volt)
			return i;

		v = fdev->volts[i] > volt ? fdev->volts[i] - volt : volt - fdev->volts[i];
		if (v < min) {
			closest = i;
			min = v;
		}
	}
	return closest;
}

static int fss_scan(struct fss_device *fdev, int volt, int min, int max,
		    unsigned int target)
{
	int i;
	int voidx, vobound;

	if (!volt)
		return 0;

	voidx = fss_voltage_to_idx(fdev, volt);

	if (fss_scan_check_voltage(fdev, volt, target)) {
		vobound = fss_voltage_to_idx(fdev, min);

		for (i = voidx - 1; i >= vobound; i--) {
			if (!fss_scan_check_voltage(fdev, fdev->volts[i], target))
				break;
			volt = fdev->volts[i];
		}
		return volt;
	}

	vobound = fss_voltage_to_idx(fdev, max);

	for (i = voidx + 1; i <= vobound; i++)
		if (fss_scan_check_voltage(fdev, fdev->volts[i], target))
			return fdev->volts[i];
	return 0;
}

static
int fss_scan_desc(struct fss_device *fdev, const struct fss_fv_desc *desc)
{
	int i;
	int init, min, max, ret;

	fss_set_freq_volt(fdev, desc->freq, desc->volt_init);
	msleep(200);
	dev_info(fdev->dev, "scan for frequency: %d MHz\n",
		(int)desc->freq / 1000000);

	init = desc->volt_init;
	min = desc->volt_min;
	max = desc->volt_max;

	for (i = 0; i < fdev->num_targets; i++) {

		ret = fss_scan(fdev, init , min, max, fdev->targets[i]);

		dev_info(fdev->dev, "fss_scan freq=%d, target=%08x: input=(%d, %d, %d), output=%d\n",
			desc->freq, fdev->targets[i], init, min, max, ret);
		if (!ret)
			return 0;
		init = min = ret;
	}

	return ret;
}

static void fss_save_state(struct fss_device *fdev)
{
	fdev->saved_volt = regulator_get_voltage(fdev->supply);
	fdev->saved_freq = clk_get_rate(fdev->clk);
}

static void fss_restore_state(struct fss_device *fdev)
{
	fss_set_freq_volt(fdev, fdev->saved_freq, fdev->saved_volt);
}

static void fss_scan_work(struct work_struct *work)
{
	struct fss_device *fdev = container_of(work, struct fss_device, work);
	int i, j;

	fdev->supply = regulator_get(fdev->dev, "cpu");
	if (IS_ERR(fdev->supply)) {
		dev_err(fdev->dev, "failed to get regulator: %ld\n",
				PTR_ERR(fdev->supply));
		return;
	}

	fss_save_state(fdev);

	for (i = 0; i < fdev->num_descs; i++) {
		int best = 0;
		int res;
		const struct fss_fv_desc *d = &fdev->descs[i];

		for (j = 0; j < d->scan_num; j++) {
			res = fss_scan_desc(fdev, d);
			if (res == 0)
				continue;
			if (best == 0 || best > res)
				best = res;
		}
		fdev->results[i] = best;
	}

	fss_restore_state(fdev);

	regulator_put(fdev->supply);
}

static void fss_scan_wait(struct fss_device *fdev)
{
	flush_work(&fdev->work);
}

static int fss_scan_start(struct fss_device *fdev)
{
	if (!queue_work_on(0, system_highpri_wq, &fdev->work))
		return -EBUSY;
	return 0;
}

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fss_device *fdev = dev_get_drvdata(dev);
	int ret = 0;

	if (!strncmp("start", buf, 5))
		ret = fss_scan_start(fdev);
	else if (!strncmp("wait", buf, 4))
		fss_scan_wait(fdev);

	return ret ?: count;
}
DEVICE_ATTR_WO(control);

static
ssize_t frequencies_mhz_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int len = 0;
	int i;
	struct fss_device *fdev = dev_get_drvdata(dev);
	const struct fss_fv_desc *d = fdev->descs;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", (int)d[0].freq / 1000000);
	for (i = 1; i < fdev->num_descs; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, " %d", (int)d[i].freq / 1000000);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
DEVICE_ATTR_RO(frequencies_mhz);

static ssize_t voltages_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_device *fdev = dev_get_drvdata(dev);
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", fdev->results[0]);
	for (i = 1; i < fdev->num_descs; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, " %d",
				fdev->results[i]);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
DEVICE_ATTR_RO(voltages);

static ssize_t targets_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_device *fdev = dev_get_drvdata(dev);
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE - len, "%08x", fdev->targets[0]);
	for (i = 1; i < fdev->num_targets; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, " %08x",
				fdev->targets[i]);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
DEVICE_ATTR_RO(targets);

static ssize_t configs_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_device *fdev = dev_get_drvdata(dev);
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE - len, "%-4s    %9s %9s %9s %s\n", "freq", "volt-init", "volt-min", "volt-max", "scan-num");
	for (i = 0; i < fdev->num_descs; i++) {
		const struct fss_fv_desc *d = &fdev->descs[i];

		len += snprintf(buf + len, PAGE_SIZE - len, "%4dMHz %7dmV %7dmV %7dmV %8d\n",
			(int)d->freq / 1000000, d->volt_init / 1000, d->volt_min / 1000, d->volt_max / 1000, d->scan_num);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
DEVICE_ATTR_RO(configs);

static struct attribute *fss_attrs[] = {
	&dev_attr_control.attr,
	&dev_attr_voltages.attr,
	&dev_attr_frequencies_mhz.attr,
	&dev_attr_targets.attr,
	&dev_attr_configs.attr,
	NULL
};

static struct attribute_group fss_attr_group = {
	.name = "fss",
	.attrs = fss_attrs,
};

static int volt_cmp(const void *a, const void *b)
{
	int va = *(int *)a;
	int vb = *(int *)b;

	return va - vb;
}

static void volt_swap(void *a, void *b, int size)
{
	int *pa = a;
	int *pb = b;
	int t;

	t = *pa;
	*pa = *pb;
	*pb = t;
}

static int fss_get_voltage_table(struct fss_device *fdev)
{
	int i;

	fdev->num_volts = regulator_count_voltages(fdev->supply);
	fdev->volts = devm_kcalloc(fdev->dev, fdev->num_volts,
				sizeof(*fdev->volts), GFP_KERNEL);
	if (!fdev->volts)
		return -ENOMEM;

	for (i = 0; i < fdev->num_volts; i++)
		fdev->volts[i] = regulator_list_voltage(fdev->supply, i);

	sort(fdev->volts, fdev->num_volts, sizeof(*fdev->volts), volt_cmp,
		volt_swap);

	return 0;
}

static int of_parse_config(struct fss_device *fdev, struct device_node *np)
{
	int num_targets;
	unsigned int *targets;
	int num_childs, num_descs = 0;
	struct device_node *child;
	struct fss_fv_desc *descs;
	int ret;

	num_targets = of_property_count_elems_of_size(np, "fss,targets", sizeof(*targets));
	if (num_targets <= 0)
		return -EINVAL;

	targets = devm_kcalloc(fdev->dev, num_targets, sizeof(*targets), GFP_KERNEL);
	if (!targets)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "fss,targets", targets, num_targets);
	if (ret)
		goto free_targets;

	num_childs = of_get_child_count(np);
	if (num_childs <= 0) {
		ret = -EINVAL;
		goto free_targets;
	}

	descs = devm_kcalloc(fdev->dev, num_childs, sizeof(*descs), GFP_KERNEL);
	if (!descs) {
		ret = -ENOMEM;
		goto free_targets;
	}

	for_each_child_of_node(np, child) {
		struct fss_fv_desc *d = &descs[num_descs];
		u32 f, v[3], s;

		if (of_property_read_u32_array(child, "fss,voltage", v, 3))
			continue;

		if (of_property_read_u32(child, "fss,frequency", &f))
			continue;

		if (of_property_read_u32(child, "fss,scan-num", &s))
			s = 3;

		d->freq = f;
		d->volt_init = v[0];
		d->volt_min = v[1];
		d->volt_max = v[2];
		d->scan_num = s;
		num_descs += 1;
	}

	fdev->descs       = descs;
	fdev->num_descs   = num_descs;
	fdev->targets     = targets;
	fdev->num_targets = num_targets;
	return 0;

free_targets:
	devm_kfree(fdev->dev, targets);
	return ret;
}

static const unsigned int rtd1319_targets[] = { 0xbbbbb, 0xccccc };
static const struct fss_fv_desc rtd1319_desc[] = {
	{ 1200000000, 1000000, 800000, 1087500, 3, },
	{ 1300000000, 1000000, 850000, 1100000, 3, },
	{ 1400000000, 1050000, 900000, 1150000, 3, },
};

static void use_default_config(struct fss_device *fdev)
{
	fdev->descs       = rtd1319_desc;
	fdev->num_descs   = ARRAY_SIZE(rtd1319_desc);
	fdev->targets     = rtd1319_targets;
	fdev->num_targets = ARRAY_SIZE(rtd1319_targets);
}

static int fss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource res;
	struct fss_device *fdev;
	int ret;

	fdev = devm_kzalloc(dev, sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return -ENOMEM;
	fdev->dev = dev;

	if (of_parse_config(fdev, np)) {
		dev_info(dev, "failed to get config from dt, use default\n");
		use_default_config(fdev);
	}

	fdev->results = devm_kcalloc(dev, fdev->num_descs,
		sizeof(*fdev->results),	GFP_KERNEL);
	if (!fdev->results)
		return -ENOMEM;


	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	fdev->base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!fdev->base)
		return -ENOMEM;

	fdev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(fdev->clk)) {
		ret = PTR_ERR(fdev->clk);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "clk is not ready, retry\n");
		else
			dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	fdev->supply = devm_regulator_get(dev, "cpu");
	if (IS_ERR(fdev->supply)) {
		ret = PTR_ERR(fdev->supply);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "regulator is not ready, retry\n");
		else
			dev_err(dev, "failed to get regulator: %d\n", ret);
		return ret;
	}

	ret = fss_get_voltage_table(fdev);
	if (ret) {
		dev_err(dev, "failed to get voltage table: %d\n", ret);
		return ret;
	}
	devm_regulator_put(fdev->supply);

	platform_set_drvdata(pdev, fdev);
	INIT_WORK(&fdev->work, fss_scan_work);

	ret = sysfs_create_group(&dev->kobj, &fss_attr_group);
	if (ret) {
		dev_err(dev, "failed to create sysfs group: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id fss_ids[] = {
	{ .compatible = "realtek,fss-scan" },
	{}
};

static struct platform_driver fss_drv = {
	.driver = {
		.name           = "rtk-fss-scan",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(fss_ids),
	},
	.probe    = fss_probe,
};
module_platform_driver(fss_drv);

MODULE_DESCRIPTION("Realtek FSS Scan driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rtk-fss-scan");

