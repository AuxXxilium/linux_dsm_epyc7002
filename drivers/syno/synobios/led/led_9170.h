#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_MV92XX_GPIO_CTRL)
#include "led_common.h"
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
extern int syno_mv_9170_disk_led_set_by_port(int iDiskNum, int iValue);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
extern int syno_mv_9170_disk_led_set(const unsigned short hostnum, int iValue);
int SetDiskLedStatusBy9170GPIO(int iDiskNum, SYNO_DISK_LED iStatus);
#endif /* MY_DEF_HERE || SYNO_SATA_MV92XX_GPIO_CTRL*/
