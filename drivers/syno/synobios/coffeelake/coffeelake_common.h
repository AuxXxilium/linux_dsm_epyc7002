#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#include "../common/common.h"

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern struct model_ops ds3619xs_ops;

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
extern int syno_libata_disk_map_table_gen(int *iDiskMapTable);
#endif
#ifdef MY_DEF_HERE
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
extern int syno_mv_9235_disk_led_get(const unsigned short hostnum);
#endif
#ifdef MY_DEF_HERE
extern void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault);
#endif

/* USB Power */
#define COFFEELAKE_GPIO_USB3A_PWR_EN1		65	// GPP_C_14 (UART1_RTSB_ISH_UART1_RTSB)
#define COFFEELAKE_GPIO_USB3A_PWR_EN2		66	// GPP_C_15 (UART1_CTSB_ISH_UART1_CTSB)
#define COFFEELAKE_GPIO_USB3C_PWR_EN1		67	// GPP_C_16 (I2C0_SDA)
/* LED control */
#define COFFEELAKE_LAN_LED_ACTIVATE_PIN		198	// GPP_H_19 (ISH_I2C0_SDA)
#define COFFEELAKE_DISK_LED_ACTIVATE_PIN	199	// GPP_H_20 (ISH_I2C0_SCL)
#define COFFEELAKE_ALL_LED_ACTIVATE_PIN		200	// GPP_H_21 (ISH_I2C1_SDA)
#define COFFEELAKE_ALARM_LED_PIN		201	// GPP_H_22 (ISH_I2C1_SCL)

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int (*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
};
