#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "armada38x_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_RS816;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1800);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_816 = MODULE_T_RS816;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_RS816:
		pType = &type_816;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
#ifdef MY_DEF_HERE
	if (1 == disknum)
		return SYNO_HDD_LED_SET(disknum, status);
	else
		return SetDiskLedStatusBy9235GPIO(disknum, status);
#else
	return 0;
#endif
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

int getBuzzerButtonStatus(unsigned char *pValue)
{
    return SYNO_BUZZER_BUTTON_GPIO_GET(pValue);
}

int setBuzzerMute (unsigned char buzzer_cleared)
{
	printk("Synobios: Buzzer is muted\n");
	return SYNO_CTRL_BUZZER_MUTE_SET(buzzer_cleared);
}

/*
 *  RS816 GPIO config table
 *
 *  Pin     In/Out    Function

 *  12       In       Model ID 0
 *  21       In       Model ID 1
 *  38       In       Buzzer Mute GPI
 *  39       In       Present Pin
 *  45       In       Model ID 2
 *  46       In       Fan 2 fail
 *  52       In       Fan 1 fail
 *  53       In       Fan 3 fail
 *  54       In       USB3 overcurrent
 *  55       In       USB2 overcurrent
 *   6      Out       LED off
 *  13      Out       HDD1 fault LED
 *  28      Out       Buzzer Mute GPO
 *  37      Out       HDD 3 power enable
 *  37      Out       HDD 4 power enable
 *  48      Out       Fan High
 *  49      Out       Fan Mid
 *  50      Out       Fan Low
 *  58      Out       USB3 power enable
 *  59      Out       USB3 power enable
 */
static SYNO_GPIO_INFO fan_ctrl = {
	.nr_gpio		= 3,
	.gpio_port		= {50, 49, 48},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 3,
	.gpio_port		= {52, 46, 53},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 1,
	.gpio_port		= {13},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 4,
	.gpio_port		= {39, 39, 39, 39},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {37, 37, 37, 37},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO model_id = {
	.nr_gpio		= 3,
	.gpio_port		= {12, 21, 45},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {6},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO mute_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {38},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO buzzer_mute_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {28},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}

	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	syno_gpio.fan_ctrl           = &fan_ctrl;
	syno_gpio.fan_fail           = &fan_fail;
	syno_gpio.hdd_fail_led       = &hdd_fail_led;
	syno_gpio.model_id           = &model_id;
	syno_gpio.disk_led_ctrl      = &disk_led_ctrl;
	syno_gpio.mute_button_detect = &mute_button_detect;
	syno_gpio.buzzer_mute_ctrl   = &buzzer_mute_ctrl;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_ctrl           = NULL;
	syno_gpio.fan_fail           = NULL;
	syno_gpio.hdd_fail_led       = NULL;
	syno_gpio.model_id           = NULL;
	syno_gpio.disk_led_ctrl      = NULL;
	syno_gpio.mute_button_detect = NULL;
	syno_gpio.buzzer_mute_ctrl   = NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable disk LED */
	SYNO_ENABLE_HDD_LED(1);

    /* attach get_fan_status handler*/
	ops->get_fan_status = GetFanStatusActivePulse;

	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;

	ops->get_buzzer_cleared = getBuzzerButtonStatus;
	ops->set_buzzer_clear = setBuzzerMute;
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
