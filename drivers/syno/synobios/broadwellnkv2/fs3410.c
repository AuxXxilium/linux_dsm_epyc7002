// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/synobios.h>
#include "broadwellnkv2_common.h"

#define PSU_I2C_BUS             0
#define PSU_TOP_I2C_ADDR        0x58
#define PSU_BTM_I2C_ADDR        0x59

// extern function from broadwellnkv2_common
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);


/* FIXME: should not directly copy following function
 * Modified from drivers/i2c/muxes/i2c-mux-pca954x.c */
int FS3410SMBusSwitchRegWrite(int bus_no, u16 addr, u8 val)
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

void FS3410SMBusSwitchInit(void) {
	FS3410SMBusSwitchRegWrite(0, 0x70, 0x8);
}

SYNO_HWMON_SENSOR_TYPE FS3410_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS3410_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS3410_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE FS3410_psu_status[2] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

SYNO_HWMON_SENSOR_TYPE FS3410_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

static
int FS3410InitModuleType(struct synobios_ops *ops)
{
	module_t type_fs3410 = MODULE_T_FS3410;
	module_t *pType = &type_fs3410;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio BROADWELLNKV2_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNKV2_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}


static
int FS3410I2CGetPowerInfo(POWER_INFO *power_info)
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

struct model_ops fs3410_ops = {
	.x86_init_module_type = FS3410InitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = FS3410I2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list fs3410_sensor_list = {
        .thermal_sensor = &FS3410_thermal_sensor,
        .voltage_sensor = &FS3410_voltage_sensor,
        .fan_speed_rpm = &FS3410_fan_speed_rpm,
        .psu_status = FS3410_psu_status,
        .hdd_backplane = &FS3410_hdd_backplane_status,
};
