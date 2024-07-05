#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"

int GetModel(void);
int SYNO_EVANSPORT_GPIO_PIN(int pin, int *pValue, int isWrite);

extern int SetUart(const char* cmd);
extern struct model_ops ds214play_ops;
extern struct model_ops ds114p_ops;
extern struct model_ops ds415play_ops;

#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /*MY_DEF_HERE*/

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
#endif

#define SYNO_GPP_HDD1_LED_ACT			13
#define SYNO_GPP_HDD1_LED_FAULTY		11
#define SYNO_GPP_HDD2_LED_ACT			15
#define SYNO_GPP_HDD2_LED_FAULTY		12
#define SYNO_GPP_LEDS_ACTIVATE			34
#define SYNO_GPP_USB_POWER			    43

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON	   "4"
#define SZ_UART_PWR_LED_OFF        "6"

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
};
