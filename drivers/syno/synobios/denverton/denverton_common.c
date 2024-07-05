#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2016 Synology Inc. All rights reserved.

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

#include "denverton_common.h"

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif

static int diskLedEnabled = 0;

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

// 14: GPIO_28 (NCSI_RXD0)
// 15: GPIO_29 (NCSI_CLK_IN)
// 16: GPIO_30 (NCSI_RXD1)
// 17: GPIO_31 (NCSI_CRS_DV)
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 4,
	.gpio_port		= {14, 15, 16, 17},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 98: GPIO_8 (GPIO_8)
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {98},
	.gpio_polarity	= ACTIVE_HIGH,
};
// GPIO_32 (NCSI_ARB_IN)
// GPIO_33 (NCSI_TX_EN)
static SYNO_GPIO_INFO redundant_power_detect = {
	.nr_gpio		= 2,
	.gpio_port		= {18, 19},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.redundant_power_detect         = &redundant_power_detect;
}

void syno_gpio_cleanup(void)
{
	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.redundant_power_detect         = NULL;
}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
int (*GetMaxInternalHostNum)(void) = NULL;
#else
static
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DVA3219:
		case MODEL_RS820p:
		case MODEL_RS820rpp:
		case MODEL_DVA3221:
			iMaxInternalDiskNum = 4;
			break;
		case MODEL_DS1618p:
			iMaxInternalDiskNum = 6;
			break;
		case MODEL_DS1819p:
		case MODEL_RS1220p:
		case MODEL_RS1220rpp:
			iMaxInternalDiskNum = 8;
			break;
		case MODEL_RS2418p:
		case MODEL_RS2418rpp:
		case MODEL_DS2419p:
		case MODEL_DS2419pII:
			iMaxInternalDiskNum = 12;
			break;
		case MODEL_RS2818rpp:
			iMaxInternalDiskNum = 16;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}

static
int SetFanStatusMircopWithGPIO(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;
	int iFanDuty = -1;
	char szUartCmd[5] = {0};

	if( status == FAN_STATUS_STOP ) {
		speed = FAN_SPEED_STOP;
	}

	if( FAN_SPEED_PWM_FORMAT_SHIFT <= (int)speed ) {
		/* PWM format is only for bandon and QC test */
		iFanDuty = FAN_SPEED_SHIFT_DUTY_GET((int)speed);
		/* set fan speed hz */
		if(0 < FAN_SPEED_SHIFT_HZ_GET((int)speed)) {
			snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_FREQUENCY, FAN_SPEED_SHIFT_HZ_GET((int)speed));
			if( 0 > SetUart(szUartCmd) ) {
				goto END;
			}
		}
	} else {
		if( 0 > (iFanDuty = model_ops->x86_fan_speed_mapping(speed)) ) {
			printk("No matched fan speed!\n");
			goto END;
		}
	}

	/* set fan speed duty cycle  */
	snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_DUTY_CYCLE, iFanDuty);
	if( 0 > SetUart(szUartCmd) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

int GetModel(void)
{
	int model = MODEL_INVALID;

	if ( !strncmp(gszSynoHWVersion, HW_RS2418rpp, strlen(HW_RS2418rpp) ) ) {
		model = MODEL_RS2418rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2418p, strlen(HW_RS2418p) ) ) {
		model = MODEL_RS2418p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1618p, strlen(HW_DS1618p) ) ) {
		model = MODEL_DS1618p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2818rpp, strlen(HW_RS2818rpp) ) ) {
		model = MODEL_RS2818rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS2419p, strlen(HW_DS2419p) ) ) {
		model = MODEL_DS2419p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1819p, strlen(HW_DS1819p) ) ) {
		model = MODEL_DS1819p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DVA3219, strlen(HW_DVA3219) ) ) {
		model = MODEL_DVA3219;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS820p, strlen(HW_RS820p) ) ) {
		model = MODEL_RS820p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS820rpp, strlen(HW_RS820rpp) ) ) {
		model = MODEL_RS820rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS1220p, strlen(HW_RS1220p) ) ) {
		model = MODEL_RS1220p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS1220rpp, strlen(HW_RS1220rpp) ) ) {
		model = MODEL_RS1220rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_DVA3221, strlen(HW_DVA3221) ) ) {
		model = MODEL_DVA3221;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS2419pII, strlen(HW_DS2419pII) ) ) {
		model = MODEL_DS2419pII;
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

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
/**
 *	For consistency of lighting disk led, we need a gpio pin to control cpld behavior
 *	Which means pull a gpio to let disk led be able to light on
 */
static
void SetHDDLedControl(int blEnable)
{
	GPIO_PIN pin;

	pin.pin = DENVERTON_DISK_LED_ACTIVATE_PIN;
	if (blEnable) {
		pin.value = 1;
	} else {
		pin.value = 0;
	}
	SetGpioPin(&pin);
}

#ifdef CONFIG_SYNO_LEDS_TRIGGER
/*
 * Setup the global Disk LED mapping for sata driver
 */
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
{
    if(NULL == pGreenLed || NULL == pOrangeLed || LP3943_NUM_MAX < iInternalDiskNum){
		return;
    }

	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = (int*)kmalloc(LP3943_NUM_MAX * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = (int*)kmalloc(LP3943_NUM_MAX * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, LP3943_NUM_MAX * sizeof(int));
	memset(gpOrangeLedMap, -1, LP3943_NUM_MAX * sizeof(int));

	memcpy(gpGreenLedMap, pGreenLed, iInternalDiskNum * sizeof(int));
	memcpy(gpOrangeLedMap, pOrangeLed, iInternalDiskNum * sizeof(int));
}

static int SetDiskLedStatusByI2CLedDimmer(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;

	if (!diskLedEnabled) {
		SetHDDLedControl(1);
		diskLedEnabled = 1;
	}

	if (0 >= iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
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

#ifdef MY_DEF_HERE
static int SYNOSetDiskLedStatusByI2CLedDimmer(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusByI2CLedDimmer(iHostNum + 1, iStatus);
}
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER

#ifdef MY_DEF_HERE
static
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	int iWrite = -1;

	/* first time we tried to light on disk led */
	if (!diskLedEnabled && DISK_LED_OFF != iStatus) {
		SetHDDLedControl(1);
		diskLedEnabled = 1;
	}

	if (DISK_LED_ORANGE_BLINK == iStatus || DISK_LED_ORANGE_SOLID == iStatus) {
		iWrite = 1;
	} else {
		iWrite = 0;
	}

	iRet = syno_mv_9235_disk_led_set(iHostNum, iWrite);

	return iRet;
}

static
int SetDiskLedStatusBy9235GPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;

	if (!diskLedEnabled) {
		SetHDDLedControl(1);
		diskLedEnabled = 1;
	}

	if (1 > iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (!gblDiskNumNeedTran) {
		iRet = SetSCSIHostLedStatusBy9235GPIO(iDiskNum - 1, iStatus);
	} else {
		iRet = SetSCSIHostLedStatusBy9235GPIO(giDiskMapTable[iDiskNum - 1], iStatus);
	}
END:
	return iRet;

}
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx = 0;

	if (NULL == pCpuTemp) {
		goto END;
	}

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /*MY_DEF_HERE*/

	if(0 != iRet) {
		goto END;
	}

END:
	return iRet;
}

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	int i = 0;
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

	for (i = 0; i < hdd_backplane->sensor_num; i++) {
		if (0 == strncmp(hdd_backplane->sensor[i].sensor_name, HWMON_HDD_BP_DETECT, strlen(HWMON_HDD_BP_DETECT))) {
			snprintf(hdd_backplane->sensor[i].value, sizeof(hdd_backplane->sensor[i].value), "%ld", hdd_detect);
		} else if (0 == strncmp(hdd_backplane->sensor[i].sensor_name, HWMON_HDD_BP_ENABLE, strlen(HWMON_HDD_BP_ENABLE))) {
			snprintf(hdd_backplane->sensor[i].value, sizeof(hdd_backplane->sensor[i].value), "%ld", hdd_enable);
		}
	}

	iRet = 0;

End:
	return iRet;
}

/*
 * For GPIO alert Led, there is no blinking
 */
static
int SetAlarmLedByGPIO(unsigned char type)
{
	GPIO_PIN pin;
	int iRet = -1;

	if (type) {
		pin.value = 1;
	}else{
		pin.value = 0;
	}
	pin.pin = DENVERTON_ALERT_PIN;
	SetGpioPin(&pin);

	iRet = 0;
	return iRet;
}

int DenvertonGetBuzzerCleared(unsigned char *buzzer_cleared)
{
	GPIO_PIN Pin;
	int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = DENVERTON_BUZZER_OFF_BUTTON_PIN;
	if ( 0 > GetGpioPin( &Pin ) ) {
		goto End;
	}

	if ( 0 == Pin.value ) {
		*buzzer_cleared = 1;
	}

	ret = 0;
End:
	return ret;
}

int SetRPBuzzerClear(unsigned char buzzer_cleared)
{
	GPIO_PIN Pin;
	int ret = -1;

	Pin.pin = DENVERTON_BUZZER_CTRL_PIN;
	Pin.value = buzzer_cleared;
	if (0 > SetGpioPin(&Pin)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
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
	/* cpufreq will get wrong value while booting for DNV platform
	,so use cpu_khz which got value from CPU MSR dirctley*/
	unsigned int freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

static int SetHddActLed(SYNO_LED ledStatus)
{
	if (SYNO_LED_OFF == ledStatus) {
		SYNO_ENABLE_HDD_LED(0);
	} else {
		SYNO_ENABLE_HDD_LED(1);
	}

	return 0;
}

static int SetPowerLedStatus(SYNO_LED ledStatus)
{
	char szCommand[5] = {0};
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > SetUart(szCommand)) {
		goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

#define GPIO_POWER_GOOD	1
int DenvertonRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	GPIO_PIN pinPower1;
	GPIO_PIN pinPower2;
	int err = -1;

	pinPower1.pin = syno_gpio.redundant_power_detect->gpio_port[0];
	pinPower2.pin = syno_gpio.redundant_power_detect->gpio_port[1];
	if (0 > GetGpioPin(&pinPower1) || 0 > GetGpioPin(&pinPower2)) {
		goto END;
	}


	if (pinPower1.value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	} else {
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (pinPower2.value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	} else {
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

END:
	return err;
}

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_bandon_get_time,
	.set_rtc_time        = rtc_bandon_set_time,
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_bandon_set_auto_poweron,
	.get_fan_status      = GetFanStatusActivePulse,
	.set_fan_status      = SetFanStatusMircopWithGPIO,
	.get_sys_temperature = NULL,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = NULL,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLedByGPIO,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.set_buzzer_clear    = SetBuzzerClear,
	.get_power_status    = GetPowerStatus,
	.uninitialize        = Uninitialize,
	.check_microp_id     = NULL,
	.set_microp_id       = NULL,
	.get_cpu_info        = GetCPUInfo,
	.set_hdd_led	     = SetHddActLed,
	.set_aha_led         = NULL,
	.set_power_led       = SetPowerLedStatus,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
    // for those model that have mix ahci and 9xxx internal disk, please implement GetMaxInternalHostNum
    GetMaxInternalHostNum = NULL;
#endif
	syno_gpio_init();
#ifdef MY_DEF_HERE
	if (0 > syno_libata_disk_map_table_gen(giDiskMapTable)) {
		gblDiskNumNeedTran = 0;
	} else {
		gblDiskNumNeedTran = 1;
	}
#endif /* MY_DEF_HERE */

	switch (GetModel()) {
		case MODEL_RS2418rpp:
			model_ops = &rs2418rpp_ops;
			hwmon_sensor_list = &rs2418rpp_sensor_list;
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
			break;
		case MODEL_RS2418p:
			model_ops = &rs2418p_ops;
			hwmon_sensor_list = &rs2418p_sensor_list;
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS1618p:
			model_ops = &ds1618p_ops;
			hwmon_sensor_list = &ds1618p_sensor_list;
			syno_gpio.fan_fail->nr_gpio = 2;
			syno_gpio.fan_fail->gpio_port[0] = 16;
			syno_gpio.fan_fail->gpio_port[1] = 14;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
#else
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
			SetupDiskLedMap((int[6]){0,2,4,6,8,10}, (int[6]){1,3,5,7,9,11}, 6);
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#else
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
			syno_ahci_disk_led_enable(5, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS2818rpp:
			model_ops = &rs2818rpp_ops;
			hwmon_sensor_list = &rs2818rpp_sensor_list;
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS2419p:
			model_ops = &ds2419p_ops;
			hwmon_sensor_list = &ds2419p_sensor_list;
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS1819p:
			model_ops = &ds1819p_ops;
			hwmon_sensor_list = &ds1819p_sensor_list;
			syno_gpio.fan_fail->nr_gpio = 2;
			syno_gpio.fan_fail->gpio_port[0] = 14;
			syno_gpio.fan_fail->gpio_port[1] = 16;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
#else
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#else
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
			syno_ahci_disk_led_enable(5, 1);
			syno_ahci_disk_led_enable(6, 1);
			syno_ahci_disk_led_enable(7, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
                        break;
		case MODEL_DVA3219:
			model_ops = &dva3219_ops;
			hwmon_sensor_list = &dva3219_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			syno_gpio.fan_fail->nr_gpio = 1;
			syno_gpio.fan_fail->gpio_port[0] = 14;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS820p:
			model_ops = &rs820p_ops;
			hwmon_sensor_list = &rs820p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			syno_gpio.fan_fail->nr_gpio = 3;
			syno_gpio.fan_fail->gpio_port[0] = 14;
			syno_gpio.fan_fail->gpio_port[1] = 16;
			syno_gpio.fan_fail->gpio_port[2] = 15;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS820rpp:
			model_ops = &rs820rpp_ops;
			hwmon_sensor_list = &rs820rpp_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			syno_gpio.fan_fail->nr_gpio = 2;
			syno_gpio.fan_fail->gpio_port[0] = 16;
			syno_gpio.fan_fail->gpio_port[1] = 15;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS1220p:
			model_ops = &rs1220p_ops;
			syno_gpio.fan_fail->nr_gpio = 2;
			syno_gpio.fan_fail->gpio_port[0] = 14;
			syno_gpio.fan_fail->gpio_port[1] = 16;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
			syno_ahci_disk_led_enable(5, 1);
			syno_ahci_disk_led_enable(6, 1);
			syno_ahci_disk_led_enable(7, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS1220rpp:
			model_ops = &rs1220rpp_ops;
			syno_gpio.fan_fail->nr_gpio = 2;
			syno_gpio.fan_fail->gpio_port[0] = 16;
			syno_gpio.fan_fail->gpio_port[1] = 14;
			syno_gpio.redundant_power_detect->gpio_port[0] = 87;
			syno_gpio.redundant_power_detect->gpio_port[1] = 140;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
			syno_ahci_disk_led_enable(5, 1);
			syno_ahci_disk_led_enable(6, 1);
			syno_ahci_disk_led_enable(7, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_DVA3221:
			model_ops = &dva3221_ops;
			hwmon_sensor_list = &dva3221_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			syno_gpio.fan_fail->nr_gpio = 1;
			syno_gpio.fan_fail->gpio_port[0] = 14;
			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#else
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_DS2419pII:
			model_ops = &ds2419pII_ops;
			hwmon_sensor_list = &ds2419pII_sensor_list;
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
			break;
		default :
			break;
	}

	if (synobios_ops.init_auto_poweron) {
		synobios_ops.init_auto_poweron();
	}

	return 0;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;

	if( synobios_ops.get_rtc_time ) {
		synobios_ops.get_rtc_time(&rtc_time_pkt);
	}

	if( synobios_ops.uninit_auto_poweron ) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	syno_gpio_cleanup();

	if ( model_ops->x86_gpio_cleanup ) {
		model_ops->x86_gpio_cleanup();
	}

	return 0;
}
