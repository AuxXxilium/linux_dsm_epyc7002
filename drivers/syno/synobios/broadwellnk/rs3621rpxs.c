// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwellnk_common.h"

#define I2C_SWITCH_ADDR 0x70
#define I2C_SWITCH_VAL 0x04

// extern function from broadwellnk_common
extern int BroadwellnkRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

void RS3621rpxsSMBusSwitchInit(void) {
	SMBusSwitchRegWrite(I2C_BUS_NO, I2C_SWITCH_ADDR, I2C_SWITCH_VAL);
}

SYNO_HWMON_SENSOR_TYPE RS3621rpxs_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS3621rpxs_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS3621rpxs_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE RS3621rpxs_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

static
int RS3621rpxsInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs3621rpxs = MODULE_T_RS3621rpxs;
	module_t *pType = &type_rs3621rpxs;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio BROADWELLNK_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNK_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops rs3621rpxs_ops = {
	.x86_init_module_type = RS3621rpxsInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BroadwellnkRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list rs3621rpxs_sensor_list = {
        .thermal_sensor = &RS3621rpxs_thermal_sensor,
        .voltage_sensor = &RS3621rpxs_voltage_sensor,
        .fan_speed_rpm = &RS3621rpxs_fan_speed_rpm,
        .psu_status = NULL,
        .hdd_backplane = NULL,
};
