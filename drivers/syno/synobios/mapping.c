#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.
#ifdef CONFIG_SYNO_RTD1619B
#else /* CONFIG_SYNO_RTD1619B */
#include <asm/uaccess.h>
#endif /* CONFIG_SYNO_RTD1619B */
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include "mapping.h"

#define TTY_MAX_RETRY 10

#if 0
#define	DBGMESG(x...)	printk(x)
#else
#define	DBGMESG(x...)
#endif

#ifdef MY_DEF_HERE
extern unsigned int guiWakeupDisksNum;
extern unsigned int giDenoOfTimeInterval;
#endif

module_t syno_module = MODULE_T_UNKNOWN;

extern int synobios_lock_ttyS_write(const char *szBuf);
extern int synobios_lock_ttyS_read(char *szBuf, int size);
void
module_type_set(module_t *pModule)
{
	if (NULL == pModule) {
#if defined(WARN_ON)
		WARN_ON(1);
#endif
		printk("Module type init error\n");
		goto End;
	}

	syno_module = *pModule;
End:
	return;
}

module_t *
module_type_get(void)
{
	return &syno_module;
}

int 
GetFanNum(int *pFanNum)
{
	int iRet = -1;

	*pFanNum = syno_module.fan_number;

	iRet = 0;
	return iRet;
}

#ifdef MY_DEF_HERE
/**
 * Set the group wakeup config
 *
 * @return 0: success, others: fail
 */
int SetGroupWakeConfig(void)
{
	int iRet = -1;

	if (UNKNOW_DISK_DENO == syno_module.group_wake_config) {
		goto END;
	}

	guiWakeupDisksNum = (unsigned int)(syno_module.group_wake_config >> 4);
	giDenoOfTimeInterval = (unsigned int)(syno_module.group_wake_config & 0x0F);

	if (ONE_DISK_DENO_ONE == syno_module.group_wake_config) {
		printk("This is default settings: ");
	}
	printk("set group disks wakeup number to %d, spinup time deno %d\n",
			guiWakeupDisksNum, giDenoOfTimeInterval);

	iRet = 0;

END:
	return iRet;
}
#endif

#if defined(CONFIG_SYNO_X86) || defined(CONFIG_SYNO_X64)
int SetUart(const char* cmd)
{
	int err = -1;
	int writeRet = -1;
	char cmdbuf[16] = {0};

	if (NULL == cmd) {
		goto ERR;
	}
	snprintf(cmdbuf, sizeof(cmdbuf), "%s", cmd);

	writeRet = synobios_lock_ttyS_write(cmdbuf);
	err = 0;
ERR:
	return err;
}

/*
 * Set Uart command and read command result
 * @return: -1 means error, 0 means success
 */
int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng)
{
	int err = -1;
	int iCount = 0;

	if (SetUart(szGoCmd)) {
		goto ERR;
	}
	// wait for ttyS1 input
	msleep(500);
	iCount = synobios_lock_ttyS_read(szResult, leng);

	if (SetUart(szStopCmd)) {
		goto ERR;
	}

	err = 0;
ERR:
	return err;
}

int SetMicropId(void)
{
	static SYNO_MICROP_ID MpId = MICROP_ID_UNKNOW;
	int iRet = -1;
	char szbuf[8] = {'\0'};

	if (MICROP_ID_UNKNOW == syno_module.microp_id) {
		printk("get microp fail\n");
		goto END;
	}

	/* if unknow means the MpId not matched or not init before */ 
	if (MICROP_ID_UNKNOW == MpId) {
		/* check_if the model and microp was matched */
		if (ReadUart(MICROP_CMD_READID, MICROP_CMD_READID, szbuf, sizeof(szbuf))) {
			goto END;
		}

		MpId = (SYNO_MICROP_ID)szbuf[0];
		/* FIXME: we not check microp id now, if got successfully we treat it ok */
		/* FIXME: RS3614(RP)xs have two different MicroP ID. If the check need to be
		          re-open, please remember to handle this special case */
#if 0
		if (MpId != syno_module.microp_id) {
			/* not matched, say Thank you to user, and reset MpId to unknow */
			printk("!!! Thank you, please purchase Synology products !!!\n");
			MpId = MICROP_ID_UNKNOW;
			goto END;
		}
#endif
	}

	iRet = 0;

END:
	return iRet;
}

int
CheckMicropId(const struct synobios_ops *pSynobiosOps)
{
	int iRet = -1;
	int iTries = 0;

	if (!pSynobiosOps || !pSynobiosOps->set_microp_id) {
		printk("no ops\n");
		goto END;
	}

	if (MICROP_ID_UNKNOW == syno_module.microp_id) {
		printk("get microp fail\n");
		goto END;
	}

	/* if fail re-try 3 times */
	for (iTries = 0; iTries < 3 && iRet; ++iTries) {
		if ((iRet = pSynobiosOps->set_microp_id())) {
			msleep(500);
		}
	}

END:
	return iRet;
}
#endif

int 
GetHwCapability(CAPABILITY *pCapability)
{
	int iRet = -1;

	if ( NULL == pCapability ) {
		iRet = -EINVAL;
		goto End;
	}

	pCapability->support = 0;

	switch (pCapability->id) {
	case CAPABILITY_DISK_LED_CTRL:
		if (DISK_LED_CTRL_SW == syno_module.diskled_ctrl_type) {
			pCapability->support = 1;
		}
		break;
	case CAPABILITY_THERMAL:
		if (THERMAL_YES == syno_module.thermal_type) {
			pCapability->support = 1;
		}
		break;
	case CAPABILITY_AUTO_POWERON:
		if (AUTO_POWERON_YES == syno_module.auto_poweron_type) {
			pCapability->support = 1;
		}
		break; 
	case CAPABILITY_CPU_TEMP:
			if (CPUTMP_YES == syno_module.cputmp_type) {
				pCapability->support = 1;
			}
			break;
	case CAPABILITY_S_LED_BREATH:
			if (HIBER_LED_STATUS_BREATH == syno_module.hibernate_led) {
				pCapability->support = 1;
			}
			break;
	case CAPABILITY_FAN_RPM_RPT:
            if (FAN_RPM_RPT_YES == syno_module.fan_rpm_rpt_type) {
				pCapability->support = 1;
			}
			break;
	case CAPABILITY_MICROP_PWM:
			if(FAN_MICROP_PWM == syno_module.fan_type || 
				FAN_MICROP_PWM_WITH_CPUFAN == syno_module.fan_type || 
				FAN_MICROP_PWM_WITH_GPIO == syno_module.fan_type || 
				FAN_MICROP_PWM_WITH_CPUFAN_AND_GPIO == syno_module.fan_type){
				pCapability->support = 1;
			}
			break;
	case CAPABILITY_CARDREADER:
			if(CARDREADER_YES == syno_module.has_cardreader){
				pCapability->support = 1;
			}
			break;
	default:
		iRet = -EINVAL;
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int
FanStatusMappingType1(FAN_STATUS status, FAN_SPEED speed, char *pSpeed_value)
{
	int ret = -1;

	if (status == FAN_STATUS_STOP) {
		*pSpeed_value = CPLD_FAN_SPEED_0;
	} else {
		switch (speed) {
		case FAN_SPEED_STOP:
		case FAN_SPEED_TEST_0:
			*pSpeed_value = CPLD_FAN_SPEED_0;
			break;
		case FAN_SPEED_ULTRA_LOW:
		case FAN_SPEED_TEST_1:
			*pSpeed_value = CPLD_FAN_SPEED_1;
			break;
		case FAN_SPEED_VERY_LOW:
		case FAN_SPEED_TEST_2:
			*pSpeed_value = CPLD_FAN_SPEED_2;
			break;
		/* by spec. The fan speed of 3/4 is inverted. 
		 * Because the last resistance is smaller than the sum of the others.
		 */
		case FAN_SPEED_MIDDLE:
		case FAN_SPEED_TEST_3:
			*pSpeed_value = CPLD_FAN_SPEED_3;
			break;
		case FAN_SPEED_LOW:
		case FAN_SPEED_TEST_4:
			*pSpeed_value = CPLD_FAN_SPEED_4;
			break;
		case FAN_SPEED_HIGH:
		case FAN_SPEED_TEST_5:
			*pSpeed_value = CPLD_FAN_SPEED_5;
			break;
		case FAN_SPEED_VERY_HIGH:
		case FAN_SPEED_TEST_6:
			*pSpeed_value = CPLD_FAN_SPEED_6;
			break;
		case FAN_SPEED_ULTRA_HIGH:
		case FAN_SPEED_FULL:
		case FAN_SPEED_TEST_7:
			*pSpeed_value = CPLD_FAN_SPEED_7;
			break;
		case FAN_SPEED_TEST_8:
                        *pSpeed_value = CPLD_FAN_SPEED_8;
                        break;
		case FAN_SPEED_TEST_9:
                        *pSpeed_value = CPLD_FAN_SPEED_9;
                        break;
		case FAN_SPEED_TEST_10:
                        *pSpeed_value = CPLD_FAN_SPEED_10;
                        break;
		case FAN_SPEED_TEST_11:
                        *pSpeed_value = CPLD_FAN_SPEED_11;
                        break;
		case FAN_SPEED_TEST_12:
                        *pSpeed_value = CPLD_FAN_SPEED_12;
                        break;
		case FAN_SPEED_TEST_13:
                        *pSpeed_value = CPLD_FAN_SPEED_13;
                        break;
		case FAN_SPEED_TEST_14:
                        *pSpeed_value = CPLD_FAN_SPEED_14;
                        break;
		case FAN_SPEED_TEST_15:
                        *pSpeed_value = CPLD_FAN_SPEED_15;
                        break;
		case FAN_SPEED_TEST_16:
                        *pSpeed_value = CPLD_FAN_SPEED_16;
                        break;
		case FAN_SPEED_TEST_17:
                        *pSpeed_value = CPLD_FAN_SPEED_17;
                        break;
		default:
			goto END;
		}
	}

	ret = 0;
END:
	return ret;
}

int
FanStatusMappingType2(FAN_STATUS status, FAN_SPEED speed, char *pSpeed_value)
{
	int ret = -1;

	if (status == FAN_STATUS_STOP) {
		*pSpeed_value = CPLD_FAN_SPEED_0;
	} else {
		switch (speed) {
		case FAN_SPEED_STOP:
		case FAN_SPEED_TEST_0:
			*pSpeed_value = CPLD_FAN_SPEED_0;
			break;
		case FAN_SPEED_ULTRA_LOW:
		case FAN_SPEED_TEST_1:
			*pSpeed_value = CPLD_FAN_SPEED_1;
			break;
		case FAN_SPEED_VERY_LOW:
        case FAN_SPEED_TEST_2:
			*pSpeed_value = CPLD_FAN_SPEED_2;
			break;
		case FAN_SPEED_LOW:
		case FAN_SPEED_TEST_3:
			*pSpeed_value = CPLD_FAN_SPEED_3;
			break;
		case FAN_SPEED_MIDDLE:	
		case FAN_SPEED_TEST_4:
			*pSpeed_value = CPLD_FAN_SPEED_4;
			break;
		case FAN_SPEED_HIGH:
		case FAN_SPEED_TEST_5:
			*pSpeed_value = CPLD_FAN_SPEED_5;
			break;
		case FAN_SPEED_VERY_HIGH:
		case FAN_SPEED_TEST_6:
			*pSpeed_value = CPLD_FAN_SPEED_6;
			break;
		case FAN_SPEED_ULTRA_HIGH:
		case FAN_SPEED_FULL:
		case FAN_SPEED_TEST_7:
			*pSpeed_value = CPLD_FAN_SPEED_7;
			break;
		default:
			goto END;
		}
	}

	ret = 0;
END:
	return ret;
}

EUNIT_PWRON_TYPE GetEUnitType(void)
{
	return syno_module.eunit_pwron_type;
}
