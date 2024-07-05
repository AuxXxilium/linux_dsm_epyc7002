// Copyright (c) 2000-2012 Synology Inc. All rights reserved.
#include "synobios.h"
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include "comcerto2k_common.h"

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
		case MODEL_DS414j:
		case MODEL_DS415j:
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
