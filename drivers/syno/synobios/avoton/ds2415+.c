// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "avoton_common.h"


PWM_FAN_SPEED_MAPPING gDS2415pSpeedMapping[] = {
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
int DS2415pInitModuleType(struct synobios_ops *ops)
{
	module_t type_ds2415p = MODULE_T_DS2415p;
	module_t *pType = &type_ds2415p;
	module_type_set(pType);
	return 0;
}

static
int DS2415pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i = 0;

	for (i = 0; i < sizeof(gDS2415pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i) {
		if (gDS2415pSpeedMapping[i].fanSpeed == speed) {
			iDutyCycle = gDS2415pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds2415p_ops = {
	.x86_init_module_type = DS2415pInitModuleType,
	.x86_fan_speed_mapping = DS2415pFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
};
