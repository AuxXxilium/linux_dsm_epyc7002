// Copyright (c) 2000-2013 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "cedarview_common.h"

//extern function from cedarview_common.c
extern int CedarviewRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus);

PWM_FAN_SPEED_MAPPING gRS2414rppSpeedMapping[] = { 
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

PWM_FAN_SPEED_MAPPING gRS2414rppCPUFanSpeedMapping[] = { 
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
int RS2414rppInitModuleType(struct synobios_ops *ops)
{	
	module_t type_rs2414rpp = MODULE_T_RS2414rpp;
	module_t *pType = &type_rs2414rpp;
	
	module_type_set(pType);
	return 0;
}

static
int RS2414rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS2414rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS2414rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS2414rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
int RS2414rppGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    GPIO_PIN Pin;
    int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = SYNO_GPP_RS_BUZZER_OFF;
    if ( 0 > GetGpioPin( &Pin ) ) {
        goto End;
    }

    if ( 0 == Pin.value ) {
        *buzzer_cleared = 1;
    }

    ret = 0;
End:
    return ret;
}

static
int RS2414rppCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS2414rppCPUFanSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS2414rppCPUFanSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS2414rppCPUFanSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs2414rpp_ops = {
	.x86_init_module_type = RS2414rppInitModuleType,
	.x86_fan_speed_mapping = RS2414rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = RS2414rppCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = RS2414rppGetBuzzerCleared,
	.x86_get_power_status = CedarviewRedundantPowerGetPowerStatus,
	.x86_get_fan_status = CedarviewGetFanStatusMircopWithGPIO,
};
