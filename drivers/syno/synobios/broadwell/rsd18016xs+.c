// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwell_common.h"

// extern function from bromolow_common
extern int BroadwellRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
/* FIXME wait HW fix this GPIO pin */
//extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int RSD18016xspInitModuleType(struct synobios_ops *ops)
{
	module_t type_rsd18016xsp = MODULE_T_RSD18016xsp;
	module_t *pType = &type_rsd18016xsp;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio 5 set to low, it will casue unwanted  buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = 5;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops rsd18016xsp_ops = {
	.x86_init_module_type = RSD18016xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
//	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_power_status    = BroadwellRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
