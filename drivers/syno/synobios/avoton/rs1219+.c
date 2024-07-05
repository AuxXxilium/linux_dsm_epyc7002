// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "avoton_common.h"

extern int AvotonRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int AvotonGetBuzzerCleared(unsigned char *buzzer_cleared);

PWM_FAN_SPEED_MAPPING gRS1219pSpeedMapping[] = {
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
int RS1219pInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs1219p = MODULE_T_RS1219p;
	module_t *pType = &type_rs1219p;

	module_type_set(pType);
	return 0;
}

static
int RS1219pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS1219pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS1219pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS1219pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs1219p_ops = {
	.x86_init_module_type = RS1219pInitModuleType,
	.x86_fan_speed_mapping = RS1219pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = AvotonGetBuzzerCleared,
	.x86_get_power_status = NULL,
};
