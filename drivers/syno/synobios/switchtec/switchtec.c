// Copyright (c) 2000-2020 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/synolib.h>
#include <linux/syno_gpio.h>
#include "switchtec.h"
#include "../led/led_gpio.h"

#define SWITCHTEC_MRPC_REGION_SIZE 0x1000
#define SWITCHTEC_NUM_MAX 8

static int g_switch_num = 0;
// Memory-based Remote Procedure Calls
static void __iomem *g_mrpc[SWITCHTEC_NUM_MAX];
static unsigned int g_led_off_gpio[SWITCHTEC_NUM_MAX];
static struct pci_dev *g_pdev[SWITCHTEC_NUM_MAX];

int switchtec_init(void)
{
	unsigned int bus, dev, fun;
	int idx, switch_num = 0;
	int rc = -1;
	struct device_node *pDeviceNode = NULL;
	char *szProperty = NULL;
	resource_size_t res_start;

	memset (g_mrpc, 0, sizeof(g_mrpc));
	memset (g_led_off_gpio, 0, sizeof(g_led_off_gpio));
	memset (g_pdev, 0, sizeof(g_pdev));

	for_each_child_of_node(of_root, pDeviceNode) {
		if (pDeviceNode->full_name == NULL) {
			continue;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
		if(0 != strncmp(pDeviceNode->full_name, DT_SWITCHTEC, strlen(DT_SWITCHTEC))) {
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		if(0 != strncmp(pDeviceNode->full_name, "/"DT_SWITCHTEC, strlen("/"DT_SWITCHTEC))) {
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
			continue;
		}
		++switch_num;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
		if (1 != sscanf(pDeviceNode->full_name, DT_SWITCHTEC"@%d", &idx)) {
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		if (1 != sscanf(pDeviceNode->full_name, "/"DT_SWITCHTEC"@%d", &idx)) {
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
			printk("synobios: cannot parse switchtec index\n");
			goto END;
		}
		if (0 > idx || SWITCHTEC_NUM_MAX <= idx) {
			printk("synobios: invalid switchtec index (%d)\n", idx);
			goto END;
		}
		szProperty = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);
		if(3 != sscanf(szProperty, "%x:%x.%x", &bus, &dev, &fun)) {
			printk("synobios: cannot parse switchtec%d %s from dts\n", idx, DT_PCIE_ROOT);
			goto END;
		}
		g_pdev[idx] = pci_get_domain_bus_and_slot(0, bus, PCI_DEVFN(dev, fun));

		if (NULL == g_pdev[idx]) {
			printk("synobios: swtichtec%d pci %02x:%02x.%d dev not found\n",
					idx, bus, dev, fun);
			goto END;
		}

		res_start = pci_resource_start(g_pdev[idx], 0);
		g_mrpc[idx] = devm_ioremap_wc(&g_pdev[idx]->dev, res_start,
				SWITCHTEC_MRPC_REGION_SIZE);
		if (NULL == g_mrpc[idx]) {
			printk("synobios: switchtec%d devm_ioremap_wc failed\n", idx);
			goto END;
		}

		szProperty = (char *)of_get_property(pDeviceNode, DT_LED_OFF_GPIO, NULL);
		if (1 != sscanf(szProperty, "%x", &g_led_off_gpio[idx])) {
			printk("synobios: cannot parse switchtec%d %s from dts\n", idx, DT_LED_OFF_GPIO);
			goto END;
		}
	}
	g_switch_num = switch_num;
	rc = 0;
END:
	if (rc) {
		switchtec_deinit();
	}
	return rc;
}

void switchtec_deinit(void)
{
	int i;

	for (i = 0; i < SWITCHTEC_NUM_MAX; ++i) {
		if (NULL == g_mrpc[i]) {
			continue;
		}
		devm_iounmap(&g_pdev[i]->dev, g_mrpc[i]);
	}
	memset (g_mrpc, 0, sizeof(g_mrpc));
	memset (g_led_off_gpio, 0, sizeof(g_led_off_gpio));
	memset (g_pdev, 0, sizeof(g_pdev));
	g_switch_num = 0;
}

int switchtec_gpio_config(int switch_no, unsigned int gpio, unsigned int level)
{
	int rc = -1;
	uint64_t val;
	if (switch_no < 0 || switch_no >= g_switch_num) {
		printk("synobios: invalid switch_no (%d)\n", switch_no);
		goto END;
	}
	if (NULL == g_mrpc[switch_no]) {
		printk("synobios: mrpc[%d] is null\n", switch_no);
		goto END;
	}
// Reference Microsemi Global Address Space and Management through MRPC
// BITS     NAME               HEX   DEC  DECODE
// [7:0]    Sub Command      = 0x3   3    Set GPIO Pin
// [15:8]   Reserved         = 0x0   0
// [23:16]  Logical Index 0  = 0xa1  161  GPIO #
// [31:24]  Logical Index 1  = 0x0   0
// [39:32]  GPIO Data        = 0x1   1    The Signal is High
// [63:40]  Reserved         = 0x0   0
	val = 0x3ull | (gpio << 16);
	if (level) {
		val |= 0x100000000ull;
	}
	writeq(val, g_mrpc[switch_no]);
	writel(0x10002, g_mrpc[switch_no]+0x800);
	rc = 0;
END:
	return rc;
}

int switchtec_led_set(SYNO_LED ledStatus)
{
	int i, rc = 0;

	for (i = 0; i < g_switch_num; ++i) {
		if (switchtec_gpio_config(i, g_led_off_gpio[i], (SYNO_LED_OFF == ledStatus))) {
			printk("synobios: switchtec_gpio_config switchtec%d failed\n", i);
			rc = -1;
		}
	}

	return rc;
}

int switchtec_set_disk_led(DISKLEDSTATUS *cmd)
{
	u8 state = PIN_BITMAP_OFF;
	u32 pin = 0, val = 0;
	int ret = -1;

	switch (cmd->status) {
		case DISK_LED_ORANGE_SOLID:
			state = PIN_BITMAP_ORANGE;
			break;
		case DISK_LED_OFF:
		case DISK_LED_GREEN_SOLID:
		case DISK_LED_GREEN_BLINK:
		case DISK_LED_ORANGE_BLINK:
			// only support set faulty LED now
			state = PIN_BITMAP_OFF;
			break;
		default:
			goto END;
	}

	if (HAVE_HDD_FAIL_LED_BY_SLOT(cmd->szNodeName, cmd->diskno)) {
		pin = HDD_FAIL_LED_PIN_BY_SLOT(cmd->szNodeName, cmd->diskno);
		val = EVAL_PIN_VAL(HDD_FAIL_LED_POLARITY_BY_SLOT(cmd->szNodeName, cmd->diskno), state & PIN_BITMAP_ORANGE);
		switchtec_gpio_config(HDD_SWITCH_NO(cmd->diskno), pin, val);
	}
	ret = 0;
END:
	return ret;
}
