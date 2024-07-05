/* Copyright (c) 2000-2016 Synology Inc. All rights reserved. */

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "../i2c/i2c-linux.h"
#include "monaco_common.h"

void SYNO_ENABLE_HDD_LED(int blEnable);
void SYNO_ENABLE_PHY_LED(int blEnable);

int GetModel(void)
{
	return MODEL_DS216play;
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	cpu->core = 2;
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1500);
}

int
InitModuleType(struct synobios_ops *ops)
{
	module_t type = MODULE_T_DS216play;
	module_t *pType = NULL;

	pType = &type;

	module_type_set(pType);

	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	return 0;
}

int SetAlarmLed(unsigned char type)
{
	return 0;
}

int GetBackPlaneStatus(BACKPLANE_STATUS *pStatus)
{
	return 0;
}

int SetPhyLed(SYNO_LED ledStatus)
{
	switch(ledStatus) {
		case SYNO_LED_OFF:
			SYNO_ENABLE_PHY_LED(0);
			break;
		case SYNO_LED_ON:
			SYNO_ENABLE_PHY_LED(1);
			break;
		default:
			printk("Invalid PHY LED status [%d]\n", ledStatus);
			return -EFAULT;
	}
	return 0;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	SYNO_ENABLE_HDD_LED((SYNO_LED_ON == ledStatus)? ENABLE : DISABLE);
	return 0;
}

int model_addon_init(struct synobios_ops *ops)
{
	ops->set_phy_led = SetPhyLed;
	ops->set_disk_led = SetDiskLedStatusByI2CLedDimmer;
	SetupDiskLedMap((int[2]){0,2}, (int[2]){1,3}, 2);
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}

int ds216play_hdd_enable_gpio[2] = {112, 115};

SYNO_HWMON_SENSOR_TYPE ds216play_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

struct hwmon_sensor_list ds216play_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &ds216play_hdd_backplane_status,
};