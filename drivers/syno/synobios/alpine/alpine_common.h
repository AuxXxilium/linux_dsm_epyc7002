/* Copyright (c) 2000-2012 Synology Inc. All rights reserved. */

#include "synobios.h"
#include "../mapping.h"
#include "../common/common.h"

#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

int GetModel(void);
int SetDiskLedStatus(int disknum, SYNO_DISK_LED status);
int SetPowerLedStatus(SYNO_LED status);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int PWMFanSpeedMapping(FAN_SPEED speed);
int InitModuleType(struct synobios_ops *ops);
int SetHDDActLed(SYNO_LED ledStatus);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength);
extern int syno_alpine_get_cpu_temperature(int *temperature);
void syno_gpio_init(void);
void syno_gpio_cleanup(void);

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};
