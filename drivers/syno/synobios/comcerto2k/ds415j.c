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
#include "syno_ttyS.h"

extern void SYNO_MASK_HDD_LED(int blEnable);
int GetModel(void)
{
	return MODEL_DS415j;
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
	int rgcVolt[2] = {0, 0};

	if ( 1 > fanno || 3 < fanno) {
		return -EINVAL;
	}

	do {
		SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus);
		rgcVolt[(int)FanStatus] ++;
		if (rgcVolt[0] && rgcVolt[1]) {
			break;
		}
		udelay(300);
	} while ( (rgcVolt[0] + rgcVolt[1]) < 200 );

	if ((rgcVolt[0] == 0) || (rgcVolt[1] == 0) ) {
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
	module_t type_415j = MODULE_T_DS415jv10;
	module_t *pType = NULL;

	switch (model) {
		case MODEL_DS415j:
			pType = &type_415j;
			break;
		default:
			break;
	}

	module_type_set(pType);

	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
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

extern void syno_genphy_ledcontrol(const int);
int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
		case SYNO_LED_OFF:
			syno_genphy_ledcontrol(ledStatus);
			break;
		default:
			goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	SYNO_MASK_HDD_LED(ledStatus);
	return 0;
}

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
static
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	int iWrite = -1;

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

	if (1 > iDiskNum || 4 < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	iRet = SetSCSIHostLedStatusBy9235GPIO(iDiskNum - 1, iStatus);
END:
	return iRet;

}
#endif /* MY_DEF_HERE */
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
int model_addon_init(struct synobios_ops *ops)
{

	SYNO_MASK_HDD_LED(1);
#ifdef MY_DEF_HERE
	ops->set_disk_led = SetDiskLedStatusBy9235GPIO;
#ifdef MY_DEF_HERE
	funcSYNOSATADiskLedCtrl = SetSCSIHostLedStatusBy9235GPIO;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
	ops->set_phy_led	 = SetPhyLed;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{

	ops->set_phy_led	 = NULL;
	return 0;
}
