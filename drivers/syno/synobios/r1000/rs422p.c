// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "r1000_common.h"

PWM_FAN_SPEED_MAPPING gRS422pSpeedMapping[] = {
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

static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 3,
	.gpio_port		= {68, 69, 40},
	.gpio_polarity	= ACTIVE_HIGH,
};

static SYNO_GPIO_INFO alarm_led = {
	.nr_gpio		= 1,
	.gpio_port		= {89},
	.gpio_polarity	= ACTIVE_HIGH,
};

static SYNO_GPIO_INFO mute_button_detect = {
        .nr_gpio                = 1,
        .gpio_port              = {3},
        .gpio_polarity  = ACTIVE_LOW,
};

static
void RS422pGpioInit(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.alarm_led             = &alarm_led;
	syno_gpio.mute_button_detect    = &mute_button_detect;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

static
void RS422pGpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.alarm_led             = NULL;
	syno_gpio.mute_button_detect    = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

SYNO_HWMON_SENSOR_TYPE rs422p_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE rs422p_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
int RS422pInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs422p = MODULE_T_RS422p;
	module_t *pType = &type_rs422p;

	module_type_set(pType);
	return 0;
}

static
int RS422pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS422pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS422pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS422pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
int RS422pGetBuzzerCleared(unsigned char *buzzer_cleared)
{
	return SYNO_BUZZER_BUTTON_GPIO_GET(buzzer_cleared);
}

struct model_ops rs422p_ops = {
	.x86_init_module_type = RS422pInitModuleType,
	.x86_fan_speed_mapping = RS422pFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = RS422pGetBuzzerCleared,
	.x86_gpio_init = RS422pGpioInit,
	.x86_gpio_cleanup = RS422pGpioCleanup,
};

struct hwmon_sensor_list rs422p_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &rs422p_hdd_backplane_status,
       .current_sensor = &rs422p_current_status,
};
