// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

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
int RS3617xsInitModuleType(struct synobios_ops *ops)
{
	module_t type = MODULE_T_RS3617xs;
	module_t *pType = &type;

	module_type_set(pType);
	return 0;
}

SYNO_HWMON_SENSOR_TYPE RS3617xs_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS3617xs_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS3617xs_fan_speed_rpm = {
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

struct model_ops rs3617xs_ops = {
	.x86_init_module_type = RS3617xsInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BromolowRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear	= xsSetBuzzerClear,
};

struct hwmon_sensor_list rs3617xs_sensor_list = {
	.thermal_sensor = &RS3617xs_thermal_sensor,
	.voltage_sensor = &RS3617xs_voltage_sensor,
	.fan_speed_rpm = &RS3617xs_fan_speed_rpm,
	.psu_status = NULL,
	.hdd_backplane = NULL,
};
