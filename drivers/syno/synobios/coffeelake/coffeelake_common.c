#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

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
#include "coffeelake_common.h"

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
extern int (*syno_valid_lsi3008_led)(u8 cmd);
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */

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

/**
 *	Set Max internal disk numbers
 */
static int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS3619xs:
			iMaxInternalDiskNum = 12;
			break;
	}
	return iMaxInternalDiskNum;
}

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

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;

	return iDutyCycle;
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
	int model = MODEL_DS3619xs;

	if (!strncmp(gszSynoHWVersion, HW_DS3619xs, strlen(HW_DS3619xs))) {
			model = MODEL_DS3619xs;
	}
	return model;
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

/**
 *	Control disk led via AHCI SGPIO
 */
static
int SetSCSIHostLedStatusByAHCIGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret = -1;
	int iFault, iPresent;

	switch(status) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			iFault = 1;
			iPresent = 0;
			break;
		case DISK_LED_GREEN_SOLID:
			iFault = 0;
			iPresent = 1;
			break;
		case DISK_LED_OFF:
			iFault = 0;
			iPresent = 0;
			break;
		default:
			printk("Invalid LED status [%d]\n", status);
			goto END;
	}
#ifdef MY_DEF_HERE
	sata_syno_ahci_diskled_set(iHostNum, iPresent, iFault);
#else
	printk(KERN_ERR "SYNO ATA AHCI DISK LED control function is not defined!!");
#endif /* MY_DEF_HERE */
	ret = 0;
END:
	return ret;
}

#ifdef MY_DEF_HERE
/**
 *	Control disk led over ahci SGPIO
 */
static
int SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret;
	int iWrite = -1, iRead = -1;
	int iInternalHost =0;

	// first time we tried to light on disk led
	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}

	switch(GetModel())
	{
		case MODEL_DS3619xs:
			iInternalHost = 6;
			break;
		default:
			iInternalHost = 0;
			break;
	}

	if (iHostNum < iInternalHost) {
		ret = SetSCSIHostLedStatusByAHCIGPIO(iHostNum, status);;
	} else {
		if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status) {
			iWrite = 1;
		} else {
			iWrite = 0;
		}
		ret = syno_mv_9235_disk_led_set(iHostNum, iWrite);

		iRead = syno_mv_9235_disk_led_get(iHostNum);
		if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
			giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		}
	}
	return ret;
}

static
int SetDiskLedStatusBy9235GPIOandAHCISGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	// first time we tried to light on disk led
	if (0 == giDiskLedControllerInit) {
		InitSynoDiskLed();
		giDiskLedControllerInit = 1;
	}

	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if (!gblDiskNumNeedTran) {
		err = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(disknum - 1, status);
	} else {
		err = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(giDiskMapTable[disknum - 1], status);
	}
END:
	return err;
}
#endif /* MY_DEF_HERE */


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

	Pin.pin = COFFEELAKE_ALARM_LED_PIN;
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
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;

	giDiskLedController = COFFEELAKE_ALL_LED_ACTIVATE_PIN;

	if (0 > syno_libata_disk_map_table_gen(giDiskMapTable)) {
		gblDiskNumNeedTran = 0;
	} else {
		gblDiskNumNeedTran = 1;
	}

	switch(GetModel())
	{
		case MODEL_DS3619xs:
			model_ops = &ds3619xs_ops;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIOandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
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
