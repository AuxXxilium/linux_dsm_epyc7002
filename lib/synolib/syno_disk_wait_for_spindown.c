#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/device.h>

atomic_t syno_disk_paraldown_wait_cnt = ATOMIC_INIT(0);

void syno_disk_paraldown_wait_inc(void)
{
	atomic_inc(&syno_disk_paraldown_wait_cnt);
}
EXPORT_SYMBOL(syno_disk_paraldown_wait_inc);

void syno_disk_paraldown_wait_dec(void)
{
	/*
	 * the counter shouldn't be decd to a negative number, so we have to
	 * warn about someone calling this function while counter is zero and stop
	 * decing.
	 *
	 */
	WARN_ON_ONCE(!atomic_add_unless(&syno_disk_paraldown_wait_cnt, -1, 0));
}
EXPORT_SYMBOL(syno_disk_paraldown_wait_dec);

/*
 * Return 0 if any of disks aren't ready and timeout isn't over.
 * Otherwise return 1.
 */
int syno_disk_paraldown_ready_check(void)
{
	int ret = 0;

	if (0 == atomic_read(&syno_disk_paraldown_wait_cnt)) {
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL(syno_disk_paraldown_ready_check);

int syno_disk_paraldown_wait(struct device *dev)
{
	int ret = 0;
	int timeout = 60, i;

	dev_info(dev, "wait for disk spindown...\n");
	// TODO :   Counter syno_disk_paraldown_wait_cnt should be owned by
	//          every scsi_host themsevles instead of using global one,
	//          to prevent waiting wrong scsi_device's spindown
	//          which not belong to the scsi host.
	for (i = 0; i < timeout; i++) {
		if (syno_disk_paraldown_ready_check()) {
			dev_info(dev, "disk spindown complete\n");
			ret = 1;
			goto END;
		} else {
			msleep(1000);
		}
	}
	printk(KERN_ERR "Disk parallel spindown TIMEOUT!\n");

END:
	return ret;
}
EXPORT_SYMBOL(syno_disk_paraldown_wait);
