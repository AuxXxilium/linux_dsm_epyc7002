#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#if defined(CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL)
#include <linux/kernel.h>
#include "led_asm116x.h"
#include <linux/time.h>
#include <linux/synolib.h>

extern int GetMaxInternalDiskNum(void);

/**
 * SetDiskLedStatusByASM116XGPIOByPort - control light of slot number on ASM116X
 *
 * return 0: success
 *        -EINVAL: fail
 */
int SetDiskLedStatusByASM116XGPIOByPort(DISKLEDSTATUS *pLedStatus)
{
	int iRet = -1;
	int iWrite = -1;
	int iDiskPort = pLedStatus->diskno;

	if (1 > iDiskPort || GetMaxInternalDiskNum() < iDiskPort) {
		printk("Invalid disk Number [%d]\n", iDiskPort);
		goto END;
	}
	iWrite = (DISK_LED_ORANGE_BLINK == pLedStatus->status || DISK_LED_ORANGE_SOLID == pLedStatus->status) ? 1 : 0;
	iRet = syno_asmedia_116x_disk_led_set(iDiskPort, iWrite);
END:
	return iRet;
}
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
int SYNOSetDiskLedStatusByASM116XGPIOByPort(int iHostNum, SYNO_DISK_LED iStatus)
{
	DISKLEDSTATUS ledStatus = {
		.diskno = iHostNum + 1,
		.status = iStatus,
		.iNodeNameLen = sizeof(DT_INTERNAL_SLOT),
		.szNodeName = DT_INTERNAL_SLOT
	};
	return SetDiskLedStatusByASM116XGPIOByPort(&ledStatus);
}
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL || CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL */
