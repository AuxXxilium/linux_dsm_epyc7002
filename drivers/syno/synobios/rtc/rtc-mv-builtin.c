#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "synobios.h"
#include "localtime.h"
#include "rtc.h"

#define DEC2BCD(dec) (((dec/10)*16)+(dec%10))
#define BCD2DEC(val) ((val)=((val)&15) + ((val)>>4)*10)

typedef unsigned char MV_U8;
typedef void MV_VOID;

#if defined(CONFIG_SYNO_ARMADA) || defined(CONFIG_SYNO_ARMADA_V2)
typedef unsigned int MV_U32;
extern SYNO_AUTO_POWERON gPwSched;
#endif
typedef struct _tag_MV_RTC_TIME
{
        MV_U8  seconds;
        MV_U8  minutes;
        MV_U8  hours;
        MV_U8  day;
        MV_U8  date;
        MV_U8  month;
        MV_U8  century;
        MV_U8  year;
} MV_RTC_TIME;

MV_VOID mvRtcTimeSet(MV_RTC_TIME* time);
MV_VOID mvRtcTimeGet(MV_RTC_TIME* time);
#if defined(CONFIG_SYNO_ARMADA) || defined(CONFIG_SYNO_ARMADA_V2)
MV_VOID SYNOmvRtcExtAlarmSet(MV_U32 time);
MV_VOID SYNOmvRtcExtAlarmClean(void);

static MV_U32 rtc_mv_calculate_config_value(unsigned char alarmYear, const struct xtm *nextAlarmTime)
{
	const MV_U32 NSM[12] = {0, 2678400, 5097600, 7776000, 10368000, 13046400, 15638400, 18316800, 20995200, 23587200, 26265600, 28857600};
	MV_U32 configValue = 0;

	// RTC counts year from 2000. _SynoRtcTimePkt starts from 1900.
	if (100 < alarmYear) {
		alarmYear = alarmYear - 100;
	}

	// use years, month, monthday, hours, minutes,seconds to calculate configValue and set it to register
	configValue = alarmYear * 31536000 + (alarmYear/4) * 86400 + NSM[nextAlarmTime->month-1] + (nextAlarmTime->monthday-1) * 86400 + nextAlarmTime->hour * 3600 + nextAlarmTime->minute * 60 + nextAlarmTime->second;

	// in Functional Specification 29.2.4.3
	// ceil(YY/4) * 86400 = (alarmYear/4) * 86400 + (0 != alarmYear % 4)? 86400:0;
	// (alarmYear/4) * 86400 is in above part, and others in below part
	// (YY%4==0) * (MM>2) * 86400 represent to (0 == alarmYear % 4 && nextAlarmTime->month > 2)
	if (0 != alarmYear % 4 || (0 == alarmYear % 4 && nextAlarmTime->month > 2)) {
		configValue = configValue + 86400;
	}

	return configValue;
}
int rtc_mv_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int ret = -1;
	SYNORTCALARMPKT alarmTime = {0};
	unsigned char nextWday = 0;
	int daysLater = 0;
	struct xtm nextAlarmTime;
	unsigned char alarmYear = 0;
	SYNORTCTIMEPKT rtcNow = {0};

	if( NULL == pAutoPowerOn || NULL == pRtcTime ) {
		goto End;
	}

	if ( SYNO_AUTO_POWERON_DISABLE == pAutoPowerOn->enabled ) {
		SYNOmvRtcExtAlarmSet(0);
	} else {
		rtc_get_alarm_time(&alarmTime, pRtcTime);
		// RTC have only one weekday saved in a interrupt register
		nextWday = rtc_get_next_weekday(alarmTime.weekdays, pRtcTime->weekday);
		if (rtc_later_equal_than_int(pRtcTime->hour, pRtcTime->min, alarmTime.hour, alarmTime.min)) {
			nextWday = rtc_get_next_weekday(alarmTime.weekdays, (pRtcTime->weekday+1)%7);
		}

		// generate Alarm time: years, Month, monthday, hours, minutes, seconds
		daysLater = (nextWday - pRtcTime->weekday + 7) % 7;
		rtc_mv_get_time(&rtcNow);
		if (0 == daysLater && (60 * alarmTime.hour + alarmTime.min) <= (60 * rtcNow.hour + rtcNow.min)) {
			daysLater = 7;
		}
		rtc_get_days_later(pRtcTime, daysLater, &nextAlarmTime);

		alarmYear = pRtcTime->year;
		// pRtcTime->month is 0-based, pRtcTime->day is 1-based
		if (11 == pRtcTime->month && 31 < daysLater + pRtcTime->day) {
			alarmYear = alarmYear + 1;
		}
		nextAlarmTime.hour = alarmTime.hour;
		nextAlarmTime.minute = alarmTime.min;
		nextAlarmTime.second = 0;

		SYNOmvRtcExtAlarmSet(rtc_mv_calculate_config_value(alarmYear, &nextAlarmTime));

	}

	ret = 0;
End:
	return ret;
}

int rtc_mv_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn)
{
	int iRet = -1;
	SYNORTCTIMEPKT rtcTime;

	if( NULL == pAutoPowerOn || pAutoPowerOn->num < 0 ) {
		printk("Parameter Error.\n");
		goto End;
	}

	if( 0 > rtc_mv_get_time(&rtcTime) ) {
		printk("Failed to get time from rtc.\n");
		goto End;
	}

	memcpy(&gPwSched, pAutoPowerOn, sizeof(SYNO_AUTO_POWERON));

	if ( 0 != rtc_mv_rotate_auto_poweron(pAutoPowerOn, &rtcTime) ) {
		printk("Failed to set alarm data.\n");
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_mv_auto_poweron_clean(void)
{
	// disable Alarm interrupt
	SYNOmvRtcExtAlarmClean();
	return 0;
}
#endif

int rtc_mv_get_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	MV_RTC_TIME mvTime;
	
	if (NULL == pRtcTimePkt) {
		return -1;
	}

	mvRtcTimeGet(&mvTime);

	pRtcTimePkt->sec = mvTime.seconds;
	pRtcTimePkt->min = mvTime.minutes;
	pRtcTimePkt->hour = mvTime.hours;
	pRtcTimePkt->weekday = mvTime.day;
	pRtcTimePkt->day = mvTime.date;
	pRtcTimePkt->month = mvTime.month - 1;
	pRtcTimePkt->year = mvTime.year + 100;

	return 0;
}

int rtc_mv_set_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	MV_RTC_TIME mvTime;

	if (NULL == pRtcTimePkt) {
		return -1;
	}

	mvTime.seconds = pRtcTimePkt->sec;
	mvTime.minutes = pRtcTimePkt->min;
	mvTime.hours = pRtcTimePkt->hour;
	mvTime.day = pRtcTimePkt->weekday;
	mvTime.date = pRtcTimePkt->day;
	/* Month: Kirkwood RTC is 1-based and _SynoRtcTimePkt is 0-based. */
	mvTime.month = pRtcTimePkt->month + 1;
	/* 
	 * Kirkwood RTC counts year from 2000. _SynoRtcTimePkt starts from 1900.
	 * Check range and fallback to 0 (year 2000).
	 */
	if (100 <= pRtcTimePkt->year) {
		mvTime.year = pRtcTimePkt->year - 100;
	} else {
		mvTime.year = 0;
	}

	mvRtcTimeSet(&mvTime);

	return 0;
}
