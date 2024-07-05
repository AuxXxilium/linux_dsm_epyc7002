// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "cedarview_common.h"

//extern function from cedarview_common.c
extern int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus);

PWM_FAN_SPEED_MAPPING gDS1512pSpeedMapping[] = { 
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
int DS1512pInitModuleType(struct synobios_ops *ops)
{	
	module_t type_1512p = MODULE_T_DS1512p;
	module_t *pType = &type_1512p;
	
	module_type_set(pType);
	return 0;
}

static
int DS1512pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS1512pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS1512pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS1512pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds1512p_ops = {
	.x86_init_module_type = DS1512pInitModuleType,
	.x86_fan_speed_mapping = DS1512pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_fan_status = CedarviewGetFanStatusMircopWithGPIO,
};
