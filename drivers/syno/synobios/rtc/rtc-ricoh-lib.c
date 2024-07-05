#include "synobios.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include "rtc.h"

extern SYNO_AUTO_POWERON gPwSched;

/*
 * Time Display Digit Table (in BCD encoding)
 * Ricoh: old RTC
 * Seiko: new RTC
 *
 * struct _SynoRtcTimePkt is designed for Ricoh RTC, so we need to
 * transform its format from Seiko RTC to Ricoh RTC.
 *
 * @see: lnxscemd-2.0/modules/rtc.c :: ScemRTCTimeGet()
 * @todo: transform struct _SynoRtcTimePkt to traditional 24hr format
 *
 * +----+-------+-------+   +----+-------+--------+
 * |24hr| Ricoh | Seiko |   |24hr| Ricoh | Seiko  |
 * +----+-------+-------+   +----+-------+--------+
 * |(AM)|       |       |   |(PM)|       | 12/24  |
 * | 00 |   12  |   00  |   | 12 |   32  | 40/52  |
 * | 01 |   01  |   01  |   | 13 |   21  | 41/53  |
 * | 02 |   02  |   02  |   | 14 |   22  | 42/54  |
 * | 03 |   03  |   03  |   | 15 |   23  | 43/55  |
 * | 04 |   04  |   04  |   | 16 |   24  | 44/56  |
 * | 05 |   05  |   05  |   | 17 |   25  | 45/57  |
 * | 06 |   06  |   06  |   | 18 |   26  | 46/58  |
 * | 07 |   07  |   07  |   | 19 |   27  | 47/59  |
 * | 08 |   08  |   08  |   | 20 |   28  | 48/60  |
 * | 09 |   09  |   09  |   | 21 |   29  | 49/61  |
 * | 10 |   10  |   10  |   | 22 |   30  | 50/62  |
 * | 11 |   11  |   11  |   | 23 |   31  | 51/63  |
 * +----+-------+-------+   +----+-------+--------+
 */
unsigned char Hour_to_Ricoh(const unsigned char hour)
{
	if ( hour == 0 ) {
		return 12;
	}else if( hour == 12 ) {
		return 32;
	}else if( hour > 12 ) {
		return hour + 8;
	}
	return hour;
}

unsigned char Ricoh_to_Hour(const unsigned char hour)
{
	if( hour == 12 ) {
		return 0;
	}else if( hour == 32 ) {
		return 12;
	}else if( hour >= 21 ) {
		return hour - 8;
	}
	return hour;
}

int rtc_ricoh_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn)
{
	int iRet = -1;	
    SYNORTCTIMEPKT rtcTime;

	if( NULL == pAutoPowerOn || pAutoPowerOn->num < 0 ) {
        printk("Parameter Error.\n");
		goto End;
	}

    if( 0 > rtc_ricoh_get_time(&rtcTime) ) {
        printk("Failed to get time from rtc.\n");
        goto End;
    }

	memcpy(&gPwSched, pAutoPowerOn, sizeof(SYNO_AUTO_POWERON));

	if ( 0 != rtc_ricoh_rotate_auto_poweron(pAutoPowerOn, &rtcTime) ) {
		printk("Failed to set alarm data.\n");
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_ricoh_auto_poweron_uninit(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_ricoh_get_time(&rtc_time_pkt);
	return rtc_ricoh_auto_poweron_init();
}
