#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
#include "led_common.h"
int DiskLedCtrlBy1475SGPIO(int disknum, SYNO_DISK_LED status);
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
int SYNODiskLedCtrlBy1475SGPIO(DISKLEDSTATUS* diskLedStatus);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
int SYNODiskLedCtrlBy1475SGPIO(int disknum, SYNO_DISK_LED status);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
extern int (*funcSYNOCtrlDiskLedBy1475)(unsigned short, unsigned short);
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */
