#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "synobios.h"
#include "rtc.h"
#include "../i2c/i2c-ppc.h"

int rtc_ricoh_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int iRet = -1;
	u8 csr = 0;
	SYNORTCALARMPKT alarmTime = {0};

    if( NULL == pAutoPowerOn || NULL == pRtcTime ) {
		goto End;
	}

	// get Alarm enable bit
	if ( 0 > (iRet = mpc_i2c_read(I2C_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), 0, I2C_RTC_CONTROL1_OFFSET)) ) {
		goto End;
	}

    if ( SYNO_AUTO_POWERON_DISABLE == pAutoPowerOn->enabled ) {
        // disable Alarm enable bit
		csr &= ~I2C_RTC_ALARMA_ENABLE;
		if ( 0 > (iRet = mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), 0, I2C_RTC_CONTROL1_OFFSET)) ) {
			goto End;
		}
    } else {
		rtc_get_alarm_time(&alarmTime, pRtcTime);
		alarmTime.hour = Hour_to_Ricoh(alarmTime.hour);
		BIN_TO_BCD(alarmTime.hour);
		BIN_TO_BCD(alarmTime.min);

		// enable Alarm
		csr |= I2C_RTC_ALARMAB_SL;
		csr |= I2C_RTC_ALARMA_ENABLE;
		if ( 0 > (iRet = mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), 0, I2C_RTC_CONTROL1_OFFSET)) ) {
			goto End;
		}

		csr = 0;
		if ( 0 > (iRet = mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), 0, I2C_RTC_CONTROL2_OFFSET)) ) {
			goto End;
		}
		
		// set Alarm data
		if ( 0 > (iRet = mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&alarmTime, sizeof(SYNORTCALARMPKT)/sizeof(u8), 0, I2C_RTC_ALARMA_OFFSET)) ) {
			goto End;
		}
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_ricoh_get_time(struct _SynoRtcTimePkt *pRtcTimePkt)
{
	int ret = 0;
	unsigned char rgRtcTimeTemp[7];
	unsigned char hrs;
	SYNO_AUTO_POWERON schedule;

	ret = mpc_i2c_read(0x32, (u8 *)rgRtcTimeTemp, 7, 0, 0);
	if (0 > ret){
		goto END;
	}

	pRtcTimePkt->sec	= BCD2BIN(rgRtcTimeTemp[0]);
	pRtcTimePkt->min	= BCD2BIN(rgRtcTimeTemp[1]);
	hrs = BCD2BIN(rgRtcTimeTemp[2]);
	pRtcTimePkt->hour = Ricoh_to_Hour(hrs);
	pRtcTimePkt->weekday	= BCD2BIN(rgRtcTimeTemp[3]);
	pRtcTimePkt->day	= BCD2BIN(rgRtcTimeTemp[4]);
	pRtcTimePkt->month	= BCD2BIN(rgRtcTimeTemp[5]);
	pRtcTimePkt->year	= BCD2BIN(rgRtcTimeTemp[6]);

	pRtcTimePkt->year += 100;
	pRtcTimePkt->month -= 1;

	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_ricoh_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

END:
	return ret;
}

int rtc_ricoh_set_time(struct _SynoRtcTimePkt *pRtcTimePkt)
{
	int ret = 0;
	unsigned char year, mon, hrs;
	unsigned char rgRtcTimeTemp[7];
	SYNO_AUTO_POWERON schedule;

	year = ((pRtcTimePkt->year + 1900 > 2000) ? (pRtcTimePkt->year - 100) : 0);
	rgRtcTimeTemp[6] = BIN2BCD(year);
	mon = pRtcTimePkt->month + 1;
	rgRtcTimeTemp[5] = BIN2BCD(mon);
	rgRtcTimeTemp[4] = BIN2BCD(pRtcTimePkt->day);
	rgRtcTimeTemp[3] = BIN2BCD(pRtcTimePkt->weekday);
	hrs = Hour_to_Ricoh(pRtcTimePkt->hour);
	rgRtcTimeTemp[2] = BIN2BCD(hrs);
	rgRtcTimeTemp[1] = BIN2BCD(pRtcTimePkt->min);
	rgRtcTimeTemp[0] = BIN2BCD(pRtcTimePkt->sec);

	ret = mpc_i2c_write(0x32, (u8 *)rgRtcTimeTemp, 7, 0, 0);

	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_ricoh_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

	return ret;
}

void rtc_ricoh_time_correction_set(void)
{
    unsigned char correction = 0x0f;
    mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&correction, 1, 0, I2C_RTC_TIME_TRIM_OFFSET);
}

void rtc_ricoh_time_correction_get(void)
{
    unsigned char correction = 0;
    mpc_i2c_read(I2C_RTC_ADDR, (u8 *)&correction, 1, 0, I2C_RTC_TIME_TRIM_OFFSET);
    printk("correction with 0x%02x\n", correction);
}

int rtc_ricoh_auto_poweron_init(void)
{
	int err = -1;
	u8 csr = 0;

	// disable matched bit
	if ( 0 > (err = mpc_i2c_write(I2C_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), 0, I2C_RTC_CONTROL2_OFFSET)) ) {
		goto End;
	}

#ifdef MY_DEF_HERE
    if ( strncmp(gszSynoHWVersion, HW_DS409p, strlen(HW_DS409p) ) &&
         strncmp(gszSynoHWVersion, HW_DS410, strlen(HW_DS410) ) )
    {
        rtc_ricoh_time_correction_set();
        rtc_ricoh_time_correction_get();
    }
#endif

	err = 0;

End:
	return err;
}
