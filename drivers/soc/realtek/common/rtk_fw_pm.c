
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Realtek DHC SoC family FW power management driver
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 */
#include <linux/arm-smccc.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <uapi/linux/psci.h>

#include <soc/realtek/avcpu.h>
#include <soc/realtek/kernel-rpc.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_ipc_shm.h>

enum cpu_id {
	SCPU = 0x1,
	ACPU,
	VCPU,
};

#define RPC_AUDIO_SET_NOTIFY (__cpu_to_be32(BIT(8)))
#define RPC_VIDEO_SET_NOTIFY (__cpu_to_be32(BIT(2)))

#define RPC_HAS_BIT(addr, bit) (readl(addr) & bit)
#define RPC_SET_BIT(addr, bit) (writel((readl(addr)|bit), addr))
#define RPC_RESET_BIT(addr, bit) (writel((readl(addr)&~bit), addr))

static void rtk_writel_swc(u32 val, u32 addr)
{
	struct arm_smccc_res res;

	arm_smccc_smc(0x8400ffff, addr, val, 0, 0, 0, 0, 0, &res);
}

static void rpc_set_flag(int type, uint32_t flag)
{
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;

	if (type == ACPU)
		writel(__cpu_to_be32(flag), &(ipc->audio_rpc_flag));
	else if (type == VCPU)
		writel(__cpu_to_be32(flag), &(ipc->video_rpc_flag));

}

static uint32_t rpc_get_flag(int type)
{
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;

	if (type == ACPU)
		return __be32_to_cpu(readl(&(ipc->audio_rpc_flag)));
	else if (type == VCPU)
		return __be32_to_cpu(readl(&(ipc->video_rpc_flag)));

	return 0xdeaddead;
}

unsigned int rtk_fw_pm_cpu_on(uint32_t cpu)
{
	unsigned int ret = 0;

	if (cpu == ACPU) {
		rtk_writel_swc(0xacd1, 0x9801a360); /* ACPU */
		rtk_writel_swc(0x30, 0x98000014); /* ACPU reset 1 */
		rtk_writel_swc(0xc0, 0x98000058); /* ACPU enable clock */
	} else {
		rtk_writel_swc(0xacd1, 0x9801b7f0); /* VCPU */
		rtk_writel_swc(0x1, 0x98000464); /* VCPU reset 1 */
		rtk_writel_swc(0xc00000, 0x98000050); /* VCPU enable clock */
	}

	return ret;
}
EXPORT_SYMBOL(rtk_fw_pm_cpu_on);

int rtk_fw_pm_cpu_off(uint32_t cpu)
{
	unsigned int i = 0;
	unsigned int ret = 0;

	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;

	for (i = 0 ; i < 1000; i++) {

		if (cpu == ACPU)
			ret = readl(&(ipc->audio_rpc_flag));
		else
			ret = readl(&(ipc->video_rpc_flag));

		if (ret == 0) {
			ret = 1;
			break;
		}

		mdelay(1);
		ret = -ENOTTY;
	}

	if (ret == -ENOTTY)
		goto err;

	if (cpu == ACPU) {
		rtk_writel_swc(0x80, 0x98000058); /* ACPU disable clock */
		rtk_writel_swc(0x20, 0x98000014); /* ACPU reset 0 */
	} else {
		rtk_writel_swc(0x800000, 0x98000050); /* VCPU disable clock */
		rtk_writel_swc(0x0, 0x98000464); /* VCPU reset 0 */
	}

err:
	return ret;
}
EXPORT_SYMBOL(rtk_fw_pm_cpu_off);

void rtk_fw_rpc_suspend(int cpu)
{
	int max_count = 500;

	if (cpu == ACPU) {
		rpc_set_flag(ACPU, 0xdaedffff); /* stop audio has_check */
		while ((rpc_get_flag(ACPU) != 0x0) && (max_count > 0)) {
			mdelay(1);
			max_count--;
		}

		/* disable interrupt */
		RPC_RESET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY);

		/* sync rpc memory */
		wmb();

		/* wait audio rpc suspend ready */
		rpc_set_flag(ACPU, 0xdeadffff);
		while ((rpc_get_flag(ACPU) != 0x0) && (max_count > 0)) {
			mdelay(1);
			max_count--;
		}
	} else {
		rpc_set_flag(VCPU, 0xdaedffff); /* stop video has_check */
		while ((rpc_get_flag(VCPU) != 0x0) && (max_count > 0)) {
			mdelay(1);
			max_count--;
		}

		/* disable interrupt */
		RPC_RESET_BIT(rpc_vcpu_int_flag, RPC_VIDEO_SET_NOTIFY);

		/* sync rpc memory */
		wmb();

		/* wait video rpc suspend ready */
		rpc_set_flag(VCPU, 0xdeadffff);
		while ((rpc_get_flag(VCPU) != 0x0) && (max_count > 0)) {
			mdelay(1);
			max_count--;
		}
	}
}
EXPORT_SYMBOL(rtk_fw_rpc_suspend);

void rtk_fw_rpc_resume(int cpu)
{
	if (cpu == ACPU) {
		RPC_SET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY);
		rpc_set_flag(ACPU, 0xffffffff);
	} else {
		RPC_SET_BIT(rpc_vcpu_int_flag, RPC_VIDEO_SET_NOTIFY);
		rpc_set_flag(VCPU, 0xffffffff);
	}
}
EXPORT_SYMBOL(rtk_fw_rpc_resume);

static int rtk_fw_pm_suspend(struct device *dev)
{
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	rtk_fw_rpc_suspend(ACPU);
	rtk_fw_rpc_suspend(VCPU);

	if (!rtk_fw_pm_cpu_off(ACPU)) {
		dev_err(dev, "ACPU FW not ready to stop\n");
		ret = -EINVAL;
		goto err;
	}

	if (!rtk_fw_pm_cpu_off(VCPU)) {
		dev_err(dev, "VCPU FW not ready to stop\n");
		ret = -EINVAL;
		goto err;
	}

err:
	return ret;
}

static int rtk_fw_pm_resume(struct device *dev)
{
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	rtk_fw_pm_cpu_on(ACPU);
	rtk_fw_pm_cpu_on(VCPU);
	rtk_fw_rpc_resume(ACPU);
	rtk_fw_rpc_resume(VCPU);

	return ret;
}

static int rtk_fw_pm_probe(struct platform_device *pdev)
{
	int ret = 0;

	dev_info(&pdev->dev, "%s\n", __func__);

	return ret;
}

static const struct dev_pm_ops rtk_fw_pm_ops = {
	.suspend = rtk_fw_pm_suspend,
	.resume = rtk_fw_pm_resume,
};

static const struct of_device_id rtk_fw_pm_ids[] = {
	{ .compatible = "realtek,fw_pm" },
	{}
};

static struct platform_driver rtk_fw_pm = {
	.driver = {
		.name = "realtek-fw-pm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rtk_fw_pm_ids),
		.pm = &rtk_fw_pm_ops,
	},
	.probe    = rtk_fw_pm_probe,
};
module_platform_driver(rtk_fw_pm);

MODULE_AUTHOR("James Tai <james.tai@realtek.com>");
MODULE_DESCRIPTION("Realtek FW power management driver");
MODULE_LICENSE("GPL v2");
