// Copyright (c) 2000-2011 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/gpio.h>
#include "evansport_common.h"

PWM_FAN_SPEED_MAPPING gDS415playSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 30 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 40 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 50 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 80 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 99 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

static
int DS415playInitModuleType(struct synobios_ops *ops)
{
	module_t type_415play = MODULE_T_DS415play;
	module_t *pType = &type_415play;

	module_type_set(pType);
	return 0;
}

static
int DS415playFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS415playSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS415playSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS415playSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds415play_ops = {
	.x86_init_module_type = DS415playInitModuleType,
	.x86_fan_speed_mapping = DS415playFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
};
