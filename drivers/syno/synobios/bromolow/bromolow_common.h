#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops rs3411rpxs_ops;
extern struct model_ops rs3411xs_ops;
extern struct model_ops rs10613xsp_ops;
extern struct model_ops rc18015xsp_ops;
extern struct model_ops ds3611xs_ops;
extern struct model_ops rs3412rpxs_ops;
extern struct model_ops rs3412xs_ops;
extern struct model_ops ds3612xs_ops;
extern struct model_ops ds3615xs_ops;
extern struct model_ops rs3413xsp_ops;
extern struct model_ops rs3614xs_ops;
extern struct model_ops rs3614rpxs_ops;
extern struct model_ops rs3614xsp_ops;
extern struct model_ops ds2414xs_ops;
extern struct model_ops rs3415xsp_ops;
extern struct model_ops rs18016xsp_ops;
extern struct model_ops rs3617xs_ops;

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
#ifdef MY_DEF_HERE
extern u32 syno_superio_gpio_pin(int pin, int *pValue, int isWrite);
#endif /*MY_DEF_HERE*/
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /*MY_DEF_HERE*/

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern struct hwmon_sensor_list ds3615xs_sensor_list;
extern struct hwmon_sensor_list rc18015xsp_sensor_list;
extern struct hwmon_sensor_list rs18016xsp_sensor_list;
extern struct hwmon_sensor_list rs3614rpxs_sensor_list;
extern struct hwmon_sensor_list rs3614xsp_sensor_list;
extern struct hwmon_sensor_list rs3614xs_sensor_list;
extern struct hwmon_sensor_list rs3617xs_sensor_list;

#define BROMOLOW_POWER1_PIN        2
#define BROMOLOW_POWER2_PIN        3
#define BROMOLOW_BUZZER_OFF_PIN    4
#define BROMOLOW_BUZZER_CTRL_PIN   5
#define IVYBRIDGE_DISK_LED_ACTIVATE_PIN 0
#define DENLOW_DISK_LED_ACTIVATE_PIN 45

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
extern int syno_sata_mv_gpio_read(const unsigned short hostnum);

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
extern int syno_libata_disk_map_table_gen(int *iDiskMapTable);
#endif

#endif
#ifdef MY_DEF_HERE
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
extern int syno_mv_9235_disk_led_get(const unsigned short hostnum);
#endif
#ifdef MY_DEF_HERE
extern void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault);
#endif

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
