#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/leds.h>
#endif
#include <linux/synolib.h>
#include "../i2c/i2c-linux.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops rsd18016xsp_ops;
extern struct model_ops rs3618xs_ops;
extern struct model_ops rs3617rpxs_ops;
extern struct model_ops rs3617xsp_ops;
extern struct model_ops rs4017xsp_ops;
extern struct model_ops ds3617xs_ops;
extern struct model_ops rs18017xsp_ops;
extern struct model_ops fs2017_ops;
extern struct model_ops ds3017xs_ops;
extern struct model_ops fs3400_ops;
extern struct model_ops ds3617xsII_ops;
extern struct model_ops ds3622xsp_ops;

extern int SMBusSwitchRegWrite(int bus_no, u16 addr, u8 val);
void RS3618xsSMBusSwitchInit(void);

#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */

#ifdef CONFIG_SYNO_ATMEGA1608_PROBE_FIXED_BUS
extern int giSynoAtmegaNum;
extern long gSynoAtmegaAddr[SYNO_ATMEGA_NUM_MAX];
#endif /* CONFIG_SYNO_ATMEGA1608_PROBE_FIXED_BUS */

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/

#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_pmbus_status(SYNO_HWMON_SENSOR_TYPE* , int);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern struct hwmon_sensor_list rs4017xsp_sensor_list;
extern struct hwmon_sensor_list rs3618xs_sensor_list;
extern struct hwmon_sensor_list rs3617rpxs_sensor_list;
extern struct hwmon_sensor_list rs3617xsp_sensor_list;
extern struct hwmon_sensor_list ds3617xs_sensor_list;
extern struct hwmon_sensor_list rs18017xsp_sensor_list;
extern struct hwmon_sensor_list fs2017_sensor_list;
extern struct hwmon_sensor_list fs3400_sensor_list;
extern struct hwmon_sensor_list ds3617xsII_sensor_list;
extern struct hwmon_sensor_list ds3622xsp_sensor_list;

extern SYNO_HWMON_SENSOR_TYPE RS3618xs_hdd_backplane_status;

#ifdef CONFIG_SYNO_LEDS_TRIGGER
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#endif /* CONFIG_SYNO_LEDS_TRIGGER */

#define BROADWELL_POWER1_PIN        50
#define BROADWELL_POWER2_PIN        54
#define BROADWELL_BUZZER_OFF_PIN    2
#define BROADWELL_BUZZER_CTRL_PIN   3
#define BROADWELL_ALARM_LED_PIN     28
#define BROADWELL_DISK_LED_ACTIVATE_PIN 45

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"

#define PSU_DELTA_AC139_I2C_REG 		0x79
#define PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT 	0x0800

#define I2C_BUS_NO             0

#if defined(CONFIG_SYNO_FIXED_DISK_NAME_MV14XX) && defined(CONFIG_SYNO_SATA_REMAP)
extern int (*syno_disk_map_table_gen_mv14xx)(int *iDiskMapTable, int iPortMax);
#endif /* CONFIG_SYNO_FIXED_DISK_NAME_MV14XX && CONFIG_SYNO_SATA_REMAP */
#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
extern int (*funcSYNOCtrlDiskLedBy1475)(unsigned short, unsigned short);
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int 	(*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};

