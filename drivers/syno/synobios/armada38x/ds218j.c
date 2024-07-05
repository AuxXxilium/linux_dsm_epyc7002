// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "armada38x_common.h"
#include "syno_ttyS.h"


int GetModel(void)
{
	return MODEL_DS218j;
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
#if defined(CONFIG_SMP)
	int i;

	cpu->core = 0;
	for_each_online_cpu(i) {
		cpu->core++;
	}
#else /* CONFIG_SMP */
	cpu->core = 1;
#endif
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1300);
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t type_218j = MODULE_T_DS218j;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_DS218j:
		pType = &type_218j;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

#define MAX_MPP_PIN 59

#define INTER_REGS_PHYS_BASE				0xf1000000
#define MPP_CONTROL_REG_BASE				0x00018000 | INTER_REGS_PHYS_BASE
#define GPIO_0_31_OUT_ENABLE_CONTROL_REG	0x00018004 | INTER_REGS_PHYS_BASE
#define GPIO_32_59_OUT_ENABLE_CONTROL_REG	0x00018044 | INTER_REGS_PHYS_BASE

#define MPP_MODE_GPIO			0x0
#define MPP_MODE_SATA_PRESENT	0x4

#define REG_READ(base, mppGroup) \
		ioread32(base + (mppGroup << 2))
#define REG_WRITE(val, base, mppGroup) \
		iowrite32(val, base + (mppGroup << 2));

int SYNOMppCtrlRegWrite(unsigned int mppPin, unsigned int mppVal)
{
	unsigned int origVal;
	unsigned int mppGroup;
	void *base_addr = ioremap(MPP_CONTROL_REG_BASE, 32);

	if (MAX_MPP_PIN < mppPin)
		return -EINVAL;

	/* get the group the pin belongs to, for addressing */
	/* 32 bits per register, 4 bits per pin, 8 pins in a group */
	mppGroup = mppPin / 8;
	mppVal &= 0x0F;
	origVal = REG_READ(base_addr, mppGroup);

	/* get the corresponding bits */
	origVal &= ~(0xF << ((mppPin % 8) * 4));
	origVal |= mppVal << ((mppPin % 8) * 4);

	REG_WRITE(origVal, base_addr, mppGroup);

	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	if (HAVE_HDD_PRESENT_LED(disknum)) {
		if (DISK_LED_ORANGE_SOLID == status ||
			DISK_LED_ORANGE_BLINK == status)
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(disknum),
					MPP_MODE_GPIO);
		else
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(disknum),
					MPP_MODE_SATA_PRESENT);
	}

	return SYNO_HDD_LED_SET(disknum, status);
}

int SetPowerLedStatus(SYNO_LED status)
{
	char szCommand[5] = {0};
	int err = -1;

        switch(status){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > synobios_lock_ttyS_write(szCommand)) {
		goto ERR;
	}

	err = 0;
ERR:
	return err;
}

int SetHddLed(SYNO_LED ledStatus)
{
	switch (ledStatus) {
		case SYNO_LED_OFF:
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(1),
					MPP_MODE_GPIO);
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(2),
					MPP_MODE_GPIO);
			SYNO_HDD_LED_SET(1, DISK_LED_OFF);
			SYNO_HDD_LED_SET(2, DISK_LED_OFF);
			break;
		case SYNO_LED_ON:
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(1),
					MPP_MODE_SATA_PRESENT);
			SYNOMppCtrlRegWrite(HDD_PRESENT_LED_PIN(2),
					MPP_MODE_SATA_PRESENT);
			break;
		default:
			WARN(1 ,"invalid opration: %d\n", ledStatus);
			return -EINVAL;
	}

	return 0;
}

#define LINK_DELAY_MS	50
#define IRQ_ETH0		193

typedef int MV_STATUS;
typedef unsigned int MV_U32;
typedef unsigned short MV_U16;

MV_STATUS mvEthPhyRegWrite(MV_U32 phyAddr, MV_U32 regOffs, MV_U16 data);
MV_STATUS mvEthPhyRegRead(MV_U32 phyAddr, MV_U32 regOffs, MV_U16 *data);

extern spinlock_t phy_smi_lock;

int SetPhyLed(SYNO_LED ledStatus)
{
	u32 iPhyAddr;
	u16 uiRegValue;
	int err = -1;
	u16 old_page = 0;
	static SYNO_LED ledNowStat = SYNO_LED_OFF;

	if (ledStatus == ledNowStat) {
		err = 0;
		goto ERR;
	}
	spin_lock(&phy_smi_lock);

	/* Eth phy mv1514 will link down/up if switched register page. Besides, we
	 * should disable GBE interrupt and wait a short period to prevent race
	 * condition and make sure eth link stat stable */
	disable_irq(IRQ_ETH0);

	iPhyAddr = 1; /* boardEthSmiAddr */

	mvEthPhyRegRead(iPhyAddr, 0x16, &old_page);

	mvEthPhyRegWrite(iPhyAddr, 0x16, 0x3); /* set to page 3 */
	mvEthPhyRegRead(iPhyAddr, 0x10, &uiRegValue);

	switch(ledStatus){
		/*Set Phy led[0] of led[0-2]*/
		case SYNO_LED_ON:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0001;
			break;
		case SYNO_LED_OFF:
			uiRegValue &= 0xFFF0;
			uiRegValue |= 0x0008;
			break;
		default:
			goto ERR;
	}

	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x10, uiRegValue);
	mvEthPhyRegWrite((unsigned int)iPhyAddr, 0x16, old_page); /* restore page */

	mdelay(LINK_DELAY_MS);
	enable_irq(IRQ_ETH0);

	spin_unlock(&phy_smi_lock);

	ledNowStat = ledStatus;
	err = 0;

ERR:
	return err;
}

/*
 *  DS218j GPIO config table
 *
 *  Pin     In/Out    Function

 *  12       In       Model ID 0
 *  21       In       Model ID 1
 *  45       In       Model ID 2
 *  52       In       Fan 1 fail
 *  54       In       USB1 overcurrent
 *  55       In       USB2 overcurrent
 *   6      Out       LED on
 *  13      Out       HDD 1 fault LED
 *  14      Out       HDD 2 fault LED
 *  15      Out       HDD 1 power enable
 *  16      Out       HDD 2 power enable
 *  48      Out       Fan High
 *  49      Out       Fan Mid
 *  50      Out       Fan Low
 *  58      Out       USB1 power enable
 *  59      Out       USB2 power enable
 */

static SYNO_GPIO_INFO fan_ctrl = {
	.nr_gpio		= 3,
	.gpio_port		= {50, 49, 48},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO fan_fail = {
	.nr_gpio		= 1,
	.gpio_port		= {52},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_fail_led = {
	.nr_gpio		= 2,
	.gpio_port		= {13, 14},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO hdd_present_led = {
	.nr_gpio		= 2,
	.gpio_port		= {20, 19},
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO hdd_enable = {
	.nr_gpio		= 2,
	.gpio_port		= {15, 16},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO model_id = {
	.nr_gpio		= 3,
	.gpio_port		= {12, 21, 45},
	.gpio_polarity	= ACTIVE_HIGH,
};
static SYNO_GPIO_INFO disk_led_ctrl = {
	.nr_gpio		= 1,
	.gpio_port		= {6},
	.gpio_polarity	= ACTIVE_HIGH,
};

void syno_gpio_init(void)
{
	if (!syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = &hdd_enable;
	} else {
		check_gpio_consistency(syno_gpio.hdd_enable, &hdd_enable);
	}

	syno_gpio.fan_ctrl 			= &fan_ctrl;
	syno_gpio.hdd_present_led	= &hdd_present_led;
	syno_gpio.fan_fail 			= &fan_fail;
	syno_gpio.hdd_fail_led 		= &hdd_fail_led;
	syno_gpio.model_id 			= &model_id;
	syno_gpio.disk_led_ctrl 	= &disk_led_ctrl;
}

void syno_gpio_cleanup(void)
{
	if (&hdd_enable == syno_gpio.hdd_enable) {
		syno_gpio.hdd_enable = NULL;
	}

	syno_gpio.fan_ctrl 			= NULL;
	syno_gpio.hdd_present_led	= NULL;
	syno_gpio.fan_fail 			= NULL;
	syno_gpio.hdd_fail_led	 	= NULL;
	syno_gpio.model_id 			= NULL;
	syno_gpio.disk_led_ctrl 	= NULL;
}

int model_addon_init(struct synobios_ops *ops)
{
	/* enable disk LED */
	SYNO_ENABLE_HDD_LED(1);

	/* enable to control PWR_LED */
	ops->set_power_led = SetPowerLedStatus;
	/* enable to control LAN_LED */
	ops->set_phy_led = SetPhyLed;
	/* enable to control HDD_LED */
	ops->set_hdd_led = SetHddLed;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
