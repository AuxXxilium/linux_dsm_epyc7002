/*
 * Copyright (c) 2000-2021 Synology Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/leds-atmega1608-seg7.h>
#include <linux/spinlock.h>
#include <linux/synobios.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/synolib.h>
#include <linux/string.h>
#include <linux/syno_fdt.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DRIVER_NAME			"atmega1608_seg7"

#define MAX_NUM_LEDS		24
#define MAX_NUM_SEG7		3
#define NUM_LEDS_IN_SEG7	8
#define LED_OFF			0

#define BRIGHTNESS_ON		255
#define BRIGHTNESS_OFF		0

/* Registers */
#define ATMEGA1608_INPUT1	0x00
#define ATMEGA1608_INPUT2	0x01
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
#define ATMEGA1608_MASK_M	0x01
#define ATMEGA1608_SEL0_M	0x03
#define ATMEGA1608_SEL1_M	0x0C
#define ATMEGA1608_SEL2_M	0x30
#define ATMEGA1608_SEL3_M	0xC0
#define ATMEGA1608_MASK_S	0
#define ATMEGA1608_SEL0_S	0
#define ATMEGA1608_SEL1_S	2
#define ATMEGA1608_SEL2_S	4
#define ATMEGA1608_SEL3_S	6

#define SIZE_END_CHAR		1
#define SIZE_CHAR_ARRAY		64

#define SYNO_ATMEGA1608_MAX_RETRY 5

static DEFINE_MUTEX(ModeLock);

static const u8 map_char[] = {
	' ',
	'0',
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7',
	'8',
	'9',
};

  // 7-segment map:

  //     AAA
  //    F   B
  //    F   B
  //     GGG
  //    E   C
  //    E   C
  //     DDD  H

static const u8 map_digit[] = {
  //HGFEDCBA     char
  0b00000000, // ' '
  0b00111111, // '0'
  0b00000110, // '1'
  0b01011011, // '2'
  0b01001111, // '3'
  0b01100110, // '4'
  0b01101101, // '5'
  0b01111101, // '6'
  0b00100111, // '7'
  0b01111111, // '8'
  0b01101111, // '9'
};

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

struct atmega1608_led_node {
	int mode;
	u8 prescale;
	int channel; //read from dts seg_led_map
};

struct atmega1608_led {
	int id;
	struct atmega1608_led_node node;
	struct work_struct brtwork;
	u8 brightness;
	int retry_count;
};

struct atmega1608_seg7 {
	int id;
	int num_leds;
	struct atmega1608_led led[NUM_LEDS_IN_SEG7];
};

struct atmega1608 {
	char name[SIZE_CHAR_ARRAY];
	char display_number[SIZE_CHAR_ARRAY];
	int device_index;	//read from dts device_index
	struct i2c_client *client;
	struct device *dev;
	struct atmega1608_seg7 seg7[MAX_NUM_SEG7];
	int num_map_char;
	int num_seg7;	//read from dts seg7_num
	int num_leds_in_seg7;
	struct device_attribute dev_attr;
	struct led_classdev cdev;
};

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
		goto End;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = atmega1608_write_byte(at, reg, tmp);

End:
	mutex_unlock(&ModeLock);
	return ret;
}

static int atmega1608_update_selector(struct atmega1608 *at, enum atmega1608_seg7_led_mode mode,
				enum atmega1608_seg7_led_channel channel)
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

static int atmega1608_update_scale(struct atmega1608 *at, enum atmega1608_seg7_led_mode mode,
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

static int atmega1608_update_pwm(struct atmega1608 *at, enum atmega1608_seg7_led_mode mode,
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

static void atmega1608_syno_brightness_set(u8 brightness, enum atmega1608_seg7_led_mode *mode, enum atmega1608_seg7_led_mode nodeMode)
{
	if (!mode) {
		goto End;
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

End:
	return;
}

static int atmega1608_update_brightness(struct atmega1608_led *led)
{
	struct atmega1608_seg7 *seg7 = container_of(led, struct atmega1608_seg7, led[led->id]);
	struct atmega1608 *at = container_of(seg7, struct atmega1608, seg7[seg7->id]);
	struct atmega1608_led_node *node = &led->node;
	enum atmega1608_seg7_led_mode mode;
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
			pr_err("%s: retry to recover led_%d for %d times\n",
				__func__, led->id, led->retry_count);
			schedule_work(&led->brtwork);
		} else {
			pr_err("%s: failed to recover led_%d\n",
				__func__, led->id);
		}
	} else {
		led->retry_count = 0;
	}
}

static int character_valid_check(u8 ch) {
	int i;
	for (i = 0 ; i < ARRAY_SIZE(map_char) ; i++) {
		if (ch == map_char[i]) {
			return 0;
		}
	}
	return -EINVAL;
}

static int character_index_find(u8 ch) {
	int i;
	for (i = 0 ; i < ARRAY_SIZE(map_char) ; i++) {
		if (ch == map_char[i]) {
			return i;
		}
	}
	return -EINVAL;
}

static void atmega1608_brightness_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	int i, j;
	struct atmega1608 *at;

	at = container_of(led_cdev, struct atmega1608, cdev);
	if(0 != brightness) {
		return;
	}

	for (i = 0 ; i < at->num_seg7 ; i++) {
		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			at->seg7[i].led[j].brightness = BRIGHTNESS_OFF;
			schedule_work(&at->seg7[i].led[j].brtwork);
		}
	}
}

static ssize_t atmega1608_seg7_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct atmega1608 *at;

	at = container_of(attr, struct atmega1608, dev_attr);

	memcpy(buf, at->display_number, at->num_seg7 + SIZE_END_CHAR);
	return (at->num_seg7 + SIZE_END_CHAR);
}

static ssize_t atmega1608_seg7_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i, j, ret;
	u8 map_index;
	u8 digit;
	struct atmega1608 *at;
	size_t input_size = 0;
	size_t noshow_size = 0;

	at = container_of(attr, struct atmega1608, dev_attr);

	if (count > (at->num_seg7 + SIZE_END_CHAR)) {
		printk("ERROR: Invalid Input Size\n");
		goto End;
	}
	input_size = count - 1;
	noshow_size = at->num_seg7 - input_size;

	for (i = 0 ; i < input_size ; i++) {
		ret = character_valid_check(buf[i]);
		if (ret) {
			printk("ERROR: Invalid Input Character\n");
			goto End;
		}
	}

	for (i = 0 ; i < noshow_size ; i++) {
		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			at->seg7[i].led[j].brightness = BRIGHTNESS_OFF;
			schedule_work(&at->seg7[i].led[j].brtwork);
		}
	}
	for (i = noshow_size ; i < at->num_seg7 ; i++) {
		map_index = character_index_find(buf[i - noshow_size]);
		digit = map_digit[map_index];
		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			if ((digit >> j) & 0x1) {
				at->seg7[i].led[j].brightness = BRIGHTNESS_ON;
			} else {
				at->seg7[i].led[j].brightness = BRIGHTNESS_OFF;
			}
			schedule_work(&at->seg7[i].led[j].brtwork);
		}
	}

	snprintf(at->display_number, SIZE_CHAR_ARRAY, buf);
	return count;
End:
	return -EIO;
}

static int atmega1608_seg7_initial(struct atmega1608 *at)
{
	int i, j;

	for (i = 0 ; i < at->num_seg7 ; i++) {
		at->seg7[i].id = i;
		at->seg7[i].num_leds = NUM_LEDS_IN_SEG7;
		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			at->seg7[i].led[j].id = j;
			at->seg7[i].led[j].node.mode = ATMEGA1608_LED_DIM0;
			at->seg7[i].led[j].node.prescale = 30;
			at->seg7[i].led[j].retry_count = 0;
			INIT_WORK(&at->seg7[i].led[j].brtwork, atmega1608_brightness_work);
		}
	}

	return 0;
}

static int atmega1608_leds_mask(struct atmega1608* at,
				enum atmega1608_seg7_led_mask led_mask)
{
	u8 addr, mask, shift;

	addr = ATMEGA1608_INPUT2;
	mask = ATMEGA1608_MASK_M;
	shift = ATMEGA1608_MASK_S;

	return atmega1608_update_bits(at, addr, mask, led_mask << shift);
}

static int atmega1608_chip_detect(struct atmega1608 *at)
{
	u8 val;
	return atmega1608_read_byte(at, ATMEGA1608_INPUT1, &val);
}

static int atmega1608_device_tree_read(struct atmega1608 *at, struct i2c_client *cl)
{
	int i = 0, j = 0;
	int ret = 0;
	struct device_node *pI2CNode = NULL;
	struct device_node *pI2CDevNode = NULL;
	char *seg7_led_map_name = NULL;

	if (NULL == at || NULL == cl) {
		printk("ERROR: at pointer or cl pointer is NULL");
		goto Err;
	}

	if (!of_root) {
		printk("ERROR: of_root is NULL");
		goto Err;
	}

	pI2CNode = syno_of_i2c_adapter_match(cl->adapter);
	if (!pI2CNode) {
		dev_err(at->dev, "i2c device match err - i2c node\n");
		goto Err;
	}
	
	pI2CDevNode = syno_of_i2c_device_match(cl, DRIVER_NAME, pI2CNode);
	if (!pI2CDevNode) {
		dev_err(at->dev, "i2c device match err - i2c device node\n");
		goto Err;
	}

	ret = of_property_read_u32_index(pI2CDevNode, DT_DEVICE_INDEX, 0, &at->device_index);
	if (ret) {
		dev_err(at->dev, "not found device_index err - i2c device node\n");
		goto Err;
	}

	ret = of_property_read_u32_index(pI2CDevNode, DT_SEG7_NUM, 0, &at->num_seg7);
	if (ret) {
		dev_err(at->dev, "not found seg7_num err\n");
		goto Err;
	}
	if (0 > at->num_seg7 || MAX_NUM_SEG7 < at->num_seg7) {
		goto Err;
	}

	for (i = 0 ; i < at->num_seg7 ; i++) {

		switch (i) {
			case 0:
				seg7_led_map_name = DT_SEG7_LED_MAP_0;
				break;
			case 1:
				seg7_led_map_name = DT_SEG7_LED_MAP_1;
				break;
			case 2:
				seg7_led_map_name = DT_SEG7_LED_MAP_2;
				break;
			default:
				goto Err;
		}

		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			ret = of_property_read_u32_index(pI2CDevNode, seg7_led_map_name, j, &at->seg7[i].led[j].node.channel);
			if (ret) {
				dev_err(at->dev, "not found index %d in %s err\n", j, seg7_led_map_name);
				goto Err;
			}
			if (0 > at->seg7[i].led[j].node.channel || MAX_NUM_LEDS < at->seg7[i].led[j].node.channel) {
				goto Err;
			}
		}
	}

	return 0;
Err:
	return -EINVAL;
}

static int atmega1608_probe(struct i2c_client *cl,
				const struct i2c_device_id *id)
{
	struct atmega1608 *at = NULL;
	int ret;
	int i;

	if (NULL == cl || NULL == id) {
		goto End;
	}

	if (ARRAY_SIZE(map_char) != ARRAY_SIZE(map_digit)) {
		goto End;
	}

	at = devm_kzalloc(&cl->dev, sizeof(struct atmega1608), GFP_KERNEL);
	if (!at) {
		goto End;
	}

	at->client = cl;
	at->dev = &cl->dev;
	at->num_map_char = ARRAY_SIZE(map_char);
	at->num_leds_in_seg7 = NUM_LEDS_IN_SEG7;

	i2c_set_clientdata(cl, at);

	ret = atmega1608_device_tree_read(at, cl);
	if (ret) {
		dev_err(at->dev, "fail to get device tree\n");
		goto FreeEnd;
	}

	ret = atmega1608_chip_detect(at);
	if (ret) {
		dev_err(at->dev, "chip detection err: %d\n", ret);
		goto FreeEnd;
	}

	ret = atmega1608_leds_mask(at, ATMEGA1608_LED_UNMASK);
	if (ret) {
		dev_err(at->dev, "let unmask err: %d\n", ret);
		goto FreeEnd;
	}
	
	ret = atmega1608_seg7_initial(at);
	if (ret) {
		dev_err(at->dev, "seg7 initial err: %d\n", ret);
		goto FreeEnd;
	}

	for (i = 0 ; i < at->num_seg7 ; i++) {
		at->display_number[i] = ' ';
	}
	at->display_number[at->num_seg7] = '\0';
	snprintf(at->name, SIZE_CHAR_ARRAY, "syno_seg7_%d", at->device_index);
	at->cdev.name = at->name;
	at->cdev.max_brightness = 0;
	at->cdev.brightness_set = atmega1608_brightness_set;
	at->dev_attr.attr.name = "display";
	at->dev_attr.attr.mode = (S_IRUGO | S_IWUSR);
	at->dev_attr.show = atmega1608_seg7_show;
	at->dev_attr.store = atmega1608_seg7_store;

	ret = led_classdev_register(at->dev, &at->cdev);
	if (ret) {
		dev_err(at->dev, "led class device register err\n");
		goto FreeEnd;
	}

	ret = device_create_file(at->cdev.dev, &at->dev_attr);
	if (ret) {
		printk("ERROR: class file can not be created!\n");
		goto FreeEnd;
	}

	return 0;
FreeEnd:
	devm_kfree(&cl->dev, at);
End:
	return -EINVAL;
}

static int atmega1608_remove(struct i2c_client *cl)
{
	int i, j;
	struct atmega1608 *at = i2c_get_clientdata(cl);

	device_remove_file(at->cdev.dev, &at->dev_attr);
	led_classdev_unregister(&at->cdev);
	for (i = 0 ; i < at->num_seg7 ; i++) {
		for (j = 0 ; j < at->num_leds_in_seg7 ; j++) {
			cancel_work_sync(&at->seg7[i].led[j].brtwork);
		}
	}
	atmega1608_brightness_force_off(at);
	atmega1608_leds_mask(at, ATMEGA1608_LED_MASKED);
	devm_kfree(&cl->dev, at);

	return 0;
}

static const struct i2c_device_id atmega1608_id[] = {
	{DRIVER_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, atmega1608_id);

static struct i2c_driver atmega1608_driver = {
	.probe = atmega1608_probe,
	.remove = atmega1608_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.id_table = atmega1608_id,
};

module_i2c_driver(atmega1608_driver);

MODULE_DESCRIPTION("ATMEGA1608 LED SEVEN SEGMENT Driver");
MODULE_AUTHOR("Jack Zhang");
MODULE_LICENSE("GPL");
