#if defined(CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL)
#include "led_common.h"
extern int (*GetMaxInternalHostNum)(void);

int SetDiskLedStatusByASM116XGPIOByPort(DISKLEDSTATUS *pLedStatus);
int SYNOSetDiskLedStatusByASM116XGPIOByPort(int iHostNum, SYNO_DISK_LED iStatus);
extern int syno_asmedia_116x_disk_led_set(const int iDiskPort, const int iValue);

#endif /* CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL || CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL */
