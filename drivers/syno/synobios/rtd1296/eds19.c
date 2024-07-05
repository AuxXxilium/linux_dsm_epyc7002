// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "rtd1296_common.h"
#include "syno_ttyS.h"
#include "../syno_power_outage/syno_power_outage.h"


int GetModel(void)
{
	return MODEL_EDS19;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1000);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_eds19 = MODULE_T_EDS19;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_EDS19:
		pType = &type_eds19;
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

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	/* eds19 doesn't need set fan status */
	return 0;
}

/*
 *  EDS19 GPIO config table
 *
 *  Pin     In/Out    Function
 *
 * *** General ***
 *   6       In       HDD 1 present
 *  18      Out       HDD 1 green LED
 *  11      Out       lan + disk led control
 *  22      Out       USB3.0 port1 power
 *  23      Out       USB3.0 port0 power
 *  60       In       USB3.0 port1 over current
 *  61       In       USB3.0 port2 over current
 *  98      Out       HDD 1 orange LED
 * 128      Out       LAN 1G Link/Activity LED
 * 129      Out       LAN 10/100 Link/Activity LED
 * 130      Out       PCIE1 reset
 *
 *  *** Power ***
 *   4       In       Super Cap BP present
 *   5       In       PoE BP present
 *   7       In       Super Cap BP PFOn
 *                    (Power loss and the power source change to Super Cap BP)
 *  26       In       Power Adapter Present
 *  97       In       Super Cap power good
 *
 *  *** SD card ***
 *  81      Out       SD command(CMD)
 *  82      Out       SD clock(CLK)
 *  83       In       SD write protest(WP)
 *  84       In       SD card detect(CD)
 *  85       Bi       SD data 0(D0)
 *  86       Bi       SD data 1(D1)
 *  87       Bi       SD data 2(D2)
 *  88       Bi       SD data 3(D3)
 *
 *  99      Out       SD card power enable
 * 100       In       SD eject button
 */

/*
 *  EDS19 other control
 *
 *  HDD green LED	Realtek driver (ahci-rtk.c, rtd-1296-synology-eds19.dts)
 *  Lan LED			Realtek driver (r8169.c, rtd-1296-synology-eds19.dts)
 *
 */

static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 1,
	.gpio_port		= {98},
	.gpio_polarity	= ACTIVE_LOW,
};

static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {6},
	.gpio_polarity	= ACTIVE_LOW,
};

/* GPIO 11 is lan + disk led control
 * We need it mask only at poweron and phy_led_ctrl behavior is fit our needs. */
static SYNO_GPIO_INFO phy_led_ctrl = {
	.nr_gpio        = 1,
	.gpio_port      = {11},
	.gpio_polarity  = ACTIVE_HIGH,
};

// high: not using super cap, low: using super cap
SYNO_GPIO_INFO power_outage_gpio = {
	.nr_gpio		= 1,
	.gpio_port		= {7},
	.gpio_polarity	= ACTIVE_LOW,
};

// high: power adapter present, low: no power adapter
static SYNO_GPIO_INFO power_adapter_present = {
	.nr_gpio		= 1,
	.gpio_port		= {26},
	.gpio_polarity	= ACTIVE_HIGH,
};

// high: super cap percentage more than 92%, low: less than 92%
static SYNO_GPIO_INFO super_cap_good = {
	.nr_gpio		= 1,
	.gpio_port		= {97},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}

	syno_gpio.hdd_fail_led			= &hdd_fail_led;
	syno_gpio.phy_led_ctrl			= &phy_led_ctrl;

	// gpio direction input
	syno_gpio_direction_input(power_adapter_present.gpio_port[0]);
	syno_gpio_direction_input(super_cap_good.gpio_port[0]);

	syno_power_outage_init();
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	syno_gpio.hdd_fail_led			= NULL;
	syno_gpio.phy_led_ctrl			= NULL;

	syno_power_outage_remove();
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;

	/* enable to control PHY_LED */
	ops->set_phy_led = SetPhyLed;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
