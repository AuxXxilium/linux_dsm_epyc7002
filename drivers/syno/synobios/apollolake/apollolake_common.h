// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#include "../led/led_trigger.h"
#else
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#endif
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#include "../common/common.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops ds918p_ops;
extern struct model_ops ds218p_ops;
extern struct model_ops ds718p_ops;
extern struct model_ops ds418play_ops;
extern struct model_ops ds419p_ops;
extern struct model_ops ds1019p_ops;
extern struct model_ops ds719p_ops;
extern struct model_ops ds620slim_ops;
extern struct model_ops rs419p_ops;

extern struct hwmon_sensor_list ds918p_sensor_list;
extern struct hwmon_sensor_list ds218p_sensor_list;
extern struct hwmon_sensor_list ds718p_sensor_list;
extern struct hwmon_sensor_list ds418play_sensor_list;
extern struct hwmon_sensor_list ds419p_sensor_list;
extern struct hwmon_sensor_list ds1019p_sensor_list;
extern struct hwmon_sensor_list ds719p_sensor_list;
extern struct hwmon_sensor_list ds620slim_sensor_list;
extern struct hwmon_sensor_list rs419p_sensor_list;
extern struct hwmon_sensor_list ds220p_sensor_list;

#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

/* Apollolake will use LP3943 as disk led controler */
#if defined(CONFIG_SYNO_LEDS_TRIGGER) && !defined(CONFIG_SYNO_PORT_MAPPING_V2)
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#endif /* CONFIG_SYNO_LEDS_TRIGGER && !CONFIG_SYNO_PORT_MAPPING_V2 */

#if defined(CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY) && !defined(CONFIG_SYNO_PORT_MAPPING_V2)
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY && ! CONFIG_SYNO_PORT_MAPPING_V2 */

/*
 * Apollo Lake has 4 GPIO controllers with different number of pins which are loosely arranged.
 * We hard-coded the base offsets of these controllers so the GPIO number can be determined at static time,
 * and the ranges are asigned in the pinctrl driver.
 * look up for the pin number in linux-4.4.x/drivers/pinctrl/intel/pinctrl-broxton.c
 * The global gpio number ranges of each controller are listed below -
 * North 0~77;
 * NorthWest 78~154;
 * West 155~201;
 * SouthWest 202~244;
 */

/* Fan Sense */
#define APOLLOLAKE_GPIO_FAN1_SEN	17	// GPIO_17
#define APOLLOLAKE_GPIO_FAN2_SEN	16	// GPIO_16
/* LED/Button */
#define APOLLOLAKE_GPIO_HDD_LED_CTL	14	// GPIO_14
#define APOLLOLAKE_GPIO_LAN_LED_CTL	15	// GPIO_15
#define APOLLOLAKE_GPIO_USB_COPY_BTN	174	// ISH_GPIO_3
#define APOLLOLAKE_GPIO_FRONT_LED_CTL	12	// GPIO_12
/* Disk Present */
#define APOLLOLAKE_GPIO_HDD1_PRE	18	// GPIO_18
#define APOLLOLAKE_GPIO_HDD2_PRE	179	// ISH_GPIO_8
#define APOLLOLAKE_GPIO_HDD3_PRE	176	// ISH_GPIO_5
#define APOLLOLAKE_GPIO_HDD4_PRE	175	// ISH_GPIO_4
/* Disk Power Enable */
#define APOLLOLAKE_GPIO_HDD1_PWR_EN	21 	// GPIO_21
#define APOLLOLAKE_GPIO_HDD2_PWR_EN	20	// GPIO_20
#define APOLLOLAKE_GPIO_HDD3_PWR_EN	19	// GPIO_19
#define APOLLOLAKE_GPIO_HDD4_PWR_EN	9	// GPIO_9
/* USB Power Enable */
#define APOLLOLAKE_GPIO_USB_0_PWR_EN	11	// GPIO_11
#define APOLLOLAKE_GPIO_USB_1_PWR_EN	10	// GPIO_10
#define APOLLOLAKE_GPIO_USB_FP_PWR_EN	13	// GPIO_13


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
