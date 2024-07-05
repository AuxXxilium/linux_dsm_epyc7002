// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "denverton_common.h"

extern int DenvertonRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int DenvertonGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int SetRPBuzzerClear(unsigned char buzzer_cleared);

PWM_FAN_SPEED_MAPPING gRS2818rppSpeedMapping[] = {
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
int RS2818rppInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs2818rpp = MODULE_T_RS2818rpp;
	module_t *pType = &type_rs2818rpp;

	module_type_set(pType);
	return 0;
}

static
int RS2818rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS2818rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS2818rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS2818rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs2818rpp_ops = {
	.x86_init_module_type = RS2818rppInitModuleType,
	.x86_fan_speed_mapping = RS2818rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = DenvertonGetBuzzerCleared,
	.x86_get_power_status = DenvertonRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = SetRPBuzzerClear,
};

struct hwmon_sensor_list rs2818rpp_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = NULL,
};