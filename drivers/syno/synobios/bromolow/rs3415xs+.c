// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

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
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);

static
int RS3415xspInitModuleType(struct synobios_ops *ops)
{
	module_t type = MODULE_T_RS3415xsp;
	module_t *pType = &type;

	module_type_set(pType);
	return 0;
}

struct model_ops rs3415xsp_ops = {
	.x86_init_module_type = RS3415xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BromolowRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear	= xsSetBuzzerClear,
};
