// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "apollolake_common.h"

PWM_FAN_SPEED_MAPPING gRS419pSpeedMapping[] = {
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

// 17: GPIO_17
// 16: GPIO_16
// 26: GPIO_26
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 3,
	.gpio_port		= {17, 16, 26},
	.gpio_polarity	= ACTIVE_HIGH,
};
//  18: GPIO_18
// 179: ISH_GPIO_8
// 176: ISH_GPIO_5
// 175: ISH_GPIO_4
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 4,
	.gpio_port		= {18,179,176,175},
	.gpio_polarity	= ACTIVE_LOW,
};
//  21: GPIO_21
//  20: GPIO_20
//  19: GPIO_19
//   9: GPIO_9
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {21, 20, 19, 9},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 12: GPIO_14
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {14},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 15: GPIO_15
static SYNO_GPIO_INFO phy_led_ctrl = {
    .nr_gpio        = 1,
    .gpio_port      = {15},
    .gpio_polarity  = ACTIVE_HIGH,
};
// 174: ISH_GPIO_3
static SYNO_GPIO_INFO copy_button_detect = {
    .nr_gpio        = 1,
    .gpio_port      = {174},
    .gpio_polarity  = ACTIVE_LOW,
};
// 178: ISH_GPIO_7
static SYNO_GPIO_INFO mute_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {178},
	.gpio_polarity	= ACTIVE_LOW,
};

SYNO_HWMON_SENSOR_TYPE rs419p_hdd_backplane_status = {
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
void RS419pGpioInit(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}

	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
	syno_gpio.copy_button_detect    = &copy_button_detect;
	syno_gpio.mute_button_detect    = &mute_button_detect;
}

static
void RS419pGpioCleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
	syno_gpio.mute_button_detect    = NULL;
}

static
int RS419pInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs419p = MODULE_T_RS419p;
	module_t *pType = &type_rs419p;

	module_type_set(pType);
	return 0;
}

static
int RS419pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS419pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS419pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS419pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

int getBuzzerButtonStatus(unsigned char *pValue)
{
    return SYNO_BUZZER_BUTTON_GPIO_GET(pValue);
}

struct model_ops rs419p_ops = {
	.x86_init_module_type = RS419pInitModuleType,
	.x86_fan_speed_mapping = RS419pFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = getBuzzerButtonStatus,
	.x86_gpio_init = RS419pGpioInit,
	.x86_gpio_cleanup = RS419pGpioCleanup,
};

struct hwmon_sensor_list rs419p_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &rs419p_hdd_backplane_status,
};
