#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_SYNO_HWMON_PMBUS
#include <linux/syno_fdt.h>
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#include "../rtc/rtc.h"
#include "../i2c/i2c-linux.h"
#include "../pmbus/pmbus.h"

#include "epyc7002_common.h"

#define GPIO_POWER_GOOD	1

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

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

	if ( !strcmp(gszSynoHWVersion, HW_FS6400N) ) {
		model = MODEL_FS6400N;
	} else if ( !strcmp(gszSynoHWVersion, HW_FS6410) ) {
		model = MODEL_FS6410;
	} else if ( !strcmp(gszSynoHWVersion, HW_SA6400) ) {
		model = MODEL_SA6400;
	} else if ( !strcmp(gszSynoHWVersion, HW_SA6200) ) {
		model = MODEL_SA6200;
	} else if ( !strcmp(gszSynoHWVersion, HW_SC6200) ) {
		model = MODEL_SC6200;
	}

	return model;
}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_FS6400N:
		case MODEL_FS6410:
			iInternalDiskNum = 24;
			break;
		case MODEL_SA6400:
		case MODEL_SA6200:
		case MODEL_SC6200:
			iInternalDiskNum = 12;
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
#if defined(CONFIG_SYNO_K10TEMP) || defined(CONFIG_SYNO_HWMON_AMD_K10TEMP)
	iRet = syno_k10cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_K10TEMP || CONFIG_SYNO_HWMON_AMD_K10TEMP */

END:
	return iRet;
}


static
int SetAlarmLed(unsigned char type)
{
	int iBlinking = 0;

	if (type) {
		iBlinking = 1;
	}
	return SYNO_CTRL_ALARM_LED_SET(iBlinking);
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

static
int HWMONGetThermalSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysThermal)
{
	int iRet = -1;

	if (NULL == SysThermal || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysThermal, hwmon_sensor_list->thermal_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_thermal_sensor(SysThermal);

END:
	return iRet;
}

static
int HWMONGetFanSpeedRPMFromADT(SYNO_HWMON_SENSOR_TYPE *FanSpeedRpm)
{
	int iRet = -1;

	if (NULL == FanSpeedRpm || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(FanSpeedRpm, hwmon_sensor_list->fan_speed_rpm, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_fan_speed_rpm(FanSpeedRpm);

END:
	return iRet;
}

static
int HWMONGetVoltageSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysVoltage)
{
	int iRet = -1;

	if (NULL == SysVoltage || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysVoltage, hwmon_sensor_list->voltage_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_voltage_sensor(SysVoltage);

END:
	return iRet;
}

#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
extern void syno_smbus_hdd_powerctl_init(void);
static
int HWMONGetHDDBackPlaneStatusBySMBUS(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 0;
	int val = 0;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}
	if (0 >= g_smbus_hdd_powerctl) {
		goto End;
	}
	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	for (index = 0; index < GetMaxInternalDiskNum(); index++){
		if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read) {
			val = SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index+1);
			hdd_detect |= (val & 0x01) << index;
		}
		if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read) {
			val = SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index+1);
			hdd_enable |= (val & 0x01) << index;
		}
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read) {
		snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read) {
		snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_enable);
	}

	iRet = 0;
End:
	return iRet;
}
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#if defined(CONFIG_SYNO_HWMON_PMBUS) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
int EPYC7002RedundantPowerGetPowerStatusByI2C(POWER_INFO *power_info)
{
	int iRet = -1;

	if (NULL == power_info) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto END;
	}

	if (0 > I2CPmbusReadPowerStatus(0, &(power_info->power_1))) {
		goto END;
	}
	if (0 > I2CPmbusReadPowerStatus(1, &(power_info->power_2))) {
		goto END;
	}

	iRet = 0;

END:
	return iRet;
}
#endif /* CONFIG_SYNO_HWMON_PMBUS */

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_seiko_get_time,
	.set_rtc_time        = rtc_seiko_set_time,
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_seiko_set_auto_poweron,
	.init_auto_poweron   = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron = rtc_seiko_auto_poweron_uninit,
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
	.uninitialize        = Uninitialize,
	.get_cpu_info        = GetCPUInfo,
	.set_aha_led         = NULL,
	.get_copy_button_status = NULL, // for matching userspace usage, button pressed = 0, else = 1
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
};

#ifdef CONFIG_SYNO_HWMON_PMBUS
int EPYC7002GetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
{
	int iRet = -1;

	if (NULL == hwmon_sensor_list) {
		goto END;
	}
	memcpy(psu_status, hwmon_sensor_list->psu_status,
		iPsuNumber * sizeof(SYNO_HWMON_SENSOR_TYPE));
	iRet = HWMONGetPSUStatusByI2C(psu_status, iPsuNumber);
END:
	return iRet;
}
#endif /* CONFIG_SYNO_HWMON_PMBUS */

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	DISKLEDSTATUS diskLedStatus;
	const char* abbrName[] = {"internal", "sys"};
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_FS6400N:
			model_ops = &fs6400n_ops;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &fs6400n_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
			break;

		case MODEL_FS6410:
			model_ops = &fs6410_ops;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &fs6410_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = EPYC7002GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
			syno_ahci_disk_led_enable_by_port(9, 1);
			syno_ahci_disk_led_enable_by_port(10, 1);
			syno_ahci_disk_led_enable_by_port(11, 1);
			syno_ahci_disk_led_enable_by_port(12, 1);
			syno_ahci_disk_led_enable_by_port(13, 1);
			syno_ahci_disk_led_enable_by_port(14, 1);
			syno_ahci_disk_led_enable_by_port(15, 1);
			syno_ahci_disk_led_enable_by_port(16, 1);
			syno_ahci_disk_led_enable_by_port(17, 1);
			syno_ahci_disk_led_enable_by_port(18, 1);
			syno_ahci_disk_led_enable_by_port(19, 1);
			syno_ahci_disk_led_enable_by_port(20, 1);
			syno_ahci_disk_led_enable_by_port(21, 1);
			syno_ahci_disk_led_enable_by_port(22, 1);
			syno_ahci_disk_led_enable_by_port(23, 1);
			syno_ahci_disk_led_enable_by_port(24, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_SA6400:
			model_ops = &sa6400_ops;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &sa6400_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = EPYC7002GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
			syno_ahci_disk_led_enable_by_port(9, 1);
			syno_ahci_disk_led_enable_by_port(10, 1);
			syno_ahci_disk_led_enable_by_port(11, 1);
			syno_ahci_disk_led_enable_by_port(12, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_SA6200:
			model_ops = &sa6200_ops;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &sa6200_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = EPYC7002GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
			syno_ahci_disk_led_enable_by_port(9, 1);
			syno_ahci_disk_led_enable_by_port(10, 1);
			syno_ahci_disk_led_enable_by_port(11, 1);
			syno_ahci_disk_led_enable_by_port(12, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_SC6200:
			model_ops = &sc6200_ops;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &sc6200_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = EPYC7002GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_alarm_led = SetAlarmLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
			syno_ahci_disk_led_enable_by_port(9, 1);
			syno_ahci_disk_led_enable_by_port(10, 1);
			syno_ahci_disk_led_enable_by_port(11, 1);
			syno_ahci_disk_led_enable_by_port(12, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
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
