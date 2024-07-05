// Copyright (c) 2000-2012 Synology Inc. All rights reserved.
#include "synobios.h"
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include "armada38x_common.h"

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	char speed_value;
	int res = -EINVAL;
	int model = GetModel();

	switch (model) {
		default:
			if (FanStatusMappingType1(status, speed, &speed_value)) {
				goto END;
			}
	}
	if (SYNO_CTRL_FAN_RESISTOR(speed_value)) {
		goto END;
	}

	res = 0;
END:
	return res;
}
