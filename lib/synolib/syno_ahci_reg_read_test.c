#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");

struct _ahci_port_st {
	struct pci_dev *pDev;
	int iPortIdx;
};

static struct kobject *synoAhciRegTestKobject;
struct _ahci_port_st ahciPorts[256];
int iTotalSlot = 0;

static void find_ahci_slots(void)
{
	struct pci_dev *pDev = NULL;
	void __iomem *bar5 = NULL;
	u32 cap = 0;
	int i = 0;
	int iTmpIdx = 0;
	int iSlots = 0;

	memset(ahciPorts, 0, sizeof(ahciPorts));

	for_each_pci_dev (pDev) {
		if (0 == strcmp(dev_driver_string(&pDev->dev), "ahci")) {
			bar5 = ioremap(pci_resource_start(pDev, 5), pci_resource_len(pDev, 5));
			if (NULL == bar5) {
				continue;
			}
			cap = readl(bar5);
			iSlots = (cap & 0x1f) + 1;
			iTmpIdx = iTotalSlot;
			for (i = 0; i < iSlots; i++) {
				ahciPorts[iTotalSlot].pDev = pDev;
				ahciPorts[iTotalSlot].iPortIdx = i;
				iTotalSlot++;
			}
			dev_info(&pDev->dev, "With %d ports: %d - %d\n", iSlots, iTmpIdx, iTmpIdx + iSlots - 1);
		}
	}
}

static void show_ahci_slots(void)
{
	int i = 0;
	struct pci_dev *pDev = NULL;

	for (i = 0; i < iTotalSlot; i++) {
		pDev = ahciPorts[i].pDev;
		if (pDev) {
			dev_info(&pDev->dev, "controller slot[%d] index=%d\n", ahciPorts[i].iPortIdx, i);
		}
	}
}

static ssize_t run_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int iVal = 0;
	int i = 0;
	int iPortIdx = 0;
	unsigned long ulStart = 0;
	unsigned long ulDuration = 0;
	struct pci_dev *pDev = NULL;
	void __iomem *bar5 = NULL;
	void __iomem *mmio = 0;
	u32 status = 0;

	sscanf(buf, "%d", &iVal);
	if (0 > iVal || iTotalSlot <= iVal) {
		goto END;
	}

	pDev = ahciPorts[iVal].pDev;
	iPortIdx = ahciPorts[iVal].iPortIdx;

	if (NULL == pDev) {
		goto END;
	}

	bar5 = ioremap(pci_resource_start(pDev, 5), pci_resource_len(pDev, 5));
	if (NULL == bar5) {
		goto END;
	}
	// read PxIS
	mmio = bar5 + 0x100 + 0x80 * iPortIdx + 0x10;

	ulStart = jiffies;
	for (i = 0; i < 1000000; i++) {
		status = readl(mmio);
	}
	ulDuration = jiffies - ulStart;
	dev_info(&pDev->dev, "read slot[%d] PxIS * 1000000, ulDuration: %lu ms\n", iPortIdx, ulDuration*1000/HZ);

END:
	return count;
}

static ssize_t run_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	show_ahci_slots();
	return sprintf(buf, "Check dmesg\n");
}

static struct kobj_attribute run_attribute =__ATTR(run, 0600, run_show, run_store);


static int syno_achi_reg_read_test_init(void)
{
	int iRet = 0;
	synoAhciRegTestKobject = kobject_create_and_add("syno_ahci_reg_read_test", kernel_kobj);
	if (NULL == synoAhciRegTestKobject) {
		iRet = -ENOMEM;
		goto END;
	}

	if (0 != sysfs_create_file(synoAhciRegTestKobject, &run_attribute.attr)) {
		pr_debug("failed to create the run file in /sys/kernel/syno_ahci_reg_read_test \n");
		iRet = -ENOMEM;
		goto END;
	}
	find_ahci_slots();

END:
	return iRet;
}

static void syno_achi_reg_read_test_exit(void)
{
	kobject_put(synoAhciRegTestKobject);

	printk(KERN_INFO "unloading syno ahci reg test\n");
	return;
}

module_init(syno_achi_reg_read_test_init);
module_exit(syno_achi_reg_read_test_exit);
