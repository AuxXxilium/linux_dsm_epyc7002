#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_MV92XX_GPIO_CTRL)
#include "led_common.h"
extern int (*GetMaxInternalHostNum)(void);
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
extern int syno_mv_9235_disk_led_get_by_port(int iDiskPort);
extern int syno_mv_9235_disk_led_set_by_port(const unsigned short iDiskPort, int iValue);
extern void sata_syno_ahci_diskled_set_by_port(int iDiskPort, int iPresent, int iFault);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
extern int syno_mv_9235_disk_led_set(const unsigned short hostnum, int iValue);
extern int syno_mv_9235_disk_led_get(const unsigned short hostnum);
extern void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault);
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED iStatus);
int SetDiskLedStatusBy9235GPIO(int iDiskNum, SYNO_DISK_LED iStatus);
int SetDiskLedStatusBy9235GPIOandAHCISGPIO(int disknum, SYNO_DISK_LED status);
int SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(int iHostNum, SYNO_DISK_LED status);
#endif /* MY_DEF_HERE || SYNO_SATA_MV92XX_GPIO_CTRL */
