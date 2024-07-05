// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <asm/io.h>
#include "alpine_common.h"

int GetModel(void)
{
	return MODEL_DS1515;
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
	module_t type_1515 = MODULE_T_DS1515;
	module_t *pType = NULL;

	switch (model) {
		case MODEL_DS1515:
			pType = &type_1515;
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
 *  DS1515 GPIO config table
 *  (High=1)
 *
 *  Pin     In/Out    Function
 *   0       In       FAN 1
 *   1       In       FAN 2
 *   5      Out       High = Disk LED off
 *  18      Out       High = GPIO fan fail
 *  10      Out       High = HDD 1 activity
 *  11      Out       High = HDD 2 activity
 *  22      Out       High = HDD 3 activity
 *  23      Out       High = HDD 4 activity
 *  24      Out       High = HDD 5 activity
 *  29      Out       High = HDD 1 present
 *  31      Out       High = HDD 2 present
 *  32      Out       High = HDD 3 present
 *  33      Out       High = HDD 4 present
 *  34      Out       High = HDD 5 present
 *  38      Out       High = HDD 1 fault
 *  39      Out       High = HDD 2 fault
 *  40      Out       High = HDD 3 fault
 *  41      Out       High = HDD 4 fault
 *  42      Out       High = HDD 5 fault
 *  43      Out       VTT off
 */

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 2,
	.gpio_port		= {0, 1},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 5,
	.gpio_port		= {38, 39, 40, 41, 42},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 5,
	.gpio_port		= {29, 31, 32, 33, 34},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_act_led = {
	.nr_gpio		= 5,
	.gpio_port		= {10, 11, 22, 23, 24},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {5},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO alarm_led = {
	.nr_gpio		= 1,
	.gpio_port		= {18},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_act_led) {
		syno_gpio.hdd_act_led = &hdd_act_led;
	} else {
		check_gpio_consistency(syno_gpio.hdd_act_led, &hdd_act_led);
	}

	syno_gpio.fan_fail 			= &fan_fail;
	syno_gpio.hdd_fail_led 		= &hdd_fail_led;
	syno_gpio.hdd_present_led 	= &hdd_present_led;
	syno_gpio.disk_led_ctrl 	= &disk_led_ctrl;
	syno_gpio.alarm_led			= &alarm_led;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_act_led == syno_gpio.hdd_act_led) {
		syno_gpio.hdd_act_led = NULL;
	}

	syno_gpio.fan_fail 			= NULL;
	syno_gpio.hdd_fail_led 		= NULL;
	syno_gpio.hdd_present_led 	= NULL;
	syno_gpio.disk_led_ctrl 	= NULL;
	syno_gpio.alarm_led			= NULL;
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
