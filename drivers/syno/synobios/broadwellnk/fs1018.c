// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwellnk_common.h"

// extern function from broadwellnk_common
extern int BroadwellnkRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int FS1018InitModuleType(struct synobios_ops *ops)
{
	module_t type_fs1018 = MODULE_T_FS1018;
	module_t *pType = &type_fs1018;

	module_type_set(pType);
	return 0;
}

struct model_ops fs1018_ops = {
	.x86_init_module_type = FS1018InitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_power_status    = NULL,
	.x86_set_buzzer_clear = NULL,
};
