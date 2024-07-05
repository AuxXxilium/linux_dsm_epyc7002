// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "denverton_common.h"


PWM_FAN_SPEED_MAPPING gDS2419pIISpeedMapping[] = {
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
int DS2419pIIInitModuleType(struct synobios_ops *ops)
{
	module_t type_ds2419pII = MODULE_T_DS2419pII;
	module_t *pType = &type_ds2419pII;

	module_type_set(pType);
	return 0;
}

static
int DS2419pIIFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS2419pIISpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS2419pIISpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS2419pIISpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds2419pII_ops = {
	.x86_init_module_type = DS2419pIIInitModuleType,
	.x86_fan_speed_mapping = DS2419pIIFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_power_status = NULL,
};

struct hwmon_sensor_list ds2419pII_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = NULL,
};
