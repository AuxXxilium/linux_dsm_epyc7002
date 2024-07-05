// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "bromolow_common.h"

// extern function from bromolow_common
extern int BromolowRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsFanSpeedMapping(FAN_SPEED speed);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);

static
int DS3612xsInitModuleType(struct synobios_ops *ops)
{
	module_t type = MODULE_T_DS3612xs;
	module_t *pType = &type;

	module_type_set(pType);
	return 0;
}

struct model_ops ds3612xs_ops = {
	.x86_init_module_type = DS3612xsInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BromolowRedundantPowerGetPowerStatus,
};
