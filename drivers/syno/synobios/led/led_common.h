#include <linux/leds.h>
#include "synobios.h"
#include <linux/syno_gpio.h>
extern void SYNO_ENABLE_HDD_LED(int blEnable);
#include "../common/common.h"
extern int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue);
extern int syno_ahci_disk_led_enable_by_port(const unsigned short diskPort, const int iValue);
extern SYNO_GPIO syno_gpio;
