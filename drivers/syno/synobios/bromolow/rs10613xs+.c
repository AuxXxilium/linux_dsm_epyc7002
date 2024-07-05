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
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);

static
int RS10613xspInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs10613xsp = MODULE_T_RS10613xsp;
	module_t *pType = &type_rs10613xsp;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power or lcd panel then poweron, 
	 * It may cause gpio 5 set to low, it will casue unwanted  buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = 5;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops rs10613xsp_ops = {
	.x86_init_module_type = RS10613xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BromolowRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
