#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/kernel.h>
#include "led_9235.h"

int GetMaxInternalDiskNum(void);

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

inline int SYNO_LED_ORANGE_STATUS_SET(SYNO_DISK_LED iStatus)
{
	return (DISK_LED_ORANGE_BLINK == iStatus || DISK_LED_ORANGE_SOLID == iStatus) ? 1 : 0;
}

/**
 * SetSCSIHostLedStatusByAHCIGPIO - control led light of input slot by ahci gpio
 * @iHostNum: the scsi host of disk
 *
 * return 0: success
                  -1: fail
 */
int SetSCSIHostLedStatusByAHCIGPIO(int iHostNum, SYNO_DISK_LED status)
{
    int ret = -1;
    int iFault, iPresent;

    switch(status) {
            case DISK_LED_ORANGE_BLINK:
            case DISK_LED_ORANGE_SOLID:
                    iFault = 1;
                    iPresent = 0;
                    break;
            case DISK_LED_GREEN_SOLID:
                    iFault = 0;
                    iPresent = 1;
                    break;
            case DISK_LED_OFF:
                    iFault = 0;
                    iPresent = 0;
                    break;
            default:
                    printk("Invalid LED status [%d]\n", status);
                    goto END;
    }
#ifdef MY_DEF_HERE
    sata_syno_ahci_diskled_set(iHostNum, iPresent, iFault);
#else
    printk(KERN_ERR "SYNO ATA AHCI DISK LED control function is not defined!!");
#endif /* MY_DEF_HERE */
    ret = 0;
END:
    return ret;
}

int SetDiskLedStatusByAHCIGPIO(int iDiskPort, SYNO_DISK_LED status)
{
    int ret = -1;
    int iFault, iPresent;

    switch(status) {
            case DISK_LED_ORANGE_BLINK:
            case DISK_LED_ORANGE_SOLID:
                    iFault = 1;
                    iPresent = 0;
                    break;
            case DISK_LED_GREEN_SOLID:
                    iFault = 0;
                    iPresent = 1;
                    break;
            case DISK_LED_OFF:
                    iFault = 0;
                    iPresent = 0;
                    break;
            default:
                    printk("Invalid LED status [%d]\n", status);
                    goto END;
    }
#ifdef MY_DEF_HERE
    sata_syno_ahci_diskled_set_by_port(iDiskPort, iPresent, iFault);
#else
    printk(KERN_ERR "SYNO ATA AHCI DISK LED control function is not defined!!");
#endif /* MY_DEF_HERE */
    ret = 0;
END:
    return ret;
}

/**
 * SetDiskLedStatusBy9235GPIOByPort - control light of slot number on 9235
 * @iDiskPort: the slot number of disk
 * @iStatus: light status
 *
 * return 0: success
 *        -EINVAL: fail
 */
int SetDiskLedStatusBy9235GPIOByPort(int iDiskPort, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	int iWrite = -1;

	iWrite = SYNO_LED_ORANGE_STATUS_SET(iStatus);
	iRet = syno_mv_9235_disk_led_set_by_port(iDiskPort, iWrite);
	return iRet;
}

/**
 * SetSCSIHostLedStatusBy9235GPIO - control light of scsi host number on 9235
 * @iDiskPort: the scsi host number of disk
 * @iStatus: light status
 *
 * return 0: success
 *        -EINVAL: fail
 */
int SetSCSIHostLedStatusBy9235GPIO(int iHostNum, SYNO_DISK_LED iStatus)
{
	int iRet = 0;
	int iWrite = -1;

	iWrite = SYNO_LED_ORANGE_STATUS_SET(iStatus);
	iRet = syno_mv_9235_disk_led_set(iHostNum, iWrite);
	return iRet;
}

/**
 * SetDiskLedStatusBy9235GPIO - control light of slot number on 9235
 * @iDiskPort: the slot number of disk
 * @iStatus: light status
 *
 * return 0: success
 *        -EINVAL: fail
 */
int SetDiskLedStatusBy9235GPIO(int iDiskNum, SYNO_DISK_LED iStatus)
{
	int iRet = -1;

	if (1 > iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	iRet = SetDiskLedStatusBy9235GPIOByPort(iDiskNum, iStatus);
END:
	return iRet;
}

/**
 * SetDiskLedStatusBy9235GPIOandAHCISGPIObyPort - control mix ahci and 9235 led of slot
 * @iDiskPort: the slot number of disk
 * @iStatus: light status
 *
 * return 0: success
 *        -EINVAL: fail
 */
int SetDiskLedStatusBy9235GPIOandAHCISGPIObyPort(int iDiskPort, SYNO_DISK_LED status)
{
	int ret;
	int iWrite = -1, iRead = -1;

	if (GetMaxInternalHostNum && iDiskPort < GetMaxInternalHostNum()) {
		ret = SetDiskLedStatusByAHCIGPIO(iDiskPort, status);;
	} else {
		iWrite = SYNO_LED_ORANGE_STATUS_SET(status);
		ret = syno_mv_9235_disk_led_set_by_port(iDiskPort, iWrite);

		iRead = syno_mv_9235_disk_led_get_by_port(iDiskPort);
		if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
			giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		}
	}
	return ret;
}

int SetSCSIHostLedStatusBy9235GPIOandAHCISGPIO(int iHostNum, SYNO_DISK_LED status)
{
	int ret;
	int iWrite = -1, iRead = -1;

	if (GetMaxInternalHostNum && iHostNum < GetMaxInternalHostNum()) {
		ret = SetSCSIHostLedStatusByAHCIGPIO(iHostNum, status);;
	} else {
		iWrite = SYNO_LED_ORANGE_STATUS_SET(status);
		ret = syno_mv_9235_disk_led_set(iHostNum, iWrite);

		iRead = syno_mv_9235_disk_led_get(iHostNum);
		if (-1 == iRead || iRead != iWrite) {
#ifdef MY_DEF_HERE
			giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		}
	}
	return ret;
}

int SetDiskLedStatusBy9235GPIOandAHCISGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;
	if (1 > disknum) {
		goto END;
	}

	if (GetMaxInternalDiskNum() < disknum) {
		goto END;
	}
	err = SetDiskLedStatusBy9235GPIOandAHCISGPIObyPort(disknum, status);

END:
	return err;
}
