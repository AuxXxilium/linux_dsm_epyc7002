#include <linux/kernel.h>
#include "led_1475.h"
int GetMaxInternalDiskNum(void);

#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
int SYNODiskLedCtrlBy1475SGPIO(DISKLEDSTATUS* diskLedStatus)
{
	int err = -1;

	if (1 > diskLedStatus->diskno || GetMaxInternalDiskNum() < diskLedStatus->diskno) {
		goto END;
	}
	if (NULL != funcSYNOCtrlDiskLedBy1475) {
		err = funcSYNOCtrlDiskLedBy1475(diskLedStatus->diskno, diskLedStatus->status);
	}
END:
	return err;
}
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
/* Control disk led via 1475 SGPIO for kernel */
int SYNODiskLedCtrlBy1475SGPIO(int disknum, SYNO_DISK_LED status)
{
	int err = -1;

	if (1 > disknum || GetMaxInternalDiskNum() < disknum) {
		goto END;
	}

	if (funcSYNOCtrlDiskLedBy1475 != NULL) {
		err = funcSYNOCtrlDiskLedBy1475(disknum, status);
	}
END:
	return err;
}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
