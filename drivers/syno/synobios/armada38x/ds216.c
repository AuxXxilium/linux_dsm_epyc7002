// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "armada38x_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS216;
}

void VdimmPwrCtrl(SYNO_LED ledStatus)
{
	int gpio_number;
	int pinValue;

	/* Custmized GPIO pin number of VdimmPWM control for DS216 */
	/* OFF: make all LED off, except breathing status led. */
	/* ON:  enable all LED which can be switched on/off    */
	gpio_number = 42;

	switch (ledStatus) {
		case SYNO_LED_OFF:
			pinValue = 0;
			break;
		case SYNO_LED_ON:
			pinValue = 1;
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			return;
	}
	SYNO_GPIO_WRITE(gpio_number, pinValue);
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

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_216 = MODULE_T_DS216;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS216:
		pType = &type_216;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	return SYNO_HDD_LED_SET(disknum, status);
}

int SetPhyLed(SYNO_LED ledStatus)
{
	int ret = -1;
	int gpio_number;
	int pinValue;

	/* Custmized GPIO pin number of PHY LED for DS216 */
	gpio_number = 16;

	switch (ledStatus) {
		case SYNO_LED_OFF:
			pinValue = 0;
			break;
		case SYNO_LED_ON:
			pinValue = 1;
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			ret = -EINVAL;
			goto END;
	}
	SYNO_GPIO_WRITE(gpio_number, pinValue);
	ret = 0;

END:
	return ret;
}

int SetHddLed(SYNO_LED ledStatus)
{
	int ret = -1;
	int gpio_number;
	int pinValue;

	/* Custmized GPIO pin number of HDD LED for DS216 */
	gpio_number = 9;

	switch (ledStatus) {
		case SYNO_LED_OFF:
			pinValue = 0;
			break;
		case SYNO_LED_ON:
			pinValue = 1;
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			ret = -EINVAL;
			goto END;
	}
	SYNO_GPIO_WRITE(gpio_number, pinValue);
	ret = 0;

END:
	return ret;
}

int SetPowerLedStatus(SYNO_LED status)
{
	char szCommand[5] = {0};
	int err = -1;

        switch(status){
		case SYNO_LED_ON:
			VdimmPwrCtrl(SYNO_LED_ON);
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			VdimmPwrCtrl(SYNO_LED_OFF);
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

int getCopyButtonStatus(void)
{
    // for matching userspace usage, button pressed = 0, else = 1
    return SYNO_COPY_BUTTON_GPIO_GET();
}

/*
 *  DS216 GPIO config table
 *
 *  Pin     In/Out    Function

 *  12       In       Model ID 0
 *  21       In       Model ID 1
 *  45       In       Model ID 2
 *  39       In       HDD 1 present
 *  40       In       HDD 2 present
 *  41       In       USB3 overcurrent
 *  42       In       USB copy button press intterupt
 *  43       In       USB2 overcurrent
 *  46       In       SD card insert interrupt
 *   6      Out       COPY BUTTON
 *  13      Out       HDD 1 fault LED
 *  14      Out       HDD 2 fault LED
 *  26      Out       HDD 1 power enable
 *  27      Out       HDD 2 power enable
 *  15      Out       Fan High
 *  37      Out       Fan Mid
 *  38      Out       Fan Low
 *  44      Out       USB3 power enable
 *  47      Out       USB2 power enable
 */

static SYNO_GPIO_INFO fan_ctrl = {
	.nr_gpio		= 3,
	.gpio_port		= {38, 37, 15},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 2,
	.gpio_port		= {13, 14},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 2,
	.gpio_port		= {39, 40},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 2,
	.gpio_port		= {26, 27},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO model_id = {
	.nr_gpio		= 3,
	.gpio_port		= {12, 21, 45},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO copy_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {6},
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

	syno_gpio.fan_ctrl 				= &fan_ctrl;
	syno_gpio.hdd_fail_led 			= &hdd_fail_led;
	syno_gpio.model_id 				= &model_id;
	syno_gpio.copy_button_detect 	= &copy_button_detect;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_ctrl 				= NULL;
	syno_gpio.hdd_fail_led 			= NULL;
	syno_gpio.model_id 				= NULL;
	syno_gpio.copy_button_detect 	= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable all LED */
	VdimmPwrCtrl(SYNO_LED_ON);
	/* enable to control LAN_LED */
	ops->set_phy_led = SetPhyLed;
	/* enable to control HDD_LED */
	ops->set_hdd_led = SetHddLed;
	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;
	/* pull high SATA_LED_CTRL pin, otherwise the hdd led will be off in junior mode*/
	ops->set_hdd_led(SYNO_LED_ON);
	/* enable to read USBCOPY GPIO status. */
	/* for matching userspace usage, button pressed = 0, else = 1 */
	ops->get_copy_button_status = getCopyButtonStatus;
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
