#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "synobios.h"
#include "../i2c/i2c-plx.h"
#include "rtc.h"

/*
 * Reset interrupt mode in Status2 register. It'll clear interrupts from RTC.
 */
int rtc_seiko_reset_interrupt_mode(void)
{
	int iRet = -1;
	u8 csr = 0x00;

	iRet = plxI2CCharWrite(SEIKO_RTC_STATUS2_ADDR, (u8 *)&csr, 1, -1);

	// enable interrupt mode for auto poweron
	// [INT1FE, INT1ME, INT1AE, 32kE, INT2FE, INT2ME, INT2AE, TEST] = [0, 0, 1, 0, 0, 0, 1, 0]
	csr = 0x22;
	iRet = plxI2CCharWrite(SEIKO_RTC_STATUS2_ADDR, (u8 *)&csr, 1, -1);

	return iRet;
}

/*
 * Write interrupt register on RTC for auto power-on
 *
 * @param intAddr: SEIKO_RTC_INT1_ADDR or SEIKO_RTC_INT2_ADDR
 * @param data: [weekday, hour, min] (Hour: Richo RTC Format)
 * @param enable: 0 is disable, otherwise enable
 */
static
int rtc_set_interrupt(const unsigned char intAddr, const unsigned char data[3], const int enable)
{
	int iRet = -1;
	unsigned char rgIntReg[3] = {0, 0, 0};

	if( intAddr != SEIKO_RTC_INT1_ADDR && intAddr != SEIKO_RTC_INT2_ADDR ) {
		goto End;
	}

	rgIntReg[2] = RB(data[2]);
	rgIntReg[1] = RB(data[1]);
	rgIntReg[0] = RB(data[0]);

	if( enable ) {
		rgIntReg[2] |= 0x01;
		rgIntReg[1] |= 0x01;
		rgIntReg[0] |= 0x01;
	}

	rtc_seiko_reset_interrupt_mode();

	if( (iRet = plxI2CCharWrite(intAddr, (u8 *)rgIntReg, 3, -1) ) < 0 ) {
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_seiko_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int iRet = -1;
	SYNORTCALARMPKT alarmTime = {0};
	const unsigned char rgEmptyData[3] = {0, 0, 0};
	unsigned char rgIntReg[3] = {0, 0, 0};
	unsigned char nextWday = 0;

    if( NULL == pAutoPowerOn || NULL == pRtcTime ) {
		goto End;
	}

	if( SYNO_AUTO_POWERON_DISABLE == pAutoPowerOn->enabled ) {
		rtc_set_interrupt(SEIKO_RTC_INT1_ADDR, rgEmptyData, 0);
		rtc_set_interrupt(SEIKO_RTC_INT2_ADDR, rgEmptyData, 0);
	} else {
		rtc_get_alarm_time(&alarmTime, pRtcTime);
		// RTC have only one weekday saved in a interrupt register
		nextWday = rtc_get_next_weekday(alarmTime.weekdays, pRtcTime->weekday);
		if (rtc_later_equal_than_int(pRtcTime->hour, pRtcTime->min, alarmTime.hour, alarmTime.min)) {
			nextWday = rtc_get_next_weekday(alarmTime.weekdays, (pRtcTime->weekday+1)%7);
		}
		rgIntReg[0] = nextWday;
		rgIntReg[1] = BIN2BCD(Hour_to_Seiko(alarmTime.hour));
		rgIntReg[2] = BIN2BCD(alarmTime.min);
		rtc_set_interrupt(SEIKO_RTC_INT1_ADDR, rgIntReg, 1);
		rtc_set_interrupt(SEIKO_RTC_INT2_ADDR, rgEmptyData, 0);
	}

	// set *weekdays* to Free register
	BIN_TO_BCD(alarmTime.weekdays);
	if( (iRet = plxI2CCharWrite(SEIKO_RTC_FREE_ADDR, (u8*)&(alarmTime.weekdays),
					sizeof(unsigned char)/sizeof(u8), -1) ) < 0 ) {
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_seiko_get_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	int ret = 0;
	unsigned char rgRtcTimeTemp[7];
	SYNO_AUTO_POWERON schedule;

	if( (ret = plxI2CCharRead(SEIKO_RTC_REALTIME1_ADDR, (u8 *)rgRtcTimeTemp, 
					sizeof(rgRtcTimeTemp)/sizeof(unsigned char), -1) < 0) ) {
		goto End;
	}

	pRtcTimePkt->sec     = BCD2BIN(RB(rgRtcTimeTemp[6]));
	pRtcTimePkt->min     = BCD2BIN(RB(rgRtcTimeTemp[5]));
	pRtcTimePkt->hour    = Seiko_to_Hour(BCD2BIN(RB(rgRtcTimeTemp[4])));
	pRtcTimePkt->weekday = BCD2BIN(RB(rgRtcTimeTemp[3]));
	pRtcTimePkt->day     = BCD2BIN(RB(rgRtcTimeTemp[2]));
	pRtcTimePkt->month   = BCD2BIN(RB(rgRtcTimeTemp[1]));
	pRtcTimePkt->year    = BCD2BIN(RB(rgRtcTimeTemp[0]));

	pRtcTimePkt->year += 100;
	pRtcTimePkt->month -= 1;

	/*
	 * reset the same data to toggle weekday rotation in interrupt registers.
	 * scemd syncs system time with RTC every 1 hour, so it'll also do rotation.
	 */
	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_seiko_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

End:
	return ret;
}

int rtc_seiko_set_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	int iRet = -1;
	SYNO_AUTO_POWERON schedule;
	unsigned char year, mon, hrs;
	unsigned char rgRtcTimeTemp[7];

	year = ((pRtcTimePkt->year + 1900 > 2000) ? (pRtcTimePkt->year - 100) : 0);
	rgRtcTimeTemp[0] = RB(BIN2BCD(year));
	mon = pRtcTimePkt->month + 1;
	rgRtcTimeTemp[1] = RB(BIN2BCD(mon));
	rgRtcTimeTemp[2] = RB(BIN2BCD(pRtcTimePkt->day));
	rgRtcTimeTemp[3] = RB(BIN2BCD(pRtcTimePkt->weekday));
	hrs = Hour_to_Seiko(pRtcTimePkt->hour);
	rgRtcTimeTemp[4] = RB(BIN2BCD(hrs));
	rgRtcTimeTemp[5] = RB(BIN2BCD(pRtcTimePkt->min));
	rgRtcTimeTemp[6] = RB(BIN2BCD(pRtcTimePkt->sec));

	if( (iRet = plxI2CCharWrite(SEIKO_RTC_REALTIME1_ADDR, (u8 *)rgRtcTimeTemp, 
					sizeof(rgRtcTimeTemp)/sizeof(unsigned char), -1) < 0) ) {
		goto End;
	}

	/*
	 * reset the same data to toggle weekday rotation in interrupt registers.
	 */
	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_seiko_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

End:
	return iRet;
}

void rtc_seiko_time_correction_set(void)
{
	unsigned char correction = 3; /* -5.62 sec/day */
	plxI2CCharWrite(SEIKO_RTC_CORRECT_ADDR, &correction, 1, -1);
}

void rtc_seiko_time_correction_get(void)
{
	unsigned char correction = 0;
	plxI2CCharRead(SEIKO_RTC_CORRECT_ADDR, &correction, 1, -1);
	printk("correction with 0x%02x\n", correction);
}

int rtc_seiko_auto_poweron_init(void)
{
	rtc_seiko_time_correction_set();
	rtc_seiko_time_correction_get();
	return rtc_seiko_reset_interrupt_mode();
}
