// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwellntb_common.h"

// extern function from broadwellntb_common
extern int BroadwellntbRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int TAIPEIInitModuleType(struct synobios_ops *ops)
{
	module_t type_taipei = MODULE_T_TAIPEI;
	module_t *pType = &type_taipei;
	GPIO_PIN Pin;

	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNTB_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}
	module_type_set(pType);
	return 0;
}

struct model_ops taipei_ops = {
	.x86_init_module_type = TAIPEIInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = NULL,
	.x86_get_power_status    = NULL,
	.x86_set_buzzer_clear = NULL,
};
