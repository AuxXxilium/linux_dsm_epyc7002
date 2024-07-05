#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2016 Synology Inc. All rights reserved. */

#include "synobios.h"
#include <asm/delay.h>
#include "rtd1619b_common.h"
#include "../i2c/i2c-linux.h"
#include "../rtc/rtc.h"
#include "../rtc/rtc-rtk-builtin.h"
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#include "../led/led_trigger_disk.h"
#include "syno_ttyS.h"

#if defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
extern int (*syno_get_current)(unsigned char, struct tty_struct *);
extern int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty);
extern int synobios_lock_ttyS_current(char *szCommand, char *szBuf);
#endif /* CONFIG_SYNO_TTY_MICROP_FUNCTIONS */

int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);
int syno_rtd_get_temperature(void);

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

static SYNO_HWMON_SENSOR_TYPE rtd1619b_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor = {{
		.sensor_name = HWMON_HDD_BP_DETECT,
	}, {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	}}
};

static SYNO_HWMON_SENSOR_TYPE rtd1619b_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static struct hwmon_sensor_list rtd1619b_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &rtd1619b_hdd_backplane_status,
	.current_sensor = &rtd1619b_current_status,
};
static struct hwmon_sensor_list *hwmon_sensor_list = &rtd1619b_sensor_list;

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
int (*GetMaxInternalHostNum)(void) = NULL;

int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS124:
			iMaxInternalDiskNum = 1;
			break;
		case MODEL_DS223j:
		case MODEL_DS223:
			iMaxInternalDiskNum = 2;
			break;
		case MODEL_DS423:
			iMaxInternalDiskNum = 4;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	int iRet = -1;
	int iTemp = 0;

	if (!pCPUTemp)
		goto END;

	iTemp = syno_rtd_get_temperature();

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = iTemp;

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
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index - 1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY()) & 0x01) << (index - 1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

		index++;
	}

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%lu", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%lu", hdd_enable);

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

int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
			SYNO_ENABLE_PHY_LED(1);
			break;
		case SYNO_LED_OFF:
			SYNO_ENABLE_PHY_LED(0);
			break;
		default:
			goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

extern unsigned int cpufreq_quick_get(unsigned int cpu);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	int i;

	cpu->core = 0;
	for_each_online_cpu(i) {
		cpu->core++;
	}

	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1700);
}

static
int SYNOIOGetCurrentStatusByMicroP(unsigned long* pulSysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szTTYCur[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulTTYCur = 0;

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szTTYCur, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szTTYCur, 10, &ulTTYCur);
	if (0 > iRet) {
		goto End;
	}
	// microP return value * 16.13 = System current (mA) (reference : uP #130)
	*pulSysCurrent = (ulTTYCur * 16) + (ulTTYCur * 13 / 100);

	iRet = 0;
End:
	return iRet;
}

static
int HWMONGetCurrentStatusByMicroP(struct _SYNO_HWMON_SENSOR_TYPE *SysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szCurrent[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulCurrent = 0;

	if (NULL == SysCurrent || NULL == hwmon_sensor_list) {
		printk("SysCurrent null\n");
		goto End;
	}

	memcpy(SysCurrent, hwmon_sensor_list->current_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szCurrent, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szCurrent, 10, &ulCurrent);
	if (0 > iRet) {
		goto End;
	}
	snprintf(SysCurrent->sensor[0].value, sizeof(SysCurrent->sensor[0].value), "%ld", (ulCurrent * 16) + (ulCurrent * 13 / 100));

	iRet = 0;
End:
	return iRet;
}

static struct synobios_ops synobios_ops = {
	.owner                = THIS_MODULE,
	.get_brand            = GetBrand,
	.get_model            = GetModel,
	.get_rtc_time         = rtc_pericom_get_time,
	.set_rtc_time         = rtc_pericom_set_time,
	.get_fan_status       = NULL,
	.set_fan_status       = SetFanStatus,
	.get_gpio_pin         = GetGpioPin,
	.set_gpio_pin         = SetGpioPin,
	.set_power_led        = NULL,
	.set_disk_led         = SetDiskLedStatusByTrigDiskSyno,
	.get_sys_temperature  = NULL,
	.get_cpu_temperature  = GetCPUTemperature,
	.get_auto_poweron     = rtc_get_auto_poweron,
	.set_auto_poweron     = rtc_pericom_set_auto_poweron,
	.init_auto_poweron    = rtc_pericom_auto_poweron_init,
	.uninit_auto_poweron  = rtc_pericom_auto_poweron_uninit,
	.set_alarm_led        = NULL,
	.get_backplane_status = NULL,
	.get_mem_byte         = NULL,
	.get_buzzer_cleared   = NULL,
	.set_phy_led          = NULL,
	.set_hdd_led          = NULL,
	.module_type_init     = InitModuleType,
	.uninitialize         = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led          = NULL,
	// for matching userspace usage, return 0 if button is pressed, else = 1
	.get_copy_button_status = NULL,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO,
	.hwmon_get_sys_current = HWMONGetCurrentStatusByMicroP,
	.get_sys_current     = SYNOIOGetCurrentStatusByMicroP,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	module_t* pSynoModule = NULL;

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
    // for those model that have mix ahci and 9xxx internal disk, please implement GetMaxInternalHostNum
    GetMaxInternalHostNum = NULL;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
	syno_gpio_init();
#ifdef MY_DEF_HERE
	printk("Synobios %s GPIO initialized\n", syno_get_hw_version());
#endif /* MY_DEF_HERE */

	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	pSynoModule = module_type_get();

	*ops = &synobios_ops;
	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}
#if defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
	syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_MICROP_FUNCTIONS */

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
