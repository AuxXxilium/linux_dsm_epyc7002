#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
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
	return MODEL_DS214p;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1333);
}

int 
InitModuleType(struct synobios_ops *ops)
{
	module_t type = MODULE_T_DS214p;
	module_type_set(&type);
	armada_hdd_enable_gpio[0] = 42;
	armada_hdd_enable_gpio[1] = 44;
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
#ifdef MY_DEF_HERE
	return SetDiskLedStatusBySataMvGPIO(disknum, status);
#else
	return 0;
#endif
}

static int set_disk_led_all(SYNO_LED status)
{
	return SYNO_ENABLE_HDD_LED((SYNO_LED_ON == status)? TRUE : FALSE);
}

static int set_phy_led(SYNO_LED ledStatus)
{
	return SYNO_ENABLE_PHY_LED((SYNO_LED_ON == ledStatus)? TRUE : FALSE);
}

int model_addon_init(struct synobios_ops *ops)
{
	SYNO_ENABLE_HDD_LED(TRUE);

	ops->set_hdd_led = set_disk_led_all;
	ops->set_phy_led = set_phy_led;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
