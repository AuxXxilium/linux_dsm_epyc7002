// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "rtd1296_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS418;
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
	module_t type_418 = MODULE_T_DS418;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS418:
		pType = &type_418;
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

	if(disknum > 2) {
		if(status == DISK_LED_ORANGE_SOLID || status == DISK_LED_ORANGE_BLINK || status == DISK_LED_OFF) {
			syno_ahci_disk_green_led(disknum - 1, 0);
		}
		else {
			syno_ahci_disk_green_led(disknum - 1, 1);
		}
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

/*
 *  DS418 GPIO config table
 *
 *  Pin     In/Out    Function

 *  12       In       Model ID 0
 *  21       In       Model ID 1
 *  45       In       Model ID 2
 *  39       In       HDD 1 present
 *  40       In       HDD 2 present
 *  41       In       HDD 3 present
 *  43       In       HDD 4 present
 *  52       In       Fan 1 fail
 *  53       In       Fan 2 fail
 *  54       In       USB3 overcurrent
 *  55       In       USB2 overcurrent
 *   6      Out       LED on
 *  13      Out       HDD 1 fault LED
 *  14      Out       HDD 2 fault LED
 *  15      Out       HDD 3 fault LED
 *  16      Out       HDD 4 fault LED
 *  26      Out       HDD 1 power enable
 *  27      Out       HDD 2 power enable
 *  37      Out       HDD 3 power enable
 *  38      Out       HDD 4 power enable
 *  48      Out       Fan Low
 *  49      Out       Fan Mid
 *  50      Out       Fan High
 *  58      Out       USB3 power enable
 *  59      Out       USB2 power enable
 */

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio        = 2,
	.gpio_port      = {24, 12},
	.gpio_polarity  = ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 4,
	.gpio_port		= {100, 99, 98, 97},
	.gpio_polarity	= ACTIVE_HIGH,
};
/*
 * For Asmedia sata disk led, we use GPIO 54 and 63
 * For Realtek internal sata disk led, we use GPIO 18 and 19
 * GPIO 18 and 19 blink and enable are controlled by driver/ata/ahci_rtk.c driver
 * Therefore, synobios don't control GPIO 18 and 19 to prevent conflict
 */
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 2,
	.gpio_port		= {54, 63},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 4,
	.gpio_port		= {4, 5, 6, 7},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {85, 86, 82, 81},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO model_id = {
	.nr_gpio		= 3,
	.gpio_port		= {12, 21, 45},
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

	syno_gpio.fan_ctrl 		= NULL;
	syno_gpio.fan_fail 		= &fan_fail;
	syno_gpio.hdd_fail_led 	= &hdd_fail_led;
	syno_gpio.hdd_present_led	= &hdd_present_led;
	syno_gpio.model_id 		= &model_id;
	syno_gpio.disk_led_ctrl = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_ctrl 		= NULL;
	syno_gpio.fan_fail 		= NULL;
	syno_gpio.hdd_fail_led 	= NULL;
	syno_gpio.hdd_present_led	= NULL;
	syno_gpio.model_id 		= NULL;
	syno_gpio.disk_led_ctrl = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
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

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
