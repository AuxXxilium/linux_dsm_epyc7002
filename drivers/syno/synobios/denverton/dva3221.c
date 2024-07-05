// Copyright (c) 2000-2020 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "denverton_common.h"

PWM_FAN_SPEED_MAPPING gDVA3221SpeedMapping[] = {
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

static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio                = 4,
	.gpio_port              = {89,87,16,21},
	.gpio_polarity  = ACTIVE_LOW,
};

SYNO_HWMON_SENSOR_TYPE dva3221_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
};

static
int DVA3221InitModuleType(struct synobios_ops *ops)
{
	module_t type_dva3221 = MODULE_T_DVA3221;
	module_t *pType = &type_dva3221;

	module_type_set(pType);
	return 0;
}

static
int DVA3221FanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDVA3221SpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDVA3221SpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDVA3221SpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
void DVA3221GpioInit(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}
}

static
void DVA3221GpioCleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}
}

struct model_ops dva3221_ops = {
	.x86_init_module_type = DVA3221InitModuleType,
	.x86_fan_speed_mapping = DVA3221FanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
		.x86_gpio_init = DVA3221GpioInit,
	.x86_gpio_cleanup = DVA3221GpioCleanup,
};

struct hwmon_sensor_list dva3221_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &dva3221_hdd_backplane_status,
};
