/*
 * Copyright (c) 2000-2021 Synology Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/leds-atmega1608.h>
#include <linux/spinlock.h>
#include <linux/synobios.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/synolib.h>
#include <linux/string.h>
#include <linux/syno_fdt.h>

#define MAX_NUM_LEDS		24
#define MAX_BRIGHTNESS		255
#define LED_OFF			0

/* Registers */
#define ATMEGA1608_INPUT1	0x00
#define ATMEGA1608_PSC0		0x02
#define ATMEGA1608_PWM0		0x03
#define ATMEGA1608_PSC1		0x04
#define ATMEGA1608_PWM1		0x05
#define ATMEGA1608_LS0		0x06
#define ATMEGA1608_LS1		0x07
#define ATMEGA1608_LS2		0x08
#define ATMEGA1608_LS3		0x09
#define ATMEGA1608_LS4		0x0A
#define ATMEGA1608_LS5		0x0B

/* Mask, shift */
#define ATMEGA1608_SEL0_M	0x03
#define ATMEGA1608_SEL1_M	0x0C
#define ATMEGA1608_SEL2_M	0x30
#define ATMEGA1608_SEL3_M	0xC0
#define ATMEGA1608_SEL0_S	0
#define ATMEGA1608_SEL1_S	2
#define ATMEGA1608_SEL2_S	4
#define ATMEGA1608_SEL3_S	6

extern char **syno_led_trigger_name;

static const u8 mask_sel[] = {
	ATMEGA1608_SEL0_M,
	ATMEGA1608_SEL1_M,
	ATMEGA1608_SEL2_M,
	ATMEGA1608_SEL3_M,
};

static const u8 shift_sel[] = {
	ATMEGA1608_SEL0_S,
	ATMEGA1608_SEL1_S,
	ATMEGA1608_SEL2_S,
	ATMEGA1608_SEL3_S,
};

struct atmega1608_led {
	int id;
	struct led_classdev cdev;
	struct atmega1608_led_node *node;
	struct work_struct brtwork;
	u8 brightness;
	int retry_count;
};

struct atmega1608 {
	struct i2c_client *client;
	struct device *dev;
	struct atmega1608_led led[MAX_NUM_LEDS];
	int num_leds;
};

static DEFINE_MUTEX(ModeLock);

struct atmega1608_led_node syno_led_nodes[24] = {};

struct atmega1608_platform_data syno_atmega1608_pdata = {
	.node = syno_led_nodes,
	.num_nodes = ARRAY_SIZE(syno_led_nodes),
};

#define SYNO_ATMEGA1608_MAX_RETRY 5

static int atmega1608_read_byte(struct atmega1608 *at, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(at->client, reg);
	if (ret < 0) {
		dev_err(at->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	*data = (u8)ret;
	return 0;
}

static int atmega1608_write_byte(struct atmega1608 *at, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(at->client, reg, data);
}

static int atmega1608_update_bits(struct atmega1608 *at, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&ModeLock);
	ret = atmega1608_read_byte(at, reg, &tmp);
	if (ret) {
		goto END;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = atmega1608_write_byte(at, reg, tmp);

END:
	mutex_unlock(&ModeLock);
	return ret;
}

static int atmega1608_update_selector(struct atmega1608 *at, enum atmega1608_led_mode mode,
				enum atmega1608_led_channel channel)
{
	u8 addr, mask, shift, idx;

	switch (channel) {
	case ATMEGA1608_LED0 ... ATMEGA1608_LED3:
		addr = ATMEGA1608_LS0;
		break;
	case ATMEGA1608_LED4 ... ATMEGA1608_LED7:
		addr = ATMEGA1608_LS1;
		break;
	case ATMEGA1608_LED8 ... ATMEGA1608_LED11:
		addr = ATMEGA1608_LS2;
		break;
	case ATMEGA1608_LED12 ... ATMEGA1608_LED15:
		addr = ATMEGA1608_LS3;
		break;
	case ATMEGA1608_LED16 ... ATMEGA1608_LED19:
		addr = ATMEGA1608_LS4;
		break;
	case ATMEGA1608_LED20 ... ATMEGA1608_LED23:
		addr = ATMEGA1608_LS5;
		break;
	default:
		return -EINVAL;
	}

	idx = channel % 4;
	mask = mask_sel[idx];
	shift = shift_sel[idx];

	return atmega1608_update_bits(at, addr, mask, mode << shift);
}

static int atmega1608_update_scale(struct atmega1608 *at, enum atmega1608_led_mode mode,
				u8 prescale)
{
	u8 addr;

	switch (mode) {
	case ATMEGA1608_LED_DIM0:
		addr = ATMEGA1608_PSC0;
		break;
	case ATMEGA1608_LED_DIM1:
		addr = ATMEGA1608_PSC1;
		break;
	default:
		return 0;
	}

	return atmega1608_write_byte(at, addr, prescale);
}

static int atmega1608_update_pwm(struct atmega1608 *at, enum atmega1608_led_mode mode,
				u8 pwm)
{
	u8 addr;

	switch (mode) {
	case ATMEGA1608_LED_DIM0:
		addr = ATMEGA1608_PWM0;
		break;
	case ATMEGA1608_LED_DIM1:
		addr = ATMEGA1608_PWM1;
		break;
	default:
		return 0;
	}

	return atmega1608_write_byte(at, addr, pwm);
}

static void atmega1608_syno_brightness_set(u8 brightness, enum atmega1608_led_mode *mode, enum atmega1608_led_mode nodeMode)
{
	if (!mode) {
		goto END;
	}
	switch (brightness) {
		case 0:
			*mode = ATMEGA1608_LED_OFF;
			break;
		case 255:
			*mode = ATMEGA1608_LED_ON;
			break;
		default:
			*mode = nodeMode;
			break;
	}

END:
	return;
}

static int atmega1608_update_brightness(struct atmega1608_led *led)
{
	struct atmega1608 *at = container_of(led, struct atmega1608, led[led->id]);
	struct atmega1608_led_node *node = led->node;
	enum atmega1608_led_mode mode;
	int ret;


	atmega1608_syno_brightness_set(led->brightness, &mode, node->mode);

	ret = atmega1608_update_selector(at, mode, node->channel);
	if (ret) {
		return ret;
	}

	if (mode == ATMEGA1608_LED_OFF || mode == ATMEGA1608_LED_ON) {
		return 0;
	}

	ret = atmega1608_update_scale(at, mode, node->prescale);
	if (ret) {
		return ret;
	}

	ret = atmega1608_update_pwm(at, mode, led->brightness);
	if (ret) {
		return ret;
	}

	return 0;
}

static void atmega1608_brightness_force_off(struct atmega1608 *at)
{
	int i;
	u8 addr[] = { ATMEGA1608_LS0, ATMEGA1608_LS1, ATMEGA1608_LS2, ATMEGA1608_LS3, ATMEGA1608_LS4, ATMEGA1608_LS5 };

	for (i = 0 ; i < ARRAY_SIZE(addr) ; i++)
		atmega1608_write_byte(at, addr[i], LED_OFF);
}

static void atmega1608_brightness_work(struct work_struct *work)
{
	int ret = 0;
	struct atmega1608_led *led;

	led = container_of(work, struct atmega1608_led, brtwork);
	ret = atmega1608_update_brightness(led);

	if (ret) {
		if (led->retry_count < SYNO_ATMEGA1608_MAX_RETRY) {
			++led->retry_count;
			pr_err("%s: retry to recover %s for %d times\n",
				__func__, led->node->name, led->retry_count);
			schedule_work(&led->brtwork);
		} else {
			pr_err("%s: failed to recover %s\n",
				__func__, led->node->name);
		}
	} else {
		led->retry_count = 0;
	}
}

static void atmega1608_brightness_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	struct atmega1608_led *led;

	led = container_of(led_cdev, struct atmega1608_led, cdev);
	if(brightness == led->brightness) {
		return;
	}
	led->brightness = brightness;
	schedule_work(&led->brtwork);
}

static int atmega1608_leds_register(struct atmega1608 *at,
				struct atmega1608_platform_data *pdata)
{
	struct atmega1608_led_node *node;
	int i, ret;

	for (i = 0 ; i < at->num_leds ; i++) {
		node = pdata->node + i;

		if (!node || !node->name) {
			dev_err(at->dev, "invalid data on node%d\n", i);
			ret = -EINVAL;
			goto err_dev;
		}

		INIT_WORK(&at->led[i].brtwork, atmega1608_brightness_work);
		at->led[i].id = i;
		at->led[i].node = node;
		at->led[i].cdev.name = node->name;
		at->led[i].cdev.max_brightness = MAX_BRIGHTNESS;
		at->led[i].cdev.brightness_set = atmega1608_brightness_set;
		at->led[i].cdev.default_trigger = node->default_trigger;
		at->led[i].retry_count = 0;

		ret = led_classdev_register(at->dev, &at->led[i].cdev);
		if (ret) {
			dev_err(at->dev, "led(%d/%d) register err: %d\n",
					i, at->num_leds, ret);
			goto err_dev;
		}
	}

	return 0;

err_dev:
	while (--i >= 0) {
		led_classdev_unregister(&at->led[i].cdev);
		cancel_work_sync(&at->led[i].brtwork);
	}
	return ret;
}

static void atmega1608_leds_unregister(struct atmega1608 *at)
{
	int i;

	for (i = 0 ; i < at->num_leds ; i++) {
		led_classdev_unregister(&at->led[i].cdev);
		cancel_work_sync(&at->led[i].brtwork);
	}
}

static int atmega1608_validate_platform_data(struct device *dev,
				struct atmega1608_platform_data *pdata)
{
	if (!pdata || !pdata->node) {
		dev_err(dev, "invalid platform data\n");
		goto err;
	}

	if (pdata->num_nodes == 0 || pdata->num_nodes > MAX_NUM_LEDS) {
		dev_err(dev, "invalid num_nodes: %d\n", pdata->num_nodes);
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}

static int atmega1608_chip_detect(struct atmega1608 *at)
{
	u8 val;
	return atmega1608_read_byte(at, ATMEGA1608_INPUT1, &val);
}

static int atmega1608_probe(struct i2c_client *cl,
				const struct i2c_device_id *id)
{
	struct atmega1608 *at = NULL;
	struct atmega1608_platform_data *pdata = NULL;
	int device_index = 0;
	struct device_node *pI2CNode = NULL;
	struct device_node *pI2CDevNode = NULL;
	int ret;
	char *drv_name = "atmega1608";
	int i = 0;

	if (NULL == cl || NULL == id) {
		goto END;
	}

	pdata = &syno_atmega1608_pdata;

	ret = atmega1608_validate_platform_data(&cl->dev, pdata);
	if (ret) {
		goto END;
	}

	at = devm_kzalloc(&cl->dev, sizeof(struct atmega1608), GFP_KERNEL);
	if (!at) {
		goto END;
	}


	at->client = cl;
	at->dev = &cl->dev;
	at->num_leds = pdata->num_nodes;
	i2c_set_clientdata(cl, at);

	if (of_root) {
		pI2CNode = syno_of_i2c_adapter_match(cl->adapter);
		pI2CDevNode = syno_of_i2c_device_match(cl, drv_name, pI2CNode);
		of_property_read_u32_index(pI2CDevNode, DT_DEVICE_INDEX, 0, &device_index);
	} else {
		dev_err(at->dev, "fail to get device tree\n");
		goto END;
	}

	for (i = 0 ; i < pdata->num_nodes ; i++) {
                pdata->node[i].name = kzalloc(64 * sizeof(char), GFP_KERNEL);
                snprintf( pdata->node[i].name, 64, "syno_led%d", ((24 * device_index) + i));

                pdata->node[i].mode = ATMEGA1608_LED_DIM0;
                pdata->node[i].prescale = 30;
                pdata->node[i].channel = i;
                pdata->node[i].default_trigger = syno_led_trigger_name[24 * device_index + i];
        }

	ret = atmega1608_chip_detect(at);
	if (ret) {
		dev_err(at->dev, "chip detection err: %d\n", ret);
		goto END;
	}

	return atmega1608_leds_register(at, pdata);
END:
	return -EINVAL;
}

static int atmega1608_remove(struct i2c_client *cl)
{
	struct atmega1608 *at = i2c_get_clientdata(cl);

	atmega1608_brightness_force_off(at);
	atmega1608_leds_unregister(at);
	return 0;
}

static const struct i2c_device_id atmega1608_id[] = {
	{"atmega1608", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, atmega1608_id);

static struct i2c_driver atmega1608_driver = {
	.probe = atmega1608_probe,
	.remove = atmega1608_remove,
	.driver = {
		.name = "atmega1608",
		.owner = THIS_MODULE,
	},
	.id_table = atmega1608_id,
};

module_i2c_driver(atmega1608_driver);

MODULE_DESCRIPTION("ATMEGA1608 LED Driver");
MODULE_AUTHOR("Alex Lai");
MODULE_LICENSE("GPL");
