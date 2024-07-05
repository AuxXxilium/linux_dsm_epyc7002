#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2012 Synology Inc. All rights reserved. */

#include "synobios.h"
#include "../mapping.h"
#include <linux/leds.h>
#include <linux/slab.h>
#include "../common/common.h"

#define LED_NORMAL 0
#define LED_FAULTY 1
#define ENABLE     1
#define DISABLE      0

#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON	   "4"
#define SZ_UART_PWR_LED_OFF        "6"

int GetModel(void);
int SetDiskLedStatus(int disknum, SYNO_DISK_LED status);
int SetUsbDiskLedStatus(SYNO_DISK_LED status);
int SetPowerLedStatus(SYNO_LED status);
int GetFanStatus(int fanno, FAN_STATUS *pStatus);
int GetFanNum(int *pFanNum);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int SetAlarmLed(unsigned char type);
int GetBackPlaneStatus(BACKPLANE_STATUS *pStatus);
int GetFanSpeedBits(int start, int end, MEMORY_BYTE *pMemory);
int GetMemByte( MEMORY_BYTE *pMemory );
int InitModuleType(struct synobios_ops *ops);
int FanStatusMappingRS409r1(FAN_STATUS status, FAN_SPEED speed, char *pSpeed_value);
int SetFanSpeedValue(char speed_value);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int PWMFanSpeedMapping(FAN_SPEED speed);
int SetPhyLed(SYNO_LED ledStatus);
int SetHDDActLed(SYNO_LED ledStatus);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength);
extern int SynoArmadaSetPhyLed(SYNO_LED);
#ifdef MY_DEF_HERE
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status);
#endif
extern int axptemp_read_temp(void);
extern void SYNO_GPIO_SET_FALLING_EDGE(int gpio);
extern void SYNO_GPIO_SET_RISING_EDGE(int gpio);
#ifdef MY_DEF_HERE
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
extern int syno_mv_9235_disk_led_get(const unsigned short hostnum);
#endif
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
int SetDiskLedStatusByI2CLedDimmer(int iDiskNum, SYNO_DISK_LED iStatus);
int SYNOSetDiskLedStatusByI2CLedDimmer(int iHostNum, SYNO_DISK_LED iStatus);
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum);

extern struct hwmon_sensor_list ds216play_sensor_list;

extern int ds216play_hdd_enable_gpio[2];

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};