#include <linux/init.h>
#include <linux/tty.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/syno_gpio.h>
#include <linux/synolib.h>

#define SZ_DTS_PROPERTY_LOG_OUT_GPIO  "syno_uart_logout_gpio"
#define SYNO_UART2SPI_LOGOUT_TTY_PATH "/dev/ttyS2"


struct file *tty_filp = NULL;
static struct kobject *SynoUart2SpiLogoutObject = NULL;

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

static int syno_ttyS_set_termios(void)
{
	int ret = -1;
	struct ktermios new_termios;
	struct tty_struct *tty = NULL;

	if (!tty_filp) {
		printk(KERN_ERR "need open %s before set termios\n", SYNO_UART2SPI_LOGOUT_TTY_PATH);
		goto ERR;
	}

	tty = ((struct tty_file_private *)tty_filp->private_data)->tty;
	memcpy(&new_termios, &tty->termios, sizeof(struct ktermios));

	/* syno microP termios setting */
	new_termios.c_cflag &= ~(CBAUD | CBAUDEX);
	new_termios.c_cflag |= B115200;
	new_termios.c_cflag |= (CLOCAL | CREAD);
	new_termios.c_cflag &= ~PARENB;
	new_termios.c_cflag &= ~CSTOPB;
	new_termios.c_cflag &= ~CSIZE;
	new_termios.c_cflag |= CS8;
	new_termios.c_cflag &= ~CRTSCTS;
	new_termios.c_lflag = 0;
	new_termios.c_iflag = 0;


	ret = syno_tty_set_termios(tty, &new_termios);

ERR:
	return ret;
}

static int syno_logout_gpio_pin_get(unsigned int *pin, unsigned int *polarity)
{
    int iRet = -1;
    
    if (0 != of_property_read_u32_index(of_root, SZ_DTS_PROPERTY_LOG_OUT_GPIO, SYNO_GPIO_PIN, pin)) {
		printk(KERN_WARNING "Failed to read syno_uart_logout_gpio pin.\n");
		goto END;
	}
    if (0 != of_property_read_u32_index(of_root, SZ_DTS_PROPERTY_LOG_OUT_GPIO, SYNO_POLARITY_PIN, polarity)) {
		printk(KERN_WARNING "Failed to read syno_uart_logout_gpio polarity.\n");
		goto END;
	}
    iRet = 0;
END:
    return iRet;
}

static ssize_t syno_logout_gpio_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int iRet = 0;
	iRet += scnprintf(buf, PAGE_SIZE,"0: output low (default)\n1: output high\n2: input\n");

	return iRet;
}

static ssize_t syno_logout_gpio_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    unsigned int uiGpioValue = 0;

    unsigned int uiGpioPin = 0;
    unsigned int uiGpioPolarity = 0;

	if (0 > kstrtouint(buf, 10, &uiGpioValue)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

    if (0 > uiGpioValue || 2 < uiGpioValue) {
		printk(KERN_WARNING "Invalid Input\n");
		goto END;
	}

    if (0 > syno_logout_gpio_pin_get(&uiGpioPin, &uiGpioPolarity)) {
        goto END;
    }


    if (2 == uiGpioValue) {
        syno_gpio_direction_input(uiGpioPin);
    } else if (1 == uiGpioValue) {
		syno_gpio_direction_output(uiGpioPin, uiGpioPolarity);
    } else if (0 == uiGpioValue) {
		syno_gpio_direction_output(uiGpioPin, (uiGpioPolarity? 0 : 1));
	}

END:
	return count;
}


// register function to attribute
static struct kobj_attribute syno_logout_gpio = __ATTR( syno_logout_gpio, 0640, syno_logout_gpio_show, syno_logout_gpio_store);

// put attribute to attribute group
static struct attribute *SynoUart2SpiLogoutAttr[] = {
	&syno_logout_gpio.attr,
	NULL,   /* NULL terminate the list*/
};

static struct attribute_group SynoUart2SpiLogoutGroup = {
	.attrs = SynoUart2SpiLogoutAttr
};


static int syno_uart2spi_logout_init(void)
{
	int iRet = -1;
	SynoUart2SpiLogoutObject = kobject_create_and_add("syno_uart2spi_logout", kernel_kobj);
	if (!SynoUart2SpiLogoutObject) {
		iRet = -ENOMEM;
		goto END;
	}

	//create attributes (files)
	if(sysfs_create_group(SynoUart2SpiLogoutObject, &SynoUart2SpiLogoutGroup)){
		iRet = -ENOMEM;
		goto END;
	}

	// initialize /dev/ttyS2
	tty_filp = filp_open(SYNO_UART2SPI_LOGOUT_TTY_PATH, O_RDWR | O_NOCTTY, 0);
	if (IS_ERR(tty_filp)) {
		printk(KERN_ERR "unable to open %s\n", SYNO_UART2SPI_LOGOUT_TTY_PATH);
		goto END;
	}

	if (syno_ttyS_set_termios()) {
		printk(KERN_ERR "unable to set termios of %s\n", SYNO_UART2SPI_LOGOUT_TTY_PATH);
		goto END;
	}

	iRet = 0;
END:
    if (tty_filp) {
        filp_close(tty_filp, NULL);
        tty_filp = NULL;       
    }
	if (0 != iRet) {
		if (SynoUart2SpiLogoutObject) {
			kobject_put(SynoUart2SpiLogoutObject);
		}
	}
	return iRet;
}

static void syno_uart2spi_logout_exit(void)
{
	kobject_put(SynoUart2SpiLogoutObject);
}

MODULE_LICENSE("GPL");
module_init(syno_uart2spi_logout_init);
module_exit(syno_uart2spi_logout_exit);
