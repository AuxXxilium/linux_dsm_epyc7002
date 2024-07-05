// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "v1000_common.h"

#ifdef CONFIG_SYNO_HWMON_PMBUS
extern int V1000RedundantPowerGetPowerStatusByI2C(POWER_INFO *power_info);
#endif /* CONFIG_SYNO_HWMON_PMBUS */

PWM_FAN_SPEED_MAPPING gSC2500SpeedMapping[] = {
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

SYNO_HWMON_SENSOR_TYPE SC2500_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE SC2500_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE SC2500_fan_speed_rpm = {
	.type_name = HWMON_SYS_FAN_RPM_NAME,
	.sensor_num = 3,
	.sensor[0] = {
		.sensor_name = HWMON_SYS_FAN1_RPM,
	},
	.sensor[1] = {
		.sensor_name = HWMON_SYS_FAN2_RPM,
	},
	.sensor[2] = {
		.sensor_name = HWMON_SYS_FAN3_RPM,
	},
};

SYNO_HWMON_SENSOR_TYPE SC2500_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE SC2500_psu_status[] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 4,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN_VOLT,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 4,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN_VOLT,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

int SC2500GetBuzzerCleared(unsigned char *buzzer_cleared)
{
    return SYNO_BUZZER_BUTTON_GPIO_GET(buzzer_cleared);
}

// GPIO_89
static SYNO_GPIO_INFO alarm_led = {
        .nr_gpio                = 1,
        .gpio_port              = {89},
        .gpio_polarity  = ACTIVE_HIGH,
};

// GPIO_69
static SYNO_GPIO_INFO mute_button_detect = {
        .nr_gpio                = 1,
        .gpio_port              = {69},
        .gpio_polarity  = ACTIVE_LOW,
};

static
void SC2500GpioInit(void)
{
	syno_gpio.alarm_led					= &alarm_led;
	syno_gpio.mute_button_detect		= &mute_button_detect;
}

static
void SC2500GpioCleanup(void)
{
	syno_gpio.alarm_led					= NULL;
	syno_gpio.mute_button_detect		= NULL;
}

static
int SC2500InitModuleType(struct synobios_ops *ops)
{
	module_t type_SC2500 = MODULE_T_SC2500;
	module_t *pType = &type_SC2500;

	module_type_set(pType);
	return 0;
}

static
int SC2500FanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gSC2500SpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gSC2500SpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gSC2500SpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops sc2500_ops = {
	.x86_init_module_type = SC2500InitModuleType,
	.x86_fan_speed_mapping = SC2500FanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = SC2500GetBuzzerCleared, 
	.x86_gpio_init = SC2500GpioInit,
	.x86_gpio_cleanup = SC2500GpioCleanup,
#ifdef CONFIG_SYNO_HWMON_PMBUS
	.x86_get_power_status = V1000RedundantPowerGetPowerStatusByI2C,
#endif /* CONFIG_SYNO_HWMON_PMBUS */
};

struct hwmon_sensor_list sc2500_sensor_list = {
	.thermal_sensor = &SC2500_thermal_sensor,
	.voltage_sensor = &SC2500_voltage_sensor,
	.fan_speed_rpm = &SC2500_fan_speed_rpm,
	.psu_status = SC2500_psu_status,
	.hdd_backplane = &SC2500_hdd_backplane_status,
};

