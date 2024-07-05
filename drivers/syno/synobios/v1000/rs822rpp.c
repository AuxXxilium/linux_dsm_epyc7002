// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "v1000_common.h"

extern int V1000RedundantPowerGetPowerStatus(POWER_INFO *power_info);

#define FAN_NUM 2

PWM_FAN_SPEED_MAPPING gRS822rppSpeedMapping[] = {
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

SYNO_HWMON_SENSOR_TYPE RS822rpp_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS822rpp_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE RS822rpp_fan_speed_rpm = {
	.type_name = HWMON_SYS_FAN_RPM_NAME,
	.sensor_num = FAN_NUM,
	.sensor[0] = {
		.sensor_name = HWMON_SYS_FAN1_RPM,
	},
	.sensor[1] = {
		.sensor_name = HWMON_SYS_FAN2_RPM,
	},
};

SYNO_HWMON_FAN_ORDER rs822rpp_fan_order_list = {
	.fan_num = FAN_NUM,
	.fan_order_list = {1,2},
};

SYNO_HWMON_SENSOR_TYPE RS822rpp_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

int RS822rppGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    return SYNO_BUZZER_BUTTON_GPIO_GET(buzzer_cleared);
}

// GPIO_89
static SYNO_GPIO_INFO alarm_led = {
        .nr_gpio                = 1,
        .gpio_port              = {89},
        .gpio_polarity  = ACTIVE_HIGH,
};

// GPIO_3
static SYNO_GPIO_INFO mute_button_detect = {
        .nr_gpio                = 1,
        .gpio_port              = {3},
        .gpio_polarity  = ACTIVE_LOW,
};


// GPIO_91, GPIO_68
static SYNO_GPIO_INFO redundant_power_detect = {
	.nr_gpio		= 2,
	.gpio_port		= {91, 68},
	.gpio_polarity	= ACTIVE_HIGH,
};

// GPIO_86
static SYNO_GPIO_INFO redundant_power_fan_ctrl = {
        .nr_gpio                = 1,
        .gpio_port              = {86},
        .gpio_polarity  = ACTIVE_HIGH,
};

static
void RS822rppGpioInit(void)
{
	syno_gpio.alarm_led					= &alarm_led;
	syno_gpio.mute_button_detect		= &mute_button_detect;
	syno_gpio.redundant_power_detect	= &redundant_power_detect;
	syno_gpio.redundant_power_fan_ctrl	= &redundant_power_fan_ctrl;
}

static
void RS822rppGpioCleanup(void)
{
	syno_gpio.alarm_led					= NULL;
	syno_gpio.mute_button_detect		= NULL;
	syno_gpio.redundant_power_detect	= NULL;
	syno_gpio.redundant_power_fan_ctrl	= NULL;
}

static
int RS822rppInitModuleType(struct synobios_ops *ops)
{
	module_t type_RS822rpp = MODULE_T_RS822rpp;
	module_t *pType = &type_RS822rpp;

	module_type_set(pType);
	return 0;
}

static
int RS822rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS822rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS822rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS822rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops rs822rpp_ops = {
	.x86_init_module_type = RS822rppInitModuleType,
	.x86_fan_speed_mapping = RS822rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = RS822rppGetBuzzerCleared, 
	.x86_gpio_init = RS822rppGpioInit,
	.x86_gpio_cleanup = RS822rppGpioCleanup,
	.x86_get_power_status = V1000RedundantPowerGetPowerStatus,
};

struct hwmon_sensor_list rs822rpp_sensor_list = {
	.thermal_sensor = &RS822rpp_thermal_sensor,
	.voltage_sensor = &RS822rpp_voltage_sensor,
	.fan_speed_rpm = &RS822rpp_fan_speed_rpm,
	.psu_status = NULL,
	.hdd_backplane = &RS822rpp_hdd_backplane_status,
};


