#include <linux/syno.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>

#include "synobios.h"
#include "localtime.h"
#include "rtc.h"

#ifdef CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE
#include <linux/acpi.h>
#endif /* CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE */

#define RTC_IRQMASK         (RTC_PF | RTC_AF | RTC_UF)
#define RTC_MDAY_ALARM_MASK 0x3F

extern SYNO_AUTO_POWERON gPwSched;
static unsigned long epoch = 1900;  /* year corresponding to 0x00   */
static const unsigned char days_in_mo[] =
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#if 0
static
void rtc_bandon_dump(void)
{
	spin_lock_irq(&rtc_lock);
	printk("##################################\n");
	printk("CMOS_READ(RTC_YEAR)         =%02d\n", BCD2BIN(CMOS_READ(RTC_YEAR)));
	printk("CMOS_READ(RTC_MONTH)        =%02d\n", BCD2BIN(CMOS_READ(RTC_MONTH)));
	printk("CMOS_READ(RTC_DAY_OF_MONTH) =%02d\n", BCD2BIN(CMOS_READ(RTC_DAY_OF_MONTH)));
	printk("CMOS_READ(RTC_DAY_OF_WEEK)  =%02d\n", CMOS_READ(RTC_DAY_OF_WEEK));
	printk("CMOS_READ(RTC_HOURS)        =%02d\n", BCD2BIN(CMOS_READ(RTC_HOURS)));
	printk("CMOS_READ(RTC_MINUTES)      =%02d\n", BCD2BIN(CMOS_READ(RTC_MINUTES)));
	printk("CMOS_READ(RTC_SECONDS)      =%02d\n", BCD2BIN(CMOS_READ(RTC_SECONDS)));
	printk("CMOS_READ(RTC_HOURS_ALARM)  =%02d\n", BCD2BIN(CMOS_READ(RTC_HOURS_ALARM)));
	printk("CMOS_READ(RTC_MINUTES_ALARM)=%02d\n", BCD2BIN(CMOS_READ(RTC_MINUTES_ALARM)));
	printk("CMOS_READ(RTC_SECONDS_ALARM)=%02d\n", BCD2BIN(CMOS_READ(RTC_SECONDS_ALARM)));
	printk("CMOS_READ(RTC_VALID)        =0x%02x\n", CMOS_READ(RTC_VALID));
	printk("CMOS_READ(RTC_CONTROL)      =0x%02x\n", CMOS_READ(RTC_CONTROL));
	printk("CMOS_READ(RTC_INTR_FLAGS)   =0x%02x\n", CMOS_READ(RTC_INTR_FLAGS));
	spin_unlock_irq(&rtc_lock);
}
#endif

static
int rtc_correct_wday(SYNORTCTIMEPKT *pRtcTime)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
    time64_t t;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
    time_t t;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */

    struct xtm taget_time;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
    t = mktime64(pRtcTime->year+1900, pRtcTime->month+1, pRtcTime->day, pRtcTime->hour, pRtcTime->min, pRtcTime->sec);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
    t = mktime(pRtcTime->year+1900, pRtcTime->month+1, pRtcTime->day, pRtcTime->hour, pRtcTime->min, pRtcTime->sec);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
    localtime_1(&taget_time, t);
    localtime_2(&taget_time, t);
    localtime_3(&taget_time, t);

	//correct weekday
    if ( taget_time.weekday != pRtcTime->weekday ) {
		pRtcTime->weekday = taget_time.weekday; 

		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(pRtcTime->weekday, RTC_DAY_OF_WEEK);
		spin_unlock_irq(&rtc_lock);
	}

	return pRtcTime->weekday;
}

int rtc_bandon_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int iRet = -1;
	unsigned char mday_result = 0;
	unsigned char hrs_result = 0;
	unsigned char min_result = 0;
	unsigned char sec_result = 0;
	unsigned char rtc_control, rtc_valid;
	SYNORTCALARMPKT alarmTime = {0};

    if( NULL == pAutoPowerOn || NULL == pRtcTime ) {
		goto End;
	}

    if ( SYNO_AUTO_POWERON_DISABLE == pAutoPowerOn->enabled ) {
        mday_result = BIN2BCD(32);
        hrs_result = BIN2BCD(25);
        min_result = BIN2BCD(61);
        sec_result = BIN2BCD(61);
    } else {
		mday_result = rtc_get_alarm_time(&alarmTime, pRtcTime);
		BIN_TO_BCD(mday_result);
		hrs_result = BIN2BCD(alarmTime.hour);
		min_result = BIN2BCD(alarmTime.min);
	}
	sec_result = 0;

	spin_lock_irq(&rtc_lock);
#ifdef CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE
	acpi_disable_event(ACPI_EVENT_RTC, 0);
#endif /* CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE */
	rtc_control = CMOS_READ(RTC_CONTROL);
    rtc_control &= ~RTC_AIE;
    CMOS_WRITE(rtc_control, RTC_CONTROL);
    rtc_valid = pAutoPowerOn->enabled ? ((CMOS_READ(RTC_VALID) & ~RTC_MDAY_ALARM_MASK) | mday_result) : 0;

    CMOS_WRITE(rtc_valid, RTC_VALID);
    CMOS_WRITE(hrs_result, RTC_HOURS_ALARM);
    CMOS_WRITE(min_result, RTC_MINUTES_ALARM);
    CMOS_WRITE(sec_result, RTC_SECONDS_ALARM);

    if (pAutoPowerOn->enabled) {
#ifdef CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE
	acpi_enable_event(ACPI_EVENT_RTC, 0);
#endif /* CONFIG_SYNO_FIX_RTC_WAKE_AFTER_POWER_FAILURE */
        rtc_control |= RTC_AIE;
        CMOS_WRITE(rtc_control, RTC_CONTROL);
    }
    spin_unlock_irq(&rtc_lock);

	iRet = 0;
End:
	return iRet;
}

int rtc_bandon_set_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn)
{
	int iRet = -1;	
    SYNORTCTIMEPKT rtcTime;

	if( NULL == pAutoPowerOn || pAutoPowerOn->num < 0 ) {
        printk("Parameter Error.\n");
		goto End;
	}

    if( 0 > rtc_bandon_get_time(&rtcTime) ) {
        printk("Failed to get time from rtc.\n");
        goto End;
    }

	memcpy(&gPwSched, pAutoPowerOn, sizeof(SYNO_AUTO_POWERON));

	if ( 0 != rtc_bandon_rotate_auto_poweron(pAutoPowerOn, &rtcTime) ) {
		printk("Failed to set alarm data.\n");
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

/*
 * Returns true if a clock update is in progress
 */
static inline unsigned char rtc_is_updating(void)
{
	unsigned char uip;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	uip = (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return uip;
}

int rtc_bandon_get_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
    unsigned long flags;
    unsigned char ctrl;
	SYNO_AUTO_POWERON schedule;

	/*
	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then 
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy.
	 */
	if (rtc_is_updating())
		mdelay(20);

    /*
     * Only the values that we read from the RTC are set. We leave
     * tm_wday, tm_yday and tm_isdst untouched. Note that while the
     * RTC has RTC_DAY_OF_WEEK, we should usually ignore it, as it is
     * only updated by the RTC when initially set to a non-zero value.
     */
    spin_lock_irqsave(&rtc_lock, flags);
    ctrl = CMOS_READ(RTC_CONTROL);
    pRtcTimePkt->sec = CMOS_READ(RTC_SECONDS);
    pRtcTimePkt->min = CMOS_READ(RTC_MINUTES);
    pRtcTimePkt->hour = CMOS_READ(RTC_HOURS);
    pRtcTimePkt->day = CMOS_READ(RTC_DAY_OF_MONTH);
    pRtcTimePkt->month = CMOS_READ(RTC_MONTH);
    pRtcTimePkt->weekday = CMOS_READ(RTC_DAY_OF_WEEK);
    pRtcTimePkt->year = CMOS_READ(RTC_YEAR);
    spin_unlock_irqrestore(&rtc_lock, flags);

    if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
    {
        BCD_TO_BIN(pRtcTimePkt->sec);
        BCD_TO_BIN(pRtcTimePkt->min);
        BCD_TO_BIN(pRtcTimePkt->hour);
        BCD_TO_BIN(pRtcTimePkt->day);
        BCD_TO_BIN(pRtcTimePkt->month);
        BCD_TO_BIN(pRtcTimePkt->year);
    }

    /*
     * Account for differences between how the RTC uses the values
     * and how they are defined in a struct rtc_time;
     */
    if ((pRtcTimePkt->year += (epoch - 1900)) <= 69)
        pRtcTimePkt->year += 100;

    pRtcTimePkt->month--;

	//Note: rtc_correct_wday will self adding y+1900 and m+1
	rtc_correct_wday(pRtcTimePkt);

	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_bandon_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

	return 0;
}

int rtc_bandon_set_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
    unsigned char save_control, save_freq_select;
    unsigned char mon, day, hrs, min, sec, leap_yr, wday;
    unsigned int yrs;
    SYNO_AUTO_POWERON schedule;

    if (!capable(CAP_SYS_TIME)) {
       return -EACCES;
    }

    //Note: rtc_correct_wday will self adding y+1900 and m+1
    wday = rtc_correct_wday(pRtcTimePkt);
    yrs = pRtcTimePkt->year + 1900;
    mon = pRtcTimePkt->month + 1;   /* tm_mon starts at zero */
    day = pRtcTimePkt->day;
    hrs = pRtcTimePkt->hour;
    min = pRtcTimePkt->min;
    sec = pRtcTimePkt->sec;

    if (yrs < 1970)
        return -EINVAL;

    leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

    if ((mon > 12) || (day == 0))
        return -EINVAL;

    if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
        return -EINVAL;

    if ((hrs >= 24) || (min >= 60) || (sec >= 60))
        return -EINVAL;

    if ((yrs -= epoch) > 255)    /* They are unsigned */
        return -EINVAL;

    spin_lock_irq(&rtc_lock);

    /* These limits and adjustments are independent of
     * whether the chip is in binary mode or not.
     */
    if (yrs > 169) {
        spin_unlock_irq(&rtc_lock);
        return -EINVAL;
    }
    if (yrs >= 100)
        yrs -= 100;

    if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY)
        || RTC_ALWAYS_BCD) {
        BIN_TO_BCD(sec);
        BIN_TO_BCD(min);
        BIN_TO_BCD(hrs);
        BIN_TO_BCD(day);
        BIN_TO_BCD(mon);
        BIN_TO_BCD(yrs);
    }

    save_control = CMOS_READ(RTC_CONTROL);
    CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);
    save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
    CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);
    CMOS_WRITE(yrs, RTC_YEAR);
    CMOS_WRITE(mon, RTC_MONTH);
    CMOS_WRITE(day, RTC_DAY_OF_MONTH);
    CMOS_WRITE(wday, RTC_DAY_OF_WEEK);
    CMOS_WRITE(hrs, RTC_HOURS);
    CMOS_WRITE(min, RTC_MINUTES);
    CMOS_WRITE(sec, RTC_SECONDS);
    CMOS_WRITE(save_control, RTC_CONTROL);
    CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
    spin_unlock_irq(&rtc_lock);

    if( 0 == rtc_get_auto_poweron(&schedule) ) {
        rtc_bandon_rotate_auto_poweron(&schedule, pRtcTimePkt);
    }

	return 0;
}

int rtc_bandon_auto_poweron_init(void)
{
	return 0;
}

int rtc_bandon_auto_poweron_uninit(void)
{
	SYNORTCTIMEPKT rtcTime;
    rtc_bandon_get_time(&rtcTime);

	return 0;
}
