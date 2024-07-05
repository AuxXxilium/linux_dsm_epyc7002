#include <synobios.h>
#include <linux/syno_gpio.h>
#include <linux/synolib.h>

#define PIN_BITMAP_OFF      (0x00)
#define PIN_BITMAP_GREEN    (0x01)
#define PIN_BITMAP_ORANGE   (0x02)
#define PIN_BITMAP_BLINK    (0x04)

int synoDiskLedControlByGpio(DISKLEDSTATUS *cmd);
