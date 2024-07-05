// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "geminilake_common.h"

PWM_FAN_SPEED_MAPPING gDVA1622SpeedMapping[] = {
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

// GPIO_40
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 1,
	.gpio_port		= {40},
	.gpio_polarity	= ACTIVE_HIGH,
};

// GPIO_17
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {17},
	.gpio_polarity	= ACTIVE_HIGH,
};

static
void DVA1622GpioInit(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

static
void DVA1622GpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

SYNO_HWMON_SENSOR_TYPE dva1622_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE dva1622_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
int DVA1622InitModuleType(struct synobios_ops *ops)
{
	module_t type_dva1622 = MODULE_T_DVA1622;
	module_t *pType = &type_dva1622;

	module_type_set(pType);
	return 0;
}

static
int DVA1622FanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDVA1622SpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDVA1622SpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDVA1622SpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops dva1622_ops = {
	.x86_init_module_type = DVA1622InitModuleType,
	.x86_fan_speed_mapping = DVA1622FanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = DVA1622GpioInit,
	.x86_gpio_cleanup = DVA1622GpioCleanup,
};

struct hwmon_sensor_list dva1622_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &dva1622_hdd_backplane_status,
       .current_sensor = &dva1622_current_status,
};
