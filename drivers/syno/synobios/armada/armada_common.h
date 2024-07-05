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
 
#define PHY_PORTNUM 0

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

int GetModel(void);
int SetDiskLedStatus(int disknum, SYNO_DISK_LED status);
int SetUsbDiskLedStatus(SYNO_DISK_LED status);
int SetPowerLedStatus(SYNO_LED status);
int GetFanStatusActivePulse(int fanno, FAN_STATUS *pStatus);
int GetFanStatusActiveLow(int fanno, FAN_STATUS *pStatus);
int GetFanNum(int *pFanNum);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int GetFanSpeedBits(int start, int end, MEMORY_BYTE *pMemory);
int GetMemByte( MEMORY_BYTE *pMemory );
int InitModuleType(struct synobios_ops *ops);
int FanStatusMappingRS409r1(FAN_STATUS status, FAN_SPEED speed, char *pSpeed_value);
int SetFanSpeedValue(char speed_value);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int PWMFanSpeedMapping(FAN_SPEED speed);
int SetPhyLed(SYNO_LED ledStatus);
int SetHDDActLed(SYNO_LED ledStatus);
int SYNO_ARMADA_GPIO_PIN(int pin, int *pValue, int isWrite);
int SYNO_ARMADA_GPIO_BLINK(int pin, int blink);
int SYNO_CTRL_FAN_STATUS_GET(int index, int *pValue);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength);
extern int SYNO_CTRL_BUZZER_CLEARED_GET(int *pValue);
extern int SynoArmadaSetPhyLed(SYNO_LED);
#ifdef MY_DEF_HERE
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status);
#endif
extern int SYNO_CTRL_EXT_CHIP_HDD_LED_SET(int index, int status);
extern int SYNO_CTRL_FAN_PERSISTER(int index, int status, int isWrite);
extern int SYNO_SOC_HDD_LED_SET(int index, int status);
extern int set_disk_led_one(SYNO_LED ledStatus);

extern int axptemp_read_temp(void);

/* common method of armada platform by synology in synobios */

/* Referenced BSP methods */
extern int mvBoardPhyAddrGet(unsigned int uiEthPortNum);
extern int mvEthPhyRegRead(unsigned int uiPhyAddr, unsigned int uiRegOffs, unsigned short *pData);
extern int mvEthPhyRegWrite(unsigned int uiPhyAddr, unsigned int uiRegOffs, unsigned short uiData);

/* Referenced synology kernel method */
extern int SYNO_ENABLE_HDD_LED(int enable);
extern int SYNO_ENABLE_PHY_LED(int enable);
int EnablePhyLed(SYNO_LED ledStatus);
int SetHDDActLed(SYNO_LED ledStatus);

/******************************
 * Level button structure and functions
 * link with mv_level_button.c
 *******************************/
typedef enum {
	BTN_PRESSED,
	BTN_RELEASED
} BTN_STAT;

struct level_button {
	char *name;
	int irq;
	int gpio;
	int is_high_pressed;
	int (*press_act)(void*);
	int (*release_act)(void*);
	BTN_STAT stat;
};

int level_button_init(struct level_button *btn);
int level_button_exit(struct level_button *btn);

extern int armada_hdd_enable_gpio[4];

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};