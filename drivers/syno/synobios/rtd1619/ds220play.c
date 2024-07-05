// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "rtd1619_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS220play;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1300);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_ds220play = MODULE_T_DS220play;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS220play:
		pType = &type_ds220play;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(DISKLEDSTATUS *pLedStatus)
{
	int disknum = pLedStatus->diskno;
	SYNO_DISK_LED status = pLedStatus->status;
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

	switch(status) {
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
 *  DS220play GPIO config table
 *
 *  Pin     In/Out    Function
 *
 *   3      Out       HDD green LED 1
 *   4      Out       HDD green LED 2
 *  18      Out       HDD orange LED 1
 *  19      Out       HDD orange LED 2
 *  22      Out       Front Panel LED Control
 *  34      Out       HDD LED Control
 *  35      Out       LAN LED Control
 *  41       In       HDD Detect 1
 *  42       In       HDD Detect 2
 *  51      Out       LAN LED
 *  57      Out       Fan control full
 *  59      Out       Fan control high
 *  60      Out       Fan control middle
 *  61      Out       Fan control low
 *  62      Out       HDD Power Enable 2
 *  67      Out       HDD Power Enable 1
 *  68      Out       Fan control voltage
 *  70      Out       USB3 Power Enable 1
 *  71       In       USB3 OC 1
 *  72      Out       USB3 Power Enable 2
 *  73       In       USB3 OC 2
 *
 */

/*
 *  DS220play other control
 *
 *  Fan fail		MicroP
 *  LAN LED			Realtek driver (r8169soc_1619.c, rtd-1619-synology-ds220play.dts)
 *
 */

static SYNO_GPIO_INFO fan_ctrl = {
	.nr_gpio		= 5,
	.gpio_port		= {61, 60, 59, 68, 57},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 2,
	.gpio_port		= {3, 4},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 2,
	.gpio_port		= {18, 19},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 2,
	.gpio_port		= {41, 42},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 2,
	.gpio_port		= {67, 62},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {34},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO phy_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {35},
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

	syno_gpio.fan_ctrl			= &fan_ctrl;
	syno_gpio.hdd_fail_led		= &hdd_fail_led;
	syno_gpio.hdd_present_led	= &hdd_present_led;
	syno_gpio.disk_led_ctrl		= &disk_led_ctrl;
	syno_gpio.phy_led_ctrl		= &phy_led_ctrl;

#if defined(CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY) || defined(CONFIG_SYNO_AHCI_GPIO_SOFTWARE_PRESENT_BLINK)
	syno_ahci_disk_led_enable_by_port(1, 1);
	syno_ahci_disk_led_enable_by_port(2, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY || CONFIG_SYNO_AHCI_GPIO_SOFTWARE_PRESENT_BLINK */
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_ctrl			= NULL;
	syno_gpio.hdd_fail_led		= NULL;
	syno_gpio.hdd_present_led	= NULL;
	syno_gpio.disk_led_ctrl		= NULL;
	syno_gpio.phy_led_ctrl		= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;

	/* enable to control HDD_LED */
	ops->set_hdd_led = SetHddLed;

	/* enable to control PHY_LED */
	ops->set_phy_led = SetPhyLed;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
