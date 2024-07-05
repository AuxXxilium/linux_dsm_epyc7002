#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "synobios.h"
#include "../i2c/i2c-linux.h"
#include "rtc.h"
#include "../mapping.h"
#include <linux/i2c.h>

#if defined(CONFIG_SYNO_RTD1619B)
#define I2C_BUS_NO 1
#else
#define I2C_BUS_NO 0
#endif

static int pericomCharReadByI2c(struct i2c_adapter *pAdap, int target, u8 *data, int length, int offset)
{
	unsigned char *msgbuf0 = NULL;
	unsigned char *msgbuf1 = NULL;
	int ret = -1;
	struct i2c_msg msg[2] = {
		{
			.addr = target,
			.flags = 0,
			.len = 1,
		}, {
			.addr = target,
			.flags = I2C_M_RD,
			.len = length,
		},
	};

	if (NULL ==pAdap) {
                printk("Adpater NULL\n");
		goto END;
	}

	if (NULL == (msgbuf0 = kzalloc(1, GFP_KERNEL))){
                printk("error malloc\n");
                goto END;
        }

	msg[0].buf = msgbuf0;
	msgbuf0[0] = offset;

	if (NULL == (msgbuf1 = kzalloc(length, GFP_KERNEL))){
                printk("error malloc\n");
                goto END;
        }
	
	msg[1].buf = msgbuf1;

	if (2 != i2c_transfer(pAdap, msg, 2))
		goto END;

	memcpy(data, msg[1].buf, length);

	ret = 0;
END:
	return ret;
}

static int pericomCharReadBySmbus(u8 *data, int length, int offset)
{
	int iRet = -1;
	struct device_node *np = of_find_compatible_node(NULL, NULL, "pericom,pt7c4337");
	struct i2c_client *client = NULL;

	if (np == NULL) {
		printk("find pericom rtc failed\n");
		goto END;
	}

	client = of_find_i2c_device_by_node(np);

	if (NULL == client) {
		printk("get i2c client failed\n");
		goto END;
	}

	if (0 >= i2c_smbus_read_i2c_block_data(client, offset, length, data)) {
		printk("i2c_smbus_read_i2c_block_data failed\n");
		goto END;
	}
	iRet = 0;

END:
	of_node_put(np);
	return iRet;
}

static int pericomI2CCharRead(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap = i2c_get_adapter(I2C_BUS_NO);
	int iRet = -1;

	if (!pAdap) {
		printk("Cannot get i2c adapter\n");
		goto END;
	}
	if (pAdap->algo->master_xfer) {  // i2c protocol
		if (0 > pericomCharReadByI2c(pAdap, target, data, length, offset)) {
			goto END;
		}
	} else if (pAdap->algo->smbus_xfer) { // SMBus protocol
		if (0 > pericomCharReadBySmbus(data, length, offset)) {
			goto END;
		}
	} else { // No protocol is specified
		printk("No usable i2c protocol\n");
		goto END;
	}
	iRet = 0;

END:
	if (pAdap)
		i2c_put_adapter(pAdap);
	return iRet;
}

static int pericomCharWriteByI2c(struct i2c_adapter *pAdap, int target, u8 *data, int length, int offset)
{
	struct i2c_msg msg = {0};
	int ret = -1;
	int buf_len = 0;
	u8 *pDataBuf = NULL;

	if (NULL ==pAdap) {
                printk("Adpater NULL\n");
		goto END;
	}

	buf_len = length + 1;

	if (NULL == (pDataBuf = kzalloc(buf_len, GFP_KERNEL))){
		printk("error malloc\n");
		goto END;
	}

	msg.addr = target;
	msg.flags = 0;
	msg.len = buf_len;
	msg.buf = pDataBuf;

	pDataBuf[0] = offset;

	memcpy(pDataBuf + 1, data, length);

	if (1 != i2c_transfer(pAdap, &msg, 1)) {
		goto END;
	}

	ret = 0;
END:
	if (pDataBuf)
		kfree(pDataBuf);

	return ret;

}

static int pericomCharWriteBySmbus(u8 *data, int length, int offset)
{
	int iRet = -1;
	struct device_node *np = of_find_compatible_node(NULL, NULL, "pericom,pt7c4337");
	struct i2c_client *client = NULL;

	if (np == NULL) {
		printk("find pericom rtc failed\n");
		goto END;
	}

	if (NULL == (client = of_find_i2c_device_by_node(np))) {
		printk("get i2c client failed\n");
		goto END;
	}

	if (0 > i2c_smbus_write_i2c_block_data(client, offset, length, data)) {
		printk("i2c_smbus_read_i2c_block_data failed\n");
		goto END;
	}
	iRet = 0;

END:
	of_node_put(np);
	return iRet;
}

static int pericomI2CCharWrite(int target, u8 *data, int length, int offset)
{
	struct i2c_adapter *pAdap = i2c_get_adapter(I2C_BUS_NO);
	int iRet = -1;

	if (!pAdap) {
		printk("Cannot get i2c adapter\n");
		goto END;
	}
	if (pAdap->algo->master_xfer) {  // i2c protocol
		if (0 > pericomCharWriteByI2c(pAdap, target, data, length, offset)) {
			goto END;
		}
	} else if (pAdap->algo->smbus_xfer) { // SMBus protocol
		if (0 > pericomCharWriteBySmbus(data, length, offset)) {
			goto END;
		}
	} else { // No protocol is specified
		printk("No usable i2c protocol\n");
		goto END;
	}
	iRet = 0;

END:
	if (pAdap)
		i2c_put_adapter(pAdap);
	return iRet;
}


int rtc_pericom_rotate_auto_poweron(SYNO_AUTO_POWERON* pAutoPowerOn, const SYNORTCTIMEPKT *pRtcTime)
{
	int ret = -1;
	u8 csr = 0;
	u8 str = 0;
	unsigned char nextWday = 0;
	SYNORTCALARMPKT alarmTime = {0};

	if( NULL == pAutoPowerOn || NULL == pRtcTime ) {
		goto End;
	}

	if ( 0 > (ret = pericomI2CCharRead(PERICOM_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), PERICOM_RTC_CONTROL_OFFSET)) ) {
		goto End;
	}

	if ( 0 > (ret = pericomI2CCharRead(PERICOM_RTC_ADDR, (u8 *)&str, sizeof(str)/sizeof(u8), PERICOM_RTC_STATUS_OFFSET)) ) {
		goto End;
	}

	// clear alarm interrupt bit
	csr &= ~(PERICOM_RTC_A1IE_BIT | PERICOM_RTC_A2IE_BIT | PERICOM_RTC_INTCN_BIT);

	// clear status bit
	str = 0;

	rtc_get_alarm_time(&alarmTime, pRtcTime);

	nextWday = rtc_get_next_weekday(alarmTime.weekdays, pRtcTime->weekday);
	if (rtc_later_equal_than_int(pRtcTime->hour, pRtcTime->min, alarmTime.hour, alarmTime.min)) {
		nextWday = rtc_get_next_weekday(alarmTime.weekdays, (pRtcTime->weekday+1)%7);
	}

	alarmTime.hour = Hour_to_Pericom(alarmTime.hour);
	BIN_TO_BCD(alarmTime.hour);
	BIN_TO_BCD(alarmTime.min);

	if ( SYNO_AUTO_POWERON_ENABLE == pAutoPowerOn->enabled ) {

		// enable alarm interrupt
		csr |= PERICOM_RTC_A2IE_BIT;

		if( 0x7F == alarmTime.weekdays) {
			alarmTime.weekdays |= PERICOM_RTC_DAY_MASK_BIT;
		} else {
			// pericom use 01 to 07 to present weekdays
			if (nextWday == 0) {
				nextWday = 7;
			}
			alarmTime.weekdays = nextWday | PERICOM_RTC_DAY_DATE_SWITCH;  // BIT6 for date / day
		}
	}

	// set alarm data
	if( 0 > (ret = pericomI2CCharWrite(PERICOM_RTC_ADDR, (u8 *)&alarmTime,
					sizeof(SYNORTCALARMPKT)/sizeof(u8), PERICOM_RTC_ALARM2_OFFSET)) ) {
		goto End;
	}

	// set alarm interrupt
	if ( 0 > (ret = pericomI2CCharWrite(PERICOM_RTC_ADDR, (u8 *)&csr, sizeof(csr)/sizeof(u8), PERICOM_RTC_CONTROL_OFFSET)) ) {
		goto End;
	}

	// clear status
	if ( 0 > (ret = pericomI2CCharWrite(PERICOM_RTC_ADDR, (u8 *)&str, sizeof(str)/sizeof(u8), PERICOM_RTC_STATUS_OFFSET)) ) {
		goto End;
	}

	ret = 0;
End:
	return ret;

}

int rtc_pericom_get_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	int ret = 0;
	unsigned char rgRtcTimeTemp[7] = {0};
	unsigned char hrs = 0;
	SYNO_AUTO_POWERON schedule;

	if( 0 != (ret = pericomI2CCharRead(PERICOM_RTC_ADDR, (u8 *)rgRtcTimeTemp, 7, 0))) {
		goto END;
	}

	pRtcTimePkt->sec        = BCD2BIN(rgRtcTimeTemp[0]);
	pRtcTimePkt->min        = BCD2BIN(rgRtcTimeTemp[1]);
	hrs = BCD2BIN(rgRtcTimeTemp[2]);
	pRtcTimePkt->hour = Pericom_to_Hour(hrs);
	pRtcTimePkt->weekday    = BCD2BIN(rgRtcTimeTemp[3]);
	pRtcTimePkt->day        = BCD2BIN(rgRtcTimeTemp[4]);
	pRtcTimePkt->month      = BCD2BIN(rgRtcTimeTemp[5]);
	pRtcTimePkt->year       = BCD2BIN(rgRtcTimeTemp[6]);

	pRtcTimePkt->year += 100;
	pRtcTimePkt->month -= 1;

	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_pericom_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

END:
	return ret;
}


int rtc_pericom_set_time(struct _SynoRtcTimePkt* pRtcTimePkt)
{
	int ret = 0;
	unsigned char year = 0, mon = 0, hrs = 0;
	unsigned char rgRtcTimeTemp[7] = {0};
	SYNO_AUTO_POWERON schedule;

	year = ((pRtcTimePkt->year + 1900 > 2000) ? (pRtcTimePkt->year - 100) : 0);
	rgRtcTimeTemp[6] = BIN2BCD(year);
	mon = pRtcTimePkt->month + 1;
	rgRtcTimeTemp[5] = BIN2BCD(mon);
	rgRtcTimeTemp[4] = BIN2BCD(pRtcTimePkt->day);
	// pericom use 01 to 07 to present weekdays
	if (pRtcTimePkt->weekday == 0) {
		pRtcTimePkt->weekday = 7;
	}
	rgRtcTimeTemp[3] = BIN2BCD(pRtcTimePkt->weekday);
	hrs = Hour_to_Pericom(pRtcTimePkt->hour);
	rgRtcTimeTemp[2] = BIN2BCD(hrs);
	rgRtcTimeTemp[1] = BIN2BCD(pRtcTimePkt->min);
	rgRtcTimeTemp[0] = BIN2BCD(pRtcTimePkt->sec);

	if ( 0 != (ret = pericomI2CCharWrite(PERICOM_RTC_ADDR, (u8 *)rgRtcTimeTemp, 7, 0)) ) {
		ret = -1;
	}

	if( 0 == rtc_get_auto_poweron(&schedule) ) {
		rtc_pericom_rotate_auto_poweron(&schedule, pRtcTimePkt);
	}

	return ret;

}


int rtc_pericom_auto_poweron_init(void)
{
	return 0;
}
