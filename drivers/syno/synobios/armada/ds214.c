// Copyright (c) 2000-2013 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/cpumask.h>
#include "../i2c/i2c-linux.h"
#include "armada_common.h"


int GetModel(void)
{
	return MODEL_DS214;
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
#if defined(CONFIG_SMP)
	int i;

	cpu->core = 0;
	for_each_online_cpu(i) {
		cpu->core++;
	}
#else /* CONFIG_SMP */
	cpu->core = 1;
#endif
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1066);
}

int 
InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = ops->get_model();
	module_t type_214v1 = MODULE_T_DS214;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS214:
		pType = &type_214v1;
		armada_hdd_enable_gpio[0] = 37;
		armada_hdd_enable_gpio[1] = 62;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	return SYNO_SOC_HDD_LED_SET(disknum, status);
}

static int set_hdd_led_enabled(SYNO_LED ledStatus)
{
	int err = -1;

	switch(ledStatus) {
	case SYNO_LED_OFF:
		SYNO_SOC_HDD_LED_SET(1, DISK_LED_OFF);
		SYNO_SOC_HDD_LED_SET(2, DISK_LED_OFF);
		break;
	case SYNO_LED_ON:
		SYNO_SOC_HDD_LED_SET(1, DISK_LED_GREEN_SOLID);
		SYNO_SOC_HDD_LED_SET(2, DISK_LED_GREEN_SOLID);
		break;
	default:
		goto ERR;
	}

	err = 0;
ERR:
	return err;
}

#include <linux/interrupt.h>
#define IRQ_AURORA_GBE0_FIC  8
#define LINK_DELAY_MS	10
int SetPhyLed(SYNO_LED ledStatus)
{
	int iPhyAddr;
	unsigned short uiRegValue;
	int err = -1;

	iPhyAddr = mvBoardPhyAddrGet(0);

	if (-1 == iPhyAddr) {
		goto ERR;
	}

	/* Eth phy mv1512 will link down/up when switch register page from/to 0,
	   and instand link up/down will cause link handle error in multi-core (ex. AXP).
	   Thus we disable GBE interrupt and wait a short period to make sure
	   eth link stat stable */
	disable_irq(IRQ_AURORA_GBE0_FIC);

	mdelay(LINK_DELAY_MS);
	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x16, 0x3); /*set to page 3*/
	mdelay(LINK_DELAY_MS);
	mvEthPhyRegRead((unsigned int)iPhyAddr, 0x10, &uiRegValue);
	mdelay(LINK_DELAY_MS);

	switch(ledStatus){
		/*Set Phy led[0] of led[0-2]*/
		case SYNO_LED_ON:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0001;
			break;
		case SYNO_LED_OFF:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0008;
			break;
		default:
			goto ERR;
	}

	mdelay(LINK_DELAY_MS);
	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x10, uiRegValue);
	mdelay(LINK_DELAY_MS);
	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x16, 0x0); /*set to page 0*/
	mdelay(LINK_DELAY_MS);

	enable_irq(IRQ_AURORA_GBE0_FIC);

	err = 0;

ERR:
	return err;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* Tricky Fix!
	   Set disk led green to turn on MPP[24,25] sata preset function */
	SYNO_SOC_HDD_LED_SET(1, DISK_LED_GREEN_SOLID);
	SYNO_SOC_HDD_LED_SET(2, DISK_LED_GREEN_SOLID);

	ops->set_hdd_led = set_hdd_led_enabled;
	ops->set_phy_led = SetPhyLed;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
