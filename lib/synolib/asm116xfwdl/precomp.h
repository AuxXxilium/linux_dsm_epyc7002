/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */

#ifndef _PRECOMP_H
#define _PRECOMP_H

#include <linux/types.h>
#include <linux/pci.h>

#define BYTE	u8
#define WORD	u16
#define DWORD	u32


#include "asm116.h"
#include "spictrl.h"
#include "spiflash.h"
#include "spifile.h"

#define DEBUG  0

//#ifdef DEBUG
//#define func_enter() 		printk(KERN_INFO "\n\nEnter :    %s(%d)-%s\n",__FILE__,__LINE__,__FUNCTION__)
//#define func_exit()		printk(KERN_INFO "\n\nExit :    %s(%d)-%s\n",__FILE__,__LINE__,__FUNCTION__)
//#else
#define func_enter()
#define func_exit()
//#endif

#define MAX_DEVICE_CNT 16
#define GET_SPI_CONTROL_GRANT_RETRY_COUNT       3
#define SPI_PROGRAM_REASE_TRY_COUNT             3
#define SPI_PROGRAM_UPDATE_TRY_COUNT            3



// PCI_DEVICE gPciDevice[MAX_DEVICE_CNT];
// PCI_DEVICE uiPciDevice[MAX_DEVICE_CNT];
// PCI_DEVICE backup_gPciDevice[MAX_DEVICE_CNT];

enum interctl_error {
 ASMT_SUCCESS                           = 0,
 ASMT_IO_ERROR					        =-1,    		//Control I/O Fail
 ASMT_FWVERSION_UNMATCH			        =-2,            // FW bin is older than curr, no need to upgrade
 ASMT_UNMATCH					        =-3,    		// parameter comparsion result is ummatch
 ASMT_DEVICE_NOT_FOUND			        =-4,            // Target Deives can not find
 ASMT_FILE_NOT_FOUND			        =-5,    	    //Target File can not find
 ASMT_SPI_VERIFY_ERROR			        =-6,            //Error when verify SPI after update config or firmware
 ASMT_RESET_FAIL				        =-7,    	    //Re-link Failed
 ASMT_MEMORY_ALLOCATE_ERROR             =-8,            //Error when allocate memory
 ASMT_TIMEOUT 					        =-9,    		//Program Time out
 ASMT_PARAMETER_INVALID 		        =-10,           //Parameter Invalid
 ASMT_SPI_BLANK_ERROR                   =-11,
};

// Decalre SPI programer
//SPI_FLASH_PROGRAMER             gSpiProgramerArray[MAX_DEVICE_CNT];
//gloable var
extern int verblevel;



//pci.c
int asm_pci_init( void );
void asm_pci_exit( void );
int asm_select_devices( void );



#endif

