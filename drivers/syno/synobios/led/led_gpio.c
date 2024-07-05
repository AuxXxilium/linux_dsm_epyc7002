#include "led_gpio.h"

int synoDiskLedControlByGpio(DISKLEDSTATUS *cmd)
{
	int iRet = -1;
	u8 state = PIN_BITMAP_OFF;
	u32 pin = 0;
	u32 val = 0;

	switch (cmd->status) {
		case DISK_LED_OFF:
			state = PIN_BITMAP_OFF;
			break;
		case DISK_LED_GREEN_SOLID:
			state = PIN_BITMAP_GREEN;
			break;
		case DISK_LED_ORANGE_SOLID:
			state = PIN_BITMAP_ORANGE;
			break;
		case DISK_LED_GREEN_BLINK:
			state = PIN_BITMAP_GREEN | PIN_BITMAP_BLINK;
			break;
		case DISK_LED_ORANGE_BLINK:
			/* No such combination */
		default:
			goto FAIL;
	}

	if (HAVE_HDD_PRESENT_LED_BY_SLOT(cmd->szNodeName, cmd->diskno)) {
		pin = HDD_PRESENT_LED_PIN_BY_SLOT(cmd->szNodeName, cmd->diskno);
		val = EVAL_PIN_VAL(HDD_PRESENT_LED_POLARITY_BY_SLOT(cmd->szNodeName, cmd->diskno), state & PIN_BITMAP_GREEN);
		SYNO_GPIO_WRITE(pin, val);
	}
	if (HAVE_HDD_FAIL_LED_BY_SLOT(cmd->szNodeName, cmd->diskno)) {
		pin = HDD_FAIL_LED_PIN_BY_SLOT(cmd->szNodeName, cmd->diskno);
		val = EVAL_PIN_VAL(HDD_FAIL_LED_POLARITY_BY_SLOT(cmd->szNodeName, cmd->diskno), state & PIN_BITMAP_ORANGE);
		SYNO_GPIO_WRITE(pin, val);
	}
	if (HAVE_HDD_ACT_LED_BY_SLOT(cmd->szNodeName, cmd->diskno)) {
		pin = HDD_ACT_LED_PIN_BY_SLOT(cmd->szNodeName, cmd->diskno);
		val = EVAL_PIN_VAL(HDD_ACT_LED_POLARITY_BY_SLOT(cmd->szNodeName, cmd->diskno), state & PIN_BITMAP_BLINK);
		SYNO_GPIO_WRITE(pin, val);
	}

	iRet = 0;
FAIL:
	return iRet;
}
