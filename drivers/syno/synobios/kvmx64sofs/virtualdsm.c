// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "kvmx64sofs_common.h"

// extern function from bromolow_common
extern int Kvmx64sofsRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);

static
int VirtualDSMInitModuleType(struct synobios_ops *ops)
{
	module_t type_virtualdsm = MODULE_T_VirtualDSM;
	module_t *pType = &type_virtualdsm;

	module_type_set(pType);
	return 0;
}

struct model_ops virtualdsm_ops = {
	.x86_init_module_type = VirtualDSMInitModuleType,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = Kvmx64sofsRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
