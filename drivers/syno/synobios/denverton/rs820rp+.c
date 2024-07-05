// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "denverton_common.h"

extern int DenvertonRedundantPowerGetPowerStatus(POWER_INFO *power_info);
extern int DenvertonGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int SetRPBuzzerClear(unsigned char buzzer_cleared);

PWM_FAN_SPEED_MAPPING gRS820rppSpeedMapping[] = {
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
	.gpio_port              = {86,87,88,0},
	.gpio_polarity  = ACTIVE_LOW,
};

static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio                = 4,
	.gpio_port              = {140,2,17,90},
	.gpio_polarity  = ACTIVE_HIGH,
};

SYNO_HWMON_SENSOR_TYPE rs820rpp_hdd_backplane_status = {
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
int RS820rppInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs820rpp = MODULE_T_RS820rpp;
	module_t *pType = &type_rs820rpp;

	module_type_set(pType);
	return 0;
}

static
int RS820rppFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gRS820rppSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gRS820rppSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gRS820rppSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static
void RS820rppGpioInit(void)
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
}

static
void RS820rppGpioCleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}
}

struct model_ops rs820rpp_ops = {
	.x86_init_module_type = RS820rppInitModuleType,
	.x86_fan_speed_mapping = RS820rppFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = DenvertonGetBuzzerCleared,
	.x86_get_power_status = DenvertonRedundantPowerGetPowerStatus,
	.x86_set_buzzer_clear = NULL,
	.x86_gpio_init = RS820rppGpioInit,
	.x86_gpio_cleanup = RS820rppGpioCleanup,
};

struct hwmon_sensor_list rs820rpp_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &rs820rpp_hdd_backplane_status,
};