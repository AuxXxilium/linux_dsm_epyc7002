// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "geminilakevs_common.h"

PWM_FAN_SPEED_MAPPING gVS750hdSpeedMapping[] = {
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

// GPIO_39
// GPIO_40
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 2,
	.gpio_port		= {39, 40},
	.gpio_polarity	= ACTIVE_HIGH,
};
// GPIO_17
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {17},
	.gpio_polarity	= ACTIVE_HIGH,
};

// GPIO_146
static SYNO_GPIO_INFO phy_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {150},
	.gpio_polarity	= ACTIVE_HIGH,
};

static
void VS750hdGpioInit(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
	syno_gpio.copy_button_detect    = NULL;
}

static
void VS750hdGpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

SYNO_HWMON_SENSOR_TYPE vs750hd_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE vs750hd_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
int VS750hdInitModuleType(struct synobios_ops *ops)
{
	module_t type_750hd = MODULE_T_VS750hd;
	module_t *pType = &type_750hd;

	module_type_set(pType);
	return 0;
}

static
int VS750hdFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gVS750hdSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gVS750hdSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gVS750hdSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops vs750hd_ops = {
	.x86_init_module_type = VS750hdInitModuleType,
	.x86_fan_speed_mapping = VS750hdFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = VS750hdGpioInit,
	.x86_gpio_cleanup = VS750hdGpioCleanup,
};

struct hwmon_sensor_list vs750hd_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &vs750hd_hdd_backplane_status,
       .current_sensor = &vs750hd_current_status,
};
