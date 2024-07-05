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
#include <linux/gpio.h>
#include "../rtc/rtc.h"
#include "evansport_common.h"
#include "syno_ttyS.h"
#ifdef MY_DEF_HERE
#include <linux/cpu.h>
#endif

#ifdef MY_DEF_HERE
extern void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault);
#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
extern void e1000_syno_led_switch(int iEnable);
#endif /* MY_DEF_HERE */

static int Uninitialize(void);

static struct model_ops *model_ops = NULL;

static int SetGpioPin( GPIO_PIN *pPin )
{
	int iRet = -1;

	if ( NULL == pPin ) {
		goto END;
	}

	if ( 0 != SYNO_EVANSPORT_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 1) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

static int GetGpioPin( GPIO_PIN *pPin )
{
	int iRet = -1;

	if ( NULL == pPin ) {
		goto END;
	}

	if ( 0 != SYNO_EVANSPORT_GPIO_PIN((int)pPin->pin, (int*)&pPin->value, 0) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

static
int GetFanStatusMircopWithPWMGPIO(int fanno, FAN_STATUS *pStatus)
{
	int gpio_fan_map[] = {16, 100};
	int fanNum = sizeof(gpio_fan_map)/sizeof(gpio_fan_map[0]);
	GPIO_PIN gpiopin;
	int rgcStat[2] = {0, 0};

	if (pStatus == NULL) {
		return -EINVAL;
	}

	if (fanno > fanNum) {
		return -EINVAL;
	}

	do {
		gpiopin.pin = gpio_fan_map[fanno-1];
		GetGpioPin(&gpiopin);
		rgcStat[gpiopin.value] ++;
		udelay(300);
	} while (200 > rgcStat[0] + rgcStat[1]);

	if ((rgcStat[0] == 0) || (rgcStat[1] == 0)) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}

	return 0;
}

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
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

int GetModel(void)
{
	int model = MODEL_DS214play;

	if ( !strncmp(gszSynoHWVersion, HW_DS214play, strlen(HW_DS214play) ) ) {
		model = MODEL_DS214play;
	}else if ( !strncmp(gszSynoHWVersion, HW_DS114p, strlen(HW_DS114p) ) ) {
		model = MODEL_DS114p;
	}else if ( !strncmp(gszSynoHWVersion, HW_DS415play, strlen(HW_DS415play) ) ) {
		model = MODEL_DS415play;
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

#ifdef MY_DEF_HERE
/*FIXME - Too brutal and directly, should separate into levels*/
static
int SetDiskLedStatusBySataMvGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum) {
		goto END;
	}

	/*Scsi host in kernel is zero-based, disknum here is one-based,
	 *so we should minus 1 while calling the function
	 */
	if (DISK_LED_ORANGE_BLINK == status || DISK_LED_ORANGE_SOLID == status){
		syno_sata_mv_gpio_write( 1, disknum - 1);
	}else{
		syno_sata_mv_gpio_write( 0, disknum - 1);
	}

	err = 0;
END:
	return err;
}
#endif /* MY_DEF_HERE */

static
int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	int err = -1;
	GPIO_PIN PinAct, PinFaulty;

	if ( status == DISK_LED_ORANGE_BLINK ) {
		status = DISK_LED_ORANGE_SOLID;
	}

	if ( status == DISK_LED_GREEN_SOLID ) {
		PinAct.value = 0;
		PinFaulty.value = 1;
	} else if ( status == DISK_LED_ORANGE_SOLID ) {
		PinAct.value = 1;
		PinFaulty.value = 0;
	} else if ( status == DISK_LED_OFF ) {
		PinAct.value = 1;
		PinFaulty.value = 1;
	}

	switch (disknum) {
	case 1:
		PinAct.pin = SYNO_GPP_HDD1_LED_ACT;
		PinFaulty.pin = SYNO_GPP_HDD1_LED_FAULTY;
		break;
	case 2:
		PinAct.pin = SYNO_GPP_HDD2_LED_ACT;
		PinFaulty.pin = SYNO_GPP_HDD2_LED_FAULTY;
		break;
	case 3:
		if (model_ops && model_ops->x86_set_esata_led_status) {
			model_ops->x86_set_esata_led_status(status);
		}
		goto ESATA_END;
	case 4 ... 16:
		err = 0;
		goto END;
	default:
		printk("Wrong HDD number [%d]\n", disknum);
		goto END;
	}

	SetGpioPin(&PinAct);
	SetGpioPin(&PinFaulty);

ESATA_END:
    err = 0;
END:
    return err;
}

static int SetHddActLed(SYNO_LED ledStatus)
{
	int iError = -1;
	GPIO_PIN  LedActivate;

	switch(ledStatus){
		case SYNO_LED_OFF:
			LedActivate.value = 1;
			break;
		case SYNO_LED_ON:
			LedActivate.value = 0;
			break;
		default:
			goto ERR;
	}

	LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;

	SetGpioPin(&LedActivate);

	iError = 0;
ERR:
	return iError;
}

static int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
		case SYNO_LED_OFF:
#ifdef MY_DEF_HERE
			e1000_syno_led_switch(ledStatus);
#endif
			break;
		default:
			goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

static
int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp)
{
	return 0;
}

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;

	if ( NULL == pCpuTemp ) {
		goto END;
	}

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_X86_CORETEMP)
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /*MY_DEF_HERE*/

END:
	return iRet;
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

int SetPowerLedStatus(SYNO_LED status)
{
	char szCommand[5] = {0};
	int err = -1;

        switch(status){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > synobios_lock_ttyS_write(szCommand)) {
		goto ERR;
	}

	err = 0;
ERR:
	return err;
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
	.get_rtc_time        = rtc_seiko_get_time,
	.set_rtc_time        = rtc_seiko_set_time,
	.get_auto_poweron	 = rtc_get_auto_poweron,
	.set_auto_poweron	 = rtc_seiko_set_auto_poweron,
	.get_fan_status      = GetFanStatusMircopWithPWMGPIO,
	.set_fan_status      = SetFanStatus,
	.get_sys_temperature = GetSysTemperature,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = SetCpuFanStatus,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = SetDiskLedStatus,
	.set_hdd_led         = SetHddActLed,
	.set_alarm_led       = NULL,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.set_power_led       = SetPowerLedStatus,
	.set_phy_led         = SetPhyLed,
	.init_auto_poweron   = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron = rtc_seiko_auto_poweron_uninit,
	.uninitialize		 = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led         = NULL,
};

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	GPIO_PIN  LedActivate;
	GPIO_PIN  USBPower;
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_DS214play:
			model_ops = &ds214play_ops;

			LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
			LedActivate.value = 0;

			SetGpioPin(&LedActivate);
			break;
		case MODEL_DS114p:
			model_ops = &ds114p_ops;

			LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
			LedActivate.value = 0;

			SetGpioPin(&LedActivate);
			break;
        case MODEL_DS415play:
			model_ops = &ds415play_ops;
#ifdef MY_DEF_HERE
			synobios_ops.set_disk_led = SetDiskLedStatusBySataMvGPIO;
#endif
			LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
			LedActivate.value = 0;

			SetGpioPin(&LedActivate);

			USBPower.pin = SYNO_GPP_USB_POWER;
            USBPower.value = 1;

			SetGpioPin(&USBPower);

			break;
		default:
			break;
	}

	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	return 0;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	GPIO_PIN LedActivate;

	//Signal to deactivate the leds.
	LedActivate.pin = SYNO_GPP_LEDS_ACTIVATE;
	LedActivate.value = 1;

	SetGpioPin(&LedActivate);

	if( synobios_ops.get_rtc_time ) {
		synobios_ops.get_rtc_time(&rtc_time_pkt);
	}

	if( synobios_ops.uninit_auto_poweron ) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}
