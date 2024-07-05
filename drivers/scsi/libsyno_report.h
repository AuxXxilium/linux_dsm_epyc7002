#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#ifndef _SCSI_LIBSYNO_REPORT_H
#define _SCSI_LIBSYNO_REPORT_H
#include <scsi/scsi_device.h>

#ifdef MY_ABC_HERE
int SynoScsiDeviceToDiskIndex(const struct scsi_device *psdev);

void SynoSendScsiErrorEvent(struct work_struct *work);

void SynoScsiErrorWithSenseReport(struct scsi_device *psdev,
		u8 sense_key, u8 asc, u8 ascq, sector_t lba);

void SynoScsiTimeoutReport(struct scsi_device *psdev,
		unsigned char op, int iRetries);

bool SynoIsPhysicalDrive(const struct scsi_device *psdev);
#endif /* MY_ABC_HERE */

#endif /* _SCSI_LIBSYNO_REPORT_H */

