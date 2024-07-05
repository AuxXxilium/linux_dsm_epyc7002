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
 * +----+-------+-------+---------++----+-------+-------+---------+
 * |24hr| Ricoh | Seiko | Pericom ||24hr| Ricoh | Seiko | Pericom |
 * +----+-------+-------+---------++----+-------+-------+---------+
 * |(AM)|       |       |  12/24  ||(PM)|       | 12/24 |  12/24  |
 * +----+-------+-------+---------++----+-------+-------+---------+
 * | 00 |   12  |   00  |  52/00  || 12 |   32  | 40/52 |  72/12  |  
 * | 01 |   01  |   01  |  41/01  || 13 |   21  | 41/53 |  61/13  |
 * | 02 |   02  |   02  |  42/02  || 14 |   22  | 42/54 |  62/14  |
 * | 03 |   03  |   03  |  43/03  || 15 |   23  | 43/55 |  63/15  |
 * | 04 |   04  |   04  |  44/04  || 16 |   24  | 44/56 |  64/16  |
 * | 05 |   05  |   05  |  45/05  || 17 |   25  | 45/57 |  65/17  |
 * | 06 |   06  |   06  |  46/06  || 18 |   26  | 46/58 |  66/18  |
 * | 07 |   07  |   07  |  47/07  || 19 |   27  | 47/59 |  67/19  |
 * | 08 |   08  |   08  |  48/08  || 20 |   28  | 48/60 |  68/20  |
 * | 09 |   09  |   09  |  49/09  || 21 |   29  | 49/61 |  69/21  |
 * | 10 |   10  |   10  |  50/10  || 22 |   30  | 50/62 |  70/22  |
 * | 11 |   11  |   11  |  51/11  || 23 |   31  | 51/63 |  71/23  |
 * +----+-------+-------+---------++----+-------+-------+---------+
 */
unsigned char Hour_to_Pericom(const unsigned char hour)
{
	return hour;
}

unsigned char Pericom_to_Hour(const unsigned char hour)
{
	if( hour == 72 ) {
		return 12;
	}else if( hour >= 61 ) {
		return hour - 48;
	}else if( hour == 52 ) {
		return 0;
	}else if( hour >= 41 ) {
		return hour - 40;
	}
	return hour;
}

int rtc_pericom_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn)
{
	int iRet = -1;	
	SYNORTCTIMEPKT rtcTime;

	if( NULL == pAutoPowerOn || pAutoPowerOn->num < 0 ) {
		printk("Parameter Error.\n");
		goto End;
	}

	if( 0 > rtc_pericom_get_time(&rtcTime) ) {
		printk("Failed to get time from rtc.\n");
		goto End;
	}

	memcpy(&gPwSched, pAutoPowerOn, sizeof(SYNO_AUTO_POWERON));

	if ( 0 != rtc_pericom_rotate_auto_poweron(pAutoPowerOn, &rtcTime) ) {
		printk("Failed to set alarm data.\n");
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int rtc_pericom_auto_poweron_uninit(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_pericom_get_time(&rtc_time_pkt);
	return rtc_pericom_auto_poweron_init();
}
