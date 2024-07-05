#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include "../led/led_trigger.h"
#endif
#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#include "../common/common.h"
#include "../switchtec/switchtec.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops fs6400_ops;
extern struct model_ops fs6500_ops;
extern struct model_ops sa6500_ops;
extern struct model_ops hd6500_ops;
extern struct model_ops fs6600n_ops;

#ifdef CONFIG_SYNO_ADT7490_FEATURES
extern int syno_get_adt_peci(struct _SynoCpuTemp *);
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/
extern void SA6500SMBusSwitchInit(void);
extern int SetDiskLedStatus(DISKLEDSTATUS*);
extern int SA6500GetHDDBackPlaneStatus(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane);

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_pmbus_status(SYNO_HWMON_SENSOR_TYPE* , int);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern struct hwmon_sensor_list fs6400_sensor_list;
extern struct hwmon_sensor_list fs6500_sensor_list;
extern struct hwmon_sensor_list sa6500_sensor_list;
extern struct hwmon_sensor_list hd6500_sensor_list;
extern struct hwmon_sensor_list fs6600n_sensor_list;

#define PURLEY_BUZZER_CTRL_PIN       113		//GPP_D17 (BUZZER_MUTE_BOT_PCH_GPO)
#define PURLEY_BUZZER_OFF_BUTTON_PIN 114		//GPP_D18 (BUZZER_MUTE signal  from PSU to PCH)
#define PURLEY_ALARM_LED_PIN         223		//GPP_H8  (SYNO_ALERT_LED_CTRL_N)
#define PURLEY_CPLD_LED_CTRL_N       216        //GPP_H1 (No.216) for SystemDisk HDD LED mask & Pront Panel Status LED mask

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

#define PSU_DELTA_AC139_I2C_REG                 0x79
#define PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT     0x0800

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int (*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
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
