#include <linux/synobios.h>

int rtc_rtk_set_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_rtk_get_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_rtk_set_alarm(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_rtk_get_alarm(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_rtk_enable_alarm_irq(void);
