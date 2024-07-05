#include <linux/fs.h>
#include <linux/tty.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include "syno_ttyS.h"
#include <linux/syno.h>

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_DEBUG "synotty: " fmt "\n", ##args)
#else
#define DBG(fmt, args...) do { } while(0)
#endif /* #ifdef DEBUG */

typedef struct _tag_MICROPREADLIST {
	struct list_head readList;
	struct mutex readListLock;
	struct completion event;
	struct completion writing_event;
} MICROPREADLIST;

typedef struct syno_tty_buf {
	unsigned char data[TTY_BUF_SIZE];
	int widx;
	int ridx;
	spinlock_t lock;
	struct completion event;
} SYNOTTYBUF;

SYNOTTYBUF tty_buf;
SYNOTTYBUF current_tty_buf;

MICROPREADLIST micropReadList;

#define SYNO_TTYS_PATH "/dev/ttyS"
#define SYNO_UART_TTYS_INDEX "1"
/* default syno uart ttys path is /dev/ttyS1 */
#define SYNO_TTY_MAX_RETRY 5
#define SYNO_UART_TTYS_PATH SYNO_TTYS_PATH SYNO_UART_TTYS_INDEX
#define SYNO_TTY_COMPLETION_TIMEOUT (UP_DELAY*SYNO_TTY_MAX_RETRY*4)

struct file *tty_filp = NULL;
static int micropLogSwitch = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
int syno_tty_set_termios(struct tty_struct *tty, struct ktermios *new_termios)
{
	struct ktermios old_termios;
	int iRet = -1;

	if (NULL == tty || NULL == new_termios) {
		goto ERR;
	}

	down_write(&tty->termios_rwsem);
	old_termios = tty->termios;
	tty->termios = *new_termios;

	if (tty && tty->ops && tty->ops->set_termios) {
		tty->ops->set_termios(tty, &old_termios);
	}

	if (tty->ldisc && tty->ldisc->ops && tty->ldisc->ops->set_termios) {
		tty->ldisc->ops->set_termios(tty, &old_termios);
	}

	up_write(&tty->termios_rwsem);
	iRet = 0;
ERR:
	return iRet;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */

int syno_ttyS_set_termios(void)
{
	int ret = -1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct ktermios new_termios;
	struct tty_struct *tty = NULL;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	struct termios new_termios;
	mm_segment_t fs;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */

	if (!tty_filp) {
		printk(KERN_ERR "synobios need open %s before set termios\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	tty = ((struct tty_file_private *)tty_filp->private_data)->tty;
	memcpy(&new_termios, &tty->termios, sizeof(struct ktermios));
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	if (!tty_filp->f_op) {
		printk(KERN_ERR "synobios %s file->f_op struct error\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (!tty_filp->f_op->unlocked_ioctl) {
		printk(KERN_ERR "synobios %s file->f_op->unlocked_ioctl error\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}
	fs = get_fs();
	set_fs(KERNEL_DS);

	ret = tty_filp->f_op->unlocked_ioctl(tty_filp, TCGETS, (unsigned long)&new_termios);
	if (ret < 0) {
		printk(KERN_ERR "synobios ioctl TCGETS %s failed\n", SYNO_UART_TTYS_PATH);
		goto END;
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */

	/* syno microP termios setting */
	new_termios.c_cflag &= ~(CBAUD | CBAUDEX);
	new_termios.c_cflag |= B9600;
	new_termios.c_cflag |= (CLOCAL | CREAD);
	new_termios.c_cflag &= ~PARENB;
	new_termios.c_cflag &= ~CSTOPB;
	new_termios.c_cflag &= ~CSIZE;
	new_termios.c_cflag |= CS8;
	new_termios.c_cflag &= ~CRTSCTS;
	new_termios.c_lflag = 0;
	new_termios.c_oflag &= ~OPOST;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	ret = syno_tty_set_termios(tty, &new_termios);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	ret = tty_filp->f_op->unlocked_ioctl(tty_filp, TCSETS, (unsigned long)&new_termios);
	if (ret < 0) {
		printk(KERN_ERR "synobios ioctl TCSETS %s failed\n", SYNO_UART_TTYS_PATH);
		goto END;
	}
	ret = 0;
END:
	set_fs(fs);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
ERR:
	return ret;
}

int save_char_from_uart(unsigned char ch, struct tty_struct *tty)
{
	static int save_bytes = 0;
	static int state = 0;
	int save_char_flag = 0;

	if (0 != strcmp(tty->name, TTY_NAME)) {
		goto END;
	}

	switch (state) {
		case 0:
			if (ch == PREFIX_CH) {
				DBG("state 0 : get prefix");
				state = 1;
				save_char_flag = 1;
			} else {
				DBG("state 0 : not prefix %x (%c)", ch, ch);
			}
			break;
		case 1:
			save_bytes = ch - '0';
			state = 2;
			save_char_flag = 1;
			DBG("state 1 : save %d bytes (%x)", save_bytes, ch);
			break;
		case 2:
			if (save_bytes) {
				spin_lock(&tty_buf.lock);
				tty_buf.data[tty_buf.widx] = ch;
				DBG("state 2 : [%d] = 0x%x, get_ch = 0x%x", tty_buf.widx, tty_buf.data[tty_buf.widx], ch);
				tty_buf.widx = (tty_buf.widx+1) % TTY_BUF_SIZE;
				spin_unlock(&tty_buf.lock);
				save_bytes -= 1;
				save_char_flag = 1;
				if (!save_bytes)
					state = 0;
			}
			break;
		default:
			DBG("state %d : ERROR", state);
	}
END:
	return save_char_flag;
}
EXPORT_SYMBOL(save_char_from_uart);

int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty)
{
	static int state = 0;
	int save_char_flag = 0;

	if (0 != strcmp(tty->name, TTY_NAME)) {
		goto END;
	}

	switch (state) {
		case 0:
			if (ch == SZ_UART_CURRENT_PREFIX) {
				DBG("state 0 : get ADC current prefix");
				state = 1;
				save_char_flag = 1;
			} else {
				DBG("state 0 : not prefix %x (%c)", ch, ch);
			}
			break;
		case 1:
			if (SZ_UART_CURRENT_SUFFIX == ch) {
				state = 0;
				complete(&current_tty_buf.event);
			} else {
				spin_lock(&current_tty_buf.lock);
				/* To prevent uP '0' results in DSM poweroff, uP change 0 to * for current output.
				 * Therefore, we replace * with 0 to get reasonable current value.
				 */
				if (SZ_UART_CURRENT_ZERO_SUBSTITUE == ch) {
					current_tty_buf.data[current_tty_buf.widx] = SZ_UART_CURRENT_ZERO;
				} else {
					current_tty_buf.data[current_tty_buf.widx] = ch;
				}
				DBG("state 2 : [%d] = 0x%x, get_ch = 0x%x", current_tty_buf.widx, current_tty_buf.data[current_tty_buf.widx], ch);
				current_tty_buf.widx = (current_tty_buf.widx+1) % TTY_BUF_SIZE;
				spin_unlock(&current_tty_buf.lock);
			}
			save_char_flag = 1;
			break;
		default:
			DBG("state %d : ERROR", state);
	}
END:
	return save_char_flag;
}
EXPORT_SYMBOL(save_current_data_from_uart);

int syno_ttyS_open(void)
{
	int ret = -1;

	if (tty_filp) {
		printk(KERN_INFO "%s have been opened in synobios\n", SYNO_UART_TTYS_PATH);
		goto END;
	}

	mutex_init(&micropReadList.readListLock);
	INIT_LIST_HEAD(&micropReadList.readList);
	init_completion(&micropReadList.event);
	init_completion(&micropReadList.writing_event);
	complete(&micropReadList.writing_event);
	memset(&tty_buf, 0x0, sizeof(SYNOTTYBUF));
	spin_lock_init(&tty_buf.lock);
	memset(&current_tty_buf, 0x0, sizeof(SYNOTTYBUF));
	init_completion(&current_tty_buf.event);
	spin_lock_init(&current_tty_buf.lock);

	tty_filp = filp_open(SYNO_UART_TTYS_PATH, O_RDWR | O_NOCTTY | O_NONBLOCK , 0);
	if (IS_ERR(tty_filp)) {
		printk(KERN_ERR "synobios unable to open %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (syno_ttyS_set_termios()) {
		printk(KERN_ERR "synobios unable to set termios of %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	printk(KERN_INFO "synobios open %s success\n", SYNO_UART_TTYS_PATH);
END:
	ret = 0;
ERR:
	return ret;
}

/**
 * read data from microP device in synobios.
 * should open before read.
 * if read fail will return -1 else return data length.
 *
 * @param [OUT] szBuf Store read data
 * @param [IN]  size  Max size of szBuf
 * @param [IN]  micropReadWaitingNode: waiting node for queuing request of reading microp,
 *									   waiting node must be added to waiting queue before calling this function,
 *									   usually this is done with writing command to microp.
 *									   NULL for none waiting call
 * @return -1: Failed
 *       else: data length
 **/

static int syno_ttyS_read(char *szBuf, int size)
{
	int ret = -1, errorKernelRead = 0;
	int len = -1, recvLength = 0, i = 0;
	char *rgTmp = NULL;
	if (NULL == szBuf) {
		printk(KERN_ERR "synobios can't store to empty buffer from %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (1 >= size) {
		printk(KERN_ERR "synobios can't store to Null-terminated string with size <= 1\n");
		goto ERR;
	}
	if(0 != szBuf[0] && micropLogSwitch) {
		printk(KERN_INFO "%s: read microP command [%s] called by %s\n", __func__, szBuf, current->comm);
	}

	memset(szBuf, 0, size);
	size -= 1;

	rgTmp = kmalloc(size, GFP_KERNEL);
	if (!rgTmp) {
		goto ERR;
	}
	if (!tty_filp) {
		printk(KERN_ERR "synobios need open %s before read\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	// retry to fillup szBuf
	for (i = 0; i < SYNO_TTY_MAX_RETRY; i++) {
		memset(rgTmp, 0, size);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
		len = kernel_read(tty_filp, rgTmp, size - recvLength, &tty_filp->f_pos);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		len = kernel_read(tty_filp, tty_filp->f_pos, rgTmp, size - recvLength);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		if (0 > len) {
			errorKernelRead = len;
			msleep(UP_DELAY);
			continue;
		}
		memcpy(szBuf + recvLength, rgTmp, len);
		recvLength += len;
		if (recvLength >= size) {
			break;
		}
		msleep(UP_DELAY);
	}
	if (0 == recvLength && -EAGAIN != errorKernelRead) {
		goto ERR;
	}

	ret = recvLength;
ERR:
	if (rgTmp) {
		kfree(rgTmp);
	}
	return ret;
}

static int syno_ttyS_write(const char* szCmd)
{
	int ret = -1;
	int len = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	mm_segment_t fs;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0) */

	if (!tty_filp) {
		printk(KERN_ERR "synobios need open %s before write\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (!szCmd) {
		printk(KERN_ERR "synobios can't write empty command to %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}
	if(micropLogSwitch) {
		printk(KERN_INFO "%s: wirte microP command [%s] called by %s\n", __func__, szCmd, current->comm);
	}

	/* If all platform kernel version >= 3.10, can use kernel_write instead of vfs_write */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	len = kernel_write(tty_filp, szCmd, strlen(szCmd), &tty_filp->f_pos);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	fs = get_fs();
	set_fs(KERNEL_DS);
	len = vfs_write(tty_filp, (__force const char __user *)szCmd, strlen(szCmd), &tty_filp->f_pos);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	if (len < 0) {
		printk(KERN_ERR "synobios write %s to %s failed\n", szCmd, SYNO_UART_TTYS_PATH);
		goto END;
	}

	ret = len;
END:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	set_fs(fs);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0) */
ERR:
	return ret;
}

int syno_ttyS_write_with_length(const char* szCmd, const int cbCmd)
{
	int ret = -1;
	int len = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	mm_segment_t fs;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0) */

	if (!tty_filp) {
		printk(KERN_ERR "synobios need open %s before write\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (!szCmd || 0 == cbCmd) {
		printk(KERN_ERR "synobios can't write empty command to %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}
	if(micropLogSwitch) {
		printk(KERN_INFO "%s: write microP command [%s] called by %s\n", __func__, szCmd, current->comm);
	}

	/* If all platform kernel version >= 3.10, can use kernel_write instead of vfs_write */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	len = kernel_write(tty_filp, szCmd, cbCmd, &tty_filp->f_pos);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	fs = get_fs();
	set_fs(KERNEL_DS);
	len = vfs_write(tty_filp, (__force const char __user *)szCmd, cbCmd, &tty_filp->f_pos);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	if (len < 0) {
		printk(KERN_ERR "synobios write %s to %s failed\n", szCmd, SYNO_UART_TTYS_PATH);
		goto END;
	}

	ret = len;
END:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	set_fs(fs);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0) */
ERR:
	return ret;
}

int synobios_lock_ttyS_write(const char *szBuf)
{
	int ret = -1;
	mutex_lock(&micropReadList.readListLock);
	ret = syno_ttyS_write(szBuf);
	mutex_unlock(&micropReadList.readListLock);
	return ret;
}

int synobios_lock_ttyS_read(char *szBuf, int size)
{
	int ret = -1;
	LIST_HEAD(microp_reading_node);
	mutex_lock(&micropReadList.readListLock);
	if (!list_empty(&micropReadList.readList) || !completion_done(&(micropReadList.writing_event))) {
		ret = -EBUSY;
		mutex_unlock(&micropReadList.readListLock);
		goto END;
	}
	list_add_tail(&microp_reading_node, &micropReadList.readList);
	mutex_unlock(&micropReadList.readListLock);

	ret = syno_ttyS_read(szBuf, size);

	mutex_lock(&micropReadList.readListLock);
	list_del(&microp_reading_node);
	mutex_unlock(&micropReadList.readListLock);
	if (!completion_done(&(micropReadList.writing_event))) {
		complete(&micropReadList.writing_event);
	}
END:
	return ret;
}

int synobios_lock_ttyS_write_and_read(char *szBuf, int size)
{
	int ret = -1;
	LIST_HEAD(micropReadNode);

	mutex_lock(&micropReadList.readListLock);
	list_add_tail(&micropReadNode, &micropReadList.readList);
	mutex_unlock(&micropReadList.readListLock);

	if (0 == wait_for_completion_timeout(&micropReadList.writing_event, SYNO_TTY_COMPLETION_TIMEOUT) ||
	    0 > synobios_lock_ttyS_write(szBuf)) {
		ret = -EFAULT;
		goto END;
	}

	wait_for_completion_timeout(&micropReadList.event, UP_DELAY);
	ret = syno_ttyS_read(szBuf, size);

END:
	mutex_lock(&micropReadList.readListLock);
	list_del(&micropReadNode);
	mutex_unlock(&micropReadList.readListLock);
	if (!completion_done(&(micropReadList.writing_event))) {
		complete(&micropReadList.writing_event);
	}

	return ret;
}

int synobios_lock_ttyS_protection(char *szCommand, char *szBuf) {
	int ret = -1, len = 0, index = 0;
	if (NULL == szCommand || NULL == szBuf) {
		goto END;
	}


	mutex_lock(&micropReadList.readListLock);
	syno_ttyS_write_with_length(szCommand, TTY_PROTECT_CMD_SIZE);
	mdelay(UP_DELAY);

	while (tty_buf.ridx != tty_buf.widx) {
		spin_lock_irq(&tty_buf.lock);
		len += sprintf(szBuf+index*2, "%.2x", tty_buf.data[tty_buf.ridx]);
		tty_buf.ridx = (tty_buf.ridx+1) % TTY_BUF_SIZE;
		index++;
		spin_unlock_irq(&tty_buf.lock);
	}
	mutex_unlock(&micropReadList.readListLock);
	ret = len;
END:
	return ret;
}
EXPORT_SYMBOL(synobios_lock_ttyS_protection);

int synobios_lock_ttyS_current(char *szCommand, char *szBuf) {
	int ret = -1, len = 0, index = 0;
	LIST_HEAD(microp_waiting_read);

	if (NULL == szCommand || NULL == szBuf) {
		goto END;
	}

	mutex_lock(&micropReadList.readListLock);
	list_add_tail(&microp_waiting_read, &micropReadList.readList);
	mutex_unlock(&micropReadList.readListLock);

	if (0 == wait_for_completion_timeout(&micropReadList.writing_event, SYNO_TTY_COMPLETION_TIMEOUT) ||
	    0 > synobios_lock_ttyS_write(szCommand)) {
		ret = -EFAULT;
		goto END;
	}

	wait_for_completion_timeout(&current_tty_buf.event, UP_DELAY);

	spin_lock_irq(&current_tty_buf.lock);
	while (current_tty_buf.ridx != current_tty_buf.widx) {
		len += sprintf(szBuf+index, "%c", current_tty_buf.data[current_tty_buf.ridx]);
		current_tty_buf.ridx = (current_tty_buf.ridx+1) % TTY_BUF_SIZE;
		index++;
	}
	spin_unlock_irq(&current_tty_buf.lock);
	if (0 == len) {
		printk(KERN_ERR "synobios get empty ttyS current\n");
		goto END;
	}
	ret = len;
END:

	mutex_lock(&micropReadList.readListLock);
	list_del(&microp_waiting_read);
	mutex_unlock(&micropReadList.readListLock);
	if (!completion_done(&(micropReadList.writing_event))) {
		complete(&micropReadList.writing_event);
	}

	return ret;
}
EXPORT_SYMBOL(synobios_lock_ttyS_current);

void syno_ttyS_close(void)
{
	if (!tty_filp) {
		printk(KERN_INFO "%s wasn't opened in synobios", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	filp_close(tty_filp, NULL);
	tty_filp = NULL;
ERR:
	return;
}

void try_wakeup_waiting_microp(void) {
	if (!completion_done(&(micropReadList.event))) {
		complete(&micropReadList.event);
	}
}
EXPORT_SYMBOL(try_wakeup_waiting_microp);

void set_log_switch(int logSwitch){
	micropLogSwitch = logSwitch;
}
