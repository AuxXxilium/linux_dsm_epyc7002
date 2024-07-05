// Copyright (c) 2000-2012 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "cedarview_common.h"

//extern function from cedarview_common.c
extern int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus);

PWM_FAN_SPEED_MAPPING gDS713pSpeedMapping[] = { 
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

PWM_FAN_SPEED_MAPPING gDS713pCPUFanSpeedMapping[] = { 
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

SYNO_HWMON_SENSOR_TYPE ds713p_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

int ds713p_hdd_detect_gpio[] = {33, 35};
int ds713p_hdd_enable_gpio[] = {16, 20};

static
int DS713pInitModuleType(struct synobios_ops *ops)
{	
	module_t type_713p = MODULE_T_DS713p;
	module_t *pType = &type_713p;
	
	module_type_set(pType);
	return 0;
}

static
int DS713pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS713pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS713pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS713pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
int DS713pCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS713pCPUFanSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS713pCPUFanSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS713pCPUFanSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds713p_ops = {
	.x86_init_module_type = DS713pInitModuleType,
	.x86_fan_speed_mapping = DS713pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = DS713pCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_fan_status = CedarviewGetFanStatusMircopWithGPIO,
};

struct hwmon_sensor_list ds713p_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &ds713p_hdd_backplane_status,
};
