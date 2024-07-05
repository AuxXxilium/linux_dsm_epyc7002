#ifdef CONFIG_SYNO_LEDS_TRIGGER_DISK
#include "led_common.h"
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void ledtrig_syno_disk_led_on(struct led_classdev *led_cdev, bool active);
extern struct led_classdev* syno_led_dev_get(const char* szSlotName, const int diskPort, const char* szledName);

int SYNOSetDiskLedStatusByTrigDiskSyno(int iHostNum, SYNO_DISK_LED iStatus);
int SetDiskLedStatusByTrigDiskSyno(DISKLEDSTATUS* status);
void SetupLedTrigDiskMap(void);
#endif /*CONFIG_SYNO_LEDS_TRIGGER_DISK */
