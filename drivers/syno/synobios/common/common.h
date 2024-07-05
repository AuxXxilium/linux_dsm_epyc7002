/* Copyright (c) 2009-2015 Synology Inc. All rights reserved. */
#ifndef ARM_COMMON_H
#define ARM_COMMON_H

#include "synobios.h"
#include <linux/gpio.h>
#include <linux/syno_gpio.h>
#include "../mapping.h"

#define GPIO_UNDEF              0xFF

unsigned int SynoModelIDGet(SYNO_GPIO *pGpio);
int SYNO_CTRL_FAN_RESISTOR(char speed_value);
int SYNO_CTRL_FAN_STATUS_GET(int index, int *pValue);
int SYNO_CTRL_ALARM_LED_SET(int status);
int SYNO_BUZZER_BUTTON_GPIO_GET(unsigned char *pValue);
int SYNO_CTRL_BUZZER_MUTE_SET(unsigned char pValue);
int SYNO_HDD_LED_SET(int index, int status);
int SYNO_COPY_BUTTON_GPIO_GET(void);
void SYNO_ENABLE_HDD_LED(int blEnable);
void SYNO_ENABLE_PHY_LED(int blEnable);
int SYNO_CTRL_RP_FAN(int status);
char* syno_get_hw_version(void);
void check_gpio_consistency(SYNO_GPIO_INFO* pGpio1, SYNO_GPIO_INFO* pGpio2);
int GetBrand(void);
int SetGpioPin( GPIO_PIN *pPin );
int GetGpioPin( GPIO_PIN *pPin );
int GetFanStatusActiveLow(int fanno, FAN_STATUS *pStatus);
int GetFanStatusActivePulse(int fanno, FAN_STATUS *pStatus);
/* Implemented on Kernel */
extern SYNO_GPIO syno_gpio;
int SYNO_GPIO_READ(int pin);
void SYNO_GPIO_WRITE(int pin, int pValue);
int SYNO_CHECK_HDD_DETECT(int index);
#endif /* ARM_COMMON_H */
