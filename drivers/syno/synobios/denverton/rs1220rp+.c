// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "denverton_common.h"

extern int DenvertonRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int DenvertonGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int SetRPBuzzerClear(unsigned char buzzer_cleared);

PWM_FAN_SPEED_MAPPING gRS1220rppSpeedMapping[] = {
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
int RS1220rppInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs1220rpp = MODULE_T_RS1220rpp;
	module_t *pType = &type_rs1220rpp;

	module_type_set(pType);
	return 0;
}

static
int RS1220rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS1220rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS1220rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS1220rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs1220rpp_ops = {
	.x86_init_module_type = RS1220rppInitModuleType,
	.x86_fan_speed_mapping = RS1220rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = DenvertonGetBuzzerCleared,
	.x86_get_power_status = DenvertonRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = NULL,
};
