// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "avoton_common.h"

extern int AvotonRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int AvotonGetBuzzerCleared(unsigned char *buzzer_cleared);

PWM_FAN_SPEED_MAPPING gDS1616pSpeedMapping[] = {
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
int DS1616pInitModuleType(struct synobios_ops *ops)
{
	module_t type_ds1616p = MODULE_T_DS1616p;
	module_t *pType = &type_ds1616p;

	module_type_set(pType);
	return 0;
}

static
int DS1616pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS1616pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS1616pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS1616pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds1616p_ops = {
	.x86_init_module_type = DS1616pInitModuleType,
	.x86_fan_speed_mapping = DS1616pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_power_status = NULL,
};
