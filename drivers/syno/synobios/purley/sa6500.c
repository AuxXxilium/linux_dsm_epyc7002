// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include "synobios.h"
#include "purley_common.h"
#include "../i2c/i2c-linux.h"

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x58
#define PSU_BTM_I2C_ADDR 	0x59
// there two types of interface in SA6500 (U.2 NVMe, SAS/SATA)
#define SA6500_INTF_NUM			2
#define SA6500_U2_BOARD_UP_BUS		0
#define SA6500_U2_BOARD_UP_I2C_ADDR	0x47
#define INTF_ERR			1

// extern function from purley_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

/* FIXME: should not directly copy following function
 * Modified from drivers/i2c/muxes/i2c-mux-pca954x.c */
int SA6500SMBusSwitchRegWrite(int bus_no, u16 addr, u8 val)
{
        int ret = -1;
        struct i2c_adapter *adap = NULL;

        adap = i2c_get_adapter(bus_no);
        if (!adap) {
                printk("Cannot get i2c adapter!\n");
                goto END;
        }

        if (adap->algo->master_xfer) {
                struct i2c_msg msg;
                char buf[1];

                msg.addr = addr;
                msg.flags = 0;
                msg.len = 1;
                buf[0] = val;
                msg.buf = buf;
                ret = __i2c_transfer(adap, &msg, 1);
        } else {
                union i2c_smbus_data data;
                ret = adap->algo->smbus_xfer(adap, addr,
                                0, I2C_SMBUS_WRITE,
                                val, I2C_SMBUS_BYTE, &data);
        }
END:
        return ret;
}

void SA6500SMBusSwitchInit(void) {
	SA6500SMBusSwitchRegWrite(0, 0x71, 4);
	SA6500SMBusSwitchRegWrite(0, 0x72, 1);
}

static
int SA6500I2CGetPowerInfo(POWER_INFO *power_info)
{
	int ret = -1;
	int err = -1;

	if (NULL == power_info) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, &(power_info->power_1));
	if (0 != err) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_BTM_I2C_ADDR, &(power_info->power_2));
	if (0 != err) {
		goto FAIL;
	}

	ret = 0;

FAIL:
	return ret;
}

int SA6500InitModuleType(struct synobios_ops *ops)
{
    module_t type_sa6500 = MODULE_T_SA6500;
    module_t *pType = &type_sa6500;

    module_type_set(pType);
    return 0;
}

typedef enum {
	SYNO_U2_NVME,
	SYNO_SAS_SATA,
	SYNO_NO_DISK,
} SYNO_DISK_INTF_ENUM;

/* DevPresent: 0 disk present, 1 no disk present
 * IntfDetect: 0 SAS/SATA, 1, U.2 NVMe */
static int ToDiskIntf(u8 InfDet, u8 DevPre)
{
	int ret = SYNO_NO_DISK;

	if (DevPre) {
		goto END;
	}

	ret = (InfDet) ? SYNO_U2_NVME : SYNO_SAS_SATA;
END:
	return ret;
}
static char* ToDiskIntfStr[3] = {"U.2 NVMe", "SAS/SATA", "nodisk"};

#if defined(CONFIG_SYNO_LP3943_FEATURES) || defined(CONFIG_SYNO_LEDS_LP3943_FEATURES)
extern void SYNOLP3943I2CMutex (bool lock);
#endif /* CONFIG_SYNO_LP3943_FEATURES || CONFIG_SYNO_LEDS_LP3943_FEATURES */

static int SA6500GetDiskStatus(u8* IntfDetect, u8* DevPresent, u8* DevEnabled)
{
	int ret = -1;
	u16 data = 0xffff;

	/* According to HW spec
	 * 0x0A, 0x0B, 0x0C is port 8-1, 16-9, 24-17 interface detect
	 * 0x0D, 0x0E, 0x0F is port 8-1, 16-9, 24-17 device present
	 * 0x1D, 0x1E, 0x1F is port 8-1, 16-9, 24-17 device enabled
	 * */

#if defined(CONFIG_SYNO_LP3943_FEATURES) || defined(CONFIG_SYNO_LEDS_LP3943_FEATURES)
	SYNOLP3943I2CMutex (true);
#endif /* CONFIG_SYNO_LP3943_FEATURES || CONFIG_SYNO_LEDS_LP3943_FEATURES */
	SA6500SMBusSwitchRegWrite(0, 0x72, 3);
	if (0 != linuxI2CSmbusRegRead(SA6500_U2_BOARD_UP_BUS, SA6500_U2_BOARD_UP_I2C_ADDR, 0xa, &data)) {
		printk("%s: linuxI2CSmbusRegRead 0xA return error!\n", __FILE__);
		goto END;
	}

	IntfDetect[0] = data & 0xff;
	IntfDetect[1] = (data & 0xff00) >> 8;

	if (0 != linuxI2CSmbusRegRead(SA6500_U2_BOARD_UP_BUS, SA6500_U2_BOARD_UP_I2C_ADDR, 0xc, &data)) {
		printk("%s: linuxI2CSmbusRegRead 0xC return error!\n", __FILE__);
		goto END;
	}

	IntfDetect[2] = data & 0xf;
	DevPresent[0] = (data & 0xff00) >> 8;

	if (0 != linuxI2CSmbusRegRead(SA6500_U2_BOARD_UP_BUS, SA6500_U2_BOARD_UP_I2C_ADDR, 0xe, &data)) {
		printk("%s: linuxI2CSmbusRegRead 0xE return error!\n", __FILE__);
		goto END;
	}

	DevPresent[1] = data & 0xff;
	DevPresent[2] = (data & 0xf00) >> 8;

	if (NULL == DevEnabled) {
		ret = 0;
		goto END;
	}

	if (0 != linuxI2CSmbusRegRead(SA6500_U2_BOARD_UP_BUS, SA6500_U2_BOARD_UP_I2C_ADDR, 0x1d, &data)) {
		printk("%s: linuxI2CSmbusRegRead 0x1D return error!\n", __FILE__);
		goto END;
	}

	DevEnabled[0] = data & 0xff;
	DevEnabled[1] = (data & 0xff00) >> 8;

	if (0 != linuxI2CSmbusRegRead(SA6500_U2_BOARD_UP_BUS, SA6500_U2_BOARD_UP_I2C_ADDR, 0x1f, &data)) {
		printk("%s: linuxI2CSmbusRegRead 0x1F return error!\n", __FILE__);
		goto END;
	}

	DevEnabled[2] = data & 0xf;

	ret = 0;
END:
	SA6500SMBusSwitchRegWrite(0, 0x72, 1);
#if defined(CONFIG_SYNO_LP3943_FEATURES) || defined(CONFIG_SYNO_LEDS_LP3943_FEATURES)
	SYNOLP3943I2CMutex (false);
#endif /* CONFIG_SYNO_LP3943_FEATURES || CONFIG_SYNO_LEDS_LP3943_FEATURES */
	return ret;
}

int SA6500GetDiskIntf(SYNO_DISK_INTF_INFO *input)
{
	int i;
	int ret = -1;
	u8 IntfDetect[3], DevPresent[3];
	// helper array to convert number 0-7 to corresponding bit
	static u8 mask[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

	if (0 != SA6500GetDiskStatus(IntfDetect, DevPresent, NULL)) {
		printk("%s: SA6500GetDiskStatus return error!\n", __FILE__);
		goto END;
	}

	// zero out input
	memset(input, 0, sizeof(SYNO_DISK_INTF_INFO));

	input->intf_num = SA6500_INTF_NUM;

	// copy the interface string to return structure
	snprintf(input->disk_intf[SYNO_U2_NVME].intf_name, MAX_DISK_INTF_LEN, "%s", ToDiskIntfStr[SYNO_U2_NVME]);
	snprintf(input->disk_intf[SYNO_SAS_SATA].intf_name, MAX_DISK_INTF_LEN, "%s", ToDiskIntfStr[SYNO_SAS_SATA]);

	/* slot 1-4 is U.2 NVMe and located in bit 16-19
	   first four bit in IntfDetect[2], DevPresent[2] */
	for (i = 0; i < 4; ++i) {
		int disk_intf = ToDiskIntf(IntfDetect[2]&mask[i], DevPresent[2]&mask[i]);
		/* interface is correct or there is no disk, no need to do anything */
		if (disk_intf == SYNO_U2_NVME || disk_intf == SYNO_NO_DISK) {
			continue;
		}
		input->disk_intf[SYNO_U2_NVME].disk_list[i] = INTF_ERR;
	}

	/* slot 5-20 is SAS/SATA and located in bit 0-15
	   IntfDetect[0, 1], DevPresent[0, 1] */
	for (i = 0; i < 16; ++i) {
		int disk_intf = ToDiskIntf(IntfDetect[i/8]&mask[i%8], DevPresent[i/8]&mask[i%8]);
		/* interface is correct or there is no disk, no need to do anything */
		if (disk_intf == SYNO_SAS_SATA || disk_intf == SYNO_NO_DISK) {
			continue;
		}
		input->disk_intf[SYNO_SAS_SATA].disk_list[i+4] = INTF_ERR;
	}
	ret = 0;
END:
	return ret;
}

int SA6500GetHDDBackPlaneStatus(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;
	unsigned long hdd_intf = 0;
	u8 DevEnabled[3], DevPresent[3], IntfDetect[3];
	struct hwmon_sensor_list *hwmon_sensor_list = &sa6500_sensor_list;

	if (NULL == hdd_backplane) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	if (0 != SA6500GetDiskStatus(IntfDetect, DevPresent, DevEnabled)) {
		printk("%s: SA6500GetDiskStatus return error!\n", __FILE__);
		goto End;
	}

	hdd_detect = (DevPresent[1] << 12) | (DevPresent[0] << 4) | DevPresent[2];
	hdd_enable = (DevEnabled[1] << 12) | (DevEnabled[0] << 4) | DevEnabled[2];
	hdd_intf   = (IntfDetect[1] << 12) | (IntfDetect[0] << 4) | IntfDetect[2];

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%05x", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%05x", hdd_enable);
	snprintf(hdd_backplane->sensor[2].value, sizeof(hdd_backplane->sensor[2].value), "%05x", hdd_intf);

	iRet = 0;

End:
	return iRet;
}

struct model_ops sa6500_ops = {
	.x86_init_module_type = SA6500InitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = SA6500I2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

SYNO_HWMON_SENSOR_TYPE SA6500_thermal_sensor = {
        .type_name = HWMON_SYS_THERMAL_NAME,
        .sensor_num = 3,
        .sensor[0] = {
                .sensor_name = "Remote1",
        },
        .sensor[1] = {
                .sensor_name = "Local",
        },
        .sensor[2] = {
                .sensor_name = "Remote2",
        },
};

SYNO_HWMON_SENSOR_TYPE SA6500_voltage_sensor = {
        .type_name = HWMON_SYS_VOLTAGE_NAME,
        .sensor_num = 5,
        .sensor[0] = {
                .sensor_name = "VCC",
        },
        .sensor[1] = {
                .sensor_name = "VPP",
        },
        .sensor[2] = {
                .sensor_name = "V33",
        },
        .sensor[3] = {
                .sensor_name = "V5",
        },
        .sensor[4] = {
                .sensor_name = "V12",
        },
};

SYNO_HWMON_SENSOR_TYPE SA6500_fan_speed_rpm = {
        .type_name = HWMON_SYS_FAN_RPM_NAME,
        .sensor_num = 4,
        .sensor[0] = {
                .sensor_name = HWMON_SYS_FAN1_RPM,
        },
        .sensor[1] = {
                .sensor_name = HWMON_SYS_FAN2_RPM,
        },
        .sensor[2] = {
                .sensor_name = HWMON_SYS_FAN3_RPM,
        },
        .sensor[3] = {
                .sensor_name = HWMON_SYS_FAN4_RPM,
        },
};

SYNO_HWMON_SENSOR_TYPE SA6500_psu_status[] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 6,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 6,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

struct hwmon_sensor_list sa6500_sensor_list = {
        .thermal_sensor = &SA6500_thermal_sensor,
        .voltage_sensor = &SA6500_voltage_sensor,
        .fan_speed_rpm = &SA6500_fan_speed_rpm,
        .psu_status = SA6500_psu_status,
        .hdd_backplane = NULL,
};
