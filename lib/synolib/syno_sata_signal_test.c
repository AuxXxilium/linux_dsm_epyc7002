#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>

#define SATA_SPEED_LEN    3
#define MAX_PATTERN_TYPES 6

static struct kobject *SynoSataTestObject = NULL;
unsigned int uiSelectedDev = 0;
unsigned int uiPort = 0;
unsigned int uiMaxPortNum = 0;
unsigned int uiSpeed = 0;
unsigned int uiPattern = 0;
char*        szPatterns[MAX_PATTERN_TYPES] = {"NOT SELECT","LFTP","MFTP","HFTP","LBP","SSOP"};

unsigned mv_port_addr[4] = {0x178, 0x1F8, 0x278, 0x2F8};
unsigned mv_port_data[4] = {0x17C, 0x1FC, 0x27C, 0x2FC};
unsigned mv_speed_addr[SATA_SPEED_LEN] = {0x8D, 0x8F, 0x91};
int asm1061_reg_addr_port0[SATA_SPEED_LEN] = {0xCA4, 0xCA5, 0xCA6};
int asm1061_reg_addr_port1[SATA_SPEED_LEN] = {0xDA4, 0xDA5, 0xDA6};
unsigned asm116x_port_addr[SATA_SPEED_LEN][6] = {{0x122, 0x322, 0x522, 0x722, 0x922, 0xB22},
                                                 {0x123, 0x323, 0x523, 0x723, 0x923, 0xB23},
                                                 {0x124, 0x324, 0x524, 0x724, 0x924, 0xB24}};
unsigned jmb_port_addr[SATA_SPEED_LEN][5] = {{0x74, 0x76, 0x78, 0x7A, 0x7C},
                                             {0x73, 0x75, 0x77, 0x79, 0x7B},
                                             {0x04, 0x11, 0x1e, 0x2b, 0x38}};

static const struct pci_device_id syno_device[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_EXT, 0x9170) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_EXT, 0x9215) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_EXT, 0x9235) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ASMEDIA, 0x0612) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ASMEDIA, 0x1164) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ASMEDIA, 0x1165) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JMICRON, 0x0585) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JMICRON, 0x0582) },
};

static bool is_synology_device(struct pci_dev *pdev)
{
	const struct pci_device_id *match_device;

	for (match_device = syno_device;
	     match_device < syno_device + ARRAY_SIZE(syno_device);
	     match_device++) {
		if (pdev->vendor == match_device->vendor &&
		    pdev->device == match_device->device)
			return true;
	}
	return false;
}

static struct pci_dev* syno_selected_dev_get(void)
{
	struct pci_dev *pdev = NULL;
	unsigned int uiDevCount = 0;

	while (NULL != (pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (!is_synology_device(pdev)) {
			continue;
		}
		uiDevCount++;
		if (uiSelectedDev == uiDevCount) {
			break;
		}
	}

	return pdev;
}

static ssize_t syno_test_select_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pci_dev *pdev = NULL;
	unsigned int uiDevCount = 0;
	int iRet = 0;

	iRet += scnprintf(buf, PAGE_SIZE,"%s %d: NOT SELECT(default)\n", (uiSelectedDev == uiDevCount)?"*":" ", uiDevCount);
	while (NULL != (pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (!is_synology_device(pdev)) {
			continue;
		}
		uiDevCount++;
		iRet += scnprintf(buf + iRet, PAGE_SIZE - iRet,
		                   "%s %u: %02x:%02x.%x : %04x:%04x\n",
		                    (uiSelectedDev == uiDevCount)?"*":" ", uiDevCount,
		                    pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7,
		                    pdev->vendor,pdev->device);
	}

	return iRet;
}

static ssize_t syno_test_select_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = NULL;
	unsigned int uiDevCount = 0;
	bool isDevFound = false;

	if (0 > kstrtouint(buf, 10, &uiSelectedDev)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

	if (0 == uiSelectedDev) {
		printk(KERN_WARNING "No.0 means not select.  Please select device.\n list devices by cat test_select\n");
		goto END;
	}

	while (NULL != (pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (!is_synology_device(pdev)) {
			continue;
		}
		uiDevCount++;
		if (uiSelectedDev == uiDevCount) {
			isDevFound = true;
			printk(KERN_INFO "Select %u: bus %02x:%02x.%x : %04x:%04x\n", uiDevCount,
			                  pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7,
			                  pdev->vendor,pdev->device);
			break;
		}
	}

	if (!isDevFound) {
		printk(KERN_WARNING "%u is out of range.  Please select deivce again\n", uiSelectedDev);
		uiSelectedDev = 0;
	}
END:
	return count;
}

static ssize_t syno_test_ssc_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pci_dev *pdev = syno_selected_dev_get();
	u32 iValue;
	bool isSSCOn = false;
	int iRet = 0;

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (uiSpeed == 0 || SATA_SPEED_LEN < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		goto END;
	}
	if (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) {
		void __iomem *bar5 = NULL;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			goto END;
		}
		// Index port has 24 bits, PHY registers access uses bit[12:0] and bit[18], bit[18] is used to select:
		//   0: PCIe PHY registers
		//   1: SATA PHY registers.
		// Offset 0x2 is for SSC enable/disable register, Offset C0 [IDXP] is index port register
		writel((0x2 & 0x01FFFUL) + (1UL << 18UL), bar5 + 0xC0);
		mdelay(100);
		// Offset C8 [DPHY] is data port for PCIe/SATA PHY registers access.
		// 0x00003813 for disable SSC, 0x00003803 for enable
		iValue = readl(bar5 + 0xC8);
		isSSCOn = (iValue & 0x0010) ? 0 : 1;

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	} else if (pdev->vendor == 0x1b21 && (pdev->device == 0x1164 || pdev->device == 0x1165)) {
		void __iomem *bar0 = NULL;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar0 = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
		if (!bar0) {
			dev_warn(&pdev->dev, "Can't map asmedia sata registers\n");
			goto END;
		}

		iValue = (u32)readb(bar0 + 0x198c);
		isSSCOn = (iValue & 1) ? 1 : 0;
	
		if (bar0) {
			iounmap(bar0);
			bar0 = NULL;
		}
	}
	iRet += scnprintf(buf, PAGE_SIZE, "Sata SSC register: 0x%x, SSC %s\n", iValue, isSSCOn ? "on": "off");
END:
	return iRet;
}

static ssize_t syno_test_ssc_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = syno_selected_dev_get();
	unsigned int uiSSCEnable = 0;

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (0 > kstrtouint(buf, 0, &uiSSCEnable)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

	if (1 < uiSSCEnable) {
		dev_warn(&pdev->dev, "Invalid Input: %u!!\n", uiSSCEnable);
		goto END;
	}

	if (uiSpeed == 0 || SATA_SPEED_LEN < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		goto END;
	}

	if (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) {
		void __iomem *bar5 = NULL;
		u32 value = 0;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			goto END;
		}
		// Index port has 24 bits, PHY registers access uses bit[12:0] and bit[18], bit[18] is used to select:
		//   0: PCIe PHY registers
		//   1: SATA PHY registers.
		// Offset 0x2 is for SSC enable/disable register, Offset C0 [IDXP] is index port register
		writel((0x2 & 0x01FFFUL) + (1UL << 18UL), bar5 + 0xC0);
		mdelay(100);
		// Offset C8 [DPHY] is data port for PCIe/SATA PHY registers access.
		// 0x00003813 for disable SSC, 0x00003803 for enable
		if (0 == uiSSCEnable) {
			writel(0x00003813, bar5 + 0xC8);
		} else {
			writel(0x00003803, bar5 + 0xC8);
		}
		mdelay(100);
		value = readl(bar5 + 0xC8);
		dev_info(&pdev->dev, "Sata SSC register : 0x%x, SSC %s\n", value, (value & 0x0010) ? "off": "on");
		
		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	} else if (pdev->vendor == 0x1b21 && (pdev->device == 0x1164 || pdev->device == 0x1165)) {
		void __iomem *bar0 = NULL;
		u8 ssc_reg_data = 0;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar0 = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
		if (!bar0) {
			dev_warn(&pdev->dev, "Can't map asmedia sata registers\n");
			goto END;
		}

		ssc_reg_data = readb(bar0 + 0x198c);
		mdelay(100);
		if (0 == uiSSCEnable) {
			ssc_reg_data &= ~0x01;
		} else {
			ssc_reg_data |= 0x01;
		}
		writeb(ssc_reg_data, bar0 + 0x198c);
		mdelay(100);

		ssc_reg_data = readb(bar0 + 0x198c);
		dev_info(&pdev->dev, "Sata SSC register : 0x%x, SSC %s\n", ssc_reg_data, (ssc_reg_data & 0x01) ? "on": "off");
	
		if (bar0) {
			iounmap(bar0);
			bar0 = NULL;
		}
	}

END:
	return count;
}

static ssize_t syno_test_port_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf (buf, PAGE_SIZE, "%u\n", uiPort);;
}

static ssize_t syno_test_port_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = syno_selected_dev_get();

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (0 > kstrtouint(buf, 0, &uiPort)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

	if ((pdev->vendor == 0x1b4b && pdev->device == 0x9235) ||
	    (pdev->vendor == 0x1b4b && pdev->device == 0x9215) ||
	    (pdev->vendor == 0x1b21 && pdev->device == 0x1164)) {
		uiMaxPortNum = 4;
	} else if ((pdev->vendor == 0x1b4b && pdev->device == 0x9170) ||
	           (pdev->vendor == 0x1b21 && pdev->device == 0x0612) ||
	           (pdev->vendor == 0x197b && pdev->device == 0x0582)) {
		uiMaxPortNum = 2;
	} else if ((pdev->vendor == 0x197b && pdev->device == 0x0585) ||
	           (pdev->vendor == 0x1b21 && pdev->device == 0x1165)) {
		uiMaxPortNum = 5;
	}

	if (uiMaxPortNum <= uiPort) {
		printk(KERN_WARNING "Invalid Port Number: %u\n", uiPort);
		uiPort = 0;
	}

END:
	return count;
}

static ssize_t syno_test_setup_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf (buf, PAGE_SIZE, "%u\n", uiSpeed);
}

static ssize_t syno_test_setup_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = syno_selected_dev_get();

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (0 > kstrtouint(buf, 0, &uiSpeed)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

	if (0 == uiSpeed || 3 < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		uiSpeed = 0;
		goto END;
	}

	if (pdev->vendor == 0x1b4b && (pdev->device == 0x9235 || pdev->device == 0x9215 || pdev->device == 0x9170)) {
		void __iomem *bar5 = NULL;

		if (0 > uiPort || 3 < uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map mv sata registers\n");
			goto END;
		}

		dev_info(&pdev->dev, "mv sata BIST, Port %u setup to SATA Gen %u\n", uiPort, uiSpeed);

		if (uiSpeed == 1) {
			// Set speed to 1.5 Gbps
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000050, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// Assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000051, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// De-assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000050, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else if (uiSpeed == 2) {
			// Set speed to 3.0 Gbps
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000060, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// Assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000061, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// De-assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000060, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else if (uiSpeed == 3) {
			// Set speed to 6.0 Gbps
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000070, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// Assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000071, bar5+mv_port_data[uiPort]);
			mdelay(100);
			// De-assert reset
			writel(0x00000002, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000070, bar5+mv_port_data[uiPort]);
			mdelay(100);
		}

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	}

END:
	return count;
}

static ssize_t syno_test_pattern_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i = 0, iRet = 0;

	for (i = 0; i < MAX_PATTERN_TYPES; i++) {
		iRet += scnprintf(buf + iRet, PAGE_SIZE - iRet, "%s %d: %s\n", (uiPattern == i)?"*":" ", i, szPatterns[i]);
	}

	return iRet;
}

static ssize_t syno_test_pattern_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = syno_selected_dev_get();
	int i = 0;

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (uiSpeed == 0 || SATA_SPEED_LEN < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		goto END;
	}

	for (i = 1; i < MAX_PATTERN_TYPES; i++) {
		if (!memcmp(buf, szPatterns[i], strlen(szPatterns[i]))) {
			uiPattern = i;
			break;
		}
	}

	if (pdev->vendor == 0x1b4b && (pdev->device == 0x9235 || pdev->device == 0x9215 || pdev->device == 0x9170)) {
		void __iomem *bar5 = NULL;

		if (0 > uiPort || 3 < uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map mv sata registers\n");
			goto END;
		}

		dev_info(&pdev->dev, "mv sata BIST, test pattern %s", buf);

		//
		// Write test pattern
		//
		if (!memcmp(buf, "LFTP", 4)) {
			writel(0x00000096, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000097, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000098, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007E7E, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000099, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007E7E, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009A, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007E7E, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009B, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007E7E, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000095, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x000080F0, bar5+mv_port_data[uiPort]);
			mdelay(100);
		}  else if (!memcmp(buf, "MFTP", 4)) {
			writel(0x00000096, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000097, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000098, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007878, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000099, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007878, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009A, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007878, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009B, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00007878, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000095, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x000080F0, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else if (!memcmp(buf, "HFTP", 4)) {
			writel(0x00000096, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000097, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000098, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00004A4A, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000099, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00004A4A, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009A, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00004A4A, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009B, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00004A4A, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000095, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x000080F0, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else if (!memcmp(buf, "LBP", 3)) {
			writel(0x00000096, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000036, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000097, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x0000F423, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000098, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00006F43, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000099, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00003534, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009A, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x0000D353, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009B, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00004C05, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000095, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x000080E0, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else if (!memcmp(buf, "SSOP", 4)) {
			writel(0x00000096, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000097, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000098, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000099, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009A, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x0000009B, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00000000, bar5+mv_port_data[uiPort]);
			mdelay(100);
			writel(0x00000095, bar5+mv_port_addr[uiPort]);
			mdelay(100);
			writel(0x00008050, bar5+mv_port_data[uiPort]);
			mdelay(100);
		} else {
			dev_warn(&pdev->dev, "Invalid Pattern: %s !!\n", buf);
		}

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	} else if (pdev->vendor == 0x1b21 && pdev->device == 0x0612) {
		u8 reg_data_LBP[SATA_SPEED_LEN] = {0x9D, 0xAD, 0xCD};
		u8 reg_data_XFTP[SATA_SPEED_LEN] = {0x96, 0xA6, 0xC6};
		u8 reg_data_SSOP[SATA_SPEED_LEN] = {0x9C, 0xAC, 0xCC};

		if (0 > uiPort || 2 <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		dev_info(&pdev->dev, "test pattern %s", buf);

		//
		// Write test pattern
		//
		if (!memcmp(buf, "LFTP", 4)) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCB2 : 0xDB2, reg_data_XFTP[uiSpeed - 1]);
			mdelay(100);
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCC5 : 0xDC5, 0x02);
		} else if (!memcmp(buf, "MFTP", 4)) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCB2 : 0xDB2, reg_data_XFTP[uiSpeed - 1]);
			mdelay(100);
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCC5 : 0xDC5, 0x01);
		} else if (!memcmp(buf, "HFTP", 4)) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCB2 : 0xDB2, reg_data_XFTP[uiSpeed - 1]);
			mdelay(100);
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCC5 : 0xDC5, 0x00);
		} else if (!memcmp(buf, "LBP", 3)) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCB2 : 0xDB2, reg_data_LBP[uiSpeed - 1]);
		} else if (!memcmp(buf, "SSOP", 4)) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), (0 == uiPort) ? 0xCB2 : 0xDB2, reg_data_SSOP[uiSpeed - 1]);
		} else {
			dev_warn(&pdev->dev, "Invalid Pattern: %s !!\n", buf);
		}
	} else if (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) {
		unsigned jmb_BIST_enable[5] =  {0x174, 0x1f4, 0x274, 0x2f4, 0x374};
		unsigned jmb_SATA_speed[5] =   {0x12c, 0x1ac, 0x22c, 0x2ac, 0x32c};
		unsigned jmb_phy_ready[5] =    {0x170, 0x1f0, 0x270, 0x2f0, 0x370};
		unsigned jmb_pattern_addr[5] = {0x178, 0x1f8, 0x278, 0x2f8, 0x378};
		void __iomem *bar5 = NULL;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			goto END;
		}

		// 1. Enable BIST Mode
		writel(0xdc017740, bar5 + jmb_BIST_enable[uiPort]);
		mdelay(100);

		// 2. Setting SATA Speed
		switch (uiSpeed) {
			case 1:
				writel(0x00000010, bar5 + jmb_SATA_speed[uiPort]);
				break;
			case 2:
				writel(0x00000020, bar5 + jmb_SATA_speed[uiPort]);
				break;
			case 3:
				writel(0x00000000, bar5 + jmb_SATA_speed[uiPort]);
				break;
			default:
				dev_warn(&pdev->dev, "Invalid link speed !!\n");
				goto END;
		}
		mdelay(100);

		// 3. Setting force PHY ready
		writel(0x0044700f, bar5 + jmb_phy_ready[uiPort]);
		mdelay(100);

		// 4. Change SATA TX Measure Pattern
		if (!memcmp(buf, "LFTP", 4)) {
			writel(0x7E7E7E7E, bar5 + jmb_pattern_addr[uiPort]);
		} else if (!memcmp(buf, "MFTP", 4)) {
			writel(0x78787878, bar5 + jmb_pattern_addr[uiPort]);
		} else if (!memcmp(buf, "HFTP", 4)) {
			writel(0x4a4a4a4a, bar5 + jmb_pattern_addr[uiPort]);
		} else if (!memcmp(buf, "LBP", 3)) {
			writel(0x6B0C8B0c, bar5 + jmb_pattern_addr[uiPort]);
		} else if (!memcmp(buf, "SSOP", 4)) {
			writel(0x7f7f7f7f, bar5 + jmb_pattern_addr[uiPort]);
		} else {
			dev_warn(&pdev->dev, "Invalid Pattern: %s !!\n", buf);
		}
		mdelay(100);

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	}

END:
	return count;
}

static ssize_t syno_test_amp_adjust_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pci_dev *pdev = syno_selected_dev_get();
	int iRet = 0;

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (uiSpeed == 0 || SATA_SPEED_LEN < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		goto END;
	}

	if (pdev->vendor == 0x1b4b && (pdev->device == 0x9235 || pdev->device == 0x9215 || pdev->device == 0x9170)) {
		void __iomem *bar5 = NULL;
		u32 value;

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map mv sata registers\n");
			goto END;
		}
		if (0 > uiPort || 3 < uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}
		writel(mv_speed_addr[uiSpeed - 1], bar5 + mv_port_addr[uiPort]);
		value = readl(bar5 + mv_port_data[uiPort]);
		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}

		iRet += scnprintf (buf, PAGE_SIZE, "%x\n", value);
	} else if (pdev->vendor == 0x1b21 && pdev->device == 0x0612) {
		u8 reg_data = 0;

		if (0 > uiPort || 2 <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		if (0 == uiPort) {
			pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), asm1061_reg_addr_port0[uiSpeed - 1], &reg_data);
		} else if (1 == uiPort) {
			pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), asm1061_reg_addr_port1[uiSpeed - 1], &reg_data);
		}

		dev_info(&pdev->dev, "TX de-emphasis: 0x%x", (reg_data & 0xf0) >> 4);
		dev_info(&pdev->dev, "TX amplitude: 0x%x", reg_data & 0x0f);

		iRet += scnprintf(buf, PAGE_SIZE, "%x\n", reg_data);
	} else if (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) {
		void __iomem *bar5 = NULL;
		u32 value;

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			goto END;
		}
		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}
		// Index port has 24 bits, PHY registers access uses bit[12:0] and bit[18], bit[18] is used to select:
		//   0: PCIe PHY registers
		//   1: SATA PHY registers.
		// Offset C0 [IDXP] is index port register
		writel((jmb_port_addr[uiSpeed - 1][uiPort] & 0x01FFFUL) + (1UL << 18UL), bar5 + 0xC0);
		// Offset C8 [DPHY] is data port for PCIe/SATA PHY registers access.
		value = readl(bar5 + 0xC8);

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}

		dev_info(&pdev->dev, "TX de-emphasis: 0x%lx", (value & 0x3FFFFE0UL) >> 5);
		dev_info(&pdev->dev, "TX amplitude: 0x%lx", value & 0x1FUL);

		iRet += scnprintf (buf, PAGE_SIZE, "%x\n", value);
	} else if (pdev->vendor == 0x1b21 && (pdev->device == 0x1164 || pdev->device == 0x1165)) {
		void __iomem *bar0 = NULL;
		u8 reg_data = 0;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar0 = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
		if (!bar0) {
			dev_warn(&pdev->dev, "Can't map asmedia sata registers\n");
			goto END;
		}

		reg_data = readb(bar0 + asm116x_port_addr[uiSpeed][uiPort]);

		if (bar0) {
			iounmap(bar0);
			bar0 = NULL;
		}

		dev_info(&pdev->dev, "TX de-emphasis: 0x%x", (reg_data & 0xF0) >> 4);
		dev_info(&pdev->dev, "TX amplitude: 0x%x", reg_data & 0xF);

		iRet += scnprintf (buf, PAGE_SIZE, "%x\n", reg_data);
	}
END:
	return iRet;
}

static ssize_t syno_test_amp_adjust_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = syno_selected_dev_get();

	if (NULL == pdev) {
		printk(KERN_WARNING "Failed to get pci device.\n");
		goto END;
	}

	if (uiSpeed == 0 || SATA_SPEED_LEN < uiSpeed) {
		dev_warn(&pdev->dev, "Invalid link speed !!\n");
		goto END;
	}

	if (pdev->vendor == 0x1b4b && (pdev->device == 0x9235 || pdev->device == 0x9215 || pdev->device == 0x9170)) {
		void __iomem *bar5 = NULL;
		u32 iValue, i2;

		sscanf(buf, "%x", &iValue);

		if (0 > uiPort || 3 < uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map mv sata registers\n");
			goto END;
		}
		writel(0xE, bar5 + mv_port_addr[uiPort]);
		i2 = readl(bar5 + mv_port_data[uiPort]);
		// write apt adjust
		writel(0xE, bar5 + mv_port_addr[uiPort]);
		writel(i2 & ~0x100, bar5 + mv_port_data[uiPort]);
		// write amp emph
		writel(mv_speed_addr[uiSpeed - 1], bar5 + mv_port_addr[uiPort]);
		writel(iValue, bar5 + mv_port_data[uiPort]);

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	} else if (pdev->vendor == 0x1b21 && pdev->device == 0x0612) {
		u8 reg_data = 0;

		if (0 > uiPort || 2 <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		sscanf(buf, "%02hhx", &reg_data);

		if (0 == uiPort) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), asm1061_reg_addr_port0[uiSpeed - 1], reg_data);
		} else if (1 == uiPort) {
			pci_bus_write_config_byte(pdev->bus, PCI_DEVFN(0x00, 0x0), asm1061_reg_addr_port1[uiSpeed - 1], reg_data);
		}

		dev_info(&pdev->dev, "TX de-emphasis: 0x%x", (reg_data & 0xf0) >> 4);
		dev_info(&pdev->dev, "TX amplitude: 0x%x", reg_data & 0x0f);
	} else if (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) {
		void __iomem *bar5 = NULL;
		u32 iValue;

		sscanf(buf, "%x", &iValue);

		if (0 > uiPort ||  uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			goto END;
		}
		// Index port has 24 bits, PHY registers access uses bit[12:0] and bit[18], bit[18] is used to select:
		//   0: PCIe PHY registers
		//   1: SATA PHY registers.
		// Offset C0 [IDXP] is index port register
		writel((jmb_port_addr[uiSpeed - 1][uiPort] & 0x01FFFUL) + (1UL << 18UL), bar5 + 0xC0);
		// Offset C8 [DPHY] is data port for PCIe/SATA PHY registers access.
		writel(iValue, bar5 + 0xC8);

		dev_info(&pdev->dev, "TX de-emphasis: 0x%lx", (iValue & 0x3FFFFE0UL) >> 5);
		dev_info(&pdev->dev, "TX amplitude: 0x%lx", iValue & 0x1FUL);

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	} else if (pdev->vendor == 0x1b21 && (pdev->device == 0x1164 || pdev->device == 0x1165)) {
		void __iomem *bar0 = NULL;
		u8 reg_data = 0;

		if (0 > uiPort || uiMaxPortNum <= uiPort) {
			dev_warn(&pdev->dev, "Invalid port !!\n");
			goto END;
		}

		sscanf(buf, "%02hhx", &reg_data);

		bar0 = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
		if (!bar0) {
			dev_warn(&pdev->dev, "Can't map asmedia sata registers\n");
			goto END;
		}

		writeb(reg_data, bar0 + asm116x_port_addr[uiSpeed][uiPort]);

		dev_info(&pdev->dev, "TX de-emphasis: 0x%x", (reg_data & 0xF0) >> 4);
		dev_info(&pdev->dev, "TX amplitude: 0x%x", reg_data & 0xF);

		if (bar0) {
			iounmap(bar0);
			bar0 = NULL;
		}
	}

END:
	return count;
}

// register function to attribute
static struct kobj_attribute syno_test_select = __ATTR( syno_test_select, 0640, syno_test_select_show, syno_test_select_store);
static struct kobj_attribute syno_test_port = __ATTR( syno_test_port, 0640, syno_test_port_show, syno_test_port_store);
static struct kobj_attribute syno_test_setup = __ATTR( syno_test_setup, 0640, syno_test_setup_show, syno_test_setup_store);
static struct kobj_attribute syno_test_ssc = __ATTR( syno_test_ssc, 0640, syno_test_ssc_show, syno_test_ssc_store);
static struct kobj_attribute syno_test_pattern = __ATTR( syno_test_pattern, 0640, syno_test_pattern_show, syno_test_pattern_store);
static struct kobj_attribute syno_test_amp_adjust = __ATTR( syno_test_amp_adjust, 0640, syno_test_amp_adjust_show, syno_test_amp_adjust_store);

// put attribute to attribute group
static struct attribute *SynoSataTestAttr[] = {
	&syno_test_select.attr,
	&syno_test_port.attr,
	&syno_test_setup.attr,
	&syno_test_ssc.attr,
	&syno_test_pattern.attr,
	&syno_test_amp_adjust.attr,
	NULL,   /* NULL terminate the list*/
};
static struct attribute_group SynoSataTestGroup = {
	.attrs = SynoSataTestAttr
};

static int syno_sata_test_init(void)
{
	int iRet = -1;
	SynoSataTestObject = kobject_create_and_add("syno_sata_test", kernel_kobj);
	if (!SynoSataTestObject) {
		iRet = -ENOMEM;
		goto END;
	}

	//create attributes (files)
	if(sysfs_create_group(SynoSataTestObject, &SynoSataTestGroup)){
		iRet = -ENOMEM;
		goto END;
	}

	iRet = 0;
END:
	if (0 != iRet) {
		if (SynoSataTestObject) {
			kobject_put(SynoSataTestObject);
		}
	}
	return iRet;
}

static void syno_sata_test_exit(void)
{
	kobject_put(SynoSataTestObject);
}

MODULE_LICENSE("GPL");
module_init(syno_sata_test_init);
module_exit(syno_sata_test_exit);
