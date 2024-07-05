// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "rtd1296_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_RS819;
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
	PRODUCT_MODEL model = GetModel();
	module_t type_rs819 = MODULE_T_RS819;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_RS819:
		pType = &type_rs819;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	int iRet = -1;
	static int diskLedEnabled = 0;

	if (0 == diskLedEnabled && DISK_LED_OFF != status) {
		/* enable disk LED */
		SYNO_ENABLE_HDD_LED(1);
		diskLedEnabled = 1;
	}

	iRet = SYNO_HDD_LED_SET(disknum, status);
	if (0 != iRet) {
		goto END;
	}
END:
	return iRet;
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

static int SetHddLed(SYNO_LED status)
{
	int err = -1;

        switch(status){
		case SYNO_LED_ON:
			SYNO_ENABLE_HDD_LED(1);
			break;
		case SYNO_LED_OFF:
			SYNO_ENABLE_HDD_LED(0);
			break;
		default:
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

/*
 *  RS819 GPIO config table
 *
 *  Pin     In/Out    Function

 *   4       In       HDD 1 present
 *   5       In       HDD 2 present
 *   6       In       HDD 3 present
 *   7       In       HDD 4 present
 *  12       In       FAN 2 fail
 *  17       In       Buzzer mute Button
 *  18      Out       HDD 1 green LED
 *  20       In       FAN 3 fail
 *  22      Out       USB 3.0 lower power
 *  23      Out       USB 3.0 upper power
 *  24       In       FAN 1 fail
 *  55      Out       HDD 1 power
 *  81      Out       HDD 4 power
 *  82      Out       HDD 3 power
 *  87      Out       Control HDD LED
 *  88      Out       Control LAN LED
 *  97      Out       HDD 4 orange LED
 *  98      Out       HDD 3 orange LED
 *  99      Out       HDD 2 orange LED
 * 100      Out       HDD 1 orange LED
 * 128      Out       LAN 1G status LED
 * 129      Out       LAN active LED
 */

/*
 * For RS819 rtk internal sata disk led, we use GPIO 18
 * GPIO 18 blink and enable are controlled by driver/ata/ahci_rtk.c driver
 * Therefore, synobios don't control GPIO 18 to prevent conflict
 * RS819 other 3 disk are using MV9215 and don't need to control their led.
 */

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio        = 3,
	.gpio_port      = {24, 12, 20},
	.gpio_polarity  = ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 4,
	.gpio_port		= {100, 99, 98, 97},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 4,
	.gpio_port		= {4, 5, 6, 7},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {55, 86, 82, 81},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio	= 1,
	.gpio_port	= {87},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO phy_led_ctrl = {
	.nr_gpio        = 1,
	.gpio_port      = {88},
	.gpio_polarity  = ACTIVE_HIGH,
};
static SYNO_GPIO_INFO mute_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {17},
	.gpio_polarity	= ACTIVE_LOW,
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

	syno_gpio.fan_fail				= &fan_fail;
	syno_gpio.hdd_fail_led			= &hdd_fail_led;
	syno_gpio.disk_led_ctrl			= &disk_led_ctrl;
	syno_gpio.phy_led_ctrl			= &phy_led_ctrl;
	syno_gpio.mute_button_detect	= &mute_button_detect;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_fail				= NULL;
	syno_gpio.hdd_fail_led			= NULL;
	syno_gpio.disk_led_ctrl			= NULL;
	syno_gpio.phy_led_ctrl			= NULL;
	syno_gpio.mute_button_detect	= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* attach get_fan_status handler*/
	ops->get_fan_status = GetFanStatusActiveLow;

	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;

	/* enable to control PHY_LED */
	ops->set_phy_led = SetPhyLed;

	/* enable to control HDD_LED */
	ops->set_hdd_led = SetHddLed;

	/* enable for mute buzzer button */
	ops->get_buzzer_cleared = getBuzzerButtonStatus;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
