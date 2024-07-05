#include <asm/types.h>
#include "i2c-linux.h"

#ifndef CONFIG_SYNO_HI3535_VS
#include "../rtc/rtc.h"
#endif

#include <linux/slab.h>
#include <linux/i2c.h>
#if defined(CONFIG_SYNO_TI_816X) || defined(CONFIG_SYNO_RTD1619) || defined(CONFIG_SYNO_RTD1619B)|| defined(CONFIG_SYNO_V1000) || defined(CONFIG_SYNO_V1000SOFS) || defined(CONFIG_SYNO_R1000)
#define I2C_BUS_NO 1
#elif defined(CONFIG_ARCH_GEN3)
#define I2C_BUS_NO 2
#elif defined(CONFIG_SYNO_EPYC7002) || defined(CONFIG_SYNO_EPYC7002SOFS)
#define I2C_BUS_NO 3
#else 
#define I2C_BUS_NO 0
#endif

static int i2c_bus_no = I2C_BUS_NO;

void I2cBusChange(int new_bus)
{
	i2c_bus_no = new_bus;
}

int linuxI2CCharWrite(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap = NULL;
	struct i2c_msg msg = {0};
	int ret = -1;
	int buf_len = 0;
	u8 *pDataBuf = NULL;

	pAdap = i2c_get_adapter(i2c_bus_no);
	if (!pAdap) {
		printk("Cannot get i2c adapter\n");
		goto END;
	}

	if (-1 == offset) {
		buf_len = length;
	} else {
		buf_len = length + 1;
	}

	if (NULL == (pDataBuf = kzalloc(buf_len, GFP_KERNEL))){
		printk("error malloc\n");
		goto END;
	}

	msg.addr = target;
	msg.flags = 0;
	msg.len = buf_len;
	msg.buf = pDataBuf;

	if (-1 == offset) {
		memcpy(pDataBuf, data, length);
	} else {
		pDataBuf[0] = offset << 4;
		memcpy(pDataBuf + 1, data, length);
	}

	if (1 != i2c_transfer(pAdap, &msg, 1)) {
		goto END;
	}

	ret = 0;
END:
	if (pAdap)
		i2c_put_adapter(pAdap);

	if (pDataBuf)
	    kfree(pDataBuf);

	return ret;
}

int linuxI2CCharRead(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap;
	struct i2c_msg msg;
	int ret = -1;
	char *pDataBuf = NULL;
	int real_length;

	pAdap = i2c_get_adapter(i2c_bus_no);
	if (!pAdap) {
		printk("Cannot get i2c adapter\n");
		goto END;
	}

	if (-1 == offset) {
		real_length = length;
	} else {
		real_length = 17;
	}

	if (NULL == (pDataBuf = kzalloc(real_length, GFP_KERNEL))){
		printk("error malloc\n");
		goto END;
	}

	msg.addr = target;
	msg.flags = I2C_M_RD;
	msg.buf = pDataBuf;
	msg.len = real_length;

	if (1 != i2c_transfer(pAdap, &msg, 1))
		goto END;

	if (-1 != offset) {
		memcpy(data, pDataBuf + 1 + offset, length);
	} else {
		memcpy(data, pDataBuf, length);
	}

	ret = 0;
END:
	if (pAdap)
		i2c_put_adapter(pAdap);

	if (pDataBuf)
		kfree(pDataBuf);

	return ret;
}

int linuxI2CSmbusRegRead(int bus_no, u16 addr, u8 reg, u16 *data)
{
	int ret = -1;
	int err = -1;
	struct i2c_adapter *pAdap = NULL;
	u16 flags = 0;
	union i2c_smbus_data smbus_data;
	static u16 uLostAddr = 0;

	if (NULL == data) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto END;
	}

	pAdap = i2c_get_adapter(bus_no);
	if (!pAdap) {
		printk("Cannot get i2c adapter!\n");
		goto END;
	}

	err = i2c_smbus_xfer(pAdap, addr, flags, I2C_SMBUS_READ, reg, I2C_SMBUS_WORD_DATA, &smbus_data);
	if (0 > err) {
		if (uLostAddr != addr) {
			uLostAddr = addr;
			printk("i2c_smbus_xfer return error!\n");
		}
		goto END;
	}
	if (uLostAddr == addr) {
		uLostAddr = 0;
	}

	*data = smbus_data.word;

	ret = 0;
END:
	if (pAdap)
		i2c_put_adapter(pAdap);

	return ret;
}
