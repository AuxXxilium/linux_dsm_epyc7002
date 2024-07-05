// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "geminilake_common.h"

PWM_FAN_SPEED_MAPPING gDS1520pSpeedMapping[] = {
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

static
void DS1520pGpioInit(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

static
void DS1520pGpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

SYNO_HWMON_SENSOR_TYPE ds1520p_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE ds1520p_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
int DS1520pInitModuleType(struct synobios_ops *ops)
{
	module_t type_1520p = MODULE_T_DS1520p;
	module_t *pType = &type_1520p;

	module_type_set(pType);
	return 0;
}

static
int DS1520pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS1520pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS1520pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS1520pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds1520p_ops = {
	.x86_init_module_type = DS1520pInitModuleType,
	.x86_fan_speed_mapping = DS1520pFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = DS1520pGpioInit,
	.x86_gpio_cleanup = DS1520pGpioCleanup,
};

struct hwmon_sensor_list ds1520p_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &ds1520p_hdd_backplane_status,
       .current_sensor = &ds1520p_current_status,
};
