// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include "synobios.h"
#include "epyc7002_common.h"
#include "../i2c/i2c-linux.h"

#ifdef CONFIG_SYNO_HWMON_PMBUS
extern int EPYC7002RedundantPowerGetPowerStatusByI2C(POWER_INFO *power_info);
#endif /* CONFIG_SYNO_HWMON_PMBUS */

PWM_FAN_SPEED_MAPPING gFS6410SpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

SYNO_HWMON_SENSOR_TYPE FS6410_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6410_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6410_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE FS6410_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE FS6410_psu_status[] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 4,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN_VOLT,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 4,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN_VOLT,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

int FS6410GetBuzzerCleared(unsigned char *buzzer_cleared)
{
    return SYNO_BUZZER_BUTTON_GPIO_GET(buzzer_cleared);
}

// GPIO_31
static SYNO_GPIO_INFO mute_button_detect = {
	.nr_gpio                = 1,
	.gpio_port              = {31},
	.gpio_polarity  = ACTIVE_LOW,
};

static SYNO_GPIO_INFO alarm_led = {
	.nr_gpio                = 1,
	.gpio_port              = {137},
	.gpio_polarity  = ACTIVE_HIGH,
};

static
void FS6410GpioInit(void)
{
	syno_gpio.mute_button_detect		= &mute_button_detect;
	syno_gpio.alarm_led			= &alarm_led;
}

static
void FS6410GpioCleanup(void)
{
	syno_gpio.mute_button_detect		= NULL;
	syno_gpio.alarm_led			= NULL;
}

static
int FS6410InitModuleType(struct synobios_ops *ops)
{
	module_t type_FS6410 = MODULE_T_FS6410;
	module_t *pType = &type_FS6410;

	module_type_set(pType);
	return 0;
}

static
int FS6410FanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gFS6410SpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gFS6410SpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gFS6410SpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops fs6410_ops = {
	.x86_init_module_type = FS6410InitModuleType,
	.x86_fan_speed_mapping = FS6410FanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = FS6410GetBuzzerCleared,
	.x86_gpio_init = FS6410GpioInit,
	.x86_gpio_cleanup = FS6410GpioCleanup,
#ifdef CONFIG_SYNO_HWMON_PMBUS
        .x86_get_power_status = EPYC7002RedundantPowerGetPowerStatusByI2C,
#endif /* CONFIG_SYNO_HWMON_PMBUS */
};

struct hwmon_sensor_list fs6410_sensor_list = {
	.thermal_sensor = &FS6410_thermal_sensor,
	.voltage_sensor = &FS6410_voltage_sensor,
	.fan_speed_rpm = &FS6410_fan_speed_rpm,
	.psu_status = FS6410_psu_status,
	.hdd_backplane = &FS6410_hdd_backplane_status,
};

