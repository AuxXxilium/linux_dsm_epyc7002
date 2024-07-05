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
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include <linux/slab.h>
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#include "../rtc/rtc.h"
#include "../i2c/i2c-linux.h"
#include "broadwellnk_common.h"
#include <linux/i2c.h>
#include <linux/leds-atmega1608.h>

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int *hdd_enable_gpio = NULL;
static int *hdd_detect_gpio = NULL;

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
extern int (*syno_valid_lsi3008_led)(u8 cmd);
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */

#if defined(CONFIG_SYNO_FIXED_DISK_NAME_MV14XX) && defined(CONFIG_SYNO_SATA_REMAP)
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif /* CONFIG_SYNO_FIXED_DISK_NAME_MV14XX && CONFIG_SYNO_SATA_REMAP */

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
int (*GetMaxInternalHostNum)(void) = NULL;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

extern int syno_is_hw_revision(const char *hw_revision);
static int I2CGetPowerInfo(POWER_INFO *power_info);
/**
 *	Set Max internal disk numbers
 */
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
static
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS3018xs:
		case MODEL_DS1621xsp:
			iMaxInternalDiskNum = 6;
			break;
		case MODEL_FS1018:
		case MODEL_SA3400:
		case MODEL_SA3600:
		case MODEL_RS3621xsp:
		case MODEL_RS3621rpxs:
		case MODEL_DS3622xsp:
			iMaxInternalDiskNum = 12;
			break;
		case MODEL_RS4021xsp:
			iMaxInternalDiskNum = 16;
			break;
		case MODEL_RS1619xsp:
			iMaxInternalDiskNum = 4;
			break;
		case MODEL_FS3600:
			iMaxInternalDiskNum = 24;
			break;
		case MODEL_HD3400:
			iMaxInternalDiskNum = 60;
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

	Pin.pin = BROADWELLNK_BUZZER_CTRL_PIN;
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

	Pin.pin = BROADWELLNK_BUZZER_OFF_PIN;
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

#define GPIO_POWER_GOOD	1
int BroadwellnkRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;

	if ( 0 != syno_pch_lpc_gpio_pin(BROADWELLNK_POWER1_PIN , &power1_Value, 0) ) {
		goto FAIL;
	}

	if ( 0 != syno_pch_lpc_gpio_pin(BROADWELLNK_POWER2_PIN , &power2_Value, 0) ) {
		goto FAIL;
	}

	if (power1_Value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	}else{
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2_Value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	}else{
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
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

#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
/* Control disk led via 1475 SGPIO for user space */
static
int DiskLedCtrlBy1475SGPIO(int disknum, SYNO_DISK_LED status)
{
	int iRet = -1;
	// first time we tried to light on disk led
	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}
	if (funcSYNOCtrlDiskLedBy1475) {
		iRet = funcSYNOCtrlDiskLedBy1475(disknum, status);
	}
	return iRet;
}

/* Control disk led via 1475 SGPIO for kernel */
static
int SYNODiskLedCtrlBy1475SGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}
	if (1 > disknum || GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if (funcSYNOCtrlDiskLedBy1475 != NULL) {
#if defined(CONFIG_SYNO_FIXED_DISK_NAME_MV14XX) && defined(CONFIG_SYNO_SATA_REMAP)
		if (!gblDiskNumNeedTran) {
#endif /* CONFIG_SYNO_FIXED_DISK_NAME_MV14XX && CONFIG_SYNO_SATA_REMAP */
			err = funcSYNOCtrlDiskLedBy1475(disknum - 1, status);
#if defined(CONFIG_SYNO_FIXED_DISK_NAME_MV14XX) && defined(CONFIG_SYNO_SATA_REMAP)
		} else {
			err = funcSYNOCtrlDiskLedBy1475(giDiskMapTable[disknum - 1], status);
		}
#endif /* CONFIG_SYNO_FIXED_DISK_NAME && CONFIG_SYNO_SATA_REMAP */
	}
END:
	return err;
}
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

static
int GetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	int gpio_fan_map[] = {4, 31};
	int fanNum = sizeof(gpio_fan_map)/sizeof(gpio_fan_map[0]);
	GPIO_PIN gPioPin;
	int rgcVolt[2] = {0, 0};

	if (pStatus == NULL) {
		return -EINVAL;
	}

	if (fanno > fanNum) {
		return -EINVAL;
	}

	gPioPin.pin = gpio_fan_map[fanno-1];

	do {
		GetGpioPin(&gPioPin);
		if (!gPioPin.value) {
			rgcVolt[0]++;
		} else {
			rgcVolt[1]++;
		}

		if (rgcVolt[0] && rgcVolt[1]) {
			break;
		}
		udelay(300);
	} while ((rgcVolt[0] + rgcVolt[1]) < 200);

	if (rgcVolt[0] == 0) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}
	return 0;
}

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

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

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 0;
	GPIO_PIN pPin;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list || (NULL == hdd_enable_gpio && NULL == hdd_detect_gpio)) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	for (index = 0; index < GetMaxInternalDiskNum(); index++){
		if (NULL != hdd_enable_gpio) {
			pPin.pin = hdd_enable_gpio[index];
			GetGpioPin(&pPin);
			hdd_enable |= (pPin.value & 0x01) << index;
		}
		// hdd_detect_pin in broadwellnk are active low. So get the inverse value.
		if (NULL != hdd_detect_gpio) {
			pPin.pin = hdd_detect_gpio[index];
			GetGpioPin(&pPin);
			hdd_detect |= ((~pPin.value) & 0x01) << index;
		}
	}

	if (NULL != hdd_enable_gpio) {
		snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%ld", hdd_enable);
	}
	if (NULL != hdd_detect_gpio) {
		snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	}

	iRet = 0;

End:
	return iRet;
}

#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
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
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */

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
	int model = MODEL_DS3018xs;

	if (!strncmp(gszSynoHWVersion, HW_DS3018xs, strlen(HW_DS3018xs))) {
			model = MODEL_DS3018xs;
	} else if (!strncmp(gszSynoHWVersion, HW_FS1018, strlen(HW_FS1018))) {
			model = MODEL_FS1018;
	} else if (!strncmp(gszSynoHWVersion, HW_RS1619xsp, strlen(HW_RS1619xsp))) {
			model = MODEL_RS1619xsp;
	} else if (!strncmp(gszSynoHWVersion, HW_SA3400, strlen(HW_SA3400))) {
			model = MODEL_SA3400;
	} else if (!strncmp(gszSynoHWVersion, HW_SA3600, strlen(HW_SA3600))) {
			model = MODEL_SA3600;
	} else if (!strncmp(gszSynoHWVersion, HW_FS3600, strlen(HW_FS3600))) {
			model = MODEL_FS3600;
	} else if (!strncmp(gszSynoHWVersion, HW_HD3400, strlen(HW_HD3400))) {
			model = MODEL_HD3400;
	} else if (!strncmp(gszSynoHWVersion, HW_DS1621xsp, strlen(HW_DS1621xsp))) {
			model = MODEL_DS1621xsp;
	} else if (!strncmp(gszSynoHWVersion, HW_RS3621xsp, strlen(HW_RS3621xsp))) {
			model = MODEL_RS3621xsp;
	} else if (!strncmp(gszSynoHWVersion, HW_RS3621rpxs, strlen(HW_RS3621rpxs))) {
			model = MODEL_RS3621rpxs;
	} else if (!strncmp(gszSynoHWVersion, HW_RS4021xsp, strlen(HW_RS4021xsp))) {
			model = MODEL_RS4021xsp;
	} else if (!strncmp(gszSynoHWVersion, HW_DS3622xsp, strlen(HW_DS3622xsp))) {
			model = MODEL_DS3622xsp;
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

	Pin.pin = BROADWELLNK_ALARM_LED_PIN;
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

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
static
int SetDiskLedStatusByI2CLedDimmer(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;

	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}

	if (0 >= iDiskNum || 6 < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (NULL == gpGreenLedMap || NULL == gpOrangeLedMap){
		printk("Disk LED not mapped\n");
		goto END;
	}

	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			LEDOrange = LED_FULL;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_FAULTY); //disable the disk activity led trigger
			break;
		case DISK_LED_GREEN_BLINK:
			LEDOrange = LED_OFF;
			LEDGreen = LED_HALF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_GREEN_SOLID:
			LEDOrange = LED_OFF;
			LEDGreen = LED_FULL;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_OFF:
			LEDOrange = LED_OFF;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	syno_ledtrig_set(gpGreenLedMap[iDiskNum-1], LEDGreen);
	syno_ledtrig_set(gpOrangeLedMap[iDiskNum-1], LEDOrange);

	iRet = 0;
END:
	return iRet;
}

static
int SYNOSetDiskLedStatusByI2CLedDimmer(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusByI2CLedDimmer(iHostNum + 1, iStatus);
}

/*
 * Setup the global Disk LED mapping for sata driver
 */
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
{
    if(NULL == pGreenLed || NULL == pOrangeLed || 6 < iInternalDiskNum){
		return;
    }

	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = (int*)kmalloc(6 * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = (int*)kmalloc(6 * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, 6 * sizeof(int));
	memset(gpOrangeLedMap, -1, 6 * sizeof(int));

	memcpy(gpGreenLedMap, pGreenLed, iInternalDiskNum * sizeof(int));
	memcpy(gpOrangeLedMap, pOrangeLed, iInternalDiskNum * sizeof(int));

}

static
int SetDiskLedStatusByAtmega(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;

	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}

	if (0 >= iDiskNum || (giSynoAtmegaNum*12) < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (NULL == gpGreenLedMap || NULL == gpOrangeLedMap){
		printk("Disk LED not mapped\n");
		goto END;
	}

	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			LEDOrange = LED_FULL;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_FAULTY); //disable the disk activity led trigger
			break;
		case DISK_LED_GREEN_BLINK:
			LEDOrange = LED_OFF;
			LEDGreen = LED_HALF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_GREEN_SOLID:
			LEDOrange = LED_OFF;
			LEDGreen = LED_FULL;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_OFF:
			LEDOrange = LED_OFF;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	syno_ledtrig_set(gpGreenLedMap[iDiskNum-1], LEDGreen);
	syno_ledtrig_set(gpOrangeLedMap[iDiskNum-1], LEDOrange);

	iRet = 0;
END:
	return iRet;
}

static
int SYNOSetDiskLedStatusByAtmega(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusByAtmega(iHostNum + 1, iStatus);
}

/*
 * Setup the global Disk ATMEGA LED mapping for sata driver
 */
void SetupDiskAtmegaLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
{
	if(NULL == pGreenLed || NULL == pOrangeLed || (giSynoAtmegaNum*12) < iInternalDiskNum){
		return;
	}

	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = (int*)kmalloc(iInternalDiskNum * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = (int*)kmalloc(iInternalDiskNum * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, iInternalDiskNum * sizeof(int));
	memset(gpOrangeLedMap, -1, iInternalDiskNum * sizeof(int));

	memcpy(gpGreenLedMap, pGreenLed, iInternalDiskNum * sizeof(int));
	memcpy(gpOrangeLedMap, pOrangeLed, iInternalDiskNum * sizeof(int));

}

int SetHddActLedByLedTrigger(SYNO_LED ledStatus, u16 addr)
{
	struct i2c_adapter *adapter = NULL;
	union i2c_smbus_data data;
	int iRet = -1;

	adapter = i2c_get_adapter(I2C_BUS_NO);
	if (!adapter) {
		printk("synobios: cannot get i2c adapter\n");
		goto END;
	}

	data.byte = (SYNO_LED_OFF == ledStatus) ? ATMEGA1608_LED_MASKED : ATMEGA1608_LED_UNMASK;
	iRet = i2c_smbus_xfer(adapter, addr, 0, I2C_SMBUS_WRITE, ATMEGA1608_MASK, I2C_SMBUS_BYTE_DATA, &data);
	if (0 > iRet) {
		printk("synobios: fail to write i2c smbus\n");
		goto END;
	}

END:
        return iRet;
}

int SetHddAtmegaLed(SYNO_LED ledStatus)
{
       int i = 0, iRet = 0, iErr;

       for (i = 0; i < giSynoAtmegaNum; ++i) {
	       iErr = SetHddActLedByLedTrigger(ledStatus, gSynoAtmegaAddr[i]);
	       if (iErr < 0) {
		       iRet = iErr;
		       printk("fail to set HDD act LED on 0x%02x, err=%d", gSynoAtmegaAddr[i], iErr);
	       }
       }
       return iRet;
}



#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

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

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
    // for those model that have mix ahci and 9xxx internal disk, please implement GetMaxInternalHostNum
    GetMaxInternalHostNum = NULL;
#else
	giDiskLedController = BROADWELLNK_DISK_LED_ACTIVATE_PIN;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#if defined(CONFIG_SYNO_FIXED_DISK_NAME_MV14XX) && defined(CONFIG_SYNO_SATA_REMAP)
	if (NULL == syno_disk_map_table_gen_mv14xx ||
		0 > syno_disk_map_table_gen_mv14xx(giDiskMapTable, GetMaxInternalDiskNum())) {
		gblDiskNumNeedTran = 0;
	} else {
		gblDiskNumNeedTran = 1;
	}
#endif /* CONFIG_SYNO_FIXED_DISK_NAME_MV14XX && CONFIG_SYNO_SATA_REMAP */

#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
	synobios_ops.set_disk_led = SYNODiskLedCtrlBy1475SGPIO;
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
	funcSYNOSATADiskLedCtrl = SYNODiskLedCtrlBy1475SGPIO;
#else
	funcSYNOSATADiskLedCtrl = DiskLedCtrlBy1475SGPIO;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */

	switch(GetModel())
	{
		case MODEL_DS3018xs:
			model_ops = &ds3018xs_ops;
			synobios_ops.get_fan_status = GetFanStatusMircopWithGPIO;
			synobios_ops.set_fan_status = SetFanStatusMircopWithGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
#else
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
			SetupDiskLedMap((int[6]){0,2,4,6,8,10}, (int[6]){1,3,5,7,9,11}, 6);
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#else
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(12, 1);
			syno_ahci_disk_led_enable(13, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_FS1018:
			model_ops = &fs1018_ops;
			synobios_ops.get_fan_status = GetFanStatusMircopWithGPIO;
			synobios_ops.set_fan_status = SetFanStatusMircopWithGPIO;
			break;
		case MODEL_RS1619xsp:
			model_ops = &rs1619xsp_ops;
			hwmon_sensor_list = &rs1619xsp_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			synobios_ops.get_fan_status = GetFanStatusMircopWithGPIO;
			synobios_ops.set_fan_status = SetFanStatusMircopWithGPIO;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			hdd_enable_gpio = rs1619xsp_hdd_enable_gpio;
			hdd_detect_gpio = rs1619xsp_hdd_detect_gpio;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
            synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
#else
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
            funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#else
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_SA3400:
			model_ops = &sa3400_ops;
			hwmon_sensor_list = &sa3400_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			break;
		case MODEL_SA3600:
			model_ops = &sa3600_ops;
			hwmon_sensor_list = &sa3600_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			break;
		case MODEL_FS3600:
			model_ops = &fs3600_ops;
			hwmon_sensor_list = &fs3600_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			break;
		case MODEL_HD3400:
			model_ops = &hd3400_ops;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led) {
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */
			break;
	case MODEL_DS1621xsp:
			model_ops = &ds1621xsp_ops;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[6]){0,2,4,6,8,10}, (int[6]){1,3,5,7,9,11}, 6);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(12, 1);
			syno_ahci_disk_led_enable(13, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			hwmon_sensor_list = &ds1621xsp_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
			break;
	case MODEL_RS3621xsp:
			model_ops = &rs3621xsp_ops;
			if (syno_is_hw_revision(HW_R1)) {
				model_ops->x86_get_power_status    = I2CGetPowerInfo;
				RS3621xspSMBusSwitchInit();
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[12]){20,2,0,22,18,8,6,4,16,14,12,10},
					(int[12]){21,3,1,23,19,9,7,5,17,15,13,11},
					12);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
				syno_ahci_disk_led_enable(1, 1);
				syno_ahci_disk_led_enable(2, 1);
				syno_ahci_disk_led_enable(3, 1);
				syno_ahci_disk_led_enable(6, 1);
				syno_ahci_disk_led_enable(7, 1);
				syno_ahci_disk_led_enable(8, 1);
				syno_ahci_disk_led_enable(11, 1);
				syno_ahci_disk_led_enable(12, 1);
				syno_ahci_disk_led_enable(13, 1);
				syno_ahci_disk_led_enable(16, 1);
				syno_ahci_disk_led_enable(17, 1);
				syno_ahci_disk_led_enable(18, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			} else if (syno_is_hw_revision(HW_R2)) {
				model_ops->x86_get_power_status    = I2CGetPowerInfo;
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[12]){20,2,0,22,18,8,6,4,16,14,12,10},
					(int[12]){21,3,1,23,19,9,7,5,17,15,13,11},
					12);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			}
			hwmon_sensor_list = &rs3621xsp_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			if (syno_is_hw_revision(HW_R1) ||
				syno_is_hw_revision(HW_R2)) {
				hwmon_sensor_list->hdd_backplane = &RS3621xsp_hdd_backplane_status;
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
				synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			}
			break;
	case MODEL_RS3621rpxs:
			model_ops = &rs3621rpxs_ops;
			if (syno_is_hw_revision(HW_R1)) {
				model_ops->x86_get_power_status    = I2CGetPowerInfo;
				RS3621rpxsSMBusSwitchInit();
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[12]){20,2,0,22,18,8,6,4,16,14,12,10},
					(int[12]){21,3,1,23,19,9,7,5,17,15,13,11},
					12);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByAtmega;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
				syno_ahci_disk_led_enable(1, 1);
				syno_ahci_disk_led_enable(2, 1);
				syno_ahci_disk_led_enable(3, 1);
				syno_ahci_disk_led_enable(6, 1);
				syno_ahci_disk_led_enable(7, 1);
				syno_ahci_disk_led_enable(8, 1);
				syno_ahci_disk_led_enable(11, 1);
				syno_ahci_disk_led_enable(12, 1);
				syno_ahci_disk_led_enable(13, 1);
				syno_ahci_disk_led_enable(16, 1);
				syno_ahci_disk_led_enable(17, 1);
				syno_ahci_disk_led_enable(18, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			} else if (syno_is_hw_revision(HW_R2)) {
				model_ops->x86_get_power_status    = I2CGetPowerInfo;
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[12]){20,2,0,22,18,8,6,4,16,14,12,10},
					(int[12]){21,3,1,23,19,9,7,5,17,15,13,11},
					12);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByAtmega;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			}
			hwmon_sensor_list = &rs3621rpxs_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			if (syno_is_hw_revision(HW_R1) ||
				syno_is_hw_revision(HW_R2)) {
				hwmon_sensor_list->hdd_backplane = &RS3621rpxs_hdd_backplane_status;
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
				synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			}
			break;
	case MODEL_RS4021xsp:
			model_ops = &rs4021xsp_ops;
			if (syno_is_hw_revision(HW_R1)) {
				RS4021xspSMBusSwitchInit();
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[16]){40,26,16,2,38,28,14,4,36,30,12,6,34,32,10,8},
					(int[16]){41,27,17,3,39,29,15,5,37,31,13,7,35,33,11,9},
					16);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByAtmega;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
				syno_ahci_disk_led_enable(0, 1);
				syno_ahci_disk_led_enable(1, 1);
				syno_ahci_disk_led_enable(2, 1);
				syno_ahci_disk_led_enable(3, 1);
				syno_ahci_disk_led_enable(5, 1);
				syno_ahci_disk_led_enable(6, 1);
				syno_ahci_disk_led_enable(7, 1);
				syno_ahci_disk_led_enable(8, 1);
				syno_ahci_disk_led_enable(10, 1);
				syno_ahci_disk_led_enable(11, 1);
				syno_ahci_disk_led_enable(12, 1);
				syno_ahci_disk_led_enable(13, 1);
				syno_ahci_disk_led_enable(15, 1);
				syno_ahci_disk_led_enable(16, 1);
				syno_ahci_disk_led_enable(17, 1);
				syno_ahci_disk_led_enable(18, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			} else if (syno_is_hw_revision(HW_R2)) {
				synobios_ops.set_hdd_led = SetHddAtmegaLed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
				synobios_ops.set_disk_led = SetDiskLedStatusByAtmega;
				SetupDiskAtmegaLedMap(
					(int[16]){40,26,16,2,38,28,14,4,36,30,12,6,34,32,10,8},
					(int[16]){41,27,17,3,39,29,15,5,37,31,13,7,35,33,11,9},
					16);
#ifdef MY_DEF_HERE
				funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByAtmega;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			}
			hwmon_sensor_list = &rs4021xsp_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
			if (syno_is_hw_revision(HW_R1) ||
				syno_is_hw_revision(HW_R2)) {
				hwmon_sensor_list->hdd_backplane = &RS4021xsp_hdd_backplane_status;
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
				synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			}
			break;
		case MODEL_DS3622xsp:
			model_ops = &ds3622xsp_ops;
			hwmon_sensor_list = &ds3622xsp_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
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

int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status)
{
	int ret = -1;
	int err = -1;
	u16 data = 0xffff;
	static u16 uLostAddr = 0;

	if (NULL == status) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto FAIL;
	}

	err = linuxI2CSmbusRegRead(i2c_bus_no, i2c_addr, PSU_DELTA_AC139_I2C_REG, &data);
	if (0 != err) {
		if (uLostAddr != i2c_addr) {
			uLostAddr = i2c_addr;
			printk("%s:%d return error!\n", __FILE__, __LINE__);
		}
		// i2c will read failed when 800W power is not inserted
		// report bad power status with success return to avoid repeated log
		*status = POWER_STATUS_BAD;
		ret = 0;
		goto FAIL;
	}
	if (uLostAddr == i2c_addr) {
		uLostAddr = 0;
	}

	if (data & PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT) {
		*status = POWER_STATUS_BAD;
	} else {
		*status = POWER_STATUS_GOOD;
	}

	ret = 0;

FAIL:
	return ret;
}

static
int I2CGetPowerInfo(POWER_INFO *power_info)
{
	int ret = -1;
	int err = -1;

	if (NULL == power_info) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, &(power_info->power_1));
	if (0 != err) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_BTM_I2C_ADDR, &(power_info->power_2));
	if (0 != err) {
		goto FAIL;
	}

        ret = 0;

FAIL:
	return ret;
}

/* FIXME: should not directly copy following function
 * Modified from drivers/i2c/muxes/i2c-mux-pca954x.c */
int SMBusSwitchRegWrite(int bus_no, u16 addr, u8 val)
{
        int ret = -1;
        struct i2c_adapter *adap = NULL;

        adap = i2c_get_adapter(bus_no);
        if (!adap) {
                printk("Cannot get i2c adapter!\n");
                goto END;
        }

        if (adap->algo->master_xfer) {
                struct i2c_msg msg;
                char buf[1];

                msg.addr = addr;
                msg.flags = 0;
                msg.len = 1;
                buf[0] = val;
                msg.buf = buf;
                ret = __i2c_transfer(adap, &msg, 1);
        } else {
                union i2c_smbus_data data;
                ret = adap->algo->smbus_xfer(adap, addr,
                                0, I2C_SMBUS_WRITE,
                                val, I2C_SMBUS_BYTE, &data);
        }
END:
        return ret;
}
