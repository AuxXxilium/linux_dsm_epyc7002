#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2017 Synology Inc. All rights reserved. */

#include "synobios.h"
#include <linux/rtc.h>
#include <linux/string.h>
#include <asm/delay.h>
#include "armada37xx_common.h"
#include "../i2c/i2c-linux.h"
#include "../rtc/rtc.h"

int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);

static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

SYNO_HWMON_SENSOR_TYPE a37xx_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

SYNO_HWMON_SENSOR_TYPE a37xx_thermal_sensor = {
	.type_name = HWMON_SYS_THERMAL_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "system",
	},
};

struct hwmon_sensor_list a37xx_sensor_list = {
	.thermal_sensor = &a37xx_thermal_sensor,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &a37xx_hdd_backplane_status,
};

#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
#ifdef MY_DEF_HERE
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif /* MY_DEF_HERE */

static int Uninitialize(void);

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
static SYNO_GPIO_INFO disk_led_ctrl = {
       .nr_gpio                = 1,
       .gpio_port              = {56},
       .gpio_polarity  = ACTIVE_HIGH,
};

int GetMaxInternalDiskNum(void)
#else
static int GetMaxInternalPCIE9xxxDiskNum(void)
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS219j:
		case MODEL_DS219se:
			iMaxInternalDiskNum = 2;
			break;
		case MODEL_DS119j:
			iMaxInternalDiskNum = 1;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}

static int ReadThermalData(int *Temperature)
{
	u16 data = 0;

	if (linuxI2CCharRead(0x48, (u8 *)&data, 2, -1)) {
		return -1;
	}

#if defined (__LITTLE_ENDIAN)
	data = __swab16(data);
#endif
	/* The temperature data only 9 bits */
	data = data >> 7;

	if (data >> 8) { /* bit 9 is minus sign */
		*Temperature = -1 * (0x100 - ((u8 *)&data)[1]);
	} else {
		*Temperature = data;
	}
	(*Temperature) >>= 1;

	return 0;
}

int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp)
{
	int iRet = -1;
	int iTemp = 0;

	if (!pThermalTemp)
		goto END;

	if (ReadThermalData(&iTemp) != 0)
		goto END;

	if( 1 == pThermalTemp->blSurface ) {
		iTemp += 18;
		iTemp = (iTemp<40) ? 40 : iTemp ;
	}

	pThermalTemp->temperature = iTemp;

	iRet = 0;
END:
	return iRet;
}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
#ifdef MY_DEF_HERE
static int SetSCSIHostLedStatusBy9170GPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	int iWrite = -1;

	if (DISK_LED_ORANGE_BLINK == iStatus || DISK_LED_ORANGE_SOLID == iStatus) {
		iWrite = 1;
	} else {
		iWrite = 0;
	}

	iRet = syno_mv_9170_disk_led_set((unsigned short)iHostNum, iWrite);

	return iRet;
}

int SetDiskLedStatusBy9170GPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;

	if (1 > iDiskNum || GetMaxInternalPCIE9xxxDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (!gblDiskNumNeedTran) {
		iRet = SetSCSIHostLedStatusBy9170GPIO(iDiskNum - 1, iStatus);
	} else {
		iRet = SetSCSIHostLedStatusBy9170GPIO(giDiskMapTable[iDiskNum - 1], iStatus);
	}
END:
	return iRet;

}

#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

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

	while(HAVE_HDD_DETECT(index)){
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY(index)) & 0x01) << (index-1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY()) & 0x01) << (index-1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		index++;
	}

	index = 1;

	while(HAVE_HDD_ENABLE(index)){
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index-1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY()) & 0x01 ) << (index-1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		index++;
	}

	index = 0;
	while (hdd_backplane->sensor_num > index) {
		if (0 == strncmp(HWMON_HDD_BP_ENABLE, hdd_backplane->sensor[index].sensor_name, strlen(HWMON_HDD_BP_ENABLE))) {
			snprintf(hdd_backplane->sensor[index].value, sizeof(hdd_backplane->sensor[index].value), "%ld", hdd_enable);
		} else if (0 == strncmp(HWMON_HDD_BP_DETECT, hdd_backplane->sensor[index].sensor_name, strlen(HWMON_HDD_BP_DETECT))){
			snprintf(hdd_backplane->sensor[index].value, sizeof(hdd_backplane->sensor[index].value), "%ld", hdd_detect);
		}
		index++;
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

int SetHDDActLed(SYNO_LED ledStatus)
{
	int err = -1;
	switch (ledStatus) {
		case SYNO_LED_OFF:
			SYNO_HDD_LED_SET(1, DISK_LED_OFF);
			SYNO_HDD_LED_SET(2, DISK_LED_OFF);
			break;
		case SYNO_LED_ON:
			SYNO_HDD_LED_SET(1, DISK_LED_GREEN_BLINK);
			SYNO_HDD_LED_SET(2, DISK_LED_GREEN_BLINK);
			break;
		default:
			goto ERR;
	}
	err = 0;
ERR:
	return err;
}

int HddPhyLedPwrCtrl(SYNO_LED ledStatus)
{
	int ret = -1;
	int pinValue;

	/* Custmized GPIO pin number of hdd & phy led power
	 * control for DS219j/se. We init it as 0(off) in uboot.
	 * 1: enable led power of hdd & phy(for system ready).
	 * 0: disable led power of hdd & phy(for hibernation/deep sleep).
	 */

	switch (ledStatus) {
		case SYNO_LED_OFF:
			pinValue = 0;
			break;
		case SYNO_LED_ON:
			pinValue = 1;
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			goto END;
	}
	SYNO_GPIO_WRITE(HDD_PHY_LED_PWR_GPIO, pinValue);
	ret = 0;

END:
	return ret;
}

static struct synobios_ops synobios_ops = {
	.owner                = THIS_MODULE,
	.get_brand            = GetBrand,
	.get_model            = GetModel,
	.get_rtc_time         = rtc_seiko_get_time,
	.set_rtc_time         = rtc_seiko_set_time,
	.get_fan_status       = NULL,
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
	.set_alarm_led        = NULL,
	.get_backplane_status = NULL,
	.get_mem_byte         = NULL,
	.get_buzzer_cleared   = NULL,
	.set_phy_led          = HddPhyLedPwrCtrl,
	.set_hdd_led          = NULL,
	.module_type_init     = InitModuleType,
	.uninitialize         = Uninitialize,
	.check_microp_id      = NULL,
	.set_microp_id        = NULL,
	.get_cpu_info         = GetCPUInfo,
	.set_aha_led          = NULL,
	// for matching userspace usage, return 0 if button is pressed, else = 1
	.get_copy_button_status = NULL,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
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
		case MODEL_DS120j:
		case MODEL_DS119j:
		case MODEL_DS219j:
		case MODEL_DS219se:
			hwmon_sensor_list = &a37xx_sensor_list;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensor;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			break;
		default:
			hwmon_sensor_list = NULL;
			synobios_ops.hwmon_get_sys_thermal = NULL;
			synobios_ops.hwmon_get_backplane_status = NULL;
			break;
	}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
    syno_gpio.disk_led_ctrl = &disk_led_ctrl;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	pSynoModule = module_type_get();

	*ops = &synobios_ops;
	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}

	model_addon_init(*ops);

	return 0;
}

static int Uninitialize(void)
{
	if (synobios_ops.uninit_auto_poweron) {
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
