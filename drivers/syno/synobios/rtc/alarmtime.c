#include "synobios.h"
#include <linux/string.h>
#include "localtime.h"

extern SYNO_AUTO_POWERON gPwSched;

unsigned char rtc_get_next_weekday(const unsigned char weekdays, const unsigned char cur_wday)
{
	unsigned char u8Nextday = 0xFF;
	unsigned int mask = 1 << cur_wday;
	unsigned int weekdays_masked = weekdays & AUTO_POWERON_WEEKDAY_MASK;

	if( weekdays_masked == 0 || mask == 0 ) { // no days set in weekday bitsmask
		goto End;
	}

	u8Nextday = cur_wday;
	weekdays_masked |= weekdays_masked << 7; // duplicate days into next week

	while( !(mask & weekdays_masked) ) {
		mask <<= 1;
		u8Nextday++;
	}
	u8Nextday %= 7;

End:
	return u8Nextday;
}

void rtc_get_days_later(const SYNORTCTIMEPKT *pRtcTime, const int daysLater, struct xtm *tagetTime)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	time64_t t;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	time_t t;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	t = mktime64(pRtcTime->year+1900, pRtcTime->month+1, pRtcTime->day, pRtcTime->hour, pRtcTime->min, pRtcTime->sec);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	t = mktime(pRtcTime->year+1900, pRtcTime->month+1, pRtcTime->day, pRtcTime->hour, pRtcTime->min, pRtcTime->sec);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	t += 86400 * daysLater;
	localtime_1(tagetTime, t);
	localtime_2(tagetTime, t);
	localtime_3(tagetTime, t);

}

static
unsigned char rtc_get_next_mday(const SYNORTCTIMEPKT *pRtcTime, const int daysLater)
{
	struct xtm tagetTime;

	rtc_get_days_later(pRtcTime, daysLater, &tagetTime);
	return (unsigned char)tagetTime.monthday;
}

int rtc_later_equal_than_int(unsigned char rtcHour, const unsigned char rtcMin,
                       unsigned char intHour, const unsigned char intMin)
{
	if( rtcHour > intHour || (rtcHour == intHour && rtcMin >= intMin) ) {
		return 1;
	}

	return 0;
}

unsigned char rtc_get_alarm_time(SYNORTCALARMPKT *pAlarmTime, const SYNORTCTIMEPKT *pRtcTime)
{
	int iLoop = 0;
	unsigned char next_wday;
	unsigned char wdays, mday, hrs, min;
	unsigned char wdays_result = 0;
	unsigned char mday_result = 0;
	unsigned char hrs_result = 0;
	unsigned char min_result = 0;
	int min_day_offset = 0;

	if (NULL == pAlarmTime || NULL == pRtcTime) {
		goto End;
	}

	for (iLoop = 0; iLoop < gPwSched.num; iLoop++) {
		int over_cur_time = 0;
		int day_offset = 0;
		int blSwitch = 0;

		wdays = gPwSched.RtcAlarmPkt[iLoop].weekdays;
		next_wday = rtc_get_next_weekday(gPwSched.RtcAlarmPkt[iLoop].weekdays, pRtcTime->weekday);
		over_cur_time = rtc_later_equal_than_int(pRtcTime->hour, pRtcTime->min, 
												 gPwSched.RtcAlarmPkt[iLoop].hour,
												 gPwSched.RtcAlarmPkt[iLoop].min);

		if( next_wday == pRtcTime->weekday && over_cur_time )
		{
			next_wday = rtc_get_next_weekday(gPwSched.RtcAlarmPkt[iLoop].weekdays, (pRtcTime->weekday+1)%7);
		}

		// only bandon use mday now
		if ((next_wday == pRtcTime->weekday) && over_cur_time) {
			day_offset = 7;
			mday = rtc_get_next_mday(pRtcTime, day_offset);
		} else if ( next_wday >= pRtcTime->weekday ) {
			day_offset = next_wday - pRtcTime->weekday;
			mday = rtc_get_next_mday(pRtcTime, day_offset);
		} else {
			day_offset = 7 - pRtcTime->weekday + next_wday;
			mday = rtc_get_next_mday(pRtcTime, day_offset);
		}

		hrs = gPwSched.RtcAlarmPkt[iLoop].hour;
		min = gPwSched.RtcAlarmPkt[iLoop].min;

		if (0 == iLoop) {
			blSwitch = 1;
		}else{
			if ( day_offset < min_day_offset ) {
				blSwitch = 1;
			}else if ( day_offset == min_day_offset ) {
				if ( hrs < hrs_result ) {
					blSwitch = 1;
				}else if ( hrs == hrs_result ) {
					if ( min < min_result ) {
						blSwitch = 1;
					}
				}
			}
		}

		if (blSwitch) {
			min_day_offset = day_offset;
			wdays_result = wdays;
			hrs_result = hrs;
			min_result = min;
			mday_result = mday;
		}
	}

	pAlarmTime->weekdays = wdays_result;
	pAlarmTime->hour = hrs_result;
	pAlarmTime->min = min_result;
End:
	return mday_result;
}

int rtc_get_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn)
{
	int iRet = -1;

	if( NULL == pAutoPowerOn ) {
		goto End;
	}

	memcpy(pAutoPowerOn, &gPwSched, sizeof(SYNO_AUTO_POWERON));
	iRet = 0;
End:
	return iRet;
}
