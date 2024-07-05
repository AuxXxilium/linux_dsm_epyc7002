#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/synolib.h>
#include "../rtc/rtc.h"

#include "geminilake_common.h"
#include "syno_ttyS.h"

#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* CONFIG_SYNO_SATA_DISK_LED_CONTROL */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int giAlertGPIOPin = -1;

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
extern int (*syno_get_current)(unsigned char, struct tty_struct *);
extern int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty);
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
extern int synobios_lock_ttyS_current(char *szCommand, char *szBuf);

static
int SetFanStatusMircopWithGPIO(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;
	int iFanDuty = -1;
	char szUartCmd[5] = {0};

	if( status == FAN_STATUS_STOP ) {
		speed = FAN_SPEED_STOP;
	}

	if( FAN_SPEED_PWM_FORMAT_SHIFT <= (int)speed ) {
		/* PWM format is only for bandon and QC test */
		iFanDuty = FAN_SPEED_SHIFT_DUTY_GET((int)speed);
		/* set fan speed hz */
		if(0 < FAN_SPEED_SHIFT_HZ_GET((int)speed)) {
			snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_FREQUENCY, FAN_SPEED_SHIFT_HZ_GET((int)speed));
			if( 0 > SetUart(szUartCmd) ) {
				goto END;
			}
		}
	} else {
		if( 0 > (iFanDuty = model_ops->x86_fan_speed_mapping(speed)) ) {
			printk("No matched fan speed!\n");
			goto END;
		}
	}

	/* set fan speed duty cycle  */
	snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_DUTY_CYCLE, iFanDuty);
	if( 0 > SetUart(szUartCmd) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

int GetModel(void)
{
	int model = MODEL_INVALID;

	if ( !strncmp(gszSynoHWVersion, HW_DS420p, strlen(HW_DS420p) ) ) {
		model = MODEL_DS420p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS720p, strlen(HW_DS720p) ) ) {
		model = MODEL_DS720p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1520p, strlen(HW_DS1520p) ) ) {
		model = MODEL_DS1520p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS220p, strlen(HW_DS220p) ) ) {
		model = MODEL_DS220p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS920p, strlen(HW_DS920p) ) ) {
		model = MODEL_DS920p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DVA1622, strlen(HW_DVA1622) ) ) {
		model = MODEL_DVA1622;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS423p, strlen(HW_DS423p) ) ) {
		model = MODEL_DS423p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS224p, strlen(HW_DS224p) ) ) {
		model = MODEL_DS224p;
	}

	return model;
}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_DS420p:
		case MODEL_DS920p:
		case MODEL_DS423p:
			iInternalDiskNum = 4;
			break;
		case MODEL_DS720p:
		case MODEL_DS220p:
		case MODEL_DVA1622:
		case MODEL_DS224p:
			iInternalDiskNum = 2;
			break;
		case MODEL_DS1520p:
			iInternalDiskNum = 5;
			break;
		default:
			printk(KERN_INFO "synobios cannot find internal disk number.\n");
			break;
	}
	return iInternalDiskNum;
}

static
int InitModuleType(struct synobios_ops *ops)
{
	int iRet = -1;

	if (model_ops && model_ops->x86_init_module_type) {
		iRet = model_ops->x86_init_module_type(ops);
	}

	return iRet;
}

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx = 0;

	if (NULL == pCpuTemp) {
		goto END;
	}
#ifdef CONFIG_SYNO_X86_CORETEMP
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

END:
	return iRet;
}

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	/* Front LAN LEDs and RJ45 Phy LEDs are contoled by GPIO pin APOLLOLAKE_GPIO_LAN_LED_CTL */
	switch(ledStatus){
		case SYNO_LED_ON:
			SYNO_ENABLE_PHY_LED(1);
			break;
		case SYNO_LED_OFF:
			SYNO_ENABLE_PHY_LED(0);
			break;
		default:
			goto ERR;
	}
	iError = 0;
ERR:
	return iError;
}

static
int SetAlarmLed(unsigned char type)
{
	const char* cmd = NULL;

	if (type) {
		cmd = SZ_UART_ALARM_LED_BLINKING;
	}else{
		cmd = SZ_UART_ALARM_LED_OFF;
	}

	return SetUart(cmd);
}

static
int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	int ret = 0;

	if (model_ops && model_ops->x86_get_buzzer_cleared) {
		ret = model_ops->x86_get_buzzer_cleared(buzzer_cleared);
	}

	return ret;
}

static int GetPowerStatus(POWER_INFO *power_info)
{
	int ret = 0;

	if (model_ops && model_ops->x86_get_power_status) {
		ret = model_ops->x86_get_power_status(power_info);
	}else{
		ret = -1;
	}

	return ret;
}

static int SetHddActLed(SYNO_LED ledStatus)
{
if (SYNO_LED_OFF == ledStatus) {
		SYNO_ENABLE_HDD_LED(0);
	} else {
		SYNO_ENABLE_HDD_LED(1);
	}

	return 0;
}

static int SetPowerLedStatus(SYNO_LED ledStatus)
{
	char szCommand[5] = {0};
	int iError = -1;

        switch(ledStatus){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > SetUart(szCommand)) {
		goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	unsigned int freq = cpufreq_quick_get(0);

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

int getCopyButtonStatus(void)
{
    // for matching userspace usage, button pressed = 0, else = 1
    return SYNO_COPY_BUTTON_GPIO_GET();
}

/*
 * For GPIO alert Led, there is no blinking
 */
static
int SetAlarmLedByGPIO(unsigned char type)
{
	GPIO_PIN pin;
	int iRet = -1;

	if (0 > giAlertGPIOPin) {
	    goto END;
	}

	if (type) {
		pin.value = 1;
	}else{
		pin.value = 0;
	}
	pin.pin = giAlertGPIOPin;
	SetGpioPin(&pin);

	iRet = 0;

END:
	return iRet;
}

static
int SYNOIOGetCurrentStatusByMicroP(unsigned long* pulSysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szTTYCur[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulTTYCur = 0;

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szTTYCur, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szTTYCur, 10, &ulTTYCur);
	if (0 > iRet) {
		goto End;
	}
	// microP return value * 16.13 = System current (mA) (reference : uP #130)
	*pulSysCurrent = (ulTTYCur * 16) + (ulTTYCur * 13 / 100);

	iRet = 0;
End:
	return iRet;
}
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	int iInternalDiskNum = -1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iInternalDiskNum = GetMaxInternalDiskNum();

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_DETECT(index)) {
			hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY(index)) & 0x01) << (index-1);
		}
	}

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_ENABLE(index)) {
			hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index-1);
		}
	}

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%ld", hdd_enable);

	iRet = 0;

End:
	return iRet;
}

static
int HWMONGetCurrentStatusByMicroP(struct _SYNO_HWMON_SENSOR_TYPE *SysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szCurrent[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulCurrent = 0;

	if (NULL == SysCurrent || NULL == hwmon_sensor_list) {
		printk("SysCurrent null\n");
		goto End;
	}

	memcpy(SysCurrent, hwmon_sensor_list->current_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szCurrent, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szCurrent, 10, &ulCurrent);
	if (0 > iRet) {
		goto End;
	}
	snprintf(SysCurrent->sensor[0].value, sizeof(SysCurrent->sensor[0].value), "%ld", (ulCurrent * 16) + (ulCurrent * 13 / 100));

	iRet = 0;
End:
	return iRet;
}

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_seiko_get_time,
	.set_rtc_time        = rtc_seiko_set_time,
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_seiko_set_auto_poweron,
	.init_auto_poweron    = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron  = rtc_seiko_auto_poweron_uninit,
	.get_fan_status      = GetFanStatusActivePulse,
	.set_fan_status      = SetFanStatusMircopWithGPIO,
	.get_sys_temperature = NULL,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = NULL,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLed,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize	     = Uninitialize,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led         = NULL,
	.get_copy_button_status = NULL, // for matching userspace usage, button pressed = 0, else = 1
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
	.hwmon_get_sys_current = NULL,
	.get_sys_current     = SYNOIOGetCurrentStatusByMicroP,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_DS420p:
			model_ops = &ds420p_ops;
			hwmon_sensor_list = &ds420p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;
		case MODEL_DS720p:
			model_ops = &ds720p_ops;
			hwmon_sensor_list = &ds720p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;
		case MODEL_DS1520p:
			model_ops = &ds1520p_ops;
			hwmon_sensor_list = &ds1520p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;
		case MODEL_DS220p:
			model_ops = &ds220p_ops;
			hwmon_sensor_list = &ds220p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			// for matching userspace usage, button pressed = 0, else = 1
			synobios_ops.get_copy_button_status = getCopyButtonStatus;
#if defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO)
			syno_append_shutdown_hook(&ds220p_shutdown_hook);
#endif /* defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO) */
			break;
		case MODEL_DS920p:
			model_ops = &ds920p_ops;
			hwmon_sensor_list = &ds920p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;
		case MODEL_DVA1622:
			model_ops = &dva1622_ops;
			hwmon_sensor_list = &dva1622_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;

		case MODEL_DS423p:
			model_ops = &ds423p_ops;
			hwmon_sensor_list = &ds423p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			break;

		case MODEL_DS224p:
			model_ops = &ds224p_ops;
			hwmon_sensor_list = &ds224p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
#endif
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
			// for matching userspace usage, button pressed = 0, else = 1
			synobios_ops.get_copy_button_status = getCopyButtonStatus;
#if defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO)
			syno_append_shutdown_hook(&ds224p_shutdown_hook);
#endif /* defined(CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK) && defined(CONFIG_SYNO_GPIO) */
			break;

		default :
			break;
	}

	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}

	return 0;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;

	if( synobios_ops.get_rtc_time ) {
		synobios_ops.get_rtc_time(&rtc_time_pkt);
	}

	if( synobios_ops.uninit_auto_poweron ) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	if ( model_ops->x86_gpio_cleanup ) {
		model_ops->x86_gpio_cleanup();
	}

	return 0;
}
