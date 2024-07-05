// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include "../led/led_trigger.h"
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#include "../common/common.h"
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern char gszSynoHWVersion[];
extern struct model_ops fs6400n_ops;
extern struct model_ops fs6410_ops;
extern struct model_ops sa6400_ops;
extern struct model_ops sa6200_ops;
extern struct model_ops sc6200_ops;

#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern struct hwmon_sensor_list fs6400n_sensor_list;
extern struct hwmon_sensor_list fs6410_sensor_list;
extern struct hwmon_sensor_list sa6400_sensor_list;
extern struct hwmon_sensor_list sa6200_sensor_list;
extern struct hwmon_sensor_list sc6200_sensor_list;

#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#if defined(CONFIG_SYNO_K10TEMP) || defined(CONFIG_SYNO_HWMON_AMD_K10TEMP)
extern int syno_k10cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_K10TEMP || CONFIG_SYNO_HWMON_AMD_K10TEMP */

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int	(*x86_get_fan_status)(int fanno, FAN_STATUS *pStatus);
	void	(*x86_gpio_init)(void);
	void	(*x86_gpio_cleanup)(void);
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};

