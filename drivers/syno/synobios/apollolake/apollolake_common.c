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
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include<linux/slab.h>
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#include "../rtc/rtc.h"

#include "apollolake_common.h"

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int giAlertGPIOPin = -1;

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

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

	if ( !strncmp(gszSynoHWVersion, HW_DS918p, strlen(HW_DS918p) ) ) {
		model = MODEL_DS918p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS218p, strlen(HW_DS218p) ) ) {
		model = MODEL_DS218p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS718p, strlen(HW_DS718p) ) ) {
		model = MODEL_DS718p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS418play, strlen(HW_DS418play) ) ) {
		model = MODEL_DS418play;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS419p, strlen(HW_DS419p) ) ) {
		model = MODEL_DS419p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1019p, strlen(HW_DS1019p) ) ) {
		model = MODEL_DS1019p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS719p, strlen(HW_DS719p) ) ) {
		model = MODEL_DS719p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS620slim, strlen(HW_DS620slim) ) ) {
		model = MODEL_DS620slim;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS419p, strlen(HW_RS419p) ) ) {
		model = MODEL_RS419p;
	}


	return model;
}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_DS218p:
		case MODEL_DS718p:
		case MODEL_DS719p:
			iInternalDiskNum = 2;
			break;
		case MODEL_DS418play:
		case MODEL_DS918p:
		case MODEL_DS419p:
			iInternalDiskNum = 4;
			break;
		case MODEL_DS1019p:
			iInternalDiskNum = 5;
			break;
		case MODEL_DS620slim:
			iInternalDiskNum = 6;
			break;
		default:
			printk(KERN_INFO "synobios cannot find internal disk number.\n");
			break;
	}
	return iInternalDiskNum;
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
#ifdef CONFIG_SYNO_LEDS_TRIGGER
static
int SetDiskLedStatusByLedTrigger(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;
	static int diskLedEnabled = 0;

	/* first time we tried to light on disk led */
	if (!diskLedEnabled && DISK_LED_OFF != iStatus) {
		SYNO_ENABLE_HDD_LED(1);
		diskLedEnabled = 1;
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
int SYNOSetDiskLedStatusByLedTrigger(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusByLedTrigger(iHostNum + 1, iStatus);
}

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
#endif // CONFIG_SYNO_LEDS_TRIGGER
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx = 0;

	if (NULL == pCpuTemp) {
		goto END;
	}
#ifdef CONFIG_SYNO_X86_CORETEMP
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

	if( 0 != iRet) {
		goto END;
	}

	if( 1 == pCpuTemp->blSurface ) {
		for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
			pCpuTemp->cpu_temp[iCPUIdx] -= 5;

			if (40 > pCpuTemp->cpu_temp[iCPUIdx]) {
				pCpuTemp->cpu_temp[iCPUIdx] = 40;
			}
		}
	}

END:
	return iRet;
}

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	/* Front LAN LEDs and RJ45 Phy LEDs are contoled by GPIO pin APOLLOLAKE_GPIO_LAN_LED_CTL */
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

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	unsigned int freq = cpufreq_quick_get(0);

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

int getCopyButtonStatus(void)
{
    // for matching userspace usage, button pressed = 0, else = 1
    return SYNO_COPY_BUTTON_GPIO_GET();
}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#else
#ifdef CONFIG_SYNO_LEDS_TRIGGER
/*
 * Setup the global Disk LED mapping for sata driver
 */
void SetupDiskTriggerLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
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
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

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

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	int iInternalDiskNum = -1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iInternalDiskNum = GetMaxInternalDiskNum();

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_DETECT(index)) {
			hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY()) & 0x01) << (index-1);
		}
	}

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_ENABLE(index)) {
			hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY()) & 0x01) << (index-1);
		}
	}

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%ld", hdd_enable);

	iRet = 0;

End:
	return iRet;
}

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_seiko_get_time,
	.set_rtc_time        = rtc_seiko_set_time,
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_seiko_set_auto_poweron,
	.init_auto_poweron    = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron  = rtc_seiko_auto_poweron_uninit,
	.get_fan_status      = GetFanStatusActivePulse,
	.set_fan_status      = SetFanStatusMircopWithGPIO,
	.get_sys_temperature = NULL,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = NULL,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLed,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize	     = Uninitialize,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led         = NULL,
	.get_copy_button_status = NULL, // for matching userspace usage, button pressed = 0, else = 1
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_DS918p:
			model_ops = &ds918p_ops;
			hwmon_sensor_list = &ds918p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif
			break;
		case MODEL_DS218p:
			model_ops = &ds218p_ops;
			hwmon_sensor_list = &ds218p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[2]){0,2}, (int[2]){1,3}, 2);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
#endif
			// for matching userspace usage, button pressed = 0, else = 1
			synobios_ops.get_copy_button_status = getCopyButtonStatus;
			break;

		case MODEL_DS718p:
			model_ops = &ds718p_ops;
			hwmon_sensor_list = &ds718p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[2]){0,2}, (int[2]){1,3}, 2);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
#endif
			// for matching userspace usage, button pressed = 0, else = 1
			synobios_ops.get_copy_button_status = getCopyButtonStatus;
			break;
		case MODEL_DS418play:
			model_ops = &ds418play_ops;
			hwmon_sensor_list = &ds418play_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif
			break;
		case MODEL_DS419p:
			model_ops = &ds419p_ops;
			hwmon_sensor_list = &ds419p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif
			break;
		case MODEL_DS1019p:
			model_ops = &ds1019p_ops;
			hwmon_sensor_list = &ds1019p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[5]){0,2,4,6,8}, (int[5]){1,3,5,7,9}, 5);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
#endif
			break;
		case MODEL_DS719p:
			model_ops = &ds719p_ops;
			hwmon_sensor_list = &ds719p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[2]){0,2}, (int[2]){1,3}, 2);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
#endif
			break;
		case MODEL_DS620slim:
			model_ops = &ds620slim_ops;
			hwmon_sensor_list = &ds620slim_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_alarm_led  = SetAlarmLedByGPIO;
			giAlertGPIOPin = 16;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[6]){0,2,4,6,8,10}, (int[6]){1,3,5,7,9,11}, 6);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
			syno_ahci_disk_led_enable(4, 1);
			syno_ahci_disk_led_enable(5, 1);
#endif
			break;
		case MODEL_RS419p:
			model_ops = &rs419p_ops;
			hwmon_sensor_list = &rs419p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}

			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap((int[4]){0,2,4,6}, (int[4]){1,3,5,7}, 4);
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable(0, 1);
			syno_ahci_disk_led_enable(1, 1);
			syno_ahci_disk_led_enable(2, 1);
			syno_ahci_disk_led_enable(3, 1);
#endif
			break;
		default :
			break;
	}

	if( synobios_ops.init_auto_poweron ) {
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
	if ( model_ops->x86_gpio_cleanup ) {
		model_ops->x86_gpio_cleanup();
	}

	return 0;
}
