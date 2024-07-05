// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/synobios.h>
#include "geminilake_common.h"

PWM_FAN_SPEED_MAPPING gDS224pSpeedMapping[] = {
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
	.nr_gpio		= 1,
	.gpio_port		= {39},
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
//GPIO_22
static SYNO_GPIO_INFO copy_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {22},
	.gpio_polarity	= ACTIVE_LOW,
};

SYNO_HWMON_SENSOR_TYPE ds224p_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE ds224p_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static
void DS224pGpioInit(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
	syno_gpio.copy_button_detect    = &copy_button_detect;
}

static
void DS224pGpioCleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = NULL;
	syno_gpio.copy_button_detect    = NULL;
}

static
int DS224pInitModuleType(struct synobios_ops *ops)
{
	module_t type_224p = MODULE_T_DS224p;
	module_t *pType = &type_224p;

	module_type_set(pType);
	return 0;
}

static
int DS224pFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS224pSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS224pSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS224pSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

struct model_ops ds224p_ops = {
	.x86_init_module_type = DS224pInitModuleType,
	.x86_fan_speed_mapping = DS224pFanSpeedMapping,
	.x86_set_esata_led_status = NULL, /* no esata led */
	.x86_cpufan_speed_mapping = NULL, /* no cpu fan */
	.x86_get_buzzer_cleared = NULL, /* no mute button */
	.x86_gpio_init = DS224pGpioInit,
	.x86_gpio_cleanup = DS224pGpioCleanup,
};

struct hwmon_sensor_list ds224p_sensor_list = {
       .thermal_sensor = NULL,
       .voltage_sensor = NULL,
       .fan_speed_rpm = NULL,
       .psu_status = NULL,
       .hdd_backplane = &ds224p_hdd_backplane_status,
       .current_sensor = &ds224p_current_status,
};

#if defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO)
static void DS224pLedDisable(void)
{
	/* When WOL is set, CPLD will light on phy led as boot if this pin is high, before BIOS initialization. */
	SYNO_GPIO_WRITE(PHY_LED_CTRL_PIN(), 0);
}
SYNO_SHUTDOWN_HOOK ds224p_shutdown_hook = {
	.shutdown = DS224pLedDisable,
};
#endif /* defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO) */

