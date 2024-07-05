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
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include<linux/slab.h>
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#include "../rtc/rtc.h"

#include "avoton_common.h"

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int giAlertGPIOPin = -1;
static int* hdd_detect_gpio = NULL;
static int* hdd_enable_gpio = NULL;

#ifdef MY_DEF_HERE
extern void (*funcSynoNicLedCtrl)(int iEnable);
#endif //MY_DEF_HERE

static int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS415p:
			iMaxInternalDiskNum = 4;
			break;
		case MODEL_DS2415p:
		case MODEL_RS2416p:
		case MODEL_RS2416rpp:
			iMaxInternalDiskNum = 12;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}

static
int GetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	int gpio_fan_map[] = {11, 12, 18, 34};
	int fanNum = sizeof(gpio_fan_map)/sizeof(gpio_fan_map[0]);
	GPIO_PIN gPioPin;
	int rgcVolt[2] = {0, 0};
	int iFanIndex = 0;

	if (pStatus == NULL) {
		return -EINVAL;
	}

	if (fanno > fanNum) {
		return -EINVAL;
	}

	/* RS818RP+ has only two fans, start from index 1 */
	if (MODEL_RS818rpp == GetModel()) {
		iFanIndex = 1;
	}

	gPioPin.pin = gpio_fan_map[fanno + iFanIndex - 1];

	do {
		GetGpioPin(&gPioPin);
		if (!gPioPin.value) {
			rgcVolt[0]++;
		} else {
			rgcVolt[1]++;
		}

		if (rgcVolt[0] && rgcVolt[1]) {
			break;
		}
		udelay(300);
	} while ((rgcVolt[0] + rgcVolt[1]) < 200);

	if (rgcVolt[0] == 0) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}
	return 0;
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
	int model = MODEL_DS2415p;

	if ( !strncmp(gszSynoHWVersion, HW_DS2415p, strlen(HW_DS2415p) ) ) {
		model = MODEL_DS2415p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS415p, strlen(HW_DS415p) ) ) {
		model = MODEL_DS415p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1815p, strlen(HW_DS1815p) ) ) {
		model = MODEL_DS1815p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1515p, strlen(HW_DS1515p) ) ) {
		model = MODEL_DS1515p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS815p, strlen(HW_RS815p) ) ) {
		model = MODEL_RS815p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS815rpp, strlen(HW_RS815rpp) ) ) {
		model = MODEL_RS815rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2416p, strlen(HW_RS2416p) ) ) {
		model = MODEL_RS2416p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2416rpp, strlen(HW_RS2416rpp) ) ) {
		model = MODEL_RS2416rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1616p, strlen(HW_DS1616p) ) ) {
		model = MODEL_DS1616p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1817p, strlen(HW_DS1817p) ) ) {
		model = MODEL_DS1817p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1517p, strlen(HW_DS1517p) ) ) {
		model = MODEL_DS1517p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS818p, strlen(HW_RS818p) ) ) {
		model = MODEL_RS818p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS818rpp, strlen(HW_RS818rpp) ) ) {
		model = MODEL_RS818rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS1219p, strlen(HW_RS1219p) ) ) {
		model = MODEL_RS1219p;
	}
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

int SetGpioPin(GPIO_PIN *pPin)
{
	int ret = -1;

	if (NULL == pPin) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 1)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

int GetGpioPin(GPIO_PIN *pPin)
{
	int ret = -1;

	if (NULL == pPin) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 0)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

static int giAllLedController = -1;
/**
 *	For consistency of led, we need a gpio pin to control cpld behavior
 *	Which means pull a gpio to let ALL LED (exclude POWER LED) be able to light on
 */
static
void SetAllLedControl(int blEnable)
{
	GPIO_PIN pin;

	// the disk led enable controller is not assigned, just return
	if (0 > giAllLedController) {
		return;
	}
	pin.pin = giAllLedController;
	if (blEnable) {
		pin.value = 1;
	} else {
		pin.value = 0;
	}
	SetGpioPin(&pin);
}


static int giDiskLedController = -1;
/**
 *	For consistency of lighting disk led, we need a gpio pin to control cpld behavior
 *	Which means pull a gpio to let disk led be able to light on
 */
static
void SetHDDLedControl(int blEnable)
{
	GPIO_PIN pin;

	// the disk led enable controller is not assigned, just return
	if (0 > giDiskLedController) {
		return;
	}
	pin.pin = giDiskLedController;
	if (blEnable) {
		pin.value = 1;
	} else {
		pin.value = 0;
	}
	SetGpioPin(&pin);
}

#ifdef CONFIG_SYNO_LEDS_TRIGGER
static
int SetDiskLedStatusByI2CLedDimmer(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;
	static int diskLedEnabled = 0;

	switch (GetModel()) {
		case MODEL_DS1517p:
		case MODEL_DS1817p:
		case MODEL_RS818p:
		case MODEL_RS818rpp:
		case MODEL_RS1219p:
			if (!diskLedEnabled) {
				/* Above models gpio 49 control "ALL LED(exclude Power LED)"
				 * If no disk, DISK_LED_OFF would not enable gpio 49 */
				SetAllLedControl(1);
				diskLedEnabled = 1;
			}
			break;
		default:
			/* first time we tried to light on disk led */
			if (!diskLedEnabled && DISK_LED_OFF != iStatus) {
				SetHDDLedControl(1);
				diskLedEnabled = 1;
			}
			break;
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
int SYNOSetDiskLedStatusByI2CLedDimmer(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusByI2CLedDimmer(iHostNum + 1, iStatus);
}
#endif // CONFIG_SYNO_LEDS_TRIGGER

#ifdef MY_DEF_HERE
static
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	static int diskLedEnabled = 0;
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

	/*
	 * Count the cpu surface termperature
	 * Coretemp is lower than heatsink about 9 celsius
	 * n += 9
	 * n < 30 show 30
	 */
	if (1 == pCpuTemp->blSurface) {
		for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
			if (MODEL_RS818rpp != GetModel() && MODEL_RS818p != GetModel() && MODEL_RS1219p != GetModel()) {
				pCpuTemp->cpu_temp[iCPUIdx] += 9;
			}
			if (pCpuTemp->cpu_temp[iCPUIdx] < 30) {
				pCpuTemp->cpu_temp[iCPUIdx] = 30;
			}
		}
	}

END:
	return iRet;
}

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int i = 0;
	GPIO_PIN Pin;
	unsigned long hdd_detect_status = 0;
	unsigned long hdd_enable_status = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list || (NULL == hdd_enable_gpio && NULL == hdd_detect_gpio)) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));
	
	for (i = 0; i < GetMaxInternalDiskNum(); i++) {
		Pin.pin = hdd_enable_gpio[i];
		if (0 > GetGpioPin(&Pin)) {
			goto End;
		}
		hdd_enable_status |= (Pin.value & 0x01) << i;

		Pin.pin = hdd_detect_gpio[i];
		if (0 > GetGpioPin(&Pin)) {
			goto End;
		}
		hdd_detect_status |= ((~Pin.value) & 0x01) << i;
	}

	for (i = 0; i < hdd_backplane->sensor_num; i++) {
		if (0 == strncmp(hdd_backplane->sensor[i].sensor_name, HWMON_HDD_BP_DETECT, strlen(HWMON_HDD_BP_DETECT))) {
			snprintf(hdd_backplane->sensor[i].value, sizeof(hdd_backplane->sensor[i].value), "%ld", hdd_detect_status);
		} else if (0 == strncmp(hdd_backplane->sensor[i].sensor_name, HWMON_HDD_BP_ENABLE, strlen(HWMON_HDD_BP_ENABLE))) {
			snprintf(hdd_backplane->sensor[i].value, sizeof(hdd_backplane->sensor[i].value), "%ld", hdd_enable_status);
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

	if (0 > giAlertGPIOPin) {
		goto END;
	}

	if (type) {
		pin.value = 1;
	}else{
		pin.value = 0;
	}
	pin.pin = giAlertGPIOPin;
	SetGpioPin(&pin);

	iRet = 0;

END:
	return iRet;
}

int AvotonGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    GPIO_PIN Pin;
    int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = AVOTON_BUZZER_OFF_PIN;
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

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus) {
		case SYNO_LED_ON:
		case SYNO_LED_OFF:
#ifdef MY_DEF_HERE
			if (funcSynoNicLedCtrl) {
				funcSynoNicLedCtrl(ledStatus);
			}
#endif
			break;
		default:
			goto ERR;
	}
	iError = 0;

ERR:
	return iError;
}

static int SetHddActLed(SYNO_LED ledStatus)
{
	if (SYNO_LED_OFF == ledStatus) {
		SetHDDLedControl(0);
	} else {
		SetHDDLedControl(1);
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

#ifdef CONFIG_SYNO_LEDS_TRIGGER
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
#endif /* CONFIG_SYNO_LEDS_TRIGGER */

#define GPIO_POWER_GOOD	1
int AvotonRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;

	if (0 != syno_pch_lpc_gpio_pin(AVOTON_POWER1_PIN , &power1_Value, 0)) {
		goto FAIL;
	}

	if (0 != syno_pch_lpc_gpio_pin(AVOTON_POWER2_PIN , &power2_Value, 0)) {
		goto FAIL;
	}

	if (power1_Value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	} else {
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2_Value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	} else {
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
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
	.set_fan_status      = SetFanStatusMircopWithGPIO,
	.get_sys_temperature = NULL,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = NULL,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = NULL,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize        = Uninitialize,
	.check_microp_id     = NULL,
	.set_microp_id       = NULL,
	.get_cpu_info        = GetCPUInfo,
	.set_aha_led         = NULL,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;

	if (0 > syno_libata_disk_map_table_gen(giDiskMapTable)) {
		gblDiskNumNeedTran = 0;
	} else {
		gblDiskNumNeedTran = 1;
	}

	giDiskLedController = AVOTON_DISK_LED_ACTIVATE_PIN;
	switch (GetModel()) {
		case MODEL_DS2415p:
			model_ops = &ds2415p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
			giAlertGPIOPin = 10;
#ifdef MY_DEF_HERE
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS415p:
			model_ops = &ds415p_ops;
			hwmon_sensor_list = &ds415p_sensor_list;
			hdd_detect_gpio = ds415p_hdd_detect_gpio;
			hdd_enable_gpio = ds415p_hdd_enable_gpio;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			break;
		case MODEL_DS1815p:
			model_ops = &ds1815p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
#ifdef MY_DEF_HERE
			/* enable internal Sil3132 sw HDD activity led */
			syno_sil3132_disk_led_enable(6, 1);
			syno_sil3132_disk_led_enable(7, 1);
#endif
			break;
		case MODEL_DS1515p:
			model_ops = &ds1515p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[5]){0,2,4,6,8}, (int[5]){1,3,5,7,9}, 5);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
			break;
		case MODEL_RS815p:
			model_ops = &rs815p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
			giAlertGPIOPin = 10;
			break;
		case MODEL_RS815rpp:
			model_ops = &rs815rpp_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
			giAlertGPIOPin = 10;
			break;
		case MODEL_RS2416p:
			model_ops = &rs2416p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
			giAlertGPIOPin = 10;
#ifdef MY_DEF_HERE
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
			break;
		case MODEL_RS2416rpp:
			model_ops = &rs2416rpp_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
			giAlertGPIOPin = 10;
#ifdef MY_DEF_HERE
			synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS1616p:
			model_ops = &ds1616p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[6]){0,2,4,6,8,10}, (int[6]){1,3,5,7,9,11}, 6);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
			break;
		case MODEL_DS1517p:
			model_ops = &ds1517p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[5]){0,2,4,6,8}, (int[5]){1,3,5,7,9}, 5);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
			giAllLedController = 49;
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(3, 1); /* mv9235 slot 3 */
			syno_ahci_disk_led_enable_by_port(4, 1); /* mv9235 slot 4 */
			syno_ahci_disk_led_enable_by_port(5, 1); /* mv9170 slot 5 */
#else
			syno_ahci_disk_led_enable(3, 1); /* mv9235 slot 2 is host3 */
			syno_ahci_disk_led_enable(4, 1); /* mv9235 slot 3 is host4 */
			syno_ahci_disk_led_enable(6, 1); /* mv9170 slot 2 is host6 */
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_DS1817p:
			model_ops = &ds1817p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
			giAllLedController = 49;
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(3, 1); /* mv9235-1 slot 3 */
			syno_ahci_disk_led_enable_by_port(4, 1); /* mv9235-1 slot 4 */
			syno_ahci_disk_led_enable_by_port(5, 1); /* mv9235-1 slot 5 */
			syno_ahci_disk_led_enable_by_port(6, 1); /* mv9235-2 slot 6 */
			syno_ahci_disk_led_enable_by_port(7, 1); /* mv9235-2 slot 7 */
			syno_ahci_disk_led_enable_by_port(8, 1); /* mv9235-2 slot 8 */
#else
			syno_ahci_disk_led_enable(3, 1); /* mv9235-1 slot 2 is host3 */
			syno_ahci_disk_led_enable(4, 1); /* mv9235-1 slot 3 is host4 */
			syno_ahci_disk_led_enable(5, 1); /* mv9235-1 slot 4 is host5 */
			syno_ahci_disk_led_enable(7, 1); /* mv9235-2 slot 2 is host7 */
			syno_ahci_disk_led_enable(8, 1); /* mv9235-2 slot 3 is host8 */
			syno_ahci_disk_led_enable(9, 1); /* mv9235-2 slot 4 is host9 */
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS818p:
			model_ops = &rs818p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
			giAlertGPIOPin = 10;
			giAllLedController = 49;
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(3, 1); /* mv9235-1 slot 1 is host3 */
			syno_ahci_disk_led_enable_by_port(4, 1); /* mv9235-2 slot 2 is host4 */
#else
			syno_ahci_disk_led_enable(3, 1); /* mv9235 slot 1 is host3 */
			syno_ahci_disk_led_enable(4, 1); /* mv9235 slot 2 is host4 */
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS818rpp:
			model_ops = &rs818rpp_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
			giAlertGPIOPin = 10;
			giAllLedController = 49;
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(3, 1); /* mv9235-1 slot 1 is host3 */
			syno_ahci_disk_led_enable_by_port(4, 1); /* mv9235-2 slot 2 is host4 */
#else
			syno_ahci_disk_led_enable(3, 1); /* mv9235 slot 1 is host3 */
			syno_ahci_disk_led_enable(4, 1); /* mv9235 slot 2 is host4 */
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS1219p:
			model_ops = &rs1219p_ops;
			synobios_ops.set_alarm_led = SetAlarmLedByGPIO;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByI2CLedDimmer;
			SetupDiskLedMap((int[8]){0,2,4,6,8,10,12,14}, (int[8]){1,3,5,7,9,11,13,15}, 8);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByI2CLedDimmer;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
			giAlertGPIOPin = 10;
			giAllLedController = 49;
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
#ifdef CONFIG_SYNO_SATA_REMAP
			syno_ahci_disk_led_enable_by_port(1, 1); /* mv9235-2 slot 2 */
			syno_ahci_disk_led_enable_by_port(2, 1); /* Internal */
			syno_ahci_disk_led_enable_by_port(3, 1); /* mv9235-1 slot 1 */
			syno_ahci_disk_led_enable_by_port(4, 1); /* mv9235-1 slot 3 */
			syno_ahci_disk_led_enable_by_port(5, 1); /* mv9235-2 slot 3 */
			syno_ahci_disk_led_enable_by_port(6, 1); /* mv9235-2 slot 4 */
			syno_ahci_disk_led_enable_by_port(7, 1); /* Internal */
			syno_ahci_disk_led_enable_by_port(8, 1); /* mv9235-1 slot 2 */
#else
			syno_ahci_disk_led_enable(1, 1); /* mv9235-2 slot 2 */
			syno_ahci_disk_led_enable(2, 1); /* Internal */
			syno_ahci_disk_led_enable(3, 1); /* mv9235-1 slot 1 */
			syno_ahci_disk_led_enable(4, 1); /* mv9235-1 slot 3 */
			syno_ahci_disk_led_enable(5, 1); /* mv9235-2 slot 3 */
			syno_ahci_disk_led_enable(6, 1); /* mv9235-2 slot 4 */
			syno_ahci_disk_led_enable(7, 1); /* Internal */
			syno_ahci_disk_led_enable(8, 1); /* mv9235-1 slot 2 */
#endif /* CONFIG_SYNO_SATA_REMAP */
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
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
	return 0;
}
