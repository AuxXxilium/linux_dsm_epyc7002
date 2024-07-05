#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

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

#include "cedarview_common.h"

#ifdef MY_DEF_HERE
extern void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault);

#ifdef SYNO_ATA_AHCI_LED_SWITCH
#ifndef SYNO_MAX_INTERNAL_DISK /* TODO XXX REMOVE MEEEEEEEEE */
#define SYNO_MAX_INTERNAL_DISK 19
#endif
extern int giSynoHddLedEnabled;
#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
extern atomic_t ata_print_id;
#else /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */
extern unsigned int ata_print_id;
#endif /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */
SYNO_DISK_LED gDiskOrgStatus[SYNO_MAX_INTERNAL_DISK] = {0};
#endif

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#define DS1812P_INTERNAL_DISK_NUM 8
#define DS1813P_INTERNAL_DISK_NUM 8
#define DS1513P_INTERNAL_DISK_NUM 5
#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
extern char gszSynoHWRevision[4];
#endif

#ifdef SYNO_E1000E_LED_SWITCH
extern void (*funcSynoNicLedCtrl)(int iEnable);
#endif

#ifdef MY_DEF_HERE
extern int syno_superio_regist_read(u8 ldn, u8 reg, u8 *value);
extern int syno_superio_regist_write(u8 ldn, u8 reg, u8 value);
#endif /*MY_DEF_HERE*/

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static int Uninitialize(void);

#define GPIO_POWER_GOOD	1
int CedarviewRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1value = 0, power2value = 0;
	int gpio_rp_map[] = {27, 28};

	if ( 0 != syno_pch_lpc_gpio_pin(gpio_rp_map[0] , &power1value, 0) ) {
		goto FAIL;
	}

	if ( 0 != syno_pch_lpc_gpio_pin(gpio_rp_map[1] , &power2value, 0) ) {
		goto FAIL;
	}	

	if (power1value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	}else{
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	}else{
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
}

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static int* hdd_detect_gpio = NULL;
static int* hdd_enable_gpio = NULL;

static int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS713p:
			iMaxInternalDiskNum = 2;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}

int GetFanStatusMircopWithGPIOCommon(const int pin, FAN_STATUS *pStatus)
{
	GPIO_PIN gpiopin;
	int rgcVolt[2] = {0, 0};

	if (pStatus == NULL) {
		return -EINVAL;
	}

	gpiopin.pin = pin;

	do {
		GetGpioPin(&gpiopin);
		if (!gpiopin.value) {
			rgcVolt[0]++;
		} else {
			rgcVolt[1]++;
		}

		if (rgcVolt[0] && rgcVolt[1]) {
			break;
		}
		udelay(300);
	} while ( (rgcVolt[0] + rgcVolt[1]) < 200 );

	if (rgcVolt[0] == 0) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}
	return 0;
}

/*
 * This function is used by all cedarview model except RS812+
 * due to the gpio mapping, RS812+ use it's own gpio defination
 */
int CedarviewGetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	int gpio_fan_map[] = {37, 36, 18, 30};
	int fanNum = sizeof(gpio_fan_map)/sizeof(gpio_fan_map[0]);

	if (pStatus == NULL) {
		return -EINVAL;
	}

	if (fanno > fanNum) {
		return -EINVAL;
	}
	GetFanStatusMircopWithGPIOCommon(gpio_fan_map[fanno-1], pStatus);

	return 0;
}

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	int iRet = -1;

	if (model_ops && model_ops->x86_get_fan_status) {
		iRet = model_ops->x86_get_fan_status(fanno, pStatus);
	}

	return iRet;
}

static
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
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
		hdd_detect_status |= (Pin.value & 0x01) << i;
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

static
int SetCpuFanStatus(FAN_STATUS status, FAN_SPEED speed)
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
			snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_CPUFAN_FREQUENCY, FAN_SPEED_SHIFT_HZ_GET((int)speed));
			if( 0 > SetUart(szUartCmd) ) {
				goto END;
			}
		}
	} else {
		if( NULL ==  model_ops->x86_cpufan_speed_mapping ) {
			goto END;
		} else if( 0 > (iFanDuty = model_ops->x86_cpufan_speed_mapping(speed)) ) {
			printk("No matched fan speed!\n");
			goto END;
		}
	}

	snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_CPUFAN_DUTY_CYCLE, iFanDuty);
	if( 0 > SetUart(szUartCmd) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

int GetModel(void)
{
	int model = MODEL_DS1512p;

	if ( !strncmp(gszSynoHWVersion, HW_DS1512p, strlen(HW_DS1512p) ) ) {
		model = MODEL_DS1512p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1513p, strlen(HW_DS1513p) ) ) {
		model = MODEL_DS1513p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS412p, strlen(HW_DS412p) ) ) {
		model = MODEL_DS412p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS713p, strlen(HW_DS713p) ) ) {
		model = MODEL_DS713p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS812p, strlen(HW_RS812p) ) ) {
		model = MODEL_RS812p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS812rpp, strlen(HW_RS812rpp) ) ) {
		model = MODEL_RS812rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS814p, strlen(HW_RS814p) ) ) {
		model = MODEL_RS814p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS814rpp, strlen(HW_RS814rpp) ) ) {
		model = MODEL_RS814rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1812p, strlen(HW_DS1812p) ) ) {
		model = MODEL_DS1812p;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS1813p, strlen(HW_DS1813p) ) ) {
		model = MODEL_DS1813p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2212p, strlen(HW_RS2212p) ) ) {
		model = MODEL_RS2212p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2212rpp, strlen(HW_RS2212rpp) ) ) {
		model = MODEL_RS2212rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2414p, strlen(HW_RS2414p) ) ) {
		model = MODEL_RS2414p;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS2414rpp, strlen(HW_RS2414rpp) ) ) {
		model = MODEL_RS2414rpp;
	} else if ( !strncmp(gszSynoHWVersion, HW_DS2413p, strlen(HW_DS2413p) ) ) {
		model = MODEL_DS2413p;
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

int SetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( pPin->pin < 100 ) {
		if ( 0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 1) ) {
			goto End;
		}
	} else {
#ifdef MY_DEF_HERE
		if ( 0 != syno_superio_gpio_pin((int)pPin->pin - 100, (int*)&pPin->value, 1) ) {
			goto End;
		}
#else /*MY_DEF_HERE*/
		goto End;
#endif /*MY_DEF_HERE*/
	}

	ret = 0;
End:
	return ret;
}

int GetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( pPin->pin < 100 ) {
		if ( 0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 0) ) {
			goto End;
		}
	} else {
#ifdef MY_DEF_HERE
		if ( 0 != syno_superio_gpio_pin((int)pPin->pin - 100, (int*)&pPin->value, 0) ) {
			goto End;
		}
#else /*MY_DEF_HERE*/
		goto End;
#endif /*MY_DEF_HERE*/
	}

	ret = 0;
End:
	return ret;
}

#ifdef MY_DEF_HERE
/*FIXME - Too brutal and directly, should separate into levels*/
static 
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;
	int iWrite = -1;
	int iRead = -1;

	if (1 > disknum) {
		goto END;
	}

	/*Scsi host in kernel is zero-based, disknum here is one-based,
	 *so we should minus 1 while calling the function
	 */
	if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status){
		iWrite = 1;
	}else{
		iWrite = 0;
	}
	syno_sata_mv_gpio_write(iWrite, disknum - 1);

	iRead = syno_sata_mv_gpio_read(disknum - 1);
#ifdef MY_DEF_HERE
	if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
	}
#endif /* MY_DEF_HERE */
	err = 0;
END:
	return err;
}
#endif /* MY_DEF_HERE */
/* refactor function to group disk led status setting together
 * pin1 & pin2 must set and pass as sequence
 */
static
int SetDiskLedGPIOPinValue(SYNO_DISK_LED iStatus, GPIO_PIN *Pin1, GPIO_PIN *Pin2)
{
	int err = -1;
	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			Pin1->value = 0;
			Pin2->value = 1;
			break;
		case DISK_LED_GREEN_SOLID:
			Pin1->value = 1;
			Pin2->value = 0;
			break;
		case DISK_LED_OFF:
			Pin1->value = 0;
			Pin2->value = 0;
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	err = 0;
END:
	return err;
}

#ifdef MY_DEF_HERE
/* this function is created for SATA disks which use ahci as its controller
 * the LED control function will be different from other GPIO controlled disks
 * before using this function, the iDiskNum must be checked
 */
static
int SetDiskLedStatusBySGPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int err = -1;
	int iFault, iPresent;

	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			iFault = 1;
			iPresent = 0;
			break;
		case DISK_LED_GREEN_SOLID:
			iFault = 0;
			iPresent = 1;
			break;
		case DISK_LED_OFF:
			iFault = 0;
			iPresent = 0;
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	/* disknum in kernel space and user space are both 1-based. */
	sata_syno_ahci_diskled_set(iDiskNum -1, iPresent, iFault);
	mdelay(100);
	err = 0;

END:
	return err;
}

/* DS1812+ use ahci & sil3132 as its internal disk controller
 */
static
int SetDiskLedStatusBySGPIOandGPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int err = -1;
	GPIO_PIN Pin1, Pin2;

	if (0 > SetDiskLedGPIOPinValue(iStatus, &Pin1, &Pin2)) {
		goto END;
	}

	switch(iDiskNum) {
		case 1 ... 6:
			SetDiskLedStatusBySGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 7:
			/* Disk 7 are set via gpio */
			Pin1.pin = SYNO_GPP_HDD7_LED_0;
			Pin2.pin = SYNO_GPP_HDD7_LED_1;
			break;
		case 8:
			/* Disk 8 are set via gpio */
			Pin1.pin = SYNO_GPP_HDD8_LED_0;
			Pin2.pin = SYNO_GPP_HDD8_LED_1;
			break;
		case 9 ... 16:
			err = 0;
			goto END;
		default:
			printk("Invalid HDD number [%d]\n", iDiskNum);
			goto END;
	}
	SetGpioPin(&Pin1);
	SetGpioPin(&Pin2);

SGPIO_END:
#ifdef SYNO_ATA_AHCI_LED_SWITCH
	gDiskOrgStatus[iDiskNum - 1] = iStatus;
#endif
	err = 0;
END:
	return err;
}

/**
 * This function is created for funcSYNOSATADiskLedCtrl to hook
 */
static
int SYNOSetDiskLedStatusBySGPIOandGPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusBySGPIOandGPIO(iHostNum + 1, iStatus);
}
#else

static
int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	int err = -1;
	GPIO_PIN Pin1, Pin2; 

	if (0 > SetDiskLedGPIOPinValue(status, &Pin1, &Pin2)) {
		goto END;
	}

	switch (disknum) {
	case 1:
		Pin1.pin = SYNO_GPP_HDD1_LED_0;
		Pin2.pin = SYNO_GPP_HDD1_LED_1;
		break;
	case 2:
		Pin1.pin = SYNO_GPP_HDD2_LED_0;
		Pin2.pin = SYNO_GPP_HDD2_LED_1;
		break;
	case 3:
		Pin1.pin = SYNO_GPP_HDD3_LED_0;
		Pin2.pin = SYNO_GPP_HDD3_LED_1;
		break;
	case 4:
		Pin1.pin = SYNO_GPP_HDD4_LED_0;
		Pin2.pin = SYNO_GPP_HDD4_LED_1;
		break;
	case 5:
		Pin1.pin = SYNO_GPP_HDD5_LED_0;
		Pin2.pin = SYNO_GPP_HDD5_LED_1;
		break;
	case 7:
		/* The eSata disk is /dev/sdg on DS710+,
		 * so it is the 7th disk on this model.
		 * ( 'g' - 'a' + 1 = 7 )
		 */
		if (model_ops && model_ops->x86_set_esata_led_status) {
			model_ops->x86_set_esata_led_status(status);
		}
		goto ESATA_END;
	case 6:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		//for esata
		err = 0;
		goto END;
	default:
		printk("Wrong HDD number [%d]\n", disknum);
		goto END;
	}

	SetGpioPin(&Pin1);
	SetGpioPin(&Pin2);

ESATA_END:
    err = 0;
END:
    return err;
}

#endif /* MY_DEF_HERE */

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)

/**
 * This function is for #29300
 * In rs2212rp+ and rs2212+, the disks are hosted by different sata controllers
 * The first 6 disks are hosted by ahci and the last 4 disks are hosted by mv7042
 * They control disk present LEDs in totally different ways.
 * Ahci is controlled by gpio, but mv7042 is controlled directly by chipset itself.
 * That means we can not control last 4 disk present leds.
 * So if we enable gpio pin 24 to enable them in synobios_init, the last 4 disks will light on but the first 6 disks will keep off.
 * And the first 6 disks will light on after scemd scan all disk ports and light them on 1 by 1.
 * To sync disk led activities, we have to delay enabling gpio pin 24 to when scemd try to light on disk leds.
 */
static int SetDiskLedEnable(SYNO_DISK_LED iStatus)
{
	static int diskLedEnable = 0;
	GPIO_PIN  LedActivate;

	// At first time to set disk led to light on, we enable gpio pin 24.
	if (0 == diskLedEnable && iStatus != DISK_LED_OFF) {
		diskLedEnable = 1;

		//Signal to activate the leds.
		LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
		LedActivate.value = 0;

		SetGpioPin(&LedActivate);
	}
	return diskLedEnable;
}

static
int SetDiskLedStatusBySGPIOandMvGPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int err = -1;

	SetDiskLedEnable(iStatus);
	switch(iDiskNum) {
		case 1 ... 6:
			SetDiskLedStatusBySGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 7 ... 10:
			SetDiskLedStatusBySataMvGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 11 ... 16:
			err = 0;
			goto END;
		default:
			printk("Invalid HDD number [%d]\n", iDiskNum);
			goto END;
	}
SGPIO_END:
	err = 0;
END:
	return err;
}

/**
 * This function is created for funcSYNOSATADiskLedCtrl to hook
 */
static
int SYNOSetDiskLedStatusBySGPIOandMvGPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusBySGPIOandMvGPIO(iHostNum + 1, iStatus);
}

static
int SetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int err = -1;
	GPIO_PIN Pin1, Pin2;

	SetDiskLedEnable(iStatus);

	if (0 > SetDiskLedGPIOPinValue(iStatus, &Pin1, &Pin2)) {
		goto END;
	}

	switch(iDiskNum) {
		case 1 ... 6:
			SetDiskLedStatusBySGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 7 ... 10:
			SetDiskLedStatusBySataMvGPIO(iDiskNum + 4, iStatus);
			goto SGPIO_END;
		case 11:
			Pin1.pin = SYNO_GPP_HDD11_LED_0;
			Pin2.pin = SYNO_GPP_HDD11_LED_1;
			break;
		case 12:
			Pin1.pin = SYNO_GPP_HDD12_LED_0;
			Pin2.pin = SYNO_GPP_HDD12_LED_1;
			break;
		case 13 ... 16:
			err = 0;
			goto END;
		default:
			printk("Invalid HDD number [%d]\n", iDiskNum);
			goto END;
	}
	SetGpioPin(&Pin1);
	SetGpioPin(&Pin2);

SGPIO_END:
	err = 0;
END:
	return err;
}

/**
 * This function is created for funcSYNOSATADiskLedCtrl to hook
 */
static
int SYNOSetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO(iHostNum + 1, iStatus);
}

static
int SetDiskLedStatusBySGPIOMvGPIOandGPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int err = -1;
	GPIO_PIN Pin1, Pin2;

	SetDiskLedEnable(iStatus);

	if (0 > SetDiskLedGPIOPinValue(iStatus, &Pin1, &Pin2)) {
		goto END;
	}

	switch(iDiskNum) {
		case 1 ... 6:
			SetDiskLedStatusBySGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 7 ... 10:
			SetDiskLedStatusBySataMvGPIO(iDiskNum, iStatus);
			goto SGPIO_END;
		case 11:
			Pin1.pin = SYNO_GPP_HDD11_LED_0;
			Pin2.pin = SYNO_GPP_HDD11_LED_1;
			break;
		case 12:
			Pin1.pin = SYNO_GPP_HDD12_LED_0;
			Pin2.pin = SYNO_GPP_HDD12_LED_1;
			break;
		case 13 ... 16:
			err = 0;
			goto END;
		default:
			printk("Invalid HDD number [%d]\n", iDiskNum);
			goto END;
	}
	SetGpioPin(&Pin1);
	SetGpioPin(&Pin2);

SGPIO_END:
	err = 0;
END:
	return err;
}

/**
 * This function is created for funcSYNOSATADiskLedCtrl to hook
 */
static
int SYNOSetDiskLedStatusBySGPIOMvGPIOandGPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	return SetDiskLedStatusBySGPIOMvGPIOandGPIO(iHostNum + 1, iStatus);
}
#endif /* MY_DEF_HERE && MY_DEF_HERE */

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
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx;

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
	 * n > 30 show n - 8
	 * 30 >= n >= 22: show 22
	 * n < 22: show n
	 */
	if(1 == pCpuTemp->blSurface) {
		for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
			if (pCpuTemp->cpu_temp[iCPUIdx] > 30) {
				pCpuTemp->cpu_temp[iCPUIdx] = pCpuTemp->cpu_temp[iCPUIdx] - 8;
			} else if (pCpuTemp->cpu_temp[iCPUIdx] >= 22){
				pCpuTemp->cpu_temp[iCPUIdx] = 22;
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

static void synobios_hw_function(void)
{
	char szbuf[8] = {0};
	char c;
	ReadUart("R", "R", szbuf, sizeof(szbuf));
	c = szbuf[0];

    switch (c) {
		case 'E':
			sprintf(gszSynoHWVersion, "%s", HW_DS412p);
			break;
		case 'F':
			sprintf(gszSynoHWVersion, "%s", HW_DS1812p);
			break;
		case 'G':
			sprintf(gszSynoHWVersion, "%s", HW_DS1512p);
			break;
		case 'H':
			sprintf(gszSynoHWVersion, "%s", HW_RS812p);
			break;
		case 'I':
			sprintf(gszSynoHWVersion, "%s", HW_RS812rpp);
			break;
		case 'J':
			sprintf(gszSynoHWVersion, "%s", HW_RS2212p);
			break;
		case 'K':
			sprintf(gszSynoHWVersion, "%s", HW_RS2212rpp);
			break;
		case 'L':
			sprintf(gszSynoHWVersion, "%s", HW_DS2413p);
			break;
		case 'P':
			sprintf(gszSynoHWVersion, "%s", HW_DS713p);
			break;
		case 'Q':
			sprintf(gszSynoHWVersion, "%s", HW_DS1513p);
			break;
		case 'R':
			sprintf(gszSynoHWVersion, "%s", HW_DS1813p);
			break;
		case 'S':
			sprintf(gszSynoHWVersion, "%s", HW_RS2414p);
			break;
		case 'T':
			sprintf(gszSynoHWVersion, "%s", HW_RS2414rpp);
			break;
		case 'U':
			sprintf(gszSynoHWVersion, "%s", HW_RS814p);
			break;
		case 'V':
			sprintf(gszSynoHWVersion, "%s", HW_RS814rpp);
			break;
	}
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
#ifdef SYNO_ATA_AHCI_LED_SWITCH
	int iError = -1;
	int iDiskIdx;
#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
	int iDiskIdxMax = atomic_read(&ata_print_id);
#endif /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */

	switch(ledStatus){
		case SYNO_LED_OFF:
			giSynoHddLedEnabled = 0;
			break;
		case SYNO_LED_ON:
			giSynoHddLedEnabled = ~0;
			break;
		default:
			goto ERR;
	}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
	for (iDiskIdx = 1; iDiskIdx < iDiskIdxMax; iDiskIdx++) {
#else /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */
	for (iDiskIdx = 1; iDiskIdx < ata_print_id; iDiskIdx++) {
#endif /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */
		SetDiskLedStatusBySGPIOandGPIO (iDiskIdx, gDiskOrgStatus[iDiskIdx - 1]);
	}

	iError = 0;
ERR:
	return iError;
#else
	return 0;
#endif
}

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
		case SYNO_LED_OFF:
#ifdef SYNO_E1000E_LED_SWITCH
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

static int superio_read (SYNO_SUPERIO_PACKAGE* pSuperioPackage){
	int iRet = -1;

#ifdef MY_DEF_HERE
	if(syno_superio_regist_read(pSuperioPackage->ldn, pSuperioPackage->reg, &pSuperioPackage->value)){
		goto END;
	}

	iRet = 0;
END:
#endif /*MY_DEF_HERE*/
	return iRet;
}

static int superio_write (SYNO_SUPERIO_PACKAGE* pSuperioPackage){
	int iRet = -1;

#ifdef MY_DEF_HERE
	if(syno_superio_regist_write(pSuperioPackage->ldn, pSuperioPackage->reg, pSuperioPackage->value)){
		goto END;
	}

	iRet = 0;
END:
#endif /*MY_DEF_HERE*/
	return iRet;
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
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_bandon_set_auto_poweron,
	.get_fan_status      = GetFanStatus,
	.set_fan_status      = SetFanStatus,
	.get_sys_temperature = GetSysTemperature,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = SetCpuFanStatus,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
#ifdef MY_DEF_HERE
	.set_disk_led        = SetDiskLedStatusBySGPIOandGPIO,
#else
	.set_disk_led        = SetDiskLedStatus,
#endif
	.set_alarm_led       = SetAlarmLed,		
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize	     = Uninitialize,
	.check_microp_id	 = CheckMicropId,
	.set_microp_id		 = SetMicropId,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led         = NULL,
	.hwmon_get_fan_speed_rpm = NULL,
	.hwmon_get_sys_thermal = NULL,
	.hwmon_get_sys_voltage = NULL,
	.hwmon_get_psu_status = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
#ifdef MY_DEF_HERE
	GPIO_PIN  LedActivate;
	int iDiskIdx;
#endif
	GPIO_PIN  UsbOverCurrent;
	*ops = &synobios_ops;

	synobios_hw_function();

#ifdef MY_DEF_HERE
#ifdef MY_DEF_HERE
	// the default function of .set_disk_led is SetDiskLedStatusBySGPIOandGPIO
	// SYNOSetDiskLedStatusBySGPIOandGPIO is actually SetDiskLedStatusBySGPIOandGPIO
	funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOandGPIO;
#endif /* MY_DEF_HERE */
#endif
	switch(GetModel())
	{
		case MODEL_DS1512p:
			model_ops = &ds1512p_ops;
			break;
		case MODEL_DS1513p:
			model_ops = &ds1513p_ops;
			synobios_ops.get_superio	= superio_read;
			synobios_ops.set_superio	= superio_write;
#ifdef MY_DEF_HERE
			//Set to off to avoid lighting up too early
			for(iDiskIdx = 1; iDiskIdx <= DS1513P_INTERNAL_DISK_NUM; iDiskIdx++) {
				SetDiskLedStatusBySGPIOandGPIO(iDiskIdx, DISK_LED_OFF);
			}

			//Signal to activate the leds.
			LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
			LedActivate.value = 0;

			SetGpioPin(&LedActivate);
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS412p:
			model_ops = &ds412p_ops;
			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			if (0 == strcmp(gszSynoHWRevision,"r2")){
				synobios_ops.get_superio	= superio_read;
				synobios_ops.set_superio	= superio_write;
			}
			break;
		case MODEL_DS713p:
			model_ops = &ds713p_ops;
			hwmon_sensor_list = &ds713p_sensor_list;
			hdd_detect_gpio = ds713p_hdd_detect_gpio;
			hdd_enable_gpio = ds713p_hdd_enable_gpio;
			synobios_ops.set_phy_led = SetPhyLed;
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			if (0 == strcmp(gszSynoHWRevision,"r2")){
				synobios_ops.get_superio	= superio_read;
				synobios_ops.set_superio	= superio_write;
			}
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			break;
		case MODEL_RS812p:
			model_ops = &rs812p_ops;
			break;
		case MODEL_RS812rpp:
			model_ops = &rs812rpp_ops;
			break;
		case MODEL_RS814p:
			model_ops = &rs814p_ops;
			synobios_ops.get_superio	= superio_read;
			synobios_ops.set_superio	= superio_write;
			break;
		case MODEL_RS814rpp:
			model_ops = &rs814rpp_ops;
			synobios_ops.get_superio	= superio_read;
			synobios_ops.set_superio	= superio_write;
			break;
		case MODEL_DS1812p:
			model_ops = &ds1812p_ops;
#ifdef MY_DEF_HERE
		//Set to off to avoid lighting up too early
		for(iDiskIdx = 1; iDiskIdx <= DS1812P_INTERNAL_DISK_NUM; iDiskIdx++) {
			SetDiskLedStatusBySGPIOandGPIO(iDiskIdx, DISK_LED_OFF);
		}

		//Signal to activate the leds.
		LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
		LedActivate.value = 0;

		SetGpioPin(&LedActivate);
#endif /* MY_DEF_HERE */
			break;
		case MODEL_DS1813p:
			model_ops = &ds1813p_ops;
			synobios_ops.get_superio	= superio_read;
			synobios_ops.set_superio	= superio_write;
#ifdef MY_DEF_HERE
			//Set to off to avoid lighting up too early
			for(iDiskIdx = 1; iDiskIdx <= DS1813P_INTERNAL_DISK_NUM; iDiskIdx++) {
				SetDiskLedStatusBySGPIOandGPIO(iDiskIdx, DISK_LED_OFF);
			}

			//Signal to activate the leds.
			LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
			LedActivate.value = 0;

			SetGpioPin(&LedActivate);
#endif /* MY_DEF_HERE */
			break;
		case MODEL_RS2212p:
			model_ops = &rs2212p_ops;
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
			synobios_ops.set_disk_led = SetDiskLedStatusBySGPIOandMvGPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOandMvGPIO;
#endif /* MY_DEF_HERE */
#endif
			break;
		case MODEL_RS2212rpp:
			model_ops = &rs2212rpp_ops;
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
			synobios_ops.set_disk_led = SetDiskLedStatusBySGPIOandMvGPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOandMvGPIO;
#endif /* MY_DEF_HERE */
#endif
			break;
		case MODEL_RS2414p:
			model_ops = &rs2414p_ops;
			synobios_ops.get_superio        = superio_read;
			synobios_ops.set_superio        = superio_write;
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
			synobios_ops.set_disk_led = SetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO;
#endif /* MY_DEF_HERE */
#endif
			/* To avoid possible interference that makes kernel complain about USB over current */
			UsbOverCurrent.pin = 43;
			UsbOverCurrent.value = 1;
			SetGpioPin(&UsbOverCurrent);

			UsbOverCurrent.pin = 42;
			UsbOverCurrent.value = 1;
			SetGpioPin(&UsbOverCurrent);

			break;
		case MODEL_RS2414rpp:
			model_ops = &rs2414rpp_ops;
			synobios_ops.get_superio        = superio_read;
			synobios_ops.set_superio        = superio_write;
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
			synobios_ops.set_disk_led = SetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOMvGPIOandAHCIGPIO;
#endif /* MY_DEF_HERE */
#endif
			/* To avoid possible interference that makes kernel complain about USB over current */
			UsbOverCurrent.pin = 43;
			UsbOverCurrent.value = 1;
			SetGpioPin(&UsbOverCurrent);

			UsbOverCurrent.pin = 42;
			UsbOverCurrent.value = 1;
			SetGpioPin(&UsbOverCurrent);

			break;
		case MODEL_DS2413p:
			model_ops = &ds2413p_ops;
#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
			synobios_ops.set_disk_led = SetDiskLedStatusBySGPIOMvGPIOandGPIO;
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusBySGPIOMvGPIOandGPIO;
#endif /* MY_DEF_HERE */
#endif
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
	GPIO_PIN LedActivate;

	if(MODEL_DS1812p == GetModel() ||
			MODEL_DS1813p == GetModel()) {
		//Signal to deactivate the leds.
		LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
		LedActivate.value = 1;

		SetGpioPin(&LedActivate);
	}

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
