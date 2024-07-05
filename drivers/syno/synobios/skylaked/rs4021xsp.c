// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "skylaked_common.h"

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x58
#define PSU_BTM_I2C_ADDR 	0x59

// extern function from broadwell_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio        = 1,
	.gpio_port      = {60},
	.gpio_polarity  = ACTIVE_LOW,
};

static
int RS4021xspI2CGetPowerInfo(POWER_INFO *power_info)
{
	int ret = -1;
	int err = -1;

	if (NULL == power_info) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, &(power_info->power_1));
	if (0 != err) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_BTM_I2C_ADDR, &(power_info->power_2));
	if (0 != err) {
		goto FAIL;
	}

        ret = 0;

FAIL:
	return ret;
}

static
int RS4021xspInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs4021xsp = MODULE_T_RS4021xsp;
	module_t *pType = &type_rs4021xsp;
	GPIO_PIN Pin;

	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio SKYLAKED_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = SKYLAKED_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops rs4021xsp_ops = {
	.x86_init_module_type = RS4021xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status = RS4021xspI2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
