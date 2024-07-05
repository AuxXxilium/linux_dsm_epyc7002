// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "armada37xx_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS119j;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 800);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_119j = MODULE_T_DS119j;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS119j:
		pType = &type_119j;
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

	/* first time we tried to light on disk led */
	if (!diskLedEnabled && DISK_LED_OFF != status) {
		HddPhyLedPwrCtrl(SYNO_LED_ON);
		diskLedEnabled = 1;
	}

	iRet = SYNO_HDD_LED_SET(disknum, status);
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

int SetHddLed(SYNO_LED ledStatus)
{
	int ret = -1;
	SYNO_DISK_LED status;
	switch (ledStatus) {
		case SYNO_LED_OFF:
			status = DISK_LED_OFF;
			break;
		case SYNO_LED_ON:
			status = DISK_LED_GREEN_SOLID;
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			goto END;
	}
	SetDiskLedStatus(1, status);
	ret = 0;
END:
	return ret;
}

/*
 *  DS119j GPIO config table
 *
 *  Pin     In/Out    Function

 *  50       In       Model ID 0
 *  51       In       Model ID 1
 *  52       In       Model ID 2
 *  45       In       Fan 1 fail
 *  56      Out       hdd & phy LED power enable
 *  57      Out       USB3 ext. hub power enable
 *  40      Out       HDD 1 power enable
 *  42      Out       Fan High
 *  43      Out       Fan Mid
 *  44      Out       Fan Low
 *  36      Out       USB1 power enable
 *  37      Out       USB2 power enable
 */

static SYNO_GPIO_INFO fan_ctrl = {
	.nr_gpio		= 3,
	.gpio_port		= {44, 43, 42},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 1,
	.gpio_port		= {45},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 1,
	.gpio_port		= {13},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 1,
	.gpio_port		= {11},
	.gpio_polarity	= ACTIVE_LOW,
};
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 1,
	.gpio_port		= {40},
	.gpio_polarity	= ACTIVE_HIGH,
};
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
static SYNO_GPIO_INFO model_id = {
	.nr_gpio		= 3,
	.gpio_port		= {50, 51, 52},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
	syno_gpio.fan_ctrl = &fan_ctrl;
	syno_gpio.fan_fail = &fan_fail;
	syno_gpio.hdd_fail_led 	= &hdd_fail_led;
	syno_gpio.hdd_present_led	= &hdd_present_led;
	syno_gpio.model_id = &model_id;
}

void syno_gpio_cleanup(void)
{
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

	syno_gpio.fan_ctrl		= NULL;
	syno_gpio.hdd_present_led	= NULL;
	syno_gpio.fan_fail		= NULL;
	syno_gpio.hdd_fail_led		= NULL;
	syno_gpio.model_id		= NULL;
	syno_gpio.disk_led_ctrl		= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	ops->set_hdd_led = SetHddLed;
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
