/* Copyright (c) 2000-2016 Synology Inc. All rights reserved. */

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/string.h>
#include "../i2c/i2c-linux.h"
#include "armada_common.h"
#include <linux/interrupt.h>

#define GPIO_US3_RESET	33
#define GPIO_US3_EJECT	38
#define GPIO_US3_STORAGE_LED	40
#define GPIO_US3_PWR_LED	42
#define GPIO_US3_SDFAIL_LED	43
#define GPIO_US3_MEMTEST_LED	60
#define GPIO_US3_SD_DETECT	62
#define IRQ_US3_BUTTON_RESET		(IRQ_AURORA_GPIO_START + GPIO_US3_RESET)
#define IRQ_US3_BUTTON_EJECT		(IRQ_AURORA_GPIO_START + GPIO_US3_EJECT)
#define IRQ_US3_SD_DETECT		(IRQ_AURORA_GPIO_START + GPIO_US3_SD_DETECT)

struct sd_softc {
	int	countEvents;
	int	idxPtr;
	SYNOBIOSEVENT	rgEvents[SYNOBIOS_NEVENTS];
	wait_queue_head_t wq_poll;
};
int synobios_record_event(struct sd_softc *sc, u_int event_type);

static int doing_factory_default=0;

static void send_reset_event(unsigned long data);
static void send_card_change_event(unsigned long data);
static DEFINE_TIMER(btn_reset_timer, send_reset_event, 0, 0);
static DEFINE_TIMER(card_detect_timer, send_card_change_event, 0, 0);

static
int send_eject_event(void *pdata)
{
	printk("synobios: eject button pressed\n");
	synobios_record_event(NULL,
						  SYNO_EVENT_USBSTATION_EJECT);
	return 0;
}

static
void send_reset_event(unsigned long data)
{
	printk("synobios: reset button pressed\n");
	synobios_record_event(NULL,
						  SYNO_EVENT_BUTTON_RESET);
}

static
int start_reset_timer(void *pdata)
{
	mod_timer(&btn_reset_timer, jiffies + 3 * HZ);

	return 0;
}

static
int cancel_reset_timer(void *pdata)
{
	del_timer_sync(&btn_reset_timer);
	return 0;
}

static
void send_card_change_event(unsigned long data)
{
	synobios_record_event(NULL, SYNO_EVENT_DETECT_CARD_CHANGE);
}

static
int detect_card_change(void *pdata)
{
	printk("synobios: SD card change detected\n");
	synobios_record_event(NULL, SYNO_EVENT_DETECT_CARD_CHANGE);

	/* US3 card reader may delay card init with different firmware
	 * Send additionl event to cover such delay */
	mod_timer(&card_detect_timer, jiffies + 2 * HZ);

	return 0;
}


int GetModel(void)
{
	return MODEL_US3;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1200);
}

static int US3SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	// dummy interface
	return 0;
}

static int US3GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	*pStatus = FAN_STATUS_RUNNING;
	return 0;
}


int 
InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = ops->get_model();
	module_t type_us3v1 = MODULE_T_US3v1;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_US3:
		pType = &type_us3v1;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
	return 0;
}

static
int armada_gpio_set(int pin, int value)
{
	return SYNO_ARMADA_GPIO_PIN(pin, &value, 1);
}

typedef struct {
	int gpio;
	int low_active;
} GPIO_LED;

static
int set_led(GPIO_LED *led, SYNO_LED status)
{
	const int active = led->low_active? 0 : 1;
	switch (status) {
		case SYNO_LED_ON:
			armada_gpio_set(led->gpio, active);
			SYNO_ARMADA_GPIO_BLINK(led->gpio, 0);
			break;
		case SYNO_LED_BLINKING:
			armada_gpio_set(led->gpio, !active);
			SYNO_ARMADA_GPIO_BLINK(led->gpio, 1);
			break;
		case SYNO_LED_OFF:
			armada_gpio_set(led->gpio, !active);
			SYNO_ARMADA_GPIO_BLINK(led->gpio, 0);
			break;
		default:
			printk("unknown led status: %d\n", status);
	}
	return 0;
}

static GPIO_LED pwr_green_led = {
	.gpio = GPIO_US3_PWR_LED,
	.low_active = 1
};

static GPIO_LED pwr_orange_led = {
	.gpio = GPIO_US3_SDFAIL_LED,
	.low_active = 1
};

static GPIO_LED storage_led = {
	.gpio = GPIO_US3_STORAGE_LED,
	.low_active = 0
};

static GPIO_LED memtest_led = {
	.gpio = GPIO_US3_MEMTEST_LED,
	.low_active = 1
};

static
int us3_set_power_led(SYNO_LED status)
{
       /* we may add mutual control with sdfail led in the future */
       return set_led(&pwr_green_led, status);
}

static
int us3_exdisplay_handler(struct _SynoMsgPkt *pMsgPkt)
{
	SYNO_LED *pLed;

	switch(pMsgPkt->usNum) {
		case SYNO_LED_USBSTATION_MEMTEST_LED:
			pLed = (SYNO_LED *)pMsgPkt->szMsg;
			set_led(&memtest_led, *pLed);
			break;
		case SYNO_LED_USBSTATION_DISK_ORANGE:
			pLed = (SYNO_LED *)pMsgPkt->szMsg;
			set_led(&pwr_orange_led, *pLed);
			break;
		case SYNO_LED_USBSTATION_DISK_GREEN:
			pLed = (SYNO_LED *)pMsgPkt->szMsg;
			set_led(&storage_led, *pLed);
			break;
		case SYNO_LED_USBSTATION_POWER:
			pLed = (SYNO_LED *)pMsgPkt->szMsg;
			set_led(&pwr_green_led, *pLed);
			break;
		case SYNO_SYS_RUN:
			/* RUN: power LED green steady */
			set_led(&pwr_green_led, SYNO_LED_ON);
			set_led(&pwr_orange_led, SYNO_LED_OFF);
			break;
		case SYNO_SYS_SHUTDOWN:
			if (doing_factory_default) {
				/* factory default: orange blinking */
				set_led(&pwr_green_led, SYNO_LED_OFF);
				set_led(&pwr_orange_led, SYNO_LED_BLINKING);
			} else {
				/* normal shutdown: green blinking */
				set_led(&pwr_green_led, SYNO_LED_BLINKING);
				set_led(&pwr_orange_led, SYNO_LED_OFF);
			}
			break;
		case SYNO_SYS_NO_SYSTEM:
			/* NODISK: sd fail led orange blinking */
			set_led(&pwr_green_led, SYNO_LED_OFF);
			set_led(&pwr_orange_led, SYNO_LED_BLINKING);
			break;
		case SYNO_SYS_WAIT_RESET:
			/* press reset button 3 sec will trigger reset password
			   change power led to orange to indicate button pressed */
			set_led(&pwr_green_led, SYNO_LED_OFF);
			set_led(&pwr_orange_led, SYNO_LED_ON);
			break;
		case SYNO_SYS_FACTORY_DEFAULT:
			/* blink sd fail led to indicating factory default in processing */
			set_led(&pwr_green_led, SYNO_LED_OFF);
			set_led(&pwr_orange_led, SYNO_LED_BLINKING);
			/* leave message to switch shutdown led mode */
			doing_factory_default=1;
			break;
		case SYNO_BEEP_ON:
			/* No buzzer. Dont care. */
			break;
		case SYNO_LED_USB_EJECT_BLINK:
		case SYNO_LED_HDD_AB:
		case SYNO_LED_HDD_GS:
			/* No harddisk. Dont care. */
			break;
		default:
			printk("Unhandled msg num %04lx\n", pMsgPkt->usNum);
			break;
	}
	return 0;
}

static struct level_button eject_btn = {
	.name = "us3_eject_button",
	.irq = IRQ_US3_BUTTON_EJECT,
	.gpio = GPIO_US3_EJECT,
	.is_high_pressed = 0,
	.press_act = send_eject_event,
	.release_act = NULL
};

static struct level_button reset_btn = {
	.name = "us3_reset_button",
	.irq = IRQ_US3_BUTTON_RESET,
	.gpio = GPIO_US3_RESET,
	.is_high_pressed = 0,
	.press_act = start_reset_timer,
	.release_act = cancel_reset_timer
};

extern int add_card_detect_proc(void);
extern int remove_card_detect_proc(void);
static struct level_button btn_sd_detect = {
	.name = "sd_detect",
	.irq = IRQ_US3_SD_DETECT,
	.gpio = GPIO_US3_SD_DETECT,
	.is_high_pressed = 0,
	.press_act = detect_card_change,
	.release_act = detect_card_change
};

int model_addon_init(struct synobios_ops *ops)
{
	/* init button timer */
	init_timer(&btn_reset_timer);
	init_timer(&card_detect_timer);

	/* set specialized functions */
	ops->set_power_led = us3_set_power_led;
	ops->exdisplay_handler = us3_exdisplay_handler;

	/* set GPIO init value for led */
	set_led(&pwr_green_led, SYNO_LED_BLINKING); // blinking when booting
	set_led(&pwr_orange_led, SYNO_LED_OFF);

	/* init reset, eject button */
	level_button_init(&eject_btn);
	level_button_init(&reset_btn);

	/* register irq for SD card detection
	 * also add proc entry to notify user space */
	level_button_init(&btn_sd_detect);
	add_card_detect_proc();  // add proc entry to tell hotplugd don't poll

	/* disable HDD LED default function */
	ops->set_hdd_led = NULL;
	ops->set_phy_led = NULL;
 	
	/* set dummy fan control interface */
	ops->set_fan_status = US3SetFanStatus;
	ops->get_fan_status = US3GetFanStatus;

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	/* remove pending timer */
	del_timer_sync(&btn_reset_timer);
	del_timer_sync(&card_detect_timer);

	remove_card_detect_proc();
	level_button_exit(&btn_sd_detect);

	level_button_exit(&eject_btn);
	level_button_exit(&reset_btn);

	return 0;
}
