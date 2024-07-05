#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2012 Synology Inc. All rights reserved. */

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <asm/io.h>
#include "../mapping.h"
#include "../i2c/i2c-linux.h"
#include "../rtc/rtc.h"
#include "monaco_common.h"

int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);
unsigned long int syno_get_temperature(void);
void SYNO_ENABLE_HDD_LED(int enable);

#define MAX_ENABLE_PIN_NUM 2
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int *hdd_enable_gpio = NULL;


static PWM_FAN_SPEED_MAPPING gPWMSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 30 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 40 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 50 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 80 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 99 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

static int Uninitialize(void);

int PWMFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gPWMSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gPWMSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gPWMSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	int ret = -1;
	unsigned long int temperature = 0;
	int cpu_temperature = 0;

	if (!pCPUTemp) {
		goto END;
	}

	temperature = syno_get_temperature();
	do_div(temperature, 1000);
	cpu_temperature = (int) temperature;

	if (cpu_temperature < 0)
		cpu_temperature = 0;

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = cpu_temperature - 5;

	ret = 0;
END:
	return ret;
}

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	/*Scsi host in kernel is zero-based, disknum here is one-based,
	 *so we should minus 1 while calling the function
	 */
	if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status){
		syno_sata_mv_gpio_write( 1, disknum - 1);
	}else{
		syno_sata_mv_gpio_write( 0, disknum - 1);
	}

	err = 0;
END:
	return err;
}
#endif /* MY_DEF_HERE */

int SetPowerLedStatus(SYNO_LED status)
{
	return 0;
}

int GetMemByte( MEMORY_BYTE *pMemory )
{
	return 0;
}

static int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	return 0;
}

/*
 * Setup the global Disk LED mapping for sata driver
 */
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
{
    if(NULL == pGreenLed || NULL == pOrangeLed || 8 < iInternalDiskNum){
		return;
    }

	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = (int*)kmalloc(8 * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = (int*)kmalloc(8 * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, 8 * sizeof(int));
	memset(gpOrangeLedMap, -1, 8 * sizeof(int));

	memcpy(gpGreenLedMap, pGreenLed, iInternalDiskNum * sizeof(int));
	memcpy(gpOrangeLedMap, pOrangeLed, iInternalDiskNum * sizeof(int));

}
int SetDiskLedStatusByI2CLedDimmer(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;
	static int diskLedEnabled = 0;

	/* first time we tried to light on disk led */
	if (!diskLedEnabled && DISK_LED_OFF != iStatus) {
		SYNO_ENABLE_HDD_LED(1);
		diskLedEnabled = 1;
	}

	if (0 >= iDiskNum || 8 < iDiskNum) {
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
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 0;
	GPIO_PIN pPin;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list || NULL == hdd_enable_gpio) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	for (index = 0; index < MAX_ENABLE_PIN_NUM; index++){
		if (NULL != hdd_enable_gpio) {
			if (-1 == hdd_enable_gpio[index]) {
				break;
			}
			pPin.pin = hdd_enable_gpio[index];
			GetGpioPin(&pPin);
			hdd_enable |= (pPin.value & 0x01) << index;
		}
	}

	if (NULL != hdd_enable_gpio) {
		snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_enable);
	}

	iRet = 0;

End:
	return iRet;
}

static struct synobios_ops synobios_ops = {
	.owner                = THIS_MODULE,
	.get_brand            = GetBrand,
	.get_model            = GetModel,
	.get_rtc_time         = rtc_seiko_get_time,
	.set_rtc_time         = rtc_seiko_set_time,
	.get_fan_status       = GetFanStatusActivePulse,
	.set_fan_status       = SetFanStatus,
	.get_gpio_pin         = GetGpioPin,
	.set_gpio_pin         = SetGpioPin,
	.set_power_led        = SetPowerLedStatus,
	.set_disk_led         = SetDiskLedStatus,
	.get_sys_temperature  = NULL,
	.get_cpu_temperature  = GetCPUTemperature,
	.get_auto_poweron     = rtc_get_auto_poweron,
	.set_auto_poweron     = rtc_seiko_set_auto_poweron,
	.init_auto_poweron    = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron  = rtc_seiko_auto_poweron_uninit,
	.set_alarm_led        = SetAlarmLed,
	.get_backplane_status = GetBackPlaneStatus,
	.get_mem_byte         = GetMemByte,
	.get_buzzer_cleared   = GetBuzzerCleared,
	.set_phy_led          = NULL,
	.set_hdd_led          = SetHDDActLed,
	.module_type_init     = InitModuleType,
	.uninitialize         = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.get_cpu_info        = GetCPUInfo,
	.set_aha_led          = NULL,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
};

/* Unlike a38x and alpine, monaco init its gpio in kernel */
int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	module_t* pSynoModule = NULL;

	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	pSynoModule = module_type_get();
	if( pSynoModule && RTC_SEIKO == pSynoModule->rtc_type ) {
		synobios_ops.get_rtc_time		 = rtc_seiko_get_time;
		synobios_ops.set_rtc_time		 = rtc_seiko_set_time;
		synobios_ops.get_auto_poweron	 = rtc_get_auto_poweron;
		synobios_ops.set_auto_poweron	 = rtc_seiko_set_auto_poweron;
		synobios_ops.init_auto_poweron	 = rtc_seiko_auto_poweron_init;
		synobios_ops.uninit_auto_poweron = rtc_seiko_auto_poweron_uninit;
	}
	if( pSynoModule && RTC_MV == pSynoModule->rtc_type ) {
		synobios_ops.get_rtc_time		 = NULL;
		synobios_ops.set_rtc_time		 = NULL;
		synobios_ops.get_auto_poweron	 = NULL;
		synobios_ops.set_auto_poweron	 = NULL;
		synobios_ops.init_auto_poweron	 = NULL;
		synobios_ops.uninit_auto_poweron = NULL;
	}

	switch (GetModel()) {
		case MODEL_DS216play:
			hwmon_sensor_list = &ds216play_sensor_list;
			hdd_enable_gpio = ds216play_hdd_enable_gpio;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			break;
		default:
			hwmon_sensor_list = NULL;
			hdd_enable_gpio = NULL;
			synobios_ops.hwmon_get_backplane_status = NULL;
			break;
	}

	*ops = &synobios_ops;
	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}

	model_addon_init(*ops);
	return 0;
}

static int Uninitialize(void)
{
	if( synobios_ops.uninit_auto_poweron ) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	model_addon_cleanup(*ops);
	return 0;
}
