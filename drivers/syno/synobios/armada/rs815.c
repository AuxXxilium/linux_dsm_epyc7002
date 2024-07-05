#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2013 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/cpumask.h>
#include "../i2c/i2c-linux.h"
#include "armada_common.h"
#include <linux/interrupt.h>

#define GPIO_RS815_BUZZER    44
#define IRQ_RS815_BUTTON_BUZZER (IRQ_AURORA_GPIO_START + GPIO_RS815_BUZZER)

struct sd_softc {
	int countEvents;
	int idxPtr;
	SYNOBIOSEVENT   rgEvents[SYNOBIOS_NEVENTS];
	wait_queue_head_t wq_poll;
};
int synobios_record_event(struct sd_softc *sc, u_int event_type);

static
int send_buzzer_clear_event(void *pdata)
{
	printk("synobios: buzzer stop button pressed\n");
	synobios_record_event(NULL, SYNO_EVENT_BUTTON_BUZZER_CLEAR);
	return 0;
}

static
struct level_button buzzer_btn = {
	.name = "rs815_button_buzzer",
	.irq = IRQ_RS815_BUTTON_BUZZER,
	.gpio = GPIO_RS815_BUZZER,
	.is_high_pressed = 0,
	.press_act = send_buzzer_clear_event,
	.release_act = NULL
};


int GetModel(void)
{
	return MODEL_RS815;
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
	snprintf(cpu->clock, sizeof(char) * maxLength, "%d", 1333);
}

int
InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = ops->get_model();
	module_t type_815 = MODULE_T_RS815;
	module_t *pType = NULL;

	switch (model) {
	case MODEL_RS815:
		pType = &type_815;
		armada_hdd_enable_gpio[0] = 42;
		armada_hdd_enable_gpio[1] = 44;
		armada_hdd_enable_gpio[2] = 45;
		armada_hdd_enable_gpio[3] = 46;
		break;
	default:
		break;
	}

	module_type_set(pType);
	return 0;
}

int SetDiskLedStatus(int disknum, SYNO_DISK_LED status)
{
#ifdef MY_DEF_HERE
	if (1 == disknum)
		return SYNO_SOC_HDD_LED_SET(disknum, status);
	else
		return SetDiskLedStatusBySataMvGPIO(disknum, status);
#else
	return 0;
#endif
}

static int set_hdd_led_all(SYNO_LED status)
{
	return SYNO_ENABLE_HDD_LED((SYNO_LED_ON == status)? TRUE : FALSE);
}

int model_addon_init(struct synobios_ops *ops)
{
	/* init disk led */
	SYNO_ENABLE_HDD_LED(TRUE);
	/* HDD1(ata5) not conctrl by mv7042 sata chip, so we default set led on*/
	SYNO_SOC_HDD_LED_SET(1, SYNO_LED_ON);
	ops->set_hdd_led = set_hdd_led_all;
	ops->set_phy_led = NULL;

	/* init buzzer clear button */
	level_button_init(&buzzer_btn);

	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	level_button_exit(&buzzer_btn);

	return 0;
}
