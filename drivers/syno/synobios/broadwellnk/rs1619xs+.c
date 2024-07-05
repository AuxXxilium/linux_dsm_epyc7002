// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "broadwellnk_common.h"

// extern function from broadwellnk_common
extern int BroadwellnkRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int RS1619xspInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs1619xsp = MODULE_T_RS1619xsp;
	module_t *pType = &type_rs1619xsp;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio BROADWELL_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNK_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

SYNO_HWMON_SENSOR_TYPE RS1619xsp_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS1619xsp_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS1619xsp_fan_speed_rpm = {
	.type_name = HWMON_SYS_FAN_RPM_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_SYS_FAN1_RPM,
	},
	.sensor[1] = {
		.sensor_name = HWMON_SYS_FAN2_RPM,
	},
};

SYNO_HWMON_SENSOR_TYPE RS1619xsp_hdd_backplane_status = {
        .type_name = HWMON_HDD_BP_STATUS_NAME,
        .sensor_num = 2,
        .sensor[0] = {
                .sensor_name = HWMON_HDD_BP_DETECT,
        },
        .sensor[1] = {
                .sensor_name = HWMON_HDD_BP_ENABLE,
        },
};

int rs1619xsp_hdd_enable_gpio[4] = {71, 70, 25, 27};
int rs1619xsp_hdd_detect_gpio[4] = {69, 26, 35, 37};

struct model_ops rs1619xsp_ops = {
	.x86_init_module_type = RS1619xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = BroadwellnkRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list rs1619xsp_sensor_list = {
	.thermal_sensor = &RS1619xsp_thermal_sensor,
	.voltage_sensor = &RS1619xsp_voltage_sensor,
	.fan_speed_rpm = &RS1619xsp_fan_speed_rpm,
	.psu_status = NULL,
	.hdd_backplane = &RS1619xsp_hdd_backplane_status,
};

