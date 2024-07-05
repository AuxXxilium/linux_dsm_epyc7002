// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "coffeelake_common.h"

extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int DS3619xsInitModuleType(struct synobios_ops *ops)
{
	module_t type_ds3619xs = MODULE_T_DS3619xs;
	module_t *pType = &type_ds3619xs;

	module_type_set(pType);
	return 0;
}

struct model_ops ds3619xs_ops = {
	.x86_init_module_type = DS3619xsInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_power_status    = NULL,
	.x86_set_buzzer_clear = NULL,
};
