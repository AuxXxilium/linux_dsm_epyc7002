#include "synobios.h"

int rtc_mvebu_set_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_mvebu_get_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_mvebu_set_alarm(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_mvebu_get_alarm(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_mvebu_enable_alarm_irq(void);
