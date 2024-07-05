// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "denverton_common.h"

PWM_FAN_SPEED_MAPPING gDVA3219SpeedMapping[] = {
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

SYNO_HWMON_SENSOR_TYPE dva3219_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
};

static
int DVA3219InitModuleType(struct synobios_ops *ops)
{
	module_t type_dva3219 = MODULE_T_DVA3219;
	module_t *pType = &type_dva3219;

	module_type_set(pType);
	return 0;
}

static
int DVA3219FanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDVA3219SpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDVA3219SpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDVA3219SpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
void DVA3219GpioInit(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}
}

static
void DVA3219GpioCleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}
}

struct model_ops dva3219_ops = {
	.x86_init_module_type = DVA3219InitModuleType,
	.x86_fan_speed_mapping = DVA3219FanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
		.x86_gpio_init = DVA3219GpioInit,
	.x86_gpio_cleanup = DVA3219GpioCleanup,
};

struct hwmon_sensor_list dva3219_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &dva3219_hdd_backplane_status,
};