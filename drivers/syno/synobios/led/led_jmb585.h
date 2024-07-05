#if defined(CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL)
#include "led_common.h"
extern int (*GetMaxInternalHostNum)(void);

int SetDiskLedStatusByJMB585GPIOByPort(DISKLEDSTATUS *pLedStatus);
int SYNOSetDiskLedStatusByJMB585GPIOByPort(int iHostNum, SYNO_DISK_LED iStatus);
extern int syno_jmb585_disk_led_set(const int iDiskPort, int iValue);
extern void syno_jmb585_led_wait(void);

#endif /* CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL || CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL */
