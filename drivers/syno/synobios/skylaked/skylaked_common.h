// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#include "../led/led_1475.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops rs4021xsp_ops;

#ifdef CONFIG_SYNO_ADT7490_FEATURES
extern int syno_get_adt_peci(struct _SynoCpuTemp *);
#endif /* CONFIG_SYNO_ADT7490_FEATURES */

#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

#define SKYLAKED_BUZZER_OFF_BUTTON_PIN 114
#define SKYLAKED_BUZZER_CTRL_PIN   113
#define SKYLAKED_ALARM_LED_PIN     223

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"

#define PSU_DELTA_AC139_I2C_REG 		0x79
#define PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT 	0x0800

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int (*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
};
