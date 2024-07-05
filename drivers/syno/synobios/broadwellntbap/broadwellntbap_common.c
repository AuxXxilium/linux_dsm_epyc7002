#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2022 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/slab.h>
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#include "../rtc/rtc.h"
#include "../i2c/i2c-linux.h"
#include "broadwellntbap_common.h"
#include <linux/interrupt.h>

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

static int gblOktoRemoveLedOn = false;
static struct delayed_work oktoremoveled_work;


#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
extern int (*syno_valid_lsi3008_led)(u8 cmd);
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */

/**
 *	Set Max internal disk numbers
 */
static int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_SA3200d:
		case MODEL_SA3400d:
			iMaxInternalDiskNum = 12;
			break;
	}
	return iMaxInternalDiskNum;
}

// If there is any necessary to modify these function on any other model, please rewrite another function to replace it
PWM_FAN_SPEED_MAPPING gxsSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

PWM_FAN_SPEED_MAPPING gxsCPUFanSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

int xsFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gxsSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gxsSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gxsSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

/*
 * On RS10613xsp, and RS3413xsp we have no buzzer clear button anymore, so we need another way to stop redundant power buzzer
 */
int xsSetBuzzerClear(unsigned char buzzer_cleared)
{
	GPIO_PIN Pin;
	int ret = -1;

	Pin.pin = BROADWELLNTBAP_BUZZER_CTRL_PIN;
	Pin.value = buzzer_cleared;
	if (0 > SetGpioPin(&Pin)) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		goto End;
	}

	ret = 0;
End:
	return ret;
}

int xsGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    GPIO_PIN Pin;
    int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = BROADWELLNTBAP_BUZZER_OFF_PIN;
    if ( 0 > GetGpioPin( &Pin ) ) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
        goto End;
    }

    if ( 0 == Pin.value ) {
        *buzzer_cleared = 1;
		// after read buzzer cleared, pull it back to high
		// an workaround for #48623, because gpio 4 & 5 are jointed together
		if (model_ops && model_ops->x86_set_buzzer_clear) {
			model_ops->x86_set_buzzer_clear(1);
		}
    }

    ret = 0;
End:
    return ret;
}

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;

	return iDutyCycle;
}

int BroadwellntbapRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;
	u16 data = 0;

	err = linuxI2CSmbusRegRead(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, PSU_DELTA_800AB_I2C_REG, &data);

	if (0 != err) {
		printk("%s:%d return error:%d!\n", __FILE__, __LINE__, err);
		goto FAIL;
	}

	power1_Value = data & 0x1;
	power2_Value = data & 0x2;

	if (power1_Value) {
		power_info->power_1 = POWER_STATUS_GOOD;
	} else {
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2_Value) {
		power_info->power_2 = POWER_STATUS_GOOD;
	} else {
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
}

static int giDiskLedController = -1;
static int giDiskLedControllerInit = 0;

/**
 *	For consistency of lighting disk led, we need a gpio pin to control cpld behavior
 *	Which means pull a gpio to let disk led be able to light on
 */
static
void InitSynoDiskLed(void)
{
	GPIO_PIN pin;

	// the disk led enable controller is not assigned, just return
	if (0 > giDiskLedController) {
		return;
	}
	pin.pin = giDiskLedController;
	pin.value = 0;
	SetGpioPin(&pin);
}

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
}

static
int SetCpuFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_bandon_get_time(&rtc_time_pkt);
	return 0;
}

int GetModel(void)
{
	int model = MODEL_SA3200d;

	if (!strncmp(gszSynoHWVersion, HW_SA3200d, strlen(HW_SA3200d))) {
			model = MODEL_SA3200d;
	}
	else if (!strncmp(gszSynoHWVersion, HW_SA3400d, strlen(HW_SA3400d))) {
			model = MODEL_SA3400d;
	}

	return model;
}

static
int GetBrand(void)
{
	return BRAND_SYNOLOGY;
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

int SetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 75 < pPin->pin ) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 1)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

int GetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 75 < pPin->pin ) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 0)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

static
int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp)
{
	if (!pThermalTemp) {
		return -1;
	}

#ifdef MY_DEF_HERE
	syno_sys_temperature(pThermalTemp);
#endif /*MY_DEF_HERE*/

	return 0;
}

static
int GetCpuTemperatureI3Transfer(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;

	if (NULL == pCpuTemp) {
		goto END;
	}

#ifdef CONFIG_SYNO_X86_CORETEMP
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

END:
	return iRet;
}

static
int SetAlarmLed(unsigned char ledON)
{
	GPIO_PIN Pin;
	int iRet = -1;

	Pin.pin = BROADWELLNTBAP_ALARM_LED_PIN;
	/**
	 *	Attetion!!
	 *	0 Means alarm on
	 *	1 Means alarm off
	 *	need to reverse
	 */
	if (ledON) {
		Pin.value = 0;
	} else {
		Pin.value = 1;
	}

	if (0 > SetGpioPin(&Pin)) {
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

static void OkToRemoveLedWork(struct work_struct *work){
	GPIO_PIN Pin;
	static unsigned char chGpioStatus = 0;

	Pin.pin = BROADWELLNTBAP_REMOTE_LED_GPIO_PIN;

	if (true == gblOktoRemoveLedOn) {
		Pin.value = chGpioStatus;
		if (0 == chGpioStatus) {
			chGpioStatus = 1;
		} else {
			chGpioStatus = 0;
		}
		if (0 > SetGpioPin(&Pin)) {
			printk("Fail to set ok to remove led gpio\n");
		}
	} else {
		if (0 == chGpioStatus) {
			chGpioStatus = 1;
			Pin.value = 0;
			if (0 > SetGpioPin(&Pin)) {
				printk("Fail to set ok to remove led gpio\n");
			}
		}
	}

	schedule_delayed_work(&oktoremoveled_work,
					msecs_to_jiffies(1000));
}

static
int SetOkToRemoveLeD(unsigned char ledON)
{
	/**
	 *	1 Means led on
	 *	0 Means led off
	 */
	static unsigned long Inited = 0;
	if (0 == test_and_set_bit(0, &Inited)) {
		INIT_DELAYED_WORK(&oktoremoveled_work, OkToRemoveLedWork);
		schedule_work(&oktoremoveled_work);
	}

	if (ledON) {
		gblOktoRemoveLedOn = true;
	} else {
		gblOktoRemoveLedOn = false;
	}

	return 0;
}

static
int SetBuzzerClear(unsigned char buzzer_cleared)
{
	int ret = 0;

	if (model_ops && model_ops->x86_set_buzzer_clear) {
		ret = model_ops->x86_set_buzzer_clear(buzzer_cleared);
	}

	return ret;
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

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	unsigned int freq = cpufreq_quick_get(0);

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

int SynoSetSasAllLed(SYNO_LED ledStatus) {
	static SYNO_LED CurrentLedStatus = -1;
	int iRet = -1;
	GPIO_PIN pin;

	pin.pin = BROADWELLNTBAP_DISK_LED_ACTIVATE_PIN;
	pin.value = 0;
	if (ledStatus != CurrentLedStatus) {

		if (SYNO_LED_OFF == ledStatus) {
			pin.value = DISK_LED_CTRL_OFF;
		} else if (SYNO_LED_ON == ledStatus) {
			pin.value = DISK_LED_CTRL_ON;
		} else {
			goto END;
		}
		SetGpioPin(&pin);

		CurrentLedStatus = ledStatus;
	}
	iRet = syno_valid_lsi3008_led(ledStatus);

END:
	return iRet;
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

int BroadwellntbapGetPowerStatus(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;
	u16 data = 0;

	err = linuxI2CSmbusRegRead(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, PSU_DELTA_800AB_I2C_REG, &data);

	if (0 != err) {
		printk("%s:%d return error:%d!\n", __FILE__, __LINE__, err);
		goto FAIL;
	}
	memcpy(psu_status, hwmon_sensor_list->psu_status,
		iPsuNumber * sizeof(SYNO_HWMON_SENSOR_TYPE));

	// we have known sa3200d and sa3400d have two power supplies.
	power1_Value = data & 0x1;
	power2_Value = data & 0x2;

	if (power1_Value) {
		snprintf(psu_status[0].sensor[0].value, MAX_SENSOR_VALUE, "%s", "psu good");
	} else {
		snprintf(psu_status[0].sensor[0].value, MAX_SENSOR_VALUE, "%s", "psu off");
	}

	if (power2_Value) {
		snprintf(psu_status[1].sensor[0].value, MAX_SENSOR_VALUE, "%s", "psu good");
	} else {
		snprintf(psu_status[1].sensor[0].value, MAX_SENSOR_VALUE, "%s", "psu off");
	}
	err = 0;

FAIL:
	return err;
}

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_bandon_get_time,
	.set_rtc_time        = rtc_bandon_set_time,
	.get_auto_poweron	 = rtc_get_auto_poweron,
	.set_auto_poweron	 = rtc_bandon_set_auto_poweron,
	.get_fan_status      = GetFanStatus,
	.set_fan_status      = SetFanStatus,
	.get_sys_temperature = GetSysTemperature,
	.get_cpu_temperature = GetCpuTemperatureI3Transfer,
	.set_cpu_fan_status  = SetCpuFanStatus,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLed,
	.set_ok_to_remove_led = NULL,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize		 = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.set_buzzer_clear	 = SetBuzzerClear,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led          = NULL,
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;


	giDiskLedController = BROADWELLNTBAP_DISK_LED_ACTIVATE_PIN;

	switch(GetModel())
	{
		case MODEL_SA3200d:
			model_ops = &sa3200d_ops;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = SynoSetSasAllLed;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			synobios_ops.set_ok_to_remove_led = SetOkToRemoveLeD;
			break;
		case MODEL_SA3400d:
			model_ops = &sa3400d_ops;
			hwmon_sensor_list = &sa3400d_sensor_list;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = SynoSetSasAllLed;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			synobios_ops.set_ok_to_remove_led = SetOkToRemoveLeD;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			synobios_ops.hwmon_get_psu_status = BroadwellntbapGetPowerStatus;
			break;
		default:
			break;
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	return 0;
}

