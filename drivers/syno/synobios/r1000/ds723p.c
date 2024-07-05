// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "r1000_common.h"

PWM_FAN_SPEED_MAPPING gDS723pSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

static
void DS723pGpioInit(void)
{
	syno_gpio.fan_fail          = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

static
void DS723pGpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

SYNO_HWMON_SENSOR_TYPE ds723p_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE ds723p_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
int DS723pInitModuleType(struct synobios_ops *ops)
{
	module_t type_723p = MODULE_T_DS723p;
	module_t *pType = &type_723p;

	module_type_set(pType);
	return 0;
}

static
int DS723pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS723pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS723pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS723pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds723p_ops = {
	.x86_init_module_type = DS723pInitModuleType,
	.x86_fan_speed_mapping = DS723pFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = DS723pGpioInit,
	.x86_gpio_cleanup = DS723pGpioCleanup,
};

struct hwmon_sensor_list ds723p_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &ds723p_hdd_backplane_status,
       .current_sensor = &ds723p_current_status,
};
