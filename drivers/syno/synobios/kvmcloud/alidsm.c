// Copyright (c) 2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "kvmcloud_common.h"

// extern function from bromolow_common
extern int KvmcloudRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);

static
int AliDSMInitModuleType(struct synobios_ops *ops)
{
	module_t type_alidsm = MODULE_T_AliDSM;
	module_t *pType = &type_alidsm;

	module_type_set(pType);
	return 0;
}

struct model_ops alidsm_ops = {
	.x86_init_module_type = AliDSMInitModuleType,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = KvmcloudRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
