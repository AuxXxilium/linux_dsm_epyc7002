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

#include "r1000_common.h"
#include "syno_ttyS.h"

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_TTY_RECEIVE
extern int (*syno_get_current)(unsigned char, struct tty_struct *);
extern int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty);
#endif /* CONFIG_SYNO_TTY_RECEIVE */
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

	if ( !strncmp(gszSynoHWVersion, HW_DS723p, strlen(HW_DS723p) ) ) {
		model = MODEL_DS723p;
	} else if (!strncmp(gszSynoHWVersion, HW_DS923p, strlen(HW_DS923p))){
		model = MODEL_DS923p;
	} else if (!strncmp(gszSynoHWVersion, HW_RS422p, strlen(HW_RS422p))){
		model = MODEL_RS422p;
	} else if (!strncmp(gszSynoHWVersion, HW_DS1522p, strlen(HW_DS1522p))){
		model = MODEL_DS1522p;
	}

	return model;
}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_DS723p:
			iInternalDiskNum = 2;
			break;
		case MODEL_DS923p:
		case MODEL_RS422p:
			iInternalDiskNum = 4;
			break;
		case MODEL_DS1522p:
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

	if (NULL == pCpuTemp) {
		goto END;
	}

#ifdef CONFIG_SYNO_K10TEMP
	iRet = syno_k10cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_K10TEMP */

END:
	return iRet;
}

static
int SetAlarmLedByGPIO(unsigned char type)
{
	int iBlinking = 0;

	if (type) {
		iBlinking = 1;
	}
	return SYNO_CTRL_ALARM_LED_SET(iBlinking);
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
	.set_alarm_led       = NULL,
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
		case MODEL_DS723p:
			model_ops = &ds723p_ops;
			hwmon_sensor_list = &ds723p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
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
#ifdef CONFIG_SYNO_TTY_RECEIVE
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE */
			break;
		case MODEL_DS923p:
			model_ops = &ds923p_ops;
			hwmon_sensor_list = &ds923p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
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
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif
#ifdef CONFIG_SYNO_TTY_RECEIVE
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE */
			break;
		case MODEL_RS422p:
			model_ops = &rs422p_ops;
			hwmon_sensor_list = &rs422p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
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
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif
#ifdef CONFIG_SYNO_TTY_RECEIVE
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE */
			break;
		case MODEL_DS1522p:
			model_ops = &ds1522p_ops;
			hwmon_sensor_list = &ds1522p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
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
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
#endif
#ifdef CONFIG_SYNO_TTY_RECEIVE
			syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_RECEIVE */
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
