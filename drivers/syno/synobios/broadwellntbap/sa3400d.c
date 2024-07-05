// Copyright (c) 2000-2022 Synology Inc. All rights reserved.

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
int SA3400dInitModuleType(struct synobios_ops *ops)
{
	module_t type_sa3400d = MODULE_T_SA3400d;
	module_t *pType = &type_sa3400d;
	GPIO_PIN Pin;

	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNTBAP_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}
	module_type_set(pType);
	return 0;
}

SYNO_HWMON_SENSOR_TYPE SA3400d_thermal_sensor = {
	.type_name = HWMON_SYS_THERMAL_NAME,
	.sensor_num = 3,
	.sensor[0] = {
		.sensor_name = "Remote1",
	},
	.sensor[1] = {
		.sensor_name = "Local",
	},
	.sensor[2] = {
		.sensor_name = "Remote2",
	},
};

SYNO_HWMON_SENSOR_TYPE SA3400d_voltage_sensor = {
	.type_name = HWMON_SYS_VOLTAGE_NAME,
	.sensor_num = 5,
	.sensor[0] = {
		.sensor_name = "VCC",
	},
	.sensor[1] = {
		.sensor_name = "VPP",
	},
	.sensor[2] = {
		.sensor_name = "V33",
	},
	.sensor[3] = {
		.sensor_name = "V5",
	},
	.sensor[4] = {
		.sensor_name = "V12",
	},
};

SYNO_HWMON_SENSOR_TYPE SA3400d_fan_speed_rpm = {
	.type_name = HWMON_SYS_FAN_RPM_NAME,
	.sensor_num = 4,
	.sensor[0] = {
		.sensor_name = HWMON_SYS_FAN1_RPM,
	},
	.sensor[1] = {
		.sensor_name = HWMON_SYS_FAN2_RPM,
	},
	.sensor[2] = {
		.sensor_name = HWMON_SYS_FAN3_RPM,
	},
	.sensor[3] = {
		.sensor_name = HWMON_SYS_FAN4_RPM,
	},
};

SYNO_HWMON_SENSOR_TYPE SA3400d_psu_status[2] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 1,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 1,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

struct model_ops sa3400d_ops = {
	.x86_init_module_type = SA3400dInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BroadwellntbapRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list sa3400d_sensor_list = {
        .thermal_sensor = &SA3400d_thermal_sensor,
        .voltage_sensor = &SA3400d_voltage_sensor,
        .fan_speed_rpm = &SA3400d_fan_speed_rpm,
        .psu_status = SA3400d_psu_status,
		// SA3400D does not support the backplane monitor
        .hdd_backplane = NULL,
};
