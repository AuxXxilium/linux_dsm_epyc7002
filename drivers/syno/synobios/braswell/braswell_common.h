// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#endif
#include "../common/common.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops ds916p_ops;
extern struct model_ops ds716p_ops;
extern struct model_ops ds716pII_ops;
extern struct model_ops ds216p_ops;
extern struct model_ops ds216pII_ops;
extern struct model_ops ds416play_ops;

extern struct hwmon_sensor_list ds916p_sensor_list;
extern struct hwmon_sensor_list ds716p_sensor_list;
extern struct hwmon_sensor_list ds716pII_sensor_list;
extern struct hwmon_sensor_list ds216p_sensor_list;
extern struct hwmon_sensor_list ds216pII_sensor_list;
extern struct hwmon_sensor_list ds416play_sensor_list;

#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

/* Braswell will use LP3943 as disk led controler */
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#endif /* CONFIG_SYNO_LEDS_TRIGGER */

#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif

/*
 * Braswell has 4 GPIO controllers with different number of pins which are loosely arranged.
 * We hard-coded the base offsets of these controllers so the GPIO number can be determined at static time,
 * and the ranges are asigned in the pinctrl driver.
 * The 'Pad Number Mapping' and info about the controller of which a pin is related to can be
 * found in 'Intel Braswell SOC BIOS Writers Guide rev 0.9 (doc no.541233)' table 32-8.
 * Both PRD and the document mentioned above are needed to look up for the pin number.
 * The global gpio number ranges of each controller are listed below -
 * SouthWest 0~55;
 * North 56~114;
 * Ease 115~138;
 * SouthEast 139~193;
 */

/* Fan Sense */
#define BRASWELL_GPIO_FAN1_SEN		91	// GP_CAMERASB02 North-community 35
#define BRASWELL_GPIO_FAN2_SEN		99	// GP_CAMERASB07 North-community 43
/* LED/Button */
#define BRASWELL_GPIO_HDD_LED_CTL	96	// GP_CAMERASB03 North-community 40
#define BRASWELL_GPIO_LAN_LED_CTL	101	// GP_CAMERASB04 North-community 45
#define BRASWELL_GPIO_USB_COPY_BTN	90	// GP_CAMERASB05 North-community 34
#define BRASWELL_GPIO_FRONT_LED_CTL	94	// GP_CAMERASB06 North-community 38
/* Disk Present */
#define BRASWELL_GPIO_HDD1_PRE		56	// GPIO_DFX0 North-community 0
#define BRASWELL_GPIO_HDD2_PRE		59	// GPIO_DFX1 North-community 3
#define BRASWELL_GPIO_HDD3_PRE		63	// GPIO_DFX2 North-community 7
#define BRASWELL_GPIO_HDD4_PRE		57	// GPIO_DFX3 North-community 1
/* Disk Power Enable */
#define BRASWELL_GPIO_HDD1_PWR_EN	61 	// GPIO_DFX4 North-community 5
#define BRASWELL_GPIO_HDD2_PWR_EN	60	// GPIO_DFX5 North-community 4
#define BRASWELL_GPIO_HDD3_PWR_EN	71	// GPIO_DFX6 North-community 15
#define BRASWELL_GPIO_HDD4_PWR_EN	58	// GPIO_DFX7 North-community 2
/* USB Power Enable */
#define BRASWELL_GPIO_USB_FP_PWR_EN	93	// GP_CAMERASB00 North-community 37
#define BRASWELL_GPIO_USB_BP_PWR_EN	98	// GP_CAMERASB01 North-community 42


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
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};
