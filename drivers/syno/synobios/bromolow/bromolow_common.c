#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include "../rtc/rtc.h"
#include "bromolow_common.h"

#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
static int giDiskMapTable[32] = {0};
static char gblDiskNumNeedTran = 0;
#endif
static int giHALedGreenPin = -1;
static int giHALedOrangePin = -1;
#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

/**
 *	Set Max internal disk numbers
 */
static int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_RS3411rpxs:
		case MODEL_RS3412rpxs:
		case MODEL_RS3413xsp: /* RS3413xs is an RP model*/
		case MODEL_RS3415xsp: /* RS3415xs is an RP model*/
			iMaxInternalDiskNum = 10;
			break;
		case MODEL_RS3411xs:
		case MODEL_RS3412xs:
			iMaxInternalDiskNum = 10;
			break;
		case MODEL_DS3611xs:
		case MODEL_DS3612xs:
		case MODEL_DS3615xs:
		case MODEL_RS3614xs:
		case MODEL_RS3614rpxs:
		case MODEL_RS3614xsp:
		case MODEL_DS2414xs:
		case MODEL_RS18016xsp:
		case MODEL_RS3617xs:
			iMaxInternalDiskNum = 12;
			break;
		case MODEL_RS10613xsp:
			iMaxInternalDiskNum = 10;
			break;
	}
	return iMaxInternalDiskNum;
}

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

// following functions are not chagned in any bromolow models, so we move them from model.o to here
// If there is any necessary to modify these function on any other model, please rewrite another function to replace it
PWM_FAN_SPEED_MAPPING gxsSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

PWM_FAN_SPEED_MAPPING gxsCPUFanSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

int xsFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gxsSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gxsSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gxsSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

/*
 * On RS10613xsp, and RS3413xsp we have no buzzer clear button anymore, so we need another way to stop redundant power buzzer
 */
int xsSetBuzzerClear(unsigned char buzzer_cleared)
{
	GPIO_PIN Pin;
	int ret = -1;

	Pin.pin = BROMOLOW_BUZZER_CTRL_PIN;
	Pin.value = buzzer_cleared;
	if (0 > SetGpioPin(&Pin)) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		goto End;
	}

	ret = 0;
End:
	return ret;
}

int xsGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    GPIO_PIN Pin;
    int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = BROMOLOW_BUZZER_OFF_PIN;
    if ( 0 > GetGpioPin( &Pin ) ) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
        goto End;
    }

    if ( 0 == Pin.value ) {
        *buzzer_cleared = 1;
		// after read buzzer cleared, pull it back to high
		// an workaround for #48623, because gpio 4 & 5 are jointed together
		if (model_ops && model_ops->x86_set_buzzer_clear) {
			model_ops->x86_set_buzzer_clear(1);
		}
    }

    ret = 0;
End:
    return ret;
}

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gxsCPUFanSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gxsCPUFanSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gxsCPUFanSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

#define GPIO_POWER_GOOD	1
int BromolowRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;

	if ( 0 != syno_pch_lpc_gpio_pin(BROMOLOW_POWER1_PIN , &power1_Value, 0) ) {
		goto FAIL;
	}

	if ( 0 != syno_pch_lpc_gpio_pin(BROMOLOW_POWER2_PIN , &power2_Value, 0) ) {
		goto FAIL;
	}

	if (power1_Value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	}else{
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2_Value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	}else{
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
}

static
int GetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	int gpio_fan_map[] = {1,6,7,68};
	int fanNum = sizeof(gpio_fan_map)/sizeof(gpio_fan_map[0]);
	GPIO_PIN gpiopin;
	int rgcVolt[2] = {0, 0};

	if (pStatus == NULL) {
		return -EINVAL;
	}

	if (fanno > fanNum) {
		return -EINVAL;
	}

	gpiopin.pin = gpio_fan_map[fanno-1];

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

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_bandon_get_time(&rtc_time_pkt);
	return 0;
}

int GetModel(void)
{
	int model = MODEL_RS3411rpxs;

	if ( !strncmp(gszSynoHWVersion, HW_RS3411rpxs, strlen(HW_RS3411rpxs) ) ) {
		model = MODEL_RS3411rpxs;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3411xs, strlen(HW_RS3411xs) ) ) {
		model = MODEL_RS3411xs;
	}else if (!strncmp(gszSynoHWVersion, HW_DS3611xs, strlen(HW_DS3611xs) ) ) {
		model = MODEL_DS3611xs;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS3412rpxs, strlen(HW_RS3412rpxs) ) ) {
		model = MODEL_RS3412rpxs;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3412xs, strlen(HW_RS3412xs) ) ) {
		model = MODEL_RS3412xs;
	}else if (!strncmp(gszSynoHWVersion, HW_DS3612xs, strlen(HW_DS3612xs) ) ) {
		model = MODEL_DS3612xs;
	}else if (!strncmp(gszSynoHWVersion, HW_DS3615xs, strlen(HW_DS3615xs) ) ) {
		model = MODEL_DS3615xs;
	}else if (!strncmp(gszSynoHWVersion, HW_RS10613xsp, strlen(HW_RS10613xsp) ) ) {
		model = MODEL_RS10613xsp;
	} else if ( !strncmp(gszSynoHWVersion, HW_RS3413xsp, strlen(HW_RS3413xsp) ) ) {
		model = MODEL_RS3413xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3614xsp, strlen(HW_RS3614xsp) ) ) {
		model = MODEL_RS3614xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3614xs, strlen(HW_RS3614xs) ) ) {
		model = MODEL_RS3614xs;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3614rpxs, strlen(HW_RS3614rpxs) ) ) {
		model = MODEL_RS3614rpxs;
	}else if (!strncmp(gszSynoHWVersion, HW_DS2414xs, strlen(HW_DS2414xs) ) ) {
		model = MODEL_DS2414xs;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3415xsp, strlen(HW_RS3415xsp) ) ) {
		model = MODEL_RS3415xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_RC18015xsp, strlen(HW_RC18015xsp) ) ) {
		model = MODEL_RC18015xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_RS18016xsp, strlen(HW_RS18016xsp) ) ) {
		model = MODEL_RS18016xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_RS3617xs, strlen(HW_RS3617xs) ) ) {
		model = MODEL_RS3617xs;
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
static
int SetSCSIHostLedStatusBySataMvGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int iWrite = -1;
	int iRead = -1;

	if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status) {
		iWrite = 1;
	} else {
		iWrite = 0;
	}
	syno_sata_mv_gpio_write(iWrite, iHostNum);

	iRead = syno_sata_mv_gpio_read(iHostNum);
#ifdef MY_DEF_HERE
	if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
	}
#endif /* MY_DEF_HERE */

	return 0;
}
/*FIXME - Too brutal and directly, should separate into levels*/
static
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if(!gblDiskNumNeedTran) {
		/*Scsi host in kernel is zero-based, disknum here is one-based,
		 *so we should minus 1 while calling the function
		 */
		SetSCSIHostLedStatusBySataMvGPIO(disknum - 1, status);
	}else{
		/* DS3611xs/DS3612xs and RS3411(rp)xs/RS3412(rp)xs have different disk sequences,
		 * when the cmd is performed on these two models, the disk numbers
		 * need to be transferred by the corresponding table.
		 */
		SetSCSIHostLedStatusBySataMvGPIO(giDiskMapTable[disknum - 1], status);
	}

	err = 0;
END:
	return err;
}
#endif /* MY_DEF_HERE */

static int giDiskLedController = -1;

/**
 *	For consistency of lighting disk led, we need a gpio pin to control cpld behavior
 *	Which means pull a gpio to let disk led be able to light on
 */
static
void InitSynoDiskLed(void)
{
	GPIO_PIN pin;

	// the disk led enable controller is not assigned, just return
	if (0 > giDiskLedController) {
		return;
	}
	pin.pin = giDiskLedController;
	pin.value = 0;
	SetGpioPin(&pin);
}

#ifdef MY_DEF_HERE
/**
 *	Set disk led via scsi host sysfs interface
 */
static
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret;
	static int diskLedEnabled = 0;
	int iWrite = -1;
	int iRead = -1;

	// first time we tried to light on disk led
	if (!diskLedEnabled && status != DISK_LED_OFF) {
		InitSynoDiskLed();
		diskLedEnabled = 1;
	}

	if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status) {
		iWrite = 1;
	} else {
		iWrite = 0;
	}
	ret = syno_mv_9235_disk_led_set(iHostNum, iWrite);

	iRead = syno_mv_9235_disk_led_get(iHostNum);
	if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
	}

	return ret;
}

/* Set disk led via 9235 */
static
int SetDiskLedStatusBy9235GPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if(!gblDiskNumNeedTran) {
		err = SetSCSIHostLedStatusBy9235GPIO(disknum - 1, status);
	}else{
		err = SetSCSIHostLedStatusBy9235GPIO(giDiskMapTable[disknum - 1], status);
	}
END:
	return err;
}
#endif /* MY_DEF_HERE */

/**
 *	Control disk led via AHCI SGPIO
 */
static
int SetSCSIHostLedStatusByAHCIGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret = -1;
	int iFault, iPresent;

	switch(status) {
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
			printk("Invalid LED status [%d]\n", status);
			goto END;
	}
#ifdef MY_DEF_HERE
	sata_syno_ahci_diskled_set(iHostNum, iPresent, iFault);
#else
	printk(KERN_ERR "SYNO ATA AHCI DISK LED control function is not defined!!");
#endif /* MY_DEF_HERE */
	ret = 0;
END:
	return ret;
}

#ifdef MY_DEF_HERE
/**
 *	The RS3614xs, RS3614rpxs, RS3415xs+, DS3615xs control their disk led over ahci SGPIO
 *	The way of controlling SGPIO is the same as Cedarview
 */
static
int SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret;
	int iWrite = -1, iRead = -1;
	int iInternalHost =0;
	static int diskLedEnabled = 0;

	// first time we tried to light on disk led
	if (!diskLedEnabled && status != DISK_LED_OFF) {
		InitSynoDiskLed();
		diskLedEnabled = 1;
	}

    switch(GetModel())
	{
	    case MODEL_RS3614rpxs:
        case MODEL_RS3614xs:
            iInternalHost = 3;
            break;
        case MODEL_RS3415xsp:
            iInternalHost = 2;
            break;
		case MODEL_DS3615xs:
            iInternalHost = 6;
            break;
        default:
            iInternalHost = 0;
            break;
	}

	if (iHostNum < iInternalHost) {
		ret = SetSCSIHostLedStatusByAHCIGPIO(iHostNum, status);;
	} else {
		if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status) {
			iWrite = 1;
		} else {
			iWrite = 0;
		}
		ret = syno_mv_9235_disk_led_set(iHostNum, iWrite);

		iRead = syno_mv_9235_disk_led_get(iHostNum);
		if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
			giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		}
	}
	return ret;
}

/**
 *	Set disk led status for RS3614rpxs, RS3614xs, RS3415xs+ and DS3615xs
 */
static
int SetDiskLedStatusBy9235GPIOandAHCISGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if(!gblDiskNumNeedTran) {
		err = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(disknum - 1, status);
	}else{
		err = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(giDiskMapTable[disknum - 1], status);
	}
END:
	return err;
}

#endif /* MY_DEF_HERE */

/**
 *	Set disk led for DS2414, the first 6 disks use internal ahci, the last 6 use sata mv
 */
static
int SetSCSIHostLedStatusBySataMvandAHCISGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret;
	int iWrite = -1, iRead = -1;
	static int diskLedEnabled = 0;

	// first time we tried to light on disk led
	if (!diskLedEnabled && status != DISK_LED_OFF) {
		InitSynoDiskLed();
		diskLedEnabled = 1;
	}

	if (iHostNum < 6) {
		ret = SetSCSIHostLedStatusByAHCIGPIO(iHostNum, status);
	} else {
		if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status) {
			iWrite = 1;
		} else {
			iWrite = 0;
		}
#ifdef MY_DEF_HERE
		syno_sata_mv_gpio_write(iWrite, iHostNum);

		iRead = syno_sata_mv_gpio_read(iHostNum);
#endif /*MY_DEF_HERE*/
#ifdef MY_DEF_HERE
		if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
			giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		}
#endif /*MY_DEF_HERE*/
		ret = 0;
	}
	return ret;
}

/**
 *	Set disk led for DS2414, the first 6 disks use internal ahci, the last 6 use sata mv
 */
static
int SetDiskLedStatusBySataMvandAHCISGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if(!gblDiskNumNeedTran) {
		err = SetSCSIHostLedStatusBySataMvandAHCISGPIO(disknum - 1, status);
	}else{
		err = SetSCSIHostLedStatusBySataMvandAHCISGPIO(giDiskMapTable[disknum - 1], status);
	}
END:
	return err;
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
int GetCpuTemperatureDenlowI3Transfer(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx;

	if ( NULL == pCpuTemp ) {
		goto END;
	}

#ifdef CONFIG_SYNO_X86_CORETEMP	
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

	if( 0 != iRet) {
		goto END;
	}

	if( 1 == pCpuTemp->blSurface) {
		for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
			pCpuTemp->cpu_temp[iCPUIdx] = (pCpuTemp->cpu_temp[iCPUIdx] * 10 - 264) / 10; // cpu temperature adjust -26.4 degree

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

/**
 * Set HA active led via GPIO
 *
 * @param ops synobios operation set
 * @param ledStatus type of led status
 * @return 0 if succeed
 * -1 if error
 */
static int SetHALedByGPIO(struct synobios_ops *ops, SYNO_AHA_LED ledStatus)
{
	GPIO_PIN greenLedPin, orangeLedPin;

	if (!ops || !ops->set_gpio_pin || -1 == giHALedGreenPin || -1 == giHALedOrangePin) {
		return -1;
	}

	greenLedPin.pin = giHALedGreenPin;
	orangeLedPin.pin = giHALedOrangePin;

	switch (ledStatus) {
		case AHA_LED_OFF:
			greenLedPin.value = 0;
			orangeLedPin.value = 0;
			break;
		case AHA_LED_GREEN_SOLID:
			greenLedPin.value = 1;
			orangeLedPin.value = 0;
			break;
		case AHA_LED_ORANGE_SOLID:
			greenLedPin.value = 0;
			orangeLedPin.value = 1;
			break;
		default:
			printk("Unknown ha led type\n");
			break;
	}

	ops->set_gpio_pin(&greenLedPin);
	ops->set_gpio_pin(&orangeLedPin);
	return 0;
}

static
int HWMONGetThermalSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysThermal)
{
	int iRet = -1;

	if (NULL == SysThermal || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysThermal, hwmon_sensor_list->thermal_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_thermal_sensor(SysThermal);

END:
	return iRet;
}

static
int HWMONGetFanSpeedRPMFromADT(SYNO_HWMON_SENSOR_TYPE *FanSpeedRpm)
{
	int iRet = -1;

	if (NULL == FanSpeedRpm || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(FanSpeedRpm, hwmon_sensor_list->fan_speed_rpm, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_fan_speed_rpm(FanSpeedRpm);

END:
	return iRet;
}

static
int HWMONGetVoltageSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysVoltage)
{
	int iRet = -1;

	if (NULL == SysVoltage || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysVoltage, hwmon_sensor_list->voltage_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_voltage_sensor(SysVoltage);

END:
	return iRet;
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
	.set_cpu_fan_status  = SetCpuFanStatus,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
#ifdef MY_DEF_HERE
	.set_disk_led        = SetDiskLedStatusBySataMvGPIO,
#else
	.set_disk_led        = NULL,
#endif
	.set_alarm_led       = SetAlarmLed,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize		 = Uninitialize,
	.check_microp_id	 = CheckMicropId,
	.set_microp_id		 = SetMicropId,
	.set_buzzer_clear	 = SetBuzzerClear,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led          = NULL,
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	*ops = &synobios_ops;


#if defined(MY_DEF_HERE) && defined(MY_DEF_HERE)
	if ( 0 > syno_libata_disk_map_table_gen(giDiskMapTable)) {
		gblDiskNumNeedTran = 0;
	} else {
		gblDiskNumNeedTran = 1;
	}
#endif

#ifdef MY_DEF_HERE
#ifdef MY_DEF_HERE
	funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBySataMvGPIO;
#endif /* MY_DEF_HERE */
#endif
	switch(GetModel())
	{
	case MODEL_RC18015xsp:
		model_ops = &rc18015xsp_ops;
		synobios_ops.set_disk_led = NULL;
		giHALedGreenPin = 21;
		giHALedOrangePin = 16;
		synobios_ops.set_aha_led = SetHALedByGPIO;
		hwmon_sensor_list = &rc18015xsp_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_RS18016xsp:
		model_ops = &rs18016xsp_ops;
		// SAS model has different disk led control mechanism, it via SES service
		// so we don't hook any function pointer here
		synobios_ops.set_disk_led = NULL;
		hwmon_sensor_list = &rs18016xsp_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_RS3411rpxs:
		model_ops = &rs3411rpxs_ops;
		break;
	case MODEL_RS3411xs:
		model_ops = &rs3411xs_ops;
		break;
	case MODEL_DS3611xs:
		model_ops = &ds3611xs_ops;
		break;
	case MODEL_RS3412rpxs:
		model_ops = &rs3412rpxs_ops;
		break;
	case MODEL_RS3412xs:
		model_ops = &rs3412xs_ops;
		break;
	case MODEL_DS3612xs:
		model_ops = &ds3612xs_ops;
		break;
	case MODEL_DS3615xs:
		model_ops = &ds3615xs_ops;
		synobios_ops.get_fan_status = NULL;
		synobios_ops.get_cpu_temperature = GetCpuTemperatureDenlowI3Transfer;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIOandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#endif // MY_DEF_HERE
#endif // MY_DEF_HERE
		giDiskLedController = DENLOW_DISK_LED_ACTIVATE_PIN;
		hwmon_sensor_list = &ds3615xs_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_RS10613xsp:
		model_ops = &rs10613xsp_ops;
		// SAS model has different disk led control mechanism, it via SES service
		// so we don't hook any function pointer here
		synobios_ops.set_disk_led = NULL;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = NULL;
#endif
		break;
	case MODEL_RS3413xsp:
		model_ops = &rs3413xsp_ops;
		break;
	case MODEL_RS3614xs:
		model_ops = &rs3614xs_ops;
		synobios_ops.get_fan_status = NULL;
		synobios_ops.get_cpu_temperature = GetCpuTemperatureDenlowI3Transfer;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIOandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
		giDiskLedController = DENLOW_DISK_LED_ACTIVATE_PIN;
		hwmon_sensor_list = &rs3614xs_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_RS3614rpxs:
		model_ops = &rs3614rpxs_ops;
		synobios_ops.get_fan_status = NULL;
		synobios_ops.get_cpu_temperature = GetCpuTemperatureDenlowI3Transfer;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIOandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
		giDiskLedController = DENLOW_DISK_LED_ACTIVATE_PIN;
		hwmon_sensor_list = &rs3614rpxs_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_RS3614xsp:
		model_ops = &rs3614xsp_ops;
		synobios_ops.get_fan_status = NULL;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
		giDiskLedController = IVYBRIDGE_DISK_LED_ACTIVATE_PIN;
		hwmon_sensor_list = &rs3614xsp_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	case MODEL_DS2414xs:
		model_ops = &ds2414xs_ops;
		synobios_ops.set_disk_led = SetDiskLedStatusBySataMvandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBySataMvandAHCISGPIO;
#endif /* MY_DEF_HERE */
		giDiskLedController = DENLOW_DISK_LED_ACTIVATE_PIN;
		break;
    case MODEL_RS3415xsp:
		model_ops = &rs3415xsp_ops;
		synobios_ops.get_fan_status = NULL;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
		giDiskLedController = IVYBRIDGE_DISK_LED_ACTIVATE_PIN;
		break;
	case MODEL_RS3617xs:
		model_ops = &rs3617xs_ops;
		synobios_ops.get_fan_status = NULL;
#ifdef MY_DEF_HERE
		synobios_ops.set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
		funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
		giDiskLedController = IVYBRIDGE_DISK_LED_ACTIVATE_PIN;
		hwmon_sensor_list = &rs3617xs_sensor_list;
		synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
		synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
		synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
		break;
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	return 0;
}
