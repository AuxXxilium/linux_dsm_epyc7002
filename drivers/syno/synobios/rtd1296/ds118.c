// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include "rtd1296_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS118;
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
	module_t type_118 = MODULE_T_DS118;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS118:
		pType = &type_118;
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

	if(status == DISK_LED_ORANGE_SOLID || status == DISK_LED_ORANGE_BLINK || status == DISK_LED_OFF) {
		syno_ahci_disk_green_led(disknum - 1, 0);
	}
	else {
		syno_ahci_disk_green_led(disknum - 1, 1);
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
 *  DS118 GPIO config table
 *
 *  Pin     In/Out    Function
 *
 *  16       In       Reset button
 *  11      Out       Control front panel LED
 *  21      Out       Status green LED
 *  22      Out       USB3.0 port1 power
 *  23      Out       USB3.0 port0 power
 *  25      Out       Status red LED
 *  18      Out       HDD 1 green LED
 *  85      Out       HDD 1 power enable
 *  87      Out       Control HDD LED
 *  98      Out       HDD 1 orange LED
 * 128      Out       LAN LED
 *
 */

/*
 *  DS118 other control
 *
 *  Fan fail		MicroP
 *  HDD green LED	Realtek driver (ahci.c, rtd-1296-synology-ds118.dts)
 *  Lan LED			Realtek driver (r8169.c, rtd-1296-synology-ds118.dts)
 *
 */

static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 1,
	.gpio_port		= {98},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 1,
	.gpio_port		= {85},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {87},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	syno_gpio.hdd_fail_led	= &hdd_fail_led;
	syno_gpio.disk_led_ctrl	= &disk_led_ctrl;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.hdd_fail_led	= NULL;
	syno_gpio.disk_led_ctrl	= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;

	/* enable to control HDD_LED */
	ops->set_hdd_led = SetHddLed;

	/* enable to control PHY_LED */
	ops->set_phy_led = SetPhyLedR8169NoCtrlPin;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
