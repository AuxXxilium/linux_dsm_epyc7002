/* Copyright (c) 2000-2015 Synology Inc. All rights reserved. */

#include <linux/synobios.h>
#include <linux/netdevice.h>
#include "../mapping.h"
#include "../common/common.h"

#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

int GetModel(void);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int InitModuleType(struct synobios_ops *ops);
int SetFanSpeedValue(char speed_value);
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed);
int SetHDDActLed(SYNO_LED ledStatus);
int SetPowerLedStatus(SYNO_LED status);
int SYNO_GPIO_READ(int pin);
void SYNO_GPIO_WRITE(int pin, int pValue);

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength);
// for matching userspace usage, return 0 if button is pressed, else = 1
int GetFanStatusMircopWithGPIOCommon(int fanno, FAN_STATUS *pStatus);
int GetFanStatusActiveLow(int fanno, FAN_STATUS *pStatus);
void VdimmPwrCtrl(SYNO_LED ledStatus);
void syno_gpio_init(void);
void syno_gpio_cleanup(void);
int PWMFanSpeedMapping(FAN_SPEED speed);
int SetPhyLed(SYNO_LED ledStatus);
#if defined(CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY) || defined(CONFIG_SYNO_AHCI_GPIO_SOFTWARE_PRESENT_BLINK)
extern int syno_ahci_disk_led_enable_by_port(const unsigned short diskPort, const int iValue);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY || CONFIG_SYNO_AHCI_GPIO_SOFTWARE_PRESENT_BLINK */

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
	SYNO_HWMON_SENSOR_TYPE *current_sensor;
};
