#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2014 Synology Inc. All rights reserved.

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

#include "braswell_common.h"

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

// 91: GP_CAMERASB02 North-community 35
// 99: GP_CAMERASB07 North-community 43
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 2,
	.gpio_port		= {91, 99},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 56: GPIO_DFX0 North-community 0
// 59: GPIO_DFX1 North-community 3
// 63: GPIO_DFX2 North-community 7
// 57: GPIO_DFX3 North-community 1
static SYNO_GPIO_INFO hdd_detect = {
	.nr_gpio		= 4,
	.gpio_port		= {56, 59, 63, 57},
	.gpio_polarity	= ACTIVE_LOW,
};
// 61: GPIO_DFX4 North-community 5
// 60: GPIO_DFX5 North-community 4
// 71: GPIO_DFX6 North-community 15
// 58: GPIO_DFX7 North-community 2
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 4,
	.gpio_port		= {61, 60, 71, 58},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 96: GP_CAMERASB03 North-community 40
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {96},
	.gpio_polarity	= ACTIVE_HIGH,
};
// 101: GP_CAMERASB04 North-community 45
static SYNO_GPIO_INFO phy_led_ctrl = {
    .nr_gpio        = 1,
    .gpio_port      = {101},
    .gpio_polarity  = ACTIVE_HIGH,
};
// 90: GP_CAMERASB05 North-community 34
static SYNO_GPIO_INFO copy_button_detect = {
	.nr_gpio		= 1,
	.gpio_port		= {90},
	.gpio_polarity	= ACTIVE_LOW,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = &hdd_detect;
	} else {
		check_gpio_consistency(syno_gpio.hdd_detect, &hdd_detect);
	}

	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	syno_gpio.fan_fail              = &fan_fail;
	syno_gpio.disk_led_ctrl         = &disk_led_ctrl;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
	syno_gpio.copy_button_detect    = &copy_button_detect;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_detect == syno_gpio.hdd_detect) {
		syno_gpio.hdd_detect = NULL;
	}

	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_fail              = NULL;
	syno_gpio.disk_led_ctrl         = NULL;
	syno_gpio.phy_led_ctrl          = &phy_led_ctrl;
	syno_gpio.copy_button_detect    = &copy_button_detect;
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
	int model = MODEL_DS916p;

	if ( !strncmp(gszSynoHWVersion, HW_DS916p, strlen(HW_DS916p) ) ) {
		model = MODEL_DS916p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS716pII, strlen(HW_DS716pII) ) ) {
		model = MODEL_DS716pII;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS716p, strlen(HW_DS716p) ) ) {
		model = MODEL_DS716p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS216pII, strlen(HW_DS216pII) ) ) {
		model = MODEL_DS216pII;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS216p, strlen(HW_DS216p) ) ) {
		model = MODEL_DS216p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS416play, strlen(HW_DS416play) ) ) {
		model = MODEL_DS416play;
	}

	return model;
}

static
int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_DS216p:
		case MODEL_DS216pII:
		case MODEL_DS716p:
		case MODEL_DS716pII:
			iInternalDiskNum = 2;
			break;
		case MODEL_DS416play:
		case MODEL_DS916p:
			iInternalDiskNum = 4;
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

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx;

	if (NULL == pCpuTemp) {
		goto END;
	}
#ifdef CONFIG_SYNO_X86_CORETEMP
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

END:
	return iRet;
}

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	/* Front LAN LEDs and RJ45 Phy LEDs are contoled by GPIO pin BRASWELL_GPIO_LAN_LED_CTL */
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

	syno_gpio_init();

	switch(GetModel())
	{
		case MODEL_DS916p:
			model_ops = &ds916p_ops;
			hwmon_sensor_list = &ds916p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
		case MODEL_DS416play:
			model_ops = &ds416play_ops;
			hwmon_sensor_list = &ds416play_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
			case MODEL_DS716p:
			model_ops = &ds716p_ops;
			hwmon_sensor_list = &ds716p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
		case MODEL_DS216p:
			model_ops = &ds216p_ops;
			hwmon_sensor_list = &ds216p_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
		case MODEL_DS216pII:
			model_ops = &ds216pII_ops;
			hwmon_sensor_list = &ds216pII_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
			case MODEL_DS716pII:
			model_ops = &ds716pII_ops;
			hwmon_sensor_list = &ds716pII_sensor_list;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;

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
	syno_gpio_cleanup();
	return 0;
}
