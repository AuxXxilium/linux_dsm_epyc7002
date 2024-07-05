#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include <linux/version.h>

MODULE_LICENSE("Dual BSD/GPL");

int flashcache_control_init(void)
{
	return 0;
}

void flashcache_control_cleanup(void)
{
}

// export symbol can be static
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
int syno_flashcache_proc_count = 0;
DEFINE_SEMAPHORE(syno_flashcache_proc_mutex);
#else
static int syno_flashcache_proc_count = 0;
static DEFINE_SEMAPHORE(syno_flashcache_proc_mutex);
#endif

EXPORT_SYMBOL(syno_flashcache_proc_count);
EXPORT_SYMBOL(syno_flashcache_proc_mutex);

module_init(flashcache_control_init);
module_exit(flashcache_control_cleanup);

