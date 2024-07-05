// Copyright (c) 2000-2013 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include "armada_common.h"
#include <linux/interrupt.h>

#define GPIO_GROUP_0	0
#define GPIO_GROUP_1	1

typedef int MV_STATUS;
typedef unsigned int MV_U32;
MV_STATUS mvGppPolaritySet(MV_U32 group, MV_U32 mask, MV_U32 value);
MV_U32  mvGppPolarityGet(MV_U32 group, MV_U32 mask);

static inline
int get_group_and_mask(int gpio, MV_U32 *group, MV_U32 *mask)
{
	if (gpio < 32) {
		*group = GPIO_GROUP_0;
		*mask = 1U << gpio;
	} else if (32 <= gpio && gpio < 64) {
		*group = GPIO_GROUP_1;
		*mask = 1U << (gpio - 32);
	} else {
		printk("Not supported button gpio %d\n", gpio);
		BUG_ON(1);
		return -1;
	}
	return 0;
}

static
int get_status_and_invert_polarity(struct level_button *btn)
{
	MV_U32 gppPolarity;
	MV_U32 gppGroup = 0;
	MV_U32 gppMask = 0;
	int invert_polarity;

	get_group_and_mask(btn->gpio, &gppGroup, &gppMask);

	/* get current polarity */
	gppPolarity = mvGppPolarityGet(gppGroup, gppMask);
	invert_polarity = gppPolarity? 1 : 0;

	/* Revert polarity of gpp */
	gppPolarity ^= gppMask;
	mvGppPolaritySet(gppGroup, gppMask, gppPolarity);

	/* GPIO interrupt is always high trigger.
	 * If polarity is inversed, incoming signal is low */
	if (invert_polarity) {
		/* currently low on PCB */
		if (btn->is_high_pressed) {
			return BTN_RELEASED;
		} else {
			return BTN_PRESSED;
		}
	} else {
		/* currently high on PCB */
		if (btn->is_high_pressed) {
			return BTN_PRESSED;
		} else {
			return BTN_RELEASED;
		}
	}
}

static
irqreturn_t btn_isr_handler (int irq, void *dev_id)
{
	struct level_button *btn = (struct level_button *)dev_id;
	BUG_ON(irq != btn->irq);

	btn->stat = get_status_and_invert_polarity(btn);

	if (!btn->press_act && !btn->release_act) {
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static
irqreturn_t btn_dsr_handler(int irq, void *dev_id)
{
	struct level_button *btn = (struct level_button *)dev_id;
	BUG_ON(irq != btn->irq);

	if (BTN_PRESSED == btn->stat) {
		if (btn->press_act) {
			btn->press_act(dev_id);
		}
	} else {
		if (btn->release_act) {
			btn->release_act(dev_id);
		}
	}

	return IRQ_HANDLED;
}

int level_button_init(struct level_button *btn)
{
	MV_U32 gppPolarity;
	MV_U32 gppGroup = 0;
	MV_U32 gppMask = 0;

	/* set polarity */
	get_group_and_mask(btn->gpio, &gppGroup, &gppMask);
	gppPolarity = btn->is_high_pressed? 0 : 0xffff;
	mvGppPolaritySet(gppGroup, gppMask, gppPolarity);

	/* register ISR */
	if (request_threaded_irq(btn->irq,
				 btn_isr_handler,
				 btn_dsr_handler,
				 0, btn->name,
				 btn))
	{
		printk("Failed to register irq button: %s(%d,%d)\n",
				btn->name, btn->irq, btn->gpio);
		return -1;
	}

	return 0;
}

int level_button_exit(struct level_button *btn)
{
	free_irq(btn->irq, btn);
	return 0;
}

