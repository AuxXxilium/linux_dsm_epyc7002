#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>

static struct kobject *jmb58xSPIObject = NULL;

#define JMB_HEADER_SIZE 3 * 1024
unsigned char read_buffer[JMB_HEADER_SIZE] = {0};
unsigned char header_buffer[JMB_HEADER_SIZE] = {0};

unsigned int uiSelectedJMB58X = 0;


static int syno_jmb58x_check(struct pci_dev* pdev)
{
	return (pdev->vendor == 0x197b && (pdev->device == 0x0585 || pdev->device == 0x0582)) ? 0 : -1;
}

u8 jmb58x_spi_wait_status_wip0(void __iomem *host_mmio, u32 timeout_ms, u8 delay_ms)
{
	unsigned long timeout;
	u8 wip0_flag = 0;

	timeout = jiffies + msecs_to_jiffies(timeout_ms*1000);

	while (time_before(jiffies, timeout)) {
		// check WIP bit
		writel(0xFF | (0x80 << 8) | (0x08 << 16) | (0x05 << 24), host_mmio + 0xB4);
		if (((u8) readl(host_mmio + 0xCC)) & 0x01) {
			ndelay(5);
		} else {
			// wip became zero
			wip0_flag = 1;
			break;
		}
	}

	if (!wip0_flag) {
		printk("\nJMB58X header update error: WIP bit of Flash Status didn't become zero\n\n");
	}

	return 1;
}

u8 jmb58x_spi_wait_read_done(void __iomem *host_mmio, u8 *buffer, u32 buffer_size, u32 offset)
{
	u32 i;
	u32 dwData = 0;

	// wait WIP bit to become zero
	if(!jmb58x_spi_wait_status_wip0(host_mmio, 2*1000, 1)) {
		return 0;
	}
	// setting for reading
	writel(0xFF | (0x80 << 8) | (0xE9 << 16) | (0x03 << 24), host_mmio + 0xB4);

	for (i = offset; i < (buffer_size+offset); i += 4) {
		writel(i, host_mmio + 0xC0);
		dwData = readl(host_mmio + 0xCC);
		buffer[i - offset] = (u8)dwData;
		buffer[i - offset + 1] = (u8)(dwData >> 8);
		buffer[i - offset + 2] = (u8)(dwData >> 16);
		buffer[i - offset + 3] = (u8)(dwData >> 24);
	}

	return 1;
}

static ssize_t jmb58x_spi_content_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pci_dev *pdev = NULL;
	void __iomem *bar5 = NULL;
	unsigned int uiCount = 0;
	int iRet = 0;

	if (0 == uiSelectedJMB58X) {
		printk(KERN_INFO "Please select a jmb58x to read spi header\n");
		memset(read_buffer, 0, sizeof(read_buffer));
		goto END;
	}

	for_each_pci_dev(pdev) {
		if (0 != syno_jmb58x_check(pdev)) {
			continue;
		}
		uiCount++;
		if (uiSelectedJMB58X != uiCount) {
			continue;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb58x sata registers\n");
			return 0;
		}

		memset(read_buffer, 0, sizeof(read_buffer));
		if(!jmb58x_spi_wait_read_done(bar5, read_buffer, JMB_HEADER_SIZE, 0)) {
			return 0;
		}

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	}
	memcpy(buf, read_buffer,JMB_HEADER_SIZE);
	iRet = JMB_HEADER_SIZE;
END:
	return iRet;
}

static ssize_t jmb58x_spi_update_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = NULL;
	u32 i = 0;
	u8 cmp_fail_flag = 0;
	unsigned int uiCount = 0;
	void __iomem *bar5 = NULL;

	for_each_pci_dev(pdev) {
		if (0 != syno_jmb58x_check(pdev)) {
			continue;
		}
		uiCount++;
		if (0 != uiSelectedJMB58X && (uiSelectedJMB58X != uiCount)) {
			continue;
		}

		bar5 = ioremap(pci_resource_start(pdev, 5), pci_resource_len(pdev, 5));
		if (!bar5) {
			dev_warn(&pdev->dev, "Can't map jmb sata registers\n");
			return count;
		}
		memcpy(header_buffer, buf, sizeof(header_buffer));

		if((header_buffer[0] != 0x85 && header_buffer[0] != 0x82) || header_buffer[1] != 0x05 || header_buffer[2] != 0x7B || header_buffer[3] != 0x19) {
			//Values not 0x197B0585 nor 0x197b0582are invalid.
			dev_warn(&pdev->dev, "JMB58X header update error: Incorrect header format!");
			return count;
		}
		// wait WIP bit to become zero
		if(!jmb58x_spi_wait_status_wip0(bar5, 2000, 1)){
			return count;
		}
		// write enable
		writel(0xFF | (0x80 << 8) | (0x00 << 16) | (0x06 << 24), bar5 + 0xB4);
		writel(0, bar5 + 0xCC);
		// disable BP(Block Protect) bits, because MXIC flash's BP(Block Protect) bits are enabled
		// write 0 to status register
		writel(0xFF | (0x80 << 8) | (0x04 << 16) | (0x01 << 24), bar5 + 0xB4);
		writel(0, bar5 + 0xCC);

		if(!jmb58x_spi_wait_status_wip0(bar5, 1000, 1)){
			return count;
		}

		// write enable
		writel(0xFF | (0x80 << 8) | (0x00 << 16) | (0x06 << 24), bar5 + 0xB4);
		writel(0, bar5 + 0xCC);

		dev_info(&pdev->dev, "JMB58X flash update: Begin Chip Erase\n");
		// chip erase
		writel(0xFF | (0x80 << 8) | (0x00 << 16) | (0xC7 << 24), bar5 + 0xB4);
		writel(0, bar5 + 0xCC);
		if(!jmb58x_spi_wait_status_wip0(bar5, 33000, 1)) {
			//Chip erase may need a longer time to complete, wait for up to 33 seconds
			return count;
		}

		dev_info(&pdev->dev, "JMB58X flash update: Chip Erase Done. Begin flash update\n");
		for (i = 0; i < JMB_HEADER_SIZE; i += 4) {
			if ( ! jmb58x_spi_wait_status_wip0(bar5, 1000, 1)) {
				return count;
			}
			// write enable
			writel(0xFF | (0x80 << 8) | (0x00 << 16) | (0x06 << 24), bar5 + 0xB4);
			writel(0, bar5 + 0xCC);
			// setting for page program
			writel(0xFF | (0x80 << 8) | (0xE5 << 16) | (0x02 << 24), bar5 + 0xB4);

			writel(i, bar5 + 0xC0);
			writel(header_buffer[i] | (header_buffer[i+1] << 8) | (header_buffer[i+2] << 16) | (header_buffer[i+3] << 24) , bar5 + 0xCC);
		}
		dev_info(&pdev->dev, "JMB58X flash update: Complete flash update.\n");

		dev_info(&pdev->dev, "JMB58X flash update: Begin flash read\n");
		memset(read_buffer, 0, sizeof(read_buffer));
		if (!jmb58x_spi_wait_read_done(bar5, read_buffer, JMB_HEADER_SIZE, 0)) {
			return count;
		}

		dev_info(&pdev->dev, "JMB58X flash update: Flash read Done. Comparing data...\n");
		// compare
		for (i = 0; i < JMB_HEADER_SIZE; i++) {
			if (header_buffer[i] != read_buffer[i]) {
				dev_warn(&pdev->dev, "\nJMB58X header update error: Comparison failed at %d, write is 0x%X but read is 0x%X!!\n", i, header_buffer[i], read_buffer[i]);
				cmp_fail_flag = 1;
				return count;
			}
		}
		if (!cmp_fail_flag) {
			dev_warn(&pdev->dev, "JMB58X flash update: JMB58X header update successful!\n");
		}

		if (bar5) {
			iounmap(bar5);
			bar5 = NULL;
		}
	}

	return count;
}

static ssize_t jmb58x_spi_select_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pci_dev *pdev = NULL;
	int iCount = 0, iCurrent = 0;

	iCurrent = snprintf(buf, PAGE_SIZE, "Current selected jmb58x : %u\n", uiSelectedJMB58X);
	iCurrent += snprintf(buf + iCurrent, PAGE_SIZE - iCurrent, "0. All jmb58x is selected (default).\n");
	for_each_pci_dev(pdev) {
		if (0 != syno_jmb58x_check(pdev)) {
			continue;
		}
		iCurrent += snprintf(buf + iCurrent, PAGE_SIZE - iCurrent, "%u. %04x:%04x pcie bus %02x:%02x.%x\n", 
		                     ++iCount, pdev->vendor, pdev->device, pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
	}
	return iCurrent;
}

static ssize_t jmb58x_spi_select_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = NULL;
	u32 iCount=0;
	int iRet = -1;

	iRet = kstrtouint(buf, 10, &uiSelectedJMB58X);
	if (0 > iRet) {
		goto END;
	}

	if (0 == uiSelectedJMB58X) {
		printk(KERN_INFO "No.0 All jmb58x are selected.");
		iRet = count;
		goto END;
	}

	for_each_pci_dev(pdev) {
		if (0 != syno_jmb58x_check(pdev)) {
			continue;
		}
        iCount++;
		if (uiSelectedJMB58X == iCount) {
			printk(KERN_INFO "No.%u jmb58x %04x:%04x on pcie bus %02x:%02x.%x has been selected\n",
			       iCount, pdev->vendor, pdev->device, pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
			break;
		}
	}
	if (0 != uiSelectedJMB58X && (uiSelectedJMB58X != iCount)) {
		printk(KERN_INFO "There is no jmb58x being selected for input no.%u. All jmb58x will be selected by default.\n", uiSelectedJMB58X);
        uiSelectedJMB58X = 0;
	}
	iRet = count;
END:
	return iRet;
}

// register function to attribute
static struct kobj_attribute jmb58xSelectAttr = __ATTR( jmb58x_spi_select, 0640, jmb58x_spi_select_show, jmb58x_spi_select_store);

// register function to attribute
static struct kobj_attribute jmb58xUpdateAttr = __ATTR( jmb58x_spi_update, 0640, jmb58x_spi_content_show, jmb58x_spi_update_store);

// put attribute to attribute group
static struct attribute *jmb58xSPIAttr[] = {
	&jmb58xUpdateAttr.attr,
	&jmb58xSelectAttr.attr,
	NULL,   /* NULL terminate the list*/
};
static struct attribute_group jmb58xSPIGroup = {
	.attrs = jmb58xSPIAttr
};

static int jmb58x_spi_update_init(void)
{
	int iRet = -1;
	jmb58xSPIObject = kobject_create_and_add("jmb58x_spi_update", kernel_kobj);
	if (!jmb58xSPIObject) {
		iRet = -ENOMEM;
		goto END;
	}

	//create attributes (files)
	if(sysfs_create_group(jmb58xSPIObject, &jmb58xSPIGroup)){
		iRet = -ENOMEM;
		goto END;
	}

	iRet = 0;
END:
	if (0 != iRet) {
		if (jmb58xSPIObject) {
			kobject_put(jmb58xSPIObject);
		}
	}
	return iRet;
}

static void jmb58x_spi_update_exit(void)
{
	kobject_put(jmb58xSPIObject);
}

MODULE_LICENSE("GPL");
module_init(jmb58x_spi_update_init);
module_exit(jmb58x_spi_update_exit);
