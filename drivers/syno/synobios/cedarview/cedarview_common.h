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
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops ds1512p_ops;
extern struct model_ops ds1513p_ops;
extern struct model_ops ds412p_ops;
extern struct model_ops ds713p_ops;
extern struct model_ops rs812p_ops;
extern struct model_ops rs812rpp_ops;
extern struct model_ops rs814p_ops;
extern struct model_ops rs814rpp_ops;
extern struct model_ops ds1812p_ops;
extern struct model_ops ds1813p_ops;
extern struct model_ops rs2212p_ops;
extern struct model_ops rs2212rpp_ops;
extern struct model_ops ds2413p_ops;
extern struct model_ops rs2414p_ops;
extern struct model_ops rs2414rpp_ops;

extern struct hwmon_sensor_list ds713p_sensor_list;

extern int ds713p_hdd_enable_gpio[2];
extern int ds713p_hdd_detect_gpio[2];

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

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
extern int syno_sata_mv_gpio_read(const unsigned short hostnum);
#endif

#define SYNO_GPP_HDD1_LED_0		16
#define SYNO_GPP_HDD1_LED_1		18
#define SYNO_GPP_HDD2_LED_0		20
#define SYNO_GPP_HDD2_LED_1		32
#define SYNO_GPP_HDD3_LED_0		33
#define SYNO_GPP_HDD3_LED_1		34
#define SYNO_GPP_HDD4_LED_0		49
#define SYNO_GPP_HDD4_LED_1		55
#define SYNO_GPP_RS_BUZZER_OFF	57
#define SYNO_GPP_HDD5_LED_0		133
#define SYNO_GPP_HDD5_LED_1		132
#define SYNO_GPP_HDD7_LED_0		47
#define SYNO_GPP_HDD7_LED_1		45
#define SYNO_GPP_HDD8_LED_0		29
#define SYNO_GPP_HDD8_LED_1		46
#define SYNO_GPP_HDD11_LED_0		47
#define SYNO_GPP_HDD11_LED_1		45
#define SYNO_GPP_HDD12_LED_0		29
#define SYNO_GPP_HDD12_LED_1		46
#define SYNO_GPP_LEDS_ACTIVATE		24

#define CEDARVIEW_GPIO_MAX_PIN  95
#define CEDARVIEW_GPIO_ACTIVE_LOW   1
#define CEDARVIEW_GPIO_ACTIVE_HIGH  0

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