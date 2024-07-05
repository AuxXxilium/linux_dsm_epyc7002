// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include "synobios.h"
#include "purley_common.h"
#include "../i2c/i2c-linux.h"

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x58
#define PSU_BTM_I2C_ADDR 	0x59

// extern function from purley_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int FS6600NI2CGetPowerInfo(POWER_INFO *power_info)
{
	int ret = -1;
	int err = -1;

	if (NULL == power_info) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, &(power_info->power_1));
	if (0 != err) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_BTM_I2C_ADDR, &(power_info->power_2));
	if (0 != err) {
		goto FAIL;
	}

	ret = 0;

FAIL:
	return ret;
}

int FS6600NInitModuleType(struct synobios_ops *ops)
{
    module_t type_fs6600n = MODULE_T_FS6600N;
    module_t *pType = &type_fs6600n;

    module_type_set(pType);
    return 0;
}

struct model_ops fs6600n_ops = {
	.x86_init_module_type = FS6600NInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = FS6600NI2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

SYNO_HWMON_SENSOR_TYPE FS6600N_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6600N_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6600N_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE FS6600N_psu_status[] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

struct hwmon_sensor_list fs6600n_sensor_list = {
        .thermal_sensor = &FS6600N_thermal_sensor,
        .voltage_sensor = &FS6600N_voltage_sensor,
        .fan_speed_rpm = &FS6600N_fan_speed_rpm,
        .psu_status = FS6600N_psu_status,
        .hdd_backplane = NULL,
};
