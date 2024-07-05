#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include "led_common.h"
#define LED_NORMAL 0
#define LED_FAULTY 1
extern void syno_ledtrig_set(int iLedNum, enum led_brightness brightness);
extern void syno_ledtrig_faulty_set(int iLedNum, int iFaulty);
extern int *gpGreenLedMap, *gpOrangeLedMap;
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
extern struct i2c_adapter* syno_i2c_adapter_get_by_node(struct device_node *pI2CBusNode);
extern int  syno_led_name_get(const char* szSlotName, const int diskPort, const char *szLedType, char *szSynoLedName, unsigned int cbSynoLedName);
#endif

int SYNOSetDiskLedStatusByLedTrigger(int iHostNum, SYNO_DISK_LED iStatus);
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
int SetDiskLedStatusByLedTrigger(DISKLEDSTATUS* status);
void SetupDiskLedMap(void);
int SetHddActLedByLedTrigger(SYNO_LED ledStatus);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
int SetDiskLedStatusByLedTrigger(int iDiskNum, SYNO_DISK_LED iStatus);
void SetupDiskLedMap(const int *pGreenLed, const int *pOrangeLed, unsigned int iInternalDiskNum);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
