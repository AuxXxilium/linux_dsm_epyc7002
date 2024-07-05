#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/synolib.h>
#include "led_trigger_disk.h"

int GetMaxInternalDiskNum(void);

struct led_classdev **gpGreenLedDevMap, **gpOrangeLedDevMap;

int SetDiskLedStatusByTrigDiskSyno(DISKLEDSTATUS *pLedStatus)
{
	int iDiskNum = pLedStatus->diskno;
	SYNO_DISK_LED iStatus = pLedStatus->status;
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;

	if (0 >= iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (NULL == gpGreenLedDevMap || NULL == gpOrangeLedDevMap){
		printk("Disk LED not mapped\n");
		goto END;
	}

	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			LEDOrange = LED_ON;
			LEDGreen = LED_OFF;
			break;
		case DISK_LED_GREEN_BLINK:
		case DISK_LED_GREEN_SOLID:
			LEDOrange = LED_OFF;
			LEDGreen = LED_ON;
			break;
		case DISK_LED_OFF:
			LEDOrange = LED_OFF;
			LEDGreen = LED_OFF;
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	ledtrig_syno_disk_led_on(gpGreenLedDevMap[iDiskNum-1], LEDGreen);
	ledtrig_syno_disk_led_on(gpOrangeLedDevMap[iDiskNum-1], LEDOrange);

	iRet = 0;
END:
	return iRet;
}

#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
int SYNOSetDiskLedStatusByTrigDiskSynoint (int iHostNum, SYNO_DISK_LED iStatus)
{
	DISKLEDSTATUS ledStatus = {
		.diskno = iHostNum + 1,
		.status = iStatus,
		.iNodeNameLen = sizeof(DT_INTERNAL_SLOT),
		.szNodeName = DT_INTERNAL_SLOT
	};
	return SetDiskLedStatusByTrigDiskSyno(&ledStatus);
}
#endif //CONFIG_SYNO_SATA_DISK_LED_CONTROL

void SetupLedTrigDiskMap(void)
{
	int i = 0;
	char szLedType[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {0};

	/* allocate and initialize disk led mapping*/
	gpGreenLedDevMap = kmalloc(GetMaxInternalDiskNum() * sizeof(struct led_classdev *), GFP_KERNEL);
	gpOrangeLedDevMap = kmalloc(GetMaxInternalDiskNum() * sizeof(struct led_classdev *), GFP_KERNEL);
	memset(gpGreenLedDevMap, 0, GetMaxInternalDiskNum() * sizeof(struct led_classdev *));
	memset(gpOrangeLedDevMap, 0, GetMaxInternalDiskNum() * sizeof(struct led_classdev *));

	for (i = 0; i < GetMaxInternalDiskNum(); i++){
		if (0 > syno_led_type_get(DT_INTERNAL_SLOT, i + 1, szLedType, sizeof(szLedType))) {
			printk("Fail to get disk %d led type\n", i + 1);
			continue;
		}
		if (0 != strcmp(szLedType, DT_HDD_LED_TYPE_TRIG_DISK_SYNO)) {
			continue;
		}

		// green
		gpGreenLedDevMap[i] = syno_led_dev_get(DT_INTERNAL_SLOT, i + 1, DT_HDD_GREEN_LED);
		// orange
		gpOrangeLedDevMap[i] = syno_led_dev_get(DT_INTERNAL_SLOT, i + 1, DT_HDD_ORANGE_LED);
	}
}
