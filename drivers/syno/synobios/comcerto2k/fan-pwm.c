/* Copyright (c) 2000-2012 Synology Inc. All rights reserved. */
#include <linux/kernel.h>
#include "synobios.h"
#include <linux/delay.h>
#include "comcerto2k_common.h"
#include "syno_ttyS.h"

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;
	int iFanDuty = -1;
	char szUartCmd[5] = {0};

	if( status == FAN_STATUS_STOP ) {
		speed = FAN_SPEED_STOP;
	}

	if( FAN_SPEED_PWM_FORMAT_SHIFT <= (int)speed ) {
		/* PWM format is only for bandon and QC test */
		iFanDuty = FAN_SPEED_SHIFT_DUTY_GET((int)speed);
		/* set fan speed hz */
		if(0 < FAN_SPEED_SHIFT_HZ_GET((int)speed)) {
			snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_FREQUENCY, FAN_SPEED_SHIFT_HZ_GET((int)speed));
			if( 0 > synobios_lock_ttyS_write(szUartCmd) ) {
				goto END;
			}
		}
	} else {
		if( 0 > (iFanDuty = PWMFanSpeedMapping(speed)) ) {
			printk("No matched fan speed!\n");
			goto END;
		}
	}
	
	/* set fan speed duty cycle  */
	snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_DUTY_CYCLE, iFanDuty);
	if( 0 > synobios_lock_ttyS_write(szUartCmd) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}
