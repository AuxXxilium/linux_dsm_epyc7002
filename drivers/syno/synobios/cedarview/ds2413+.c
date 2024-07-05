// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "cedarview_common.h"

//extern function from cedarview_common.c
extern int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus);

PWM_FAN_SPEED_MAPPING gDS2413pSpeedMapping[] = { 
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

PWM_FAN_SPEED_MAPPING gDS2413pCPUFanSpeedMapping[] = { 
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
int DS2413pInitModuleType(struct synobios_ops *ops)
{	
	module_t type_ds2413p = MODULE_T_DS2413p;
	module_t *pType = &type_ds2413p;
	
	module_type_set(pType);
	return 0;
}

static
int DS2413pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS2413pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS2413pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS2413pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
int DS2413pCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS2413pCPUFanSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS2413pCPUFanSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS2413pCPUFanSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds2413p_ops = {
	.x86_init_module_type = DS2413pInitModuleType,
	.x86_fan_speed_mapping = DS2413pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = DS2413pCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_fan_status = CedarviewGetFanStatusMircopWithGPIO,
};
