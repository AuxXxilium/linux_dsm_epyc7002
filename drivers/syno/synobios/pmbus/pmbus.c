// Copyright (c) 2000-2020 Synology Inc. All rights reserved.

#ifdef CONFIG_SYNO_HWMON_PMBUS
#include <linux/syno_fdt.h>
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#include <linux/synolib.h>
#include "pmbus.h"
#include "../i2c/i2c-linux.h"

int HWMONReadPmbusStatusReg(unsigned int adapter, unsigned int addr, unsigned int reg, char *SensorValue, int cValueSize)
{
	int iRet = -1;
	int err = -1;
	u16 data = 0xffff;
	static u16 uLostAddr = 0;

	err = linuxI2CSmbusRegRead(adapter, addr, reg, &data);
	if (0 != err) {
		if (uLostAddr != addr) {
			uLostAddr = addr;
			printk("%s:%d return error!\n", __FILE__, __LINE__);
		}
		// i2c will read failed when 800W power is not inserted
		// report bad power status with success return to avoid repeated log
		iRet = 0;
		goto END;
	}

	if (uLostAddr == addr) {
		uLostAddr = 0;
	}

	//Pmbus use linear data format
	// X = Y x 2^N
	//– X is the real world value
	//– Y = data[10:0], The mantissa is the variable components that changes as the sensor value changes.
	//      Y is a signed 11 bit 2’s complement integer
	//– N = data[15:11], exponent is a signed 5 bit 2’s complement integer
	//      The exponents are fixed for each power supply and define the resolution for each sensor.
	snprintf(SensorValue, cValueSize, "%04x", data);

	iRet = 0;

END:
	return iRet;
}

#ifdef CONFIG_SYNO_HWMON_PMBUS
int I2CPmbusReadPowerStatus(int index, SYNO_POWER_STATUS* status)
{
	int iRet = -1;
	int err = -1;
	u16 data = 0xffff;
	static u16 uLostAddr = 0;
	unsigned int uiPmbusAdapter = 0;
	unsigned int uiPmbusAddress = 0;
	unsigned int uiPmbusStatusReg = 0;
	unsigned int uiPmbusPSUOffBit = 0;
	unsigned int uiPmbusPSUPresentBit = 0;

	if (NULL == status) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto END;
	}

	// get pmbus properties from fdt
	if (0 > syno_pmbus_property_get(&uiPmbusAdapter, DT_SYNO_PMBUS_ADAPTER, index)) {
		printk("Failed to get PMBus Adapter %d from fdt\n", index);
		goto END;
	}
	if (0 > syno_pmbus_property_get(&uiPmbusAddress, DT_SYNO_PMBUS_ADDRESS, index)) {
		printk("Failed to get PMBus Address %d from fdt\n", index);
		goto END;
	}
	if (0 > syno_pmbus_property_get(&uiPmbusStatusReg, DT_SYNO_PMBUS_STATUS_REG, index)) {
		printk("Failed to get PMBus Status Register %d from fdt\n", index);
		goto END;
	}
	if (0 > syno_pmbus_property_get(&uiPmbusPSUOffBit, DT_SYNO_PMBUS_PSU_OFF_BIT, index)) {
		printk("Failed to get PMBus Power Good bit %d from fdt\n", index);
		goto END;
	}
	if (0 > syno_pmbus_property_get(&uiPmbusPSUPresentBit, DT_SYNO_PMBUS_PSU_PRESENT_BIT, index)) {
		printk("Failed to get PMBus Power Present bit %d from fdt\n", index);
		goto END;
	}

	err = linuxI2CSmbusRegRead(uiPmbusAdapter, uiPmbusAddress, uiPmbusStatusReg, &data);
	if (0 != err) {
		if (uLostAddr != uiPmbusAddress) {
			uLostAddr = uiPmbusAddress;
			printk("%s:%d return error!\n", __FILE__, __LINE__);
		}
		// i2c will read failed when 800W power is not inserted
		// report bad power status with success return to avoid repeated log
		*status = POWER_STATUS_BAD;
		iRet = 0;
		goto END;
	}
	if (uLostAddr == uiPmbusAddress) {
		uLostAddr = 0;
	}

	if ((data & uiPmbusPSUOffBit) || (uiPmbusPSUPresentBit && !(data & uiPmbusPSUPresentBit))) {
		*status = POWER_STATUS_BAD;
	} else {
		*status = POWER_STATUS_GOOD;
	}

	iRet = 0;

END:
	return iRet;
}

int HWMONGetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
{
	int iRet = -1;
	int i = 0, j = 0;
	unsigned int uiPmbusAdapter = UINT_MAX;
	unsigned int uiPmbusAddress = UINT_MAX;
	unsigned int uiPmbusRegister = UINT_MAX;
	SYNO_POWER_STATUS PowerStatus = POWER_STATUS_BAD;
	char szPsuType[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {0};

	if (NULL == psu_status || 0 >= iPsuNumber) {
		goto END;
	}

	for (i = 0; i < iPsuNumber; i++) {
		// get pmbus properties from fdt
		if (0 > syno_pmbus_property_get(&uiPmbusAdapter, DT_SYNO_PMBUS_ADAPTER, i) || UINT_MAX == uiPmbusAdapter) {
			printk("Failed to get PMBus Adapter %d from fdt\n", i);
			goto END;
		}
		if (0 > syno_pmbus_property_get(&uiPmbusAddress, DT_SYNO_PMBUS_ADDRESS, i) || UINT_MAX == uiPmbusAddress) {
			printk("Failed to get PMBus Address %d from fdt\n", i);
			goto END;
		}
		for (j = 0; j < psu_status[i].sensor_num; j++) {
			if (0 == strcmp(HWMON_PSU_SENSOR_STATUS, psu_status[i].sensor[j].sensor_name)) {
				if (0 > I2CPmbusReadPowerStatus(i, &PowerStatus)) {
					printk("Failed to get PMBus %d power status\n", i);
					continue;
				}
				snprintf(psu_status[i].sensor[j].value, MAX_SENSOR_VALUE, "%s", (POWER_STATUS_BAD == PowerStatus)?"psu off":"psu good");
			} else {
				if (0 == strcmp(HWMON_PSU_SENSOR_PIN, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_PIN_REG);
				} else if (0 == strcmp(HWMON_PSU_SENSOR_POUT, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_POUT_REG);
				} else if (0 == strcmp(HWMON_PSU_SENSOR_TEMP1, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_TEMP1_REG);
				} else if (0 == strcmp(HWMON_PSU_SENSOR_TEMP2, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_TEMP2_REG);
				} else if (0 == strcmp(HWMON_PSU_SENSOR_TEMP3, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_TEMP3_REG);
				} else if (0 == strcmp(HWMON_PSU_SENSOR_FAN, psu_status[i].sensor[j].sensor_name) ||
						0 == strcmp(HWMON_PSU_SENSOR_FAN_VOLT, psu_status[i].sensor[j].sensor_name)) {
					snprintf(szPsuType, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", DT_SYNO_PMBUS_FAN_REG);
				} else {
					printk("Failed to match PMBus %d sensor name : %s\n", i, psu_status[i].sensor[j].sensor_name);
					continue;
				}
				if (0 > syno_pmbus_property_get(&uiPmbusRegister, szPsuType, i) || UINT_MAX == uiPmbusRegister) {
					printk("Failed to get PMBus %d Register %s from fdt\n", i, szPsuType);
					continue;
				}
				if (0 > HWMONReadPmbusStatusReg(uiPmbusAdapter, uiPmbusAddress, uiPmbusRegister, psu_status[i].sensor[j].value, MAX_SENSOR_VALUE)) {
					printk("Failed to read PMBus Status Register 0x%x\n", uiPmbusRegister);
					continue;
				}
			}
		}
	}

	iRet = 0;
END:
	return iRet;
}
#endif /* CONFIG_SYNO_HWMON_PMBUS */
