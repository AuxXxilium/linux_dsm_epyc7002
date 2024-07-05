#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2020 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#endif

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops sa3200d_ops;
extern struct model_ops sa3400d_ops;

extern struct hwmon_sensor_list sa3400d_sensor_list;

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_pmbus_status(SYNO_HWMON_SENSOR_TYPE* , int);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
extern int syno_ttys_write(const int index, const char* szBuf);
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/

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

#define BROADWELLNTBAP_POWER1_PIN        50
#define BROADWELLNTBAP_POWER2_PIN        54
#define BROADWELLNTBAP_BUZZER_OFF_PIN    2
#define BROADWELLNTBAP_BUZZER_CTRL_PIN   3
#define BROADWELLNTBAP_ALARM_LED_PIN     28
#define BROADWELLNTBAP_DISK_LED_ACTIVATE_PIN 45
#define BROADWELLNTBAP_REMOTE_LED_GPIO_PIN 32

#define PSU_I2C_BUS             0
#define PSU_TOP_I2C_ADDR        0x64
#define PSU_DELTA_800AB_I2C_REG 0x20

#define DISK_LED_CTRL_ON 	0
#define DISK_LED_CTRL_OFF 	1

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int (*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};
