// Copyright (c) 2016 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "kvmx64_common.h"

// extern function from bromolow_common
extern int Kvmx64RedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);

static
int C2DSMInitModuleType(struct synobios_ops *ops)
{
	module_t type_c2dsm = MODULE_T_C2DSM;
	module_t *pType = &type_c2dsm;

	module_type_set(pType);
	return 0;
}

struct model_ops c2dsm_ops = {
	.x86_init_module_type = C2DSMInitModuleType,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = Kvmx64RedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
