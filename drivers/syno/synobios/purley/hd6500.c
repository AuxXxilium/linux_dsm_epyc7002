// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "purley_common.h"

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x59
#define PSU_BTM_I2C_ADDR 	0x58

// extern function from purley_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int HD6500I2CGetPowerInfo(POWER_INFO *power_info)
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

// GPP_H1 CPLD_LED_CTRL_N (No.216) for SystemDisk HDD LED mask & Pront Panel Status LED mask
static SYNO_GPIO_INFO disk_led_ctrl = {
    .nr_gpio        = 1,
    .gpio_port      = {PURLEY_CPLD_LED_CTRL_N},
    .gpio_polarity  = ACTIVE_HIGH,
};

static
void HD6500GpioInit(void)
{
    syno_gpio.fan_fail              = NULL;
    syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
    syno_gpio.phy_led_ctrl          = NULL;
    syno_gpio.copy_button_detect    = NULL;
}

static
void HD6500GpioCleanup(void)
{
    syno_gpio.fan_fail              = NULL;
    syno_gpio.disk_led_ctrl         = NULL;
    syno_gpio.phy_led_ctrl          = NULL;
    syno_gpio.copy_button_detect    = NULL;
}

int HD6500InitModuleType(struct synobios_ops *ops)
{
    module_t type_hd6500 = MODULE_T_HD6500;
    module_t *pType = &type_hd6500;

    module_type_set(pType);
    return 0;
}

struct model_ops hd6500_ops = {
	.x86_init_module_type = HD6500InitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = HD6500I2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
	.x86_gpio_init = HD6500GpioInit,
	.x86_gpio_cleanup = HD6500GpioCleanup,
};

SYNO_HWMON_SENSOR_TYPE HD6500_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE HD6500_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE HD6500_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE HD6500_psu_status[] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 6,
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
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 6,
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
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};
struct hwmon_sensor_list hd6500_sensor_list = {
        .thermal_sensor = &HD6500_thermal_sensor,
        .voltage_sensor = &HD6500_voltage_sensor,
        .fan_speed_rpm = &HD6500_fan_speed_rpm,
        .psu_status = HD6500_psu_status,
        .hdd_backplane = NULL,
};
