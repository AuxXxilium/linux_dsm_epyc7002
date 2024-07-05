#include "synobios.h"
#include "localtime.h"

#define SEIKO_RTC_STATUS1_ADDR   0x30
#define SEIKO_RTC_STATUS2_ADDR   0x31
#define SEIKO_RTC_REALTIME1_ADDR 0x32
#define SEIKO_RTC_REALTIME2_ADDR 0x33
#define SEIKO_RTC_INT1_ADDR      0x34
#define SEIKO_RTC_INT2_ADDR      0x35
#define SEIKO_RTC_CORRECT_ADDR   0x36
#define SEIKO_RTC_FREE_ADDR      0x37

#define PERICOM_RTC_ADDR             0x68
#define PERICOM_RTC_ALARM2_OFFSET    0x0B
#define PERICOM_RTC_CONTROL_OFFSET   0x0E
#define PERICOM_RTC_STATUS_OFFSET    0x0F
#define PERICOM_RTC_A1IE_BIT         0x01
#define PERICOM_RTC_A2IE_BIT         0x02
#define PERICOM_RTC_INTCN_BIT        0x03
#define PERICOM_RTC_DAY_DATE_SWITCH  0x40
#define PERICOM_RTC_DAY_MASK_BIT     0x80


int rtc_ricoh_get_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_ricoh_set_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_ricoh_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_ricoh_auto_poweron_init(void);
int rtc_ricoh_auto_poweron_uninit(void);
int rtc_ricoh_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime);
unsigned char Hour_to_Ricoh(const unsigned char hour);
unsigned char Ricoh_to_Hour(const unsigned char hour);

int rtc_seiko_get_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_seiko_set_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_seiko_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_seiko_auto_poweron_init(void);
int rtc_seiko_auto_poweron_uninit(void);
int rtc_seiko_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime);
int rtc_seiko_reset_interrupt_mode(void);
unsigned char Hour_to_Seiko(const unsigned char hour);
unsigned char Seiko_to_Hour(const unsigned char hour);

int rtc_pericom_get_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_pericom_set_time(struct _SynoRtcTimePkt *pRtcTimePkt);
int rtc_pericom_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_pericom_auto_poweron_init(void);
int rtc_pericom_auto_poweron_uninit(void);
int rtc_pericom_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime);
unsigned char Hour_to_Pericom(const unsigned char hour);
unsigned char Pericom_to_Hour(const unsigned char hour);

/*
 * Reverse the bits in a byte (unsigned char)
 */
#define RB(v) (reverse_bits(v))

int rtc_bandon_get_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_bandon_set_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_bandon_set_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn);
int rtc_bandon_auto_poweron_uninit(void);
int rtc_bandon_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime);

int rtc_mv_get_time(struct _SynoRtcTimePkt* pRtcTimePkt);
int rtc_mv_set_time(struct _SynoRtcTimePkt* pRtcTimePkt);
#if defined(CONFIG_SYNO_ARMADA) || defined(CONFIG_SYNO_ARMADA_V2)
int rtc_mv_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime);
int rtc_mv_set_auto_poweron(SYNO_AUTO_POWERON *pAutoPowerOn);
int rtc_mv_auto_poweron_clean(void);
#endif

unsigned char rtc_get_alarm_time(SYNORTCALARMPKT *pAlarmTime, const SYNORTCTIMEPKT *pRtcTime);
unsigned char reverse_bits(unsigned char v);
unsigned char rtc_get_next_weekday(const unsigned char weekdays, const unsigned char cur_wday);
void rtc_get_days_later(const SYNORTCTIMEPKT *pRtcTime, const int daysLater, struct xtm *tagetTime);
int rtc_get_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn);
int rtc_later_equal_than_int(unsigned char rtcHour, const unsigned char rtcMin, unsigned char intHour, const unsigned char intMin);
