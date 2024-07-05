// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "cedarview_common.h"

// extern function from cedarview_common
extern int CedarviewRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus);

PWM_FAN_SPEED_MAPPING gRS814rppSpeedMapping[] = {
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

PWM_FAN_SPEED_MAPPING gRS814rppCPUFanSpeedMapping[] = { 
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
int RS814rppInitModuleType(struct synobios_ops *ops)
{
	module_t type_814p = MODULE_T_RS814rpp;
	module_t *pType = &type_814p;
	
	module_type_set(pType);
	return 0;
}

static
int RS814rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS814rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS814rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS814rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
int RS814rppGetBuzzerCleared(unsigned char *buzzer_cleared)
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
int RS814rppCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS814rppCPUFanSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS814rppCPUFanSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS814rppCPUFanSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs814rpp_ops = {
	.x86_init_module_type = RS814rppInitModuleType,
	.x86_fan_speed_mapping = RS814rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = RS814rppCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = RS814rppGetBuzzerCleared,
	.x86_get_power_status = CedarviewRedundantPowerGetPowerStatus,
	.x86_get_fan_status = CedarviewGetFanStatusMircopWithGPIO,
};
