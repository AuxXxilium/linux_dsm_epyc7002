#include <linux/kernel.h>
#include "led_9170.h"
int GetMaxInternalDiskNum(void);

/**
 * SetDiskLedStatusBy9170GPIO - control 9170 led by slot
 * @iDiskPort: the slot number of disk
 * @iStatus: led status
 *
 * return 0: success
 *        -1: fail
 *        -EINVAL: fail
 */
int SetDiskLedStatusBy9170GPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;
	int iWrite = -1;

	if (1 > iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (DISK_LED_ORANGE_BLINK == iStatus || DISK_LED_ORANGE_SOLID == iStatus) {
		iWrite = 1;
	} else {
		iWrite = 0;
	}

	iRet = syno_mv_9170_disk_led_set_by_port(iDiskNum, iWrite);

END:
	return iRet;

}
