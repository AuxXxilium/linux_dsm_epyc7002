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
#include "alpine_common.h"

/* all alpine models share same synobios_model_init/cleanup(),
   each model must implement their own model_private_init/cleanup() */
int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);

static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

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

static SYNO_HWMON_SENSOR_TYPE hdd_backplane_status_detect = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor = {{
		.sensor_name = HWMON_HDD_BP_DETECT,
	}}
};
static struct hwmon_sensor_list hdd_detect_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &hdd_backplane_status_detect,
};

static SYNO_HWMON_SENSOR_TYPE hdd_backplane_status_enable = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor = {{
		.sensor_name = HWMON_HDD_BP_ENABLE,
	}}
};
static struct hwmon_sensor_list hdd_enable_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &hdd_backplane_status_enable,
};

int PWMFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for (i = 0; i < sizeof(gPWMSpeedMapping) / sizeof(PWM_FAN_SPEED_MAPPING); ++i) {
		if (gPWMSpeedMapping[i].fanSpeed == speed) {
			iDutyCycle = gPWMSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	int iRet = -1;
	int temperature = 0;

	if (!pCPUTemp)
		goto END;
	if (syno_alpine_get_cpu_temperature(&temperature) != 0)
		goto END;

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = temperature;

	iRet = 0;
END:
	return iRet;
}

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	while (HAVE_HDD_DETECT(index)) {
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY(index)) & 0x01) << (index - 1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY()) & 0x01) << (index - 1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		index++;
	}

	index = 1;
	while (HAVE_HDD_ENABLE(index)) {
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index-1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY()) & 0x01) << (index - 1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		index++;
	}

	index = 0;
	while (hdd_backplane->sensor_num > index) {
		if (0 == strncmp(HWMON_HDD_BP_ENABLE, hdd_backplane->sensor[index].sensor_name, strlen(HWMON_HDD_BP_ENABLE))) {
			snprintf(hdd_backplane->sensor[index].value, sizeof(hdd_backplane->sensor[index].value), "%lu", hdd_enable);
		} else if (0 == strncmp(HWMON_HDD_BP_DETECT, hdd_backplane->sensor[index].sensor_name, strlen(HWMON_HDD_BP_DETECT))){
			snprintf(hdd_backplane->sensor[index].value, sizeof(hdd_backplane->sensor[index].value), "%lu", hdd_detect);
		}
		index++;
	}

	iRet = 0;
End:
	return iRet;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	return SYNO_HDD_LED_SET(disknum, status);
}

int SetDiskLedStatusV2(int disknum, SYNO_DISK_LED status)
{
	static int diskLedEnabled = 0;
	if (0 == diskLedEnabled) {
		SYNO_ENABLE_HDD_LED(1);
		diskLedEnabled = 1;
	}
	return SYNO_HDD_LED_SET(disknum, status);
}

int SetAlarmLed(unsigned char type)
{
	int iBlinking = 0;

	if (type) {
		iBlinking = 1;
	}

	return SYNO_CTRL_ALARM_LED_SET(iBlinking);
}

int GetBackPlaneStatus(BACKPLANE_STATUS *pStatus)
{
	return 0;
}

int SetPhyLed(SYNO_LED ledStatus)
{
	return 0;
}

int GetMemByte(MEMORY_BYTE *pMemory)
{
	return 0;
}

static int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	return 0;
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
	.set_disk_led         = NULL,
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
	.check_microp_id      = NULL,
	.set_microp_id        = NULL,
	.get_cpu_info         = GetCPUInfo,
	.set_aha_led          = NULL,
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	module_t* pSynoModule = NULL;

	syno_gpio_init();
#ifdef MY_DEF_HERE
	printk("Synobios %s GPIO initialized\n", syno_get_hw_version());
#endif /* MY_DEF_HERE */

	switch (GetModel()) {
		case MODEL_DS215p:
		case MODEL_DS715:
		case MODEL_DS416:
			hwmon_sensor_list = &hdd_enable_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			break;
		case MODEL_RS1219:
			hwmon_sensor_list = &hdd_detect_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			break;
	}

	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	pSynoModule = module_type_get();
	if( pSynoModule && RTC_SEIKO == pSynoModule->rtc_type ) {
		synobios_ops.get_rtc_time        = rtc_seiko_get_time;
		synobios_ops.set_rtc_time        = rtc_seiko_set_time;
		synobios_ops.get_auto_poweron    = rtc_get_auto_poweron;
		synobios_ops.set_auto_poweron    = rtc_seiko_set_auto_poweron;
		synobios_ops.init_auto_poweron   = rtc_seiko_auto_poweron_init;
		synobios_ops.uninit_auto_poweron = rtc_seiko_auto_poweron_uninit;
	}
	if( pSynoModule && RTC_MV == pSynoModule->rtc_type ) {
		synobios_ops.get_rtc_time        = NULL;
		synobios_ops.set_rtc_time        = NULL;
		synobios_ops.get_auto_poweron    = NULL;
		synobios_ops.set_auto_poweron    = NULL;
		synobios_ops.init_auto_poweron   = NULL;
		synobios_ops.uninit_auto_poweron = NULL;
	}

	*ops = &synobios_ops;
	switch (GetModel()) {
		case MODEL_DS1817:
		case MODEL_RS1219:
			synobios_ops.set_disk_led = SetDiskLedStatusV2;
			break;
		default:
			synobios_ops.set_disk_led = SetDiskLedStatus;
			break;
	}

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
	syno_gpio_cleanup();
	model_addon_cleanup(*ops);
	return 0;
}
