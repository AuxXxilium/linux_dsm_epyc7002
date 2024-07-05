// Copyright (c) 2000-2012 Synology Inc. All rights reserved.
#include "synobios.h"
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include "armada_common.h"

int
FanStatusMappingRS409r1(FAN_STATUS status, FAN_SPEED speed, char *pSpeed_value)
{
	int ret = -1;

	if (status == FAN_STATUS_STOP) {
		*pSpeed_value = CPLD_FAN_SPEED_0;
	} else {
		switch (speed) {
		case FAN_SPEED_STOP:
			*pSpeed_value = CPLD_FAN_SPEED_0;
			break;
		case FAN_SPEED_ULTRA_LOW:
		case FAN_SPEED_VERY_LOW:
		case FAN_SPEED_LOW:
		case FAN_SPEED_TEST_1:
			*pSpeed_value = CPLD_FAN_SPEED_2;
			break;
		case FAN_SPEED_MIDDLE:
		case FAN_SPEED_TEST_2:
			*pSpeed_value = CPLD_FAN_SPEED_4;
			break;
		case FAN_SPEED_HIGH:
		case FAN_SPEED_VERY_HIGH:
		case FAN_SPEED_ULTRA_HIGH:
		case FAN_SPEED_FULL:
		case FAN_SPEED_TEST_4:
			*pSpeed_value = CPLD_FAN_SPEED_6;
			break;
		default:
			printk("%s(%d) No such fan speed exists, speed=[%d].\n",
				__FILE__, __LINE__, speed);
			goto END;
		}
	}

	ret = 0;
END:
	return ret;
}

int 
SetFanSpeedValue(char speed_value)
{
	int index = 0;
	int ret = -1;
	int status = 0;

	for (index=0; index<3; index++) {
		if (0x01 & (speed_value>>index)) {
			status = 1;
		} else {
			status = 0;
		}

		if (SYNO_CTRL_FAN_PERSISTER(index+1, status, 1)) {
			goto End;
		}
	}

	ret = 0;
End:
	return ret;
}

int
SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	char speed_value;
	int res = -EINVAL;
	int model = GetModel();

	switch (model) {
		case MODEL_RS812:
			/* this is only for RS409r1, not RS409, RS409 not use FAN_MULTI_ALWAYS in mapping.h
			 * so it never go to this case even you use this function point */
			if (FanStatusMappingRS409r1(status, speed, &speed_value)) {
				goto END;
			}
			break;
		case MODEL_RS214:
			if (FanStatusMappingType2(status, speed, &speed_value)) {
				goto END;
			}
			break;
		default:
			if (FanStatusMappingType1(status, speed, &speed_value)) {
				goto END;
			}
	}
	if (-1 == SetFanSpeedValue(speed_value)) {
		goto END;
	}

	res = 0;
END:
	return res;
}
