#include <asm/types.h>
#include "i2c-plx.h"
#include "../rtc/rtc.h"
#include <linux/i2c.h>

int plxI2CCharWrite(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap = NULL;
	struct i2c_msg msg = {0};
	int ret = -1;
	int buf_len = 0;
	u8 *pDataBuf = NULL;

	pAdap = i2c_get_adapter(0);
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

int plxI2CCharRead(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap;
	struct i2c_msg msg;
	int ret = -1;
	u8 offset_u8 = offset;
	char *pDataBuf = NULL;
	int real_length;

	pAdap = i2c_get_adapter(0);
	if (!pAdap) {
		printk("Cannot get i2c adapter\n");
		goto END;
	}

	if (-1 == offset) {
		offset_u8 = 0;
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
