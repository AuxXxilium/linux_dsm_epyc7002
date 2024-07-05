// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <asm/io.h>
#include "alpine_common.h"

int GetModel(void)
{
	return MODEL_DS715;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1400);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = ops->get_model();
	module_t type_715 = MODULE_T_DS715;
	module_t *pType = NULL;

	switch (model) {
		case MODEL_DS715:
			pType = &type_715;
			break;
		default:
			break;
	}

	module_type_set(pType);

	return 0;
}

int SetPowerLedStatus(SYNO_LED status)
{
	return 0;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	return 0;
}

/*
 *  DS715/DS215+ GPIO config table
 *  (High=1)
 *
 *  Pin     In/Out    Function
 *   0       In       FAN 1
 *   2       In       HDD_PRZ_1 (Low = HDD insert; High = plug out)
 *   3       In       HDD_PRZ_1 (Low = HDD insert; High = plug out)
 *   5      Out       High = SATA LED off
 *  34      Out       High = LAN LED on
 *  19      Out       High = Enable ESATA 1 power
 *  22      Out       High = Enable HDD 1 power (for deep sleep)
 *  23      Out       High = Enable HDD 1 power (for deep sleep)
 *  10      Out       High = HDD 1 activity LED
 *  11      Out       High = HDD 2 activity LED
 *  29      Out       High = HDD 1 present LED
 *  31      Out       High = HDD 2 present LED
 *  38      Out       High = HDD 1 faulty LED
 *  39      Out       High = HDD 2 faulty LED
 *  43      Out       VTT off
 */

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 1,
	.gpio_port		= {0},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 2,
	.gpio_port		= {38, 39},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 2,
	.gpio_port		= {29, 31},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_act_led = {
	.nr_gpio		= 2,
	.gpio_port		= {10, 11},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 2,
	.gpio_port		= {22, 23},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {5},
	.gpio_polarity	= ACTIVE_LOW,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	if (!syno_gpio.hdd_act_led) {
		syno_gpio.hdd_act_led = &hdd_act_led;
	} else {
		check_gpio_consistency(syno_gpio.hdd_act_led, &hdd_act_led);
	}

	syno_gpio.fan_fail 			= &fan_fail;
	syno_gpio.hdd_fail_led 		= &hdd_fail_led;
	syno_gpio.hdd_present_led 	= &hdd_present_led;
	syno_gpio.disk_led_ctrl 	= &disk_led_ctrl;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	if (&hdd_act_led == syno_gpio.hdd_act_led) {
		syno_gpio.hdd_act_led = NULL;
	}

	syno_gpio.fan_fail 			= NULL;
	syno_gpio.hdd_fail_led 		= NULL;
	syno_gpio.hdd_present_led 	= NULL;
	syno_gpio.disk_led_ctrl 	= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	SYNO_ENABLE_HDD_LED(1);
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
