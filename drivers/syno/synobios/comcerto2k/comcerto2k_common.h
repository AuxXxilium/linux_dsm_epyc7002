#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2012 Synology Inc. All rights reserved. */

#include "synobios.h"
#include "../mapping.h"

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
int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp);
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
int SYNO_COMCERTO2K_GPIO_PIN(int pin, int *pValue, int isWrite);
int SYNO_COMCERTO2K_GPIO_BLINK(int pin, int blink);
int SYNO_CTRL_FAN_STATUS_GET(int index, int *pValue);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength);
extern int SYNO_CTRL_BUZZER_CLEARED_GET(int *pValue);
extern int SynoArmadaSetPhyLed(SYNO_LED);
#ifdef MY_DEF_HERE
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status);
#endif
extern int SYNO_CTRL_EXT_CHIP_HDD_LED_SET(int index, int status);
extern int SYNO_CTRL_FAN_PERSISTER(int index, int status, int isWrite);
extern int SYNO_CTRL_INTERNAL_HDD_LED_SET(int index, int status);
extern int SYNO_SOC_HDD_LED_SET(int index, int status);
extern int axptemp_read_temp(void);
extern void SYNO_GPIO_SET_FALLING_EDGE(int gpio);
extern void SYNO_GPIO_SET_RISING_EDGE(int gpio);
#ifdef MY_DEF_HERE
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
extern int syno_mv_9235_disk_led_get(const unsigned short hostnum);
#endif

extern struct hwmon_sensor_list ds414j_sensor_list;

extern int ds414j_hdd_detect_gpio[4];
extern int ds414j_hdd_enable_gpio[4];

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};