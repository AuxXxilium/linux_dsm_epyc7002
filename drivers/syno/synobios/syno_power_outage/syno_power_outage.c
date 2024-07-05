#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/syno_gpio.h>
#include "../common/common.h"

extern SYNO_GPIO_INFO power_outage_gpio;
static int power_outage_gpio_pin = 7;

static DEFINE_MUTEX(power_outage_mutex);
static LIST_HEAD(power_outage_list);

struct power_outage_node {
	atomic_t			event;
	wait_queue_head_t	poll;
	struct list_head	list;
};

static int power_outage_open(struct inode *inode, struct file *file)
{
	int ret = -1;
	struct power_outage_node *pon = NULL;

	pon = kmalloc(sizeof(struct power_outage_node), GFP_KERNEL);
	if (!pon) {
		ret = -ENOMEM;
		goto END;
	}

	atomic_set(&pon->event, 0);
	init_waitqueue_head(&pon->poll);
	INIT_LIST_HEAD(&pon->list);

	mutex_lock(&power_outage_mutex);
	list_add(&pon->list, &power_outage_list);
	mutex_unlock(&power_outage_mutex);

	file->private_data = pon;
	ret = 0;
END:
	return ret;
}

static ssize_t power_outage_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char po_buf[2] = {0};
	int po_status = SYNO_GPIO_READ(power_outage_gpio_pin);
	ssize_t ret = 0;

	size = snprintf(po_buf, 2, "%d", po_status);
	if (copy_to_user(buf, po_buf, size)) {
		ret = -EFAULT;
		goto END;
	}

	ret = size;
END:
	return ret;
}

static unsigned int power_outage_poll(struct file *file, struct poll_table_struct *wait)
{
	struct power_outage_node *pon = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &pon->poll, wait);
	if (atomic_read(&pon->event)) {
		mask |= POLLIN | POLLRDNORM;
		atomic_set(&pon->event, 0);
	}

	return mask;
}

static int power_outage_release(struct inode *inode, struct file *file)
{
	struct power_outage_node *pon = file->private_data;

	mutex_lock(&power_outage_mutex);
	list_del(&pon->list);
	mutex_unlock(&power_outage_mutex);

	kfree(pon);
	return 0;
}

static const struct file_operations power_outage_fops = {
	.owner		= THIS_MODULE,
	.open		= power_outage_open,
	.read		= power_outage_read,
	.poll		= power_outage_poll,
	.release	= power_outage_release,
};

static void power_outage_notify(struct work_struct *work)
{
	struct list_head *p = NULL;
	struct power_outage_node *pon = NULL;

	mutex_lock(&power_outage_mutex);
	list_for_each(p, &power_outage_list) {
		pon = list_entry(p, struct power_outage_node, list);
		atomic_set(&pon->event, 1);
		wake_up_interruptible(&pon->poll);
	}
	mutex_unlock(&power_outage_mutex);
}
DECLARE_WORK(power_outage_work, power_outage_notify);

static irqreturn_t power_outage_handler(int irq, void *dev_id)
{
	schedule_work(&power_outage_work);

	return IRQ_HANDLED;
}

int syno_power_outage_init(void)
{
	struct proc_dir_entry *syno_power_outage_entry;

	power_outage_gpio_pin = power_outage_gpio.gpio_port[0];

	syno_power_outage_entry = proc_create_data("syno_power_outage", 0444, NULL,
						&power_outage_fops, NULL);
	if (!syno_power_outage_entry) {
		printk(KERN_ERR "synobios create syno_power_outage entry fail\n");
		return -EFAULT;
	}

	if (request_irq(syno_gpio_to_irq(power_outage_gpio_pin),
				power_outage_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"power_outage_handler", NULL)) {
		printk(KERN_ERR "synobios failed to request power outage IRQ\n");
	}

	printk(KERN_INFO "synobios syno_power_outage entry initialized\n");

	return 0;
}

void syno_power_outage_remove(void)
{
	free_irq(syno_gpio_to_irq(power_outage_gpio_pin), NULL);
	remove_proc_entry("syno_power_outage", NULL);
	return ;
}
