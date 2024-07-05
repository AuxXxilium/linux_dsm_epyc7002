// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#include "syno_ttyS.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include "../led/led_trigger.h"
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#include "../common/common.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern char gszSynoHWVersion[];
extern struct model_ops ds723p_ops;
extern struct model_ops ds923p_ops;
extern struct model_ops rs422p_ops;
extern struct model_ops ds1522p_ops;

extern struct hwmon_sensor_list ds723p_sensor_list;
extern struct hwmon_sensor_list ds923p_sensor_list;
extern struct hwmon_sensor_list rs422p_sensor_list;
extern struct hwmon_sensor_list ds1522p_sensor_list;

#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#ifdef CONFIG_SYNO_K10TEMP
extern int syno_k10cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_K10TEMP */

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
	SYNO_HWMON_SENSOR_TYPE *current_sensor;
};
