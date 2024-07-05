/* Copyright (c) 2009-2015 Synology Inc. All rights reserved. */
#include "synobios.h"
#include <asm/delay.h>
#include "common.h"
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

#ifdef CONFIG_SYNO_GPIO
void check_gpio_consistency (SYNO_GPIO_INFO* pGpio1, SYNO_GPIO_INFO* pGpio2)
{
	int i;

	if (!pGpio1 || !pGpio2) {
		printk("%s: Error Parameter\n", __FUNCTION__);
		return;
	}

	if ((pGpio1->nr_gpio != pGpio2->nr_gpio) ||
		(pGpio1->gpio_polarity != pGpio2->gpio_polarity)) {
		goto WARN;
	}

	for (i = 0; i < pGpio1->nr_gpio; i++) {
		if (pGpio1->gpio_port[i] != pGpio2->gpio_port[i]) {
			goto WARN;
		}
	}
	return ;
WARN:
	printk("WARNING: inconsistent syno gpio info");
	if (pGpio1->name) {
		printk(", %s", pGpio1->name);
	}
	printk("\n");
}

int SYNO_CTRL_FAN_RESISTOR(char speed_value)
{
	int i = 0;
	int status = 0;

	for (i = 0; i < syno_gpio.fan_ctrl->nr_gpio; i++) {
		// 3 LSBs represent fan speed.
		// bit[0]: max, bit[1]: mid, bit[2]: min
		// 5 LSBs
		// bit[0]: full, bit[1]: vol, bit[2]: high, bit[3]: mid, bit[4]: low
		if (0x01 & (speed_value >> i)) {
			status = 1;
		} else {
			status = 0;
		}
		if (!HAVE_FAN_CTRL(i + 1)) { // HAVE_FAN_CTRL is 1-based
			printk("No such fan ctrl pin. Index: %d\n", i + 1);
			WARN_ON(1);
			return -EINVAL;
		}
		SYNO_GPIO_WRITE(FAN_CTRL_PIN(i+1), status);
	}
	return 0;
}

/* SYNO_CTRL_FAN_STATUS_GET
 * Query Fan status .
 * output: 1 - not fail, 0 - fail.
 */
int SYNO_CTRL_FAN_STATUS_GET(int index, int *pValue)
{
	int failed = 0;

	if (!HAVE_FAN_FAIL(index)) { // index is 1-based
		printk("No such fan fail pin. Index: %d\n", index);
		WARN_ON(1);
		return -EINVAL;
	}

	failed = SYNO_GPIO_READ(FAN_FAIL_PIN(index));

	if (failed)
		*pValue = 0;
	else
		*pValue = 1;

	return 0;
}

/**
 * For those fans output constant 0 or 1 when active or stopped
 *
 * @param fanno		[IN] Represent which fan
 * @param pStatus	[OUT] The fan status
 *
 * @return 0: sucessful
 *         -EINVAL: error parameter
 *
*/
int GetFanStatusActiveLow(int fanno, FAN_STATUS *pStatus)
{
	int FanStatus = 0;
	int iRet = -EINVAL;

	if (!(1 <= fanno && fanno <= 3) || NULL == pStatus) {
		goto END;
	}

	if (0 != SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus)) {
		goto END;
	}
	if (FanStatus) {
		*pStatus = FAN_STATUS_RUNNING;
	} else {
		*pStatus = FAN_STATUS_STOP;
	}
	iRet = 0;

END:
	return iRet;
}

/**
 * For those fans output constant 1 or 0 when stopped and output
 * periodically 0 and 1 by truns when active.
 *
 * @param fanno		[IN] Represent which fan
 * @param pStatus	[OUT] The fan status
 *
 * @return 0: sucessful
 *         -EINVAL: error parameter
 *
*/
int GetFanStatusActivePulse(int fanno, FAN_STATUS *pStatus)
{
	int FanStatus = 0;
	int rgcVolt[2] = {0, 0};
	int iRet = -EINVAL;

	if (pStatus == NULL || !HAVE_FAN_FAIL(fanno)) {
		goto END;
	}

	do {
		if (0 != SYNO_CTRL_FAN_STATUS_GET(fanno, &FanStatus)) {
			goto END;
		}
		rgcVolt[FanStatus]++;
		if (rgcVolt[0] && rgcVolt[1]) {
			break;
		}
		udelay(300);
	} while ((rgcVolt[0] + rgcVolt[1]) < 200);

	if (rgcVolt[1] == 0) {
		*pStatus = FAN_STATUS_STOP;
	} else {
		*pStatus = FAN_STATUS_RUNNING;
	}

	iRet = 0;
END:
	return iRet;
}

int SYNO_CTRL_ALARM_LED_SET(int status)
{
	if (!HAVE_ALARM_LED()) {
		WARN_ON(1);
		return -EINVAL;
	}

	SYNO_GPIO_WRITE(ALARM_LED_PIN(), status);
	return 0;
}

#if defined(CONFIG_SYNO_ATA_PWR_CTRL) || defined(CONFIG_SYNO_SATA_PWR_CTRL)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
#define HDD_FAIL_LED_POLARITY(index) HDD_FAIL_LED_PIN_BY_SLOT(DT_INTERNAL_SLOT, index)
#define HDD_PRESENT_LED_POLARITY(index) HDD_FAIL_LED_POLARITY_BY_SLOT(DT_INTERNAL_SLOT, index)
#define HDD_PRESENT_LED_PIN(index) HDD_PRESENT_LED_PIN_BY_SLOT(DT_INTERNAL_SLOT, index)
#define HAVE_HDD_PRESENT_LED(index) HDD_PRESENT_LED_PIN_BY_SLOT(DT_INTERNAL_SLOT, index)
#define HAVE_HDD_FAIL_LED(index) HDD_PRESENT_LED_PIN_BY_SLOT(DT_INTERNAL_SLOT, index)
#define HDD_FAIL_LED_PIN(index) HDD_FAIL_LED_PIN_BY_SLOT(DT_INTERNAL_SLOT, index)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
int SYNO_HDD_LED_SET(int index, int status)
{
	int fail_value, present_value;

	if (!HAVE_HDD_FAIL_LED(index)) { // index is 1-based
		printk("No such hdd fail led pin. Index: %d\n", index);
		WARN_ON(1);
		return -EINVAL;
	}

	if (0 == SYNO_CHECK_HDD_DETECT(index)) {
		status = DISK_LED_OFF;
	}

	/* Since faulty led and present led are combined,
	   we need to disable present led when light on faulty's */
	if (DISK_LED_ORANGE_SOLID == status || DISK_LED_ORANGE_BLINK == status) {
		fail_value = 1;
		present_value = 0;
	} else if (DISK_LED_GREEN_SOLID == status || DISK_LED_GREEN_BLINK == status) {
		fail_value = 0;
		present_value = 1;
	} else if (DISK_LED_OFF == status) {
		fail_value = 0;
		present_value = 0;
	} else {
		printk("Wrong HDD led status [%d]\n", status);
		return -EINVAL;
	}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
	if (ACTIVE_LOW == HDD_FAIL_LED_POLARITY(index))
#else
	if (ACTIVE_LOW == HDD_FAIL_LED_POLARITY())
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		fail_value = !fail_value;
	SYNO_GPIO_WRITE(HDD_FAIL_LED_PIN(index),fail_value);

	/* armada38x and monaco have no hdd detect pin so it can be ignored */
	if (HAVE_HDD_PRESENT_LED(index)) {
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		if (ACTIVE_LOW == HDD_PRESENT_LED_POLARITY(index))
#else
		if (ACTIVE_LOW == HDD_PRESENT_LED_POLARITY())
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
			present_value = !present_value;
		SYNO_GPIO_WRITE(HDD_PRESENT_LED_PIN(index), present_value);
	}

	return 0;
}
#endif /* defined(CONFIG_SYNO_ATA_PWR_CTRL) || defined(CONFIG_SYNO_SATA_PWR_CTRL) */

// for matching userspace usage, return 0 if button is pressed, else = 1
int SYNO_COPY_BUTTON_GPIO_GET(void)
{
	int ret;

	if (!HAVE_COPY_BUTTON_DETECT()) {
		printk("No copy button detect pin\n");
		WARN_ON(1);
		return 1;
	}
	ret = SYNO_GPIO_READ(COPY_BUTTON_DETECT_PIN());
	if (ACTIVE_LOW == COPY_BUTTON_DETECT_POLARITY()){
		return ret;
	}
	return !ret;
}

unsigned int SynoModelIDGet(SYNO_GPIO *pGpio)
{
	int value;
	unsigned int ret = 0;

	value = SYNO_GPIO_READ(pGpio->model_id->gpio_port[0]);
	ret |= (value ? 1 : 0) << 2;
	value = SYNO_GPIO_READ(pGpio->model_id->gpio_port[1]);
	ret |= (value ? 1 : 0) << 1;
	value = SYNO_GPIO_READ(pGpio->model_id->gpio_port[2]);
	ret |= (value ? 1 : 0) << 0;

	return ret;
}

void SYNO_ENABLE_HDD_LED(int blEnable)
{
	if (!HAVE_DISK_LED_CTRL()) {
		printk("No disk led ctrl pin\n");
		WARN_ON(1);
		return;
	}

	if (ACTIVE_LOW == DISK_LED_CTRL_POLARITY()) {
		blEnable = !blEnable;
	}
	SYNO_GPIO_WRITE(DISK_LED_CTRL_PIN(), blEnable);
}

void SYNO_ENABLE_PHY_LED(int blEnable)
{
	if (!HAVE_PHY_LED_CTRL()) {
		printk("No phy led ctrl pin\n");
		WARN_ON(1);
		return;
	}

	if (ACTIVE_LOW == PHY_LED_CTRL_POLARITY()) {
		blEnable = !blEnable;
	}
	SYNO_GPIO_WRITE(PHY_LED_CTRL_PIN(), blEnable);
}

int SYNO_BUZZER_BUTTON_GPIO_GET(unsigned char *pValue)
{
	int tempVal = 0;

	if (!HAVE_MUTE_BUTTON_DETECT()) {
		printk("No buzzer mute detect pin\n");
		WARN_ON(1);
		return -EINVAL;
	}

	tempVal = SYNO_GPIO_READ(MUTE_BUTTON_DETECT_PIN());
	if (ACTIVE_LOW == MUTE_BUTTON_DETECT_POLARITY()) {
		*pValue = !tempVal;
	}
	else {
		*pValue = tempVal;
	}

	return 0;
}

int SYNO_CTRL_BUZZER_MUTE_SET(unsigned char pValue)
{
	if (!HAVE_BUZZER_MUTE_CTRL()) {
		printk("No buzzer mute ctrl pin\n");
		WARN_ON(1);
		return -EINVAL;
	}

	SYNO_GPIO_WRITE(BUZZER_MUTE_CTRL_PIN(), pValue);
	return 0;
}

int SYNO_CTRL_RP_FAN(int status)
{
	if (!HAVE_RP_FAN_CTRL()) {
		WARN_ON(1);
		return -EINVAL;
	}

	SYNO_GPIO_WRITE(RP_FAN_CTRL_PIN(), status);
	return 0;
}

/* Set GPIO pin value */
int SetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if (NULL == pPin) {
		goto End;
	}

	SYNO_GPIO_WRITE(pPin->pin, pPin->value);

	ret = 0;
End:
	return ret;
}

/* Get GPIO pin value */
int GetGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if (NULL == pPin) {
		goto End;
	}

	pPin->value = SYNO_GPIO_READ(pPin->pin);

	ret = 0;
End:
	return ret;
}
#endif /* CONFIG_SYNO_GPIO */

int GetBrand(void)
{
	return BRAND_SYNOLOGY;
}
