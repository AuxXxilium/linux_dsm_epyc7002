#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/synolib.h>
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
#include <linux/syno_gpio.h>
#include <linux/i2c.h>
#include <linux/leds-atmega1608.h>
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#include "led_trigger.h"
int GetMaxInternalDiskNum(void);

#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
#define SZ_DT_DISK_LED_TYPE_LP3943 "lp3943"
#define SZ_DT_DISK_LED_TYPE_ATMEGA1608 "atmega1608"

int SetDiskLedStatusByLedTrigger(DISKLEDSTATUS *pLedStatus)
{
	int iDiskNum = pLedStatus->diskno;
	SYNO_DISK_LED iStatus = pLedStatus->status;
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
int SetDiskLedStatusByLedTrigger(int iDiskNum, SYNO_DISK_LED iStatus)
{
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
	int iRet = -1;
	enum led_brightness LEDGreen, LEDOrange;

	if (0 >= iDiskNum || GetMaxInternalDiskNum() < iDiskNum) {
		printk("Invalid disk Number [%d]\n", iDiskNum);
		goto END;
	}

	if (NULL == gpGreenLedMap || NULL == gpOrangeLedMap){
		printk("Disk LED not mapped\n");
		goto END;
	}

	switch(iStatus) {
		case DISK_LED_ORANGE_BLINK:
		case DISK_LED_ORANGE_SOLID:
			LEDOrange = LED_FULL;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_FAULTY); //disable the disk activity led trigger
			break;
		case DISK_LED_GREEN_BLINK:
			LEDOrange = LED_OFF;
			LEDGreen = LED_HALF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_GREEN_SOLID:
			LEDOrange = LED_OFF;
			LEDGreen = LED_FULL;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		case DISK_LED_OFF:
			LEDOrange = LED_OFF;
			LEDGreen = LED_OFF;
			syno_ledtrig_faulty_set(gpGreenLedMap[iDiskNum-1], LED_NORMAL);
			break;
		default:
			printk("Invalid LED status [%d]\n", iStatus);
			goto END;
	}

	syno_ledtrig_set(gpGreenLedMap[iDiskNum-1], LEDGreen);
	syno_ledtrig_set(gpOrangeLedMap[iDiskNum-1], LEDOrange);

	iRet = 0;
END:
	return iRet;
}

#ifdef CONFIG_SYNO_SATA_DISK_LED_CONTROL
int SYNOSetDiskLedStatusByLedTrigger(int iHostNum, SYNO_DISK_LED iStatus)
{
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
	DISKLEDSTATUS ledStatus = {
		.diskno = iHostNum + 1,
		.status = iStatus,
		.iNodeNameLen = sizeof(DT_INTERNAL_SLOT),
		.szNodeName = DT_INTERNAL_SLOT
	};
	return SetDiskLedStatusByLedTrigger(&ledStatus);
#else
	return SetDiskLedStatusByLedTrigger(iHostNum + 1, iStatus);
#endif
}
#endif //CONFIG_SYNO_SATA_DISK_LED_CONTROL

/*
 * Setup the global Disk LED mapping for sata driver
 */
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
int SetHddActLedByLedTrigger(SYNO_LED ledStatus)
{
	struct device_node *pBusNode = NULL;
	struct device_node *pDevNode = NULL;
	struct i2c_adapter *adapter = NULL;
	union i2c_smbus_data data;
	u16 addr = 0x0;
	char *szDevName = NULL;
	char *szDevAddr = NULL;
	int iRet = -1;
	
	for_each_child_of_node(of_root, pBusNode) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		if (NULL != pBusNode->full_name && 0 == strncmp(pBusNode->full_name, DT_I2C_BUS, strlen(DT_I2C_BUS))) {
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0) */
		if (NULL != pBusNode->full_name && 0 == strncmp(pBusNode->full_name, "/"DT_I2C_BUS, strlen("/"DT_I2C_BUS))) {
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0) */
			for_each_child_of_node(pBusNode, pDevNode) {
				if (NULL != pDevNode->name && 0 == strncmp(pDevNode->name, DT_I2C_DEVICE, strlen(DT_I2C_DEVICE))) {
					szDevName = (char *)of_get_property(pDevNode, DT_I2C_DEVICE_NAME, NULL);
					if (0 == strcmp(szDevName, SZ_DT_DISK_LED_TYPE_ATMEGA1608)) {
						//get i2c adapter
						if (NULL == (adapter = syno_i2c_adapter_get_by_node(pBusNode))) {
							printk("synobios: cannot get i2c adapter\n");
							goto END;
						}
						//get i2c address
						szDevAddr = (char *)of_get_property(pDevNode, DT_I2C_ADDRESS, NULL);
						if (NULL == szDevAddr || 0 != kstrtou16(szDevAddr, 16, &addr)) {
							printk("synobios: cannot parse i2c address\n");
							goto END;
						}

						data.byte = (SYNO_LED_OFF == ledStatus) ? ATMEGA1608_LED_MASKED : ATMEGA1608_LED_UNMASK;
						iRet = i2c_smbus_xfer(adapter, addr, 0, I2C_SMBUS_WRITE, ATMEGA1608_MASK, I2C_SMBUS_BYTE_DATA, &data);
						if (0 > iRet) {
							printk("synobios: fail to write i2c smbus\n");
							goto END;
						}

					} // add new i2c device here
				}
			}
		}
	}
END:
	return iRet;
}

void SetupDiskLedMap()
{
	int i = 0, iLedId = -1;
	char szLedName[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {0};
	char szLedType[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {0};
	
	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = kmalloc(GetMaxInternalDiskNum() * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = kmalloc(GetMaxInternalDiskNum() * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, GetMaxInternalDiskNum() * sizeof(int));
	memset(gpOrangeLedMap, -1, GetMaxInternalDiskNum() * sizeof(int));

	for (i = 0; i < GetMaxInternalDiskNum(); i++){
		if (0 > syno_led_type_get(DT_INTERNAL_SLOT, i + 1, szLedType, sizeof(szLedType))) {
			printk("Fail to get disk %d led type\n", i + 1);
			continue;
		}
		if (0 != strcmp(szLedType, SZ_DT_DISK_LED_TYPE_LP3943) && 0 != strcmp(szLedType, SZ_DT_DISK_LED_TYPE_ATMEGA1608)) {
			continue;
		}
		if (0 > HDD_PRESENT_LED_NAME_BY_SLOT(DT_INTERNAL_SLOT, i + 1, szLedName, sizeof(szLedName))) {
			printk("Fail to get disk %d green led name\n", i + 1);
			continue;
		}
		sscanf(szLedName, "syno_led%d", &iLedId);
		gpGreenLedMap[i] = iLedId;
		if (0 > HDD_FAIL_LED_NAME_BY_SLOT(DT_INTERNAL_SLOT, i + 1, szLedName, sizeof(szLedName))) {
			printk("Fail to get disk %d orange led name\n", i + 1);
			continue;
		}
		sscanf(szLedName, "syno_led%d", &iLedId);
		gpOrangeLedMap[i] = iLedId;
	}
}
#else
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum)
{
	if (NULL == pGreenLed || NULL == pOrangeLed || GetMaxInternalDiskNum() < iInternalDiskNum) {
		return;
	}

	/* allocate and initialize disk led mapping*/
	gpGreenLedMap = kmalloc(GetMaxInternalDiskNum() * sizeof(int), GFP_KERNEL);
	gpOrangeLedMap = kmalloc(GetMaxInternalDiskNum() * sizeof(int), GFP_KERNEL);
	memset(gpGreenLedMap, -1, GetMaxInternalDiskNum() * sizeof(int));
	memset(gpOrangeLedMap, -1, GetMaxInternalDiskNum() * sizeof(int));

	memcpy(gpGreenLedMap, pGreenLed, iInternalDiskNum * sizeof(int));
	memcpy(gpOrangeLedMap, pOrangeLed, iInternalDiskNum * sizeof(int));
}
#endif
