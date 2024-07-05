// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <asm/io.h>
#include "alpine_common.h"
#include "syno_ttyS.h"

int GetModel(void)
{
	return MODEL_DS416;
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
	module_t type_416 = MODULE_T_DS416;
	module_t *pType = NULL;

	switch (model) {
		case MODEL_DS416:
			pType = &type_416;
			break;
		default:
			break;
	}

	module_type_set(pType);

	return 0;
}

int SetPowerLedStatus(SYNO_LED status)
{
	char szCommand[5] = {0};
	int err = -1;

		switch(status){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > synobios_lock_ttyS_write(szCommand)) {
		goto ERR;
	}

	err = 0;
ERR:
	return err;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	int i = 0;
	int err = -1;
	for (i = 1; i <= 4; ++i) {
		switch(ledStatus) {
			case SYNO_LED_OFF:
				SetDiskLedStatus(i, DISK_LED_OFF);
				break;
			case SYNO_LED_ON:
				SetDiskLedStatus(i, DISK_LED_GREEN_BLINK);
				break;
			default:
				goto ERR;
		}
	}
	err = 0;
ERR:
	return err;
}

/*
 *  DS416 GPIO config table
 *  (High=1)
 *
 *  Pin     In/Out    Function
 *   0       In       FAN 1
 *   1       In       FAN 2
 *   2       In       HDD_PRZ_1 (Low = HDD insert; High = plug out)
 *   3       In       HDD_PRZ_2 (Low = HDD insert; High = plug out)
 *  35       In       HDD_PRZ_3 (Low = HDD insert; High = plug out)
 *  36       In       HDD_PRZ_4 (Low = HDD insert; High = plug out)
 *   5      Out       High = Disk LED off
 *  19      Out       High = Enable ESATA 1 power
 *  10      Out       High = HDD 1 activity
 *  11      Out       High = HDD 2 activity
 *  22      Out       High = HDD 3 activity
 *  23      Out       High = HDD 4 activity
 *  24      Out       High = Enable HDD 1 power (for deep sleep)
 *  25      Out       High = Enable HDD 2 power (for deep sleep)
 *  26      Out       High = Enable HDD 3 power (for deep sleep)
 *  27      Out       High = Enable HDD 4 power (for deep sleep)
 *  29      Out       High = HDD 1 present
 *  31      Out       High = HDD 2 present
 *  32      Out       High = HDD 3 present
 *  33      Out       High = HDD 4 present
 *  38      Out       High = HDD 1 fault
 *  39      Out       High = HDD 2 fault
 *  40      Out       High = HDD 3 fault
 *  41      Out       High = HDD 4 fault
 *  43      Out       VTT off
 */

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 2,
	.gpio_port		= {0, 1},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 4,
	.gpio_port		= {38, 39, 40, 41},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 4,
	.gpio_port		= {29, 31, 32, 33},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_act_led = {
	.nr_gpio		= 4,
	.gpio_port		= {10, 11, 22, 23},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {24, 25, 26, 27},
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
