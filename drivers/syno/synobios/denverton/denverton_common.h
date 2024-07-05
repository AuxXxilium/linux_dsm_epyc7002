#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#include "../led/led_9235.h"
#include "../led/led_trigger.h"
#else
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#include <linux/slab.h>
#endif
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#include "../common/common.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops rs2418p_ops;
extern struct model_ops rs2418rpp_ops;
extern struct model_ops ds1618p_ops;
extern struct model_ops rs2818rpp_ops;
extern struct model_ops ds2419p_ops;
extern struct model_ops ds1819p_ops;
extern struct model_ops dva3219_ops;
extern struct model_ops rs820p_ops;
extern struct model_ops rs820rpp_ops;
extern struct model_ops rs1220p_ops;
extern struct model_ops rs1220rpp_ops;
extern struct model_ops dva3221_ops;
extern struct model_ops ds2419pII_ops;

extern struct hwmon_sensor_list rs2418p_sensor_list;
extern struct hwmon_sensor_list rs2418rpp_sensor_list;
extern struct hwmon_sensor_list ds1618p_sensor_list;
extern struct hwmon_sensor_list rs2818rpp_sensor_list;
extern struct hwmon_sensor_list ds2419p_sensor_list;
extern struct hwmon_sensor_list ds1819p_sensor_list;
extern struct hwmon_sensor_list dva3219_sensor_list;
extern struct hwmon_sensor_list rs820p_sensor_list;
extern struct hwmon_sensor_list rs820rpp_sensor_list;
extern struct hwmon_sensor_list dva3221_sensor_list;
extern struct hwmon_sensor_list ds2419pII_sensor_list;

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /*MY_DEF_HERE*/

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
extern int syno_libata_disk_map_table_gen(int *iDiskMapTable);
#endif

#if defined(MY_DEF_HERE) && !defined(CONFIG_SYNO_PORT_MAPPING_V2)
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
#endif /* MY_DEF_HERE && !CONFIG_SYNO_PORT_MAPPING_V2 */

#if defined(CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY) && !defined(CONFIG_SYNO_PORT_MAPPING_V2)
#ifdef CONFIG_SYNO_SATA_REMAP
extern int syno_ahci_disk_led_enable_by_port(const unsigned short diskPort, const int iValue);
#endif
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY && !CONFIG_SYNO_PORT_MAPPING_V2 */

#if defined(CONFIG_SYNO_LEDS_TRIGGER) && !defined(CONFIG_SYNO_PORT_MAPPING_V2)
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#endif /* CONFIG_SYNO_LEDS_TRIGGER && !CONFIG_SYNO_PORT_MAPPING_V2 */

/* Fan Sense */
#define DENVERTON_GPIO_FAN1_SEN		14	// GPIO_28 (NCSI_RXD0)
#define DENVERTON_GPIO_FAN2_SEN		15	// GPIO_29 (NCSI_CLK_IN)
#define DENVERTON_GPIO_FAN3_SEN		16	// GPIO_30 (NCSI_RXD1)
#define DENVERTON_GPIO_FAN4_SEN		17	// GPIO_31 (NCSI_CRS_DV)

#define DENVERTON_BUZZER_CTRL_PIN	88	// GPIO_6 (GPIO_6)
#define DENVERTON_BUZZER_OFF_BUTTON_PIN	89	// GPIO_7 (GPIO_7)
#define DENVERTON_DISK_LED_ACTIVATE_PIN 98	// GPIO_8 (GPIO_8)
#define DENVERTON_ALERT_PIN		22	// GPIO_36 (NCSI_ARB_OUT)

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
	int	(*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
	void    (*x86_gpio_init)(void);
	void    (*x86_gpio_cleanup)(void);
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};

