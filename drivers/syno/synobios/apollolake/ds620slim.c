// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "apollolake_common.h"

PWM_FAN_SPEED_MAPPING gDS620slimSpeedMapping[] = {
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

// 16: GPIO_16
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 1,
	.gpio_port		= {16},
	.gpio_polarity	= ACTIVE_HIGH,
};
//  18: GPIO_18
// 179: ISH_GPIO_8
// 176: ISH_GPIO_5
// 175: ISH_GPIO_4
// 177: ISH_GPIO_6
// 171: ISH_GPIO_0
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 6,
	.gpio_port		= {18,179,176,175,177,171},
	.gpio_polarity	= ACTIVE_LOW,
};
//  21: GPIO_21
//  20: GPIO_20
//  19: GPIO_19
//   9: GPIO_9
// 173: ISH_GPIO_2
// 172: ISH_GPIO_1
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 6,
	.gpio_port		= {21, 20, 19, 9, 173, 172},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 12: GPIO_14
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {14},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 15: GPIO_27
static SYNO_GPIO_INFO phy_led_ctrl = {
    .nr_gpio        = 1,
    .gpio_port      = {27},
    .gpio_polarity  = ACTIVE_HIGH,
};
// 174: ISH_GPIO_3
static SYNO_GPIO_INFO copy_button_detect = {
    .nr_gpio        = 1,
    .gpio_port      = {174},
    .gpio_polarity  = ACTIVE_LOW,
};

SYNO_HWMON_SENSOR_TYPE ds620slim_hdd_backplane_status = {
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
void DS620slimGpioInit(void)
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
}

static
void DS620slimGpioCleanup(void)
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
}
	
static
int DS620slimInitModuleType(struct synobios_ops *ops)
{
	module_t type_620slim = MODULE_T_DS620slim;
	module_t *pType = &type_620slim;

	module_type_set(pType);
	return 0;
}

static
int DS620slimFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS620slimSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS620slimSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS620slimSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds620slim_ops = {
	.x86_init_module_type = DS620slimInitModuleType,
	.x86_fan_speed_mapping = DS620slimFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = DS620slimGpioInit,
	.x86_gpio_cleanup = DS620slimGpioCleanup,
};

struct hwmon_sensor_list ds620slim_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &ds620slim_hdd_backplane_status,
};
