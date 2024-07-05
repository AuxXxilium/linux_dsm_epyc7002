#include <linux/rtc.h>
#include "rtc.h"

#define SYNO_RTC_RTK_DEVICE "rtc0"

extern SYNO_AUTO_POWERON gPwSched;

int rtc_rtk_set_time(SYNORTCTIMEPKT* pRtcTimePkt)
{
	int iRet = -1;
	struct rtc_time tm = {0};
	struct rtc_device *rtc = rtc_class_open(SYNO_RTC_RTK_DEVICE);

	if (rtc == NULL) {
		printk("synobios: unable to open rtc device (%s)\n", SYNO_RTC_RTK_DEVICE);
		goto End;
	}

	tm.tm_sec  = pRtcTimePkt->sec;
	tm.tm_min  = pRtcTimePkt->min;
	tm.tm_hour = pRtcTimePkt->hour;
	tm.tm_mday = pRtcTimePkt->day;
	tm.tm_mon  = pRtcTimePkt->month;
	tm.tm_year = pRtcTimePkt->year;
	tm.tm_wday = pRtcTimePkt->weekday;

	iRet = rtc_set_time(rtc, &tm);
	if (iRet) {
		printk("synobios: unable to set the hardware clock\n");
		goto End;
	}
End:
	return iRet;
}

int rtc_rtk_get_time(SYNORTCTIMEPKT* pRtcTimePkt)
{
	int iRet = -1;
	struct rtc_time tm = {0};
	struct rtc_device *rtc = rtc_class_open(SYNO_RTC_RTK_DEVICE);

	if (rtc == NULL) {
		printk("synobios: unable to open rtc device (%s)\n", SYNO_RTC_RTK_DEVICE);
		goto End;
	}

	if (0 != rtc_read_time(rtc, &tm)) {
		printk("synobios: unable to read the hardware clock\n");
		rtc_class_close(rtc);
		goto End;
	}

	if (0 != rtc_valid_tm(&tm)) {
		printk("synobios: invalid date/time read from rtc device\n");
		rtc_class_close(rtc);
		goto End;
	}

	pRtcTimePkt->sec     = tm.tm_sec;
	pRtcTimePkt->min     = tm.tm_min;
	pRtcTimePkt->hour    = tm.tm_hour;
	pRtcTimePkt->day     = tm.tm_mday;
	pRtcTimePkt->month   = tm.tm_mon;
	pRtcTimePkt->year    = tm.tm_year;
	pRtcTimePkt->weekday = tm.tm_wday;

	iRet = 0;
End:
	return iRet;
}

static int rtc_rtk_set_alarm_irq_enabled(int enabled)
{
	int iRet = -1;
	struct rtc_device *rtc = rtc_class_open(SYNO_RTC_RTK_DEVICE);

	if (rtc == NULL) {
		printk("synobios: unable to open rtc device (%s)\n", SYNO_RTC_RTK_DEVICE);
		goto End;
	}
	rtc_alarm_irq_enable(rtc, enabled);
	iRet = 0;
End:
	return iRet;
}

int rtc_rtk_enable_alarm_irq(void)
{
	return rtc_rtk_set_alarm_irq_enabled(1);
}

static unsigned long alarm_unix_time_since_1970(unsigned char alarmYear, const struct xtm *nextAlarmTime)
{
	const unsigned long NSM[12] = {0, 2678400, 5097600, 7776000, 10368000, 13046400, 15638400, 18316800, 20995200, 23587200, 26265600, 28857600};
	const unsigned long OFFSET_1970 = 946684800;  // seconds between 1970/01/01 00:00:00 ~ 2000/01/01 00:00:00
	unsigned long seconds = 0;  // seconds since 2000/01/01 00:00:00

	// RTC counts year from 2000. _SynoRtcTimePkt starts from 1900.
	if (100 < alarmYear) {
		alarmYear = alarmYear - 100;
	}

	seconds = alarmYear * 31536000 + (alarmYear / 4) * 86400 + NSM[nextAlarmTime->month - 1] + (nextAlarmTime->monthday - 1) * 86400 + nextAlarmTime->hour * 3600 + nextAlarmTime->minute * 60 + nextAlarmTime->second;

	if (0 != alarmYear % 4 || (0 == alarmYear % 4 && nextAlarmTime->month > 2)) {
		seconds += 86400;
	}

	return seconds + OFFSET_1970;
}

static unsigned long convert_next_alarm_time_to_unix_time(const SYNORTCTIMEPKT *pRtcTime)
{
	SYNORTCALARMPKT alarmTime = {0};
	unsigned char nextWday = 0;
	int daysLater = 0;
	struct xtm nextAlarmTime = {0};
	unsigned char alarmYear = 0;
	SYNORTCTIMEPKT rtcNow = {0};

	rtc_get_alarm_time(&alarmTime, pRtcTime);
	nextWday = rtc_get_next_weekday(alarmTime.weekdays, pRtcTime->weekday);
	if (rtc_later_equal_than_int(pRtcTime->hour, pRtcTime->min, alarmTime.hour, alarmTime.min)) {
		nextWday = rtc_get_next_weekday(alarmTime.weekdays, (pRtcTime->weekday + 1) % 7);
	}

	daysLater = (nextWday - pRtcTime->weekday + 7) % 7;
	rtc_rtk_get_time(&rtcNow);
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

	return alarm_unix_time_since_1970(alarmYear, &nextAlarmTime);
}

static int rtc_rtk_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int iRet = -1;
	struct rtc_wkalrm alarm = {0};
	struct rtc_time time= {0};
	struct rtc_device *rtc = rtc_class_open(SYNO_RTC_RTK_DEVICE);;

	if (NULL == pAutoPowerOn || NULL == pRtcTime) {
		goto End;
	}

	if (rtc == NULL) {
		printk("synobios: unable to open rtc device (%s)\n", SYNO_RTC_RTK_DEVICE);
		iRet = -1;
		goto End;
	}

	if (SYNO_AUTO_POWERON_ENABLE == pAutoPowerOn->enabled) {
		rtc_time_to_tm(convert_next_alarm_time_to_unix_time(pRtcTime), &time);
		alarm.time = time;
		alarm.enabled = 1;
		rtc_set_alarm(rtc, &alarm);
	}
	rtc_alarm_irq_enable(rtc, pAutoPowerOn->enabled);

	iRet = 0;
End:
	return iRet;
}

int rtc_rtk_set_alarm(SYNO_AUTO_POWERON *pAutoPowerOn)
{
	int iRet = -1;
	SYNORTCTIMEPKT rtcTime;

	if (NULL == pAutoPowerOn || pAutoPowerOn->num < 0) {
		printk("synobios: RTC set alarm parameter error\n");
		goto End;
	}

	if (0 != rtc_rtk_get_time(&rtcTime)) {
		goto End;
	}

	memcpy(&gPwSched, pAutoPowerOn, sizeof(SYNO_AUTO_POWERON));

	if (0 != rtc_rtk_rotate_auto_poweron(pAutoPowerOn, &rtcTime)) {
		printk("synobios: Failed to set alarm data\n");
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_rtk_get_alarm(SYNO_AUTO_POWERON* pAutoPowerOn)
{
	int iRet = -1;

	if (NULL == pAutoPowerOn) {
		goto End;
	}

	memcpy(pAutoPowerOn, &gPwSched, sizeof(SYNO_AUTO_POWERON));
	iRet = 0;
End:
	return iRet;
}
