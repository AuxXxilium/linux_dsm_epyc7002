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
#include "comcerto2k_common.h"

/* all armada370 models share same synobios_model_init/cleanup(),
   each model must implement their own model_private_init/cleanup() */
int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);

#define MAX_ENABLE_PIN_NUM 4
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int *hdd_enable_gpio = NULL;
static int *hdd_detect_gpio = NULL;


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

int GetBrand(void)
{
	return BRAND_SYNOLOGY;
}

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

int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp)
{
	u16 data = 0;

	if (linuxI2CCharRead(0x48, (u8 *)&data, 2, 0)) {
		return -1;
	}

	/* The temperature data only 9 bits */
	data = data >> 7;

	if (data >> 8) { /* bit 9 is minus sign */
		pThermalTemp->temperature = -1 * (0x100 - ((u8 *)&data)[1]);
	} else {
		pThermalTemp->temperature = data;
	}
	(pThermalTemp->temperature) >>= 1;

	return 0;
}

static int SetGpioPin(GPIO_PIN *pPin)
{
	int iRet = -1;

	if(NULL == pPin) {
		goto END;
	}

	if(0 != SYNO_COMCERTO2K_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 1)) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

static int GetGpioPin( GPIO_PIN *pPin )
{
	int iRet = -1;

	if(NULL == pPin) {
		goto END;
	}

	if(0 != SYNO_COMCERTO2K_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 0)) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
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


int GetMemByte( MEMORY_BYTE *pMemory )
{
	return 0;
}

static int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	return 0;
}

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 0;
	GPIO_PIN pPin;
	unsigned long hdd_enable = 0;
	unsigned long hdd_detect = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list || (NULL == hdd_enable_gpio && NULL == hdd_detect_gpio)) {
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
		if (NULL != hdd_detect_gpio) {
			if (-1 == hdd_detect_gpio[index]) {
				break;
			}
			pPin.pin = hdd_detect_gpio[index];
			GetGpioPin(&pPin);
			hdd_detect |= (pPin.value & 0x01) << index;
		}
	}

	if (NULL != hdd_detect_gpio) {
		snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	}
	if (NULL != hdd_enable_gpio) {
		snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%ld", hdd_enable);
	}

	iRet = 0;

End:
	return iRet;
}

static
int HWMONGetThermalSensor(struct _SYNO_HWMON_SENSOR_TYPE *SysThermal)
{
	int iRet = -1;
	struct _SynoThermalTemp ThermalTemp;

	if (NULL == SysThermal) {
		return -ENODEV;
	}

	memcpy(SysThermal, hwmon_sensor_list->thermal_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	if (0 != GetSysTemperature(&ThermalTemp)) {
		goto End;
	}

	snprintf(SysThermal->sensor[0].value, sizeof(SysThermal->sensor[0].value), "%d", ThermalTemp.temperature);

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
	.get_fan_status       = GetFanStatus,
	.set_fan_status       = SetFanStatus,
	.get_gpio_pin         = GetGpioPin,
	.set_gpio_pin         = SetGpioPin,
	.set_power_led        = SetPowerLedStatus,
	.set_disk_led         = SetDiskLedStatus,
	.get_sys_temperature  = GetSysTemperature,
	.get_cpu_temperature  = NULL,
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
		case MODEL_DS414j:
			hwmon_sensor_list = &ds414j_sensor_list;
			hdd_enable_gpio = ds414j_hdd_enable_gpio;
			hdd_detect_gpio = ds414j_hdd_detect_gpio;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensor;
			break;
		default:
			hwmon_sensor_list = NULL;
			hdd_enable_gpio = NULL;
			hdd_detect_gpio = NULL;
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
