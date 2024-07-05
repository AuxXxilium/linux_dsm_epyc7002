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
#include "armada_common.h"
#include "../mapping.h"
#include "../i2c/i2c-linux.h"
#include "../rtc/rtc.h"
#include "syno_ttyS.h"

/* all armada370 models share same synobios_model_init/cleanup(),
   each model must implement their own model_private_init/cleanup() */
int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);

#define MAX_ENABLE_PIN_NUM 4
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int *hdd_enable_gpio = NULL;

int armada_hdd_enable_gpio[4] = {-1, -1, -1, -1};

SYNO_HWMON_SENSOR_TYPE armada_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

struct hwmon_sensor_list armada_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &armada_hdd_backplane_status,
};


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

int GetFanStatusActiveLow(int fanno, FAN_STATUS *pStatus)
{
	int FanStatus;

	if (1 > fanno || 3 < fanno) {
		return -EINVAL;
	}

	if (NULL == pStatus) {
		return -EINVAL;
	}

	SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus);
	if (FanStatus) {
		*pStatus = FAN_STATUS_RUNNING;
	} else {
		*pStatus = FAN_STATUS_STOP;
	}

	return 0;
}

int GetFanStatusActivePulse(int fanno, FAN_STATUS *pStatus)
{
    int FanStatus;
	int rgcVolt[2] = {0, 0};

	if ( 1 > fanno || 3 < fanno) {
		return -EINVAL;
	}

	do {
		SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus);
		rgcVolt[(int)FanStatus] ++;
		if (rgcVolt[0] && rgcVolt[1]) {
			break; 
		}       
		udelay(300);
	} while ( (rgcVolt[0] + rgcVolt[1]) < 200 );

	if ((rgcVolt[0] == 0) || (rgcVolt[1] == 0) ) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}

	return 0;
}

static int SetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 0 != SYNO_ARMADA_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 1) ) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

static int GetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 0 != SYNO_ARMADA_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 0) ) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

#ifdef MY_DEF_HERE
extern void syno_sata_mv_gpio_write(u8 blFaulty, const unsigned short hostnum);
/*FIXME - Too brutal and directly, should separate into levels*/
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
	char szCommand[5] = {0};
	int err = -1;

        switch(status){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > synobios_lock_ttyS_write(szCommand)) {
		goto ERR;
	}

	err = 0;
ERR:
	return err;
}

static int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	int value;
	int model = GetModel();

	if (model != MODEL_RS214) {
		goto END;
	}

	SYNO_CTRL_BUZZER_CLEARED_GET(&value);
	if(value) {
		*buzzer_cleared = 1;
	} else {
		*buzzer_cleared = 0;
	}

END:
	return 0;
}

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	int	ret = -1;

	if (!pCPUTemp) {
		goto END;
	}

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = axptemp_read_temp() - 13;

	ret = 0;
END:
	return ret;
}

int EnablePhyLed(SYNO_LED ledStatus)
{
	int iPhyAddr;
	unsigned short uiRegValue;
	int err = -1;

	iPhyAddr = mvBoardPhyAddrGet(PHY_PORTNUM);

	if (-1 == iPhyAddr) {
		goto ERR;
	}

	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x16, 0x3); /*set to page 3*/
	mvEthPhyRegRead((unsigned int)iPhyAddr, 0x10, &uiRegValue);

	switch(ledStatus){
		/*Set Phy led[1] of led[1-2]*/
		case SYNO_LED_ON:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0001;
			break;
		case SYNO_LED_OFF:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0008;
			break;
		default:
			goto ERR;
	}

	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x10, uiRegValue);
	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x16, 0x0); /*set to page 0*/

	err = 0;

ERR:
	return err;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	int err = -1;
	switch(ledStatus) {
		case SYNO_LED_OFF:
			SYNO_SOC_HDD_LED_SET(1, DISK_LED_OFF);
			SYNO_SOC_HDD_LED_SET(2, DISK_LED_OFF);
			break;
		case SYNO_LED_ON:
			SYNO_SOC_HDD_LED_SET(1, DISK_LED_GREEN_BLINK);
			SYNO_SOC_HDD_LED_SET(2, DISK_LED_GREEN_BLINK);
			break;
		default:
			goto ERR;
	}
	err = 0;
ERR:
	return err;
}

int set_disk_led_one(SYNO_LED ledStatus)
{
	int err = -1;
	switch(ledStatus) {
		case SYNO_LED_OFF:
			SYNO_SOC_HDD_LED_SET(1, DISK_LED_OFF);
			break;
		case SYNO_LED_ON:
			SYNO_SOC_HDD_LED_SET(1, DISK_LED_GREEN_BLINK);
			break;
		default:
			goto ERR;
	}
	err = 0;
ERR:
	return err;
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
	.set_alarm_led        = NULL,
	.get_backplane_status = NULL,
	.get_mem_byte         = NULL,
	.get_buzzer_cleared   = GetBuzzerCleared,
	.set_phy_led          = EnablePhyLed,
	.set_hdd_led          = SetHDDActLed,
	.module_type_init     = InitModuleType,
	.uninitialize         = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.get_cpu_info		 = GetCPUInfo,
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

	synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
	hwmon_sensor_list = &armada_sensor_list;
	switch (GetModel()) {
		case MODEL_DS114:
		case MODEL_DS115:
		case MODEL_DS115j:
		case MODEL_DS213j:
		case MODEL_DS214:
		case MODEL_DS214p:
		case MODEL_DS214se:
		case MODEL_DS215j:
		case MODEL_DS216se:
		case MODEL_DS414:
		case MODEL_RS214:
		case MODEL_RS814:
		case MODEL_RS815:
			hdd_enable_gpio = armada_hdd_enable_gpio;
			break;
		default:
			hwmon_sensor_list = NULL;
			hdd_enable_gpio = NULL;
			synobios_ops.hwmon_get_backplane_status = NULL;
			break;
	}

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
		synobios_ops.get_rtc_time		 = rtc_mv_get_time;
		synobios_ops.set_rtc_time		 = rtc_mv_set_time;
		switch(GetModel())
		{
		case MODEL_DS414:
		case MODEL_RS814:
		case MODEL_DS215j:
		case MODEL_DS115:
		case MODEL_RS815:
			synobios_ops.init_auto_poweron	 = rtc_mv_auto_poweron_clean;
			synobios_ops.get_auto_poweron	 = rtc_get_auto_poweron;
			synobios_ops.set_auto_poweron	 = rtc_mv_set_auto_poweron;
			break;
		default:
			synobios_ops.init_auto_poweron	 = NULL;
			synobios_ops.get_auto_poweron	 = NULL;
			synobios_ops.set_auto_poweron	 = NULL;
			break;
		}
		synobios_ops.uninit_auto_poweron = rtc_mv_auto_poweron_clean;
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

