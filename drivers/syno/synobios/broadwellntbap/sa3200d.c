// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwellntbap_common.h"

// extern function from broadwellntbap_common
extern int BroadwellntbapRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int SA3200dInitModuleType(struct synobios_ops *ops)
{
	module_t type_sa3200d = MODULE_T_SA3200d;
	module_t *pType = &type_sa3200d;
	GPIO_PIN Pin;

	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNTBAP_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}
	module_type_set(pType);
	return 0;
}

struct model_ops sa3200d_ops = {
	.x86_init_module_type = SA3200dInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BroadwellntbapRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};
