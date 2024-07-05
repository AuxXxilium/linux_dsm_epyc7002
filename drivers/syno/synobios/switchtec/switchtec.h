// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#include "synobios.h"
int switchtec_init(void);
void switchtec_deinit(void);
int switchtec_led_set(SYNO_LED ledStatus);
int switchtec_set_disk_led(DISKLEDSTATUS *cmd);
