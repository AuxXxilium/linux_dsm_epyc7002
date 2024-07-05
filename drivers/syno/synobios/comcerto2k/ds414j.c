#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2010 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <asm/io.h>
#include "../i2c/i2c-linux.h"
#include "comcerto2k_common.h"

int GetModel(void)
{
	return MODEL_DS414j;
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
#if defined(CONFIG_SMP)
	int i;

	cpu->core = 0;
	for_each_online_cpu(i) {
		cpu->core++;
	}
#else /* CONFIG_SMP */
	cpu->core = 1;
#endif
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1200);
}


int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	int FanStatus;

	if ( 1 > fanno || 3 < fanno) {
		return -EINVAL;
	}

	SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus);

	if (0 == FanStatus) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}

	return 0;
}

int 
InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = ops->get_model();
	module_t type_414j = MODULE_T_DS414jv10;
	module_t *pType = NULL;

	switch (model) {
		case MODEL_DS414j:
			pType = &type_414j;
			break;
		default:
			break;
	}

	module_type_set(pType);

	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
#ifdef MY_DEF_HERE
	return SetDiskLedStatusBySataMvGPIO(disknum, status);
#else
	return 0;
#endif
}

int SetPowerLedStatus(SYNO_LED status)
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
	return 0;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	return 0;
}

int model_addon_init(struct synobios_ops *ops)
{

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{

	return 0;
}

int ds414j_hdd_detect_gpio[4] = {39, 38, 37, 36};
int ds414j_hdd_enable_gpio[4] = {5, 4, 3, 2};

SYNO_HWMON_SENSOR_TYPE ds414j_thermal_sensor = {
		.type_name = HWMON_SYS_THERMAL_NAME,
		.sensor_num = 1,
		.sensor[0] = {
		.sensor_name = "system",
	},
};

SYNO_HWMON_SENSOR_TYPE ds414j_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
    },
};

struct hwmon_sensor_list ds414j_sensor_list = {
	.thermal_sensor = &ds414j_thermal_sensor,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &ds414j_hdd_backplane_status,
};


