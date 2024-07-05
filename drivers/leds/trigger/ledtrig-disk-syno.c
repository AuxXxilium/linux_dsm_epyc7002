// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Kernel Timer Trigger
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include "../leds.h"

#define DEFAULT_BLINK_DELAY 150

static ssize_t led_delay_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_on);
}

static ssize_t led_delay_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_cdev->blink_delay_on = state;

	return size;
}

static ssize_t led_delay_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_off);
}

static ssize_t led_delay_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_cdev->blink_delay_off = state;

	return size;
}

static ssize_t led_activated_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);

	return sprintf(buf, "%s\n", (led_cdev->activated ? "ON" : "OFF"));
}

void ledtrig_syno_disk_led_on(struct led_classdev *led_cdev, bool active)
{
	led_cdev->activated = active;

	if (active) {
		led_set_brightness_nosleep(led_cdev, led_cdev->max_brightness);

		//init blink rate
		led_cdev->blink_delay_on = DEFAULT_BLINK_DELAY;	
		led_cdev->blink_delay_off = DEFAULT_BLINK_DELAY;
	} else {
		led_set_brightness(led_cdev, LED_OFF);
	}
}
EXPORT_SYMBOL(ledtrig_syno_disk_led_on);

static ssize_t led_activated_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	ledtrig_syno_disk_led_on(led_cdev, (state > 0 ? true : false));

	return size;
}

static DEVICE_ATTR(delay_on, 0644, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0644, led_delay_off_show, led_delay_off_store);
static DEVICE_ATTR(activated, 0644, led_activated_show, led_activated_store);

static struct attribute *syno_disk_trig_attrs[] = {
	&dev_attr_delay_on.attr,
	&dev_attr_delay_off.attr,
	&dev_attr_activated.attr,
	NULL
};
ATTRIBUTE_GROUPS(syno_disk_trig);

void ledtrig_syno_disk_activity_on(struct led_classdev *led_cdev)
{
	if (!led_cdev->activated) {
		return ;
	}

	// the forth parameter of led_blink_set_oneshot is invert.
	// This controlls the led behavior after the blink activity.
	// For invert = 1, led is set to on after the blink which matched synology need.
	// The led present is on after a disk is inserted on DSM.
	// For invert = 0, led is set to off after the blink which matched normal pc behavior.
	// The HDD led is usually off, and blinks when data transfer on most pc.
	led_blink_set_oneshot(led_cdev, &led_cdev->blink_delay_on, &led_cdev->blink_delay_off, 1);
}
EXPORT_SYMBOL(ledtrig_syno_disk_activity_on);

static int syno_disk_trig_activate(struct led_classdev *led_cdev)
{
	//init blink rate
	led_cdev->blink_delay_on = DEFAULT_BLINK_DELAY;	
	led_cdev->blink_delay_off = DEFAULT_BLINK_DELAY;

	return 0;
}

static void syno_disk_trig_deactivate(struct led_classdev *led_cdev)
{
	led_set_brightness(led_cdev, LED_OFF);
	led_cdev->activated = false;
}

static struct led_trigger syno_disk_led_trigger = {
	.name     = "disk_syno",
	.activate = syno_disk_trig_activate,
	.deactivate = syno_disk_trig_deactivate,
	.groups = syno_disk_trig_groups,
};
module_led_trigger(syno_disk_led_trigger);

MODULE_DESCRIPTION("Synology Disk LED trigger");
MODULE_LICENSE("GPL v2");
