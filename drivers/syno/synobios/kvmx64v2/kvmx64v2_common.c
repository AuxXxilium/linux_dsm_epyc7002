#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include "../rtc/rtc.h"
#include "kvmx64v2_common.h"

static struct model_ops *model_ops = NULL;

#ifdef CONFIG_SYNO_XR17V35X_SERIAL
extern int gSynoBuzzerMutePressed;
#endif

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */

/*
 * On RS10613xsp, and RS3413xsp we have no buzzer clear button anymore, so we need another way to stop redundant power buzzer
 */
int xsSetBuzzerClear(unsigned char buzzer_cleared)
{
	int ret = -1;

	return ret;
}

int xsGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    int ret = -1;

#ifdef CONFIG_SYNO_XR17V35X_SERIAL
	*buzzer_cleared = gSynoBuzzerMutePressed;
	gSynoBuzzerMutePressed = 0;
	ret = 0;
#endif
    return ret;
}

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;

	return iDutyCycle;
}

#define GPIO_POWER_GOOD	1
int Kvmx64v2RedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;

	return err;
}

static
int GetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

static
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
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
	int model = MODEL_VirtualDSM;

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

	return ret;
}

int GetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

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
	int iCPUIdx;

	if ( NULL == pCpuTemp ) {
		goto END;
	}

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /*MY_DEF_HERE*/

	if( 0 != iRet) {
		goto END;
	}

	if( 1 == pCpuTemp->blSurface) {
		for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
			pCpuTemp->cpu_temp[iCPUIdx] = (pCpuTemp->cpu_temp[iCPUIdx] * 1000 - 19682) / 1000;

			if(40 > pCpuTemp->cpu_temp[iCPUIdx]) {
				pCpuTemp->cpu_temp[iCPUIdx] = 40;
			}
		}
	}
END:
	return iRet;
}

static
int SetAlarmLed(unsigned char type)
{
	const char* cmd = NULL;

	if (type) {
		cmd = SZ_UART_ALARM_LED_BLINKING;
	}else{
		cmd = SZ_UART_ALARM_LED_OFF;
	}

	return SetUart(cmd);
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
	.get_fan_status      = GetFanStatusMircopWithGPIO,
	.set_fan_status      = SetFanStatus,
	.get_sys_temperature = GetSysTemperature,
	.get_cpu_temperature = GetCpuTemperatureI3Transfer,
	.set_cpu_fan_status  = NULL,
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

	switch(GetModel())
	{
		case MODEL_VirtualDSM:
			model_ops = &virtualdsm_ops;
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
