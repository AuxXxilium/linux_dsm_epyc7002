#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#endif

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops ds2415p_ops;
extern struct model_ops ds415p_ops;
extern struct model_ops ds1815p_ops;
extern struct model_ops ds1515p_ops;
extern struct model_ops rs815p_ops;
extern struct model_ops rs815rpp_ops;
extern struct model_ops rs2416p_ops;
extern struct model_ops rs2416rpp_ops;
extern struct model_ops ds1616p_ops;
extern struct model_ops ds1817p_ops;
extern struct model_ops ds1517p_ops;
extern struct model_ops rs818p_ops;
extern struct model_ops rs818rpp_ops;
extern struct model_ops rs1219p_ops;

extern struct hwmon_sensor_list ds415p_sensor_list;

extern int ds415p_hdd_enable_gpio[4];
extern int ds415p_hdd_detect_gpio[4];

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /*MY_DEF_HERE*/

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
#endif

#ifdef MY_DEF_HERE
extern int syno_sil3132_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif

#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
extern int syno_ahci_disk_led_enable_by_port(const unsigned short diskPort, const int iValue);
#endif
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */

#ifdef CONFIG_SYNO_LEDS_TRIGGER
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
extern int syno_libata_disk_map_table_gen(int *iDiskMapTable);
#endif

#ifdef MY_DEF_HERE
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
#endif

#define AVOTON_POWER1_PIN        44
#define AVOTON_POWER2_PIN        51
#define AVOTON_BUZZER_OFF_PIN    58

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

#define AVOTON_DISK_LED_ACTIVATE_PIN 49

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