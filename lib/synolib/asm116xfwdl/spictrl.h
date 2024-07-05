//***************************************************************************
//Name: spictrl.h
//
//Description:
//      Declare Asmedia ASM116 SPI controller
//      DOS4GW32 32-bit environment.
//      Compiler: Open Watcom C/C++ Win32 V1.8
//
//Revision History:
//2013/05/27    V2.01  James Peng      add IndexDataPair and MMIO command ,
//                                                        add AP-FW interface  Vendor Command
//2013/04/03    V1.0    Jesse Chang     First revision
//
/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */
//***************************************************************************

#ifndef _SPICTRL_H_
#define _SPICTRL_H_


//
// Bit encodes for PCI_COMMON_CONFIG.u.type0.BaseAddresses
//

#define PCI_ADDRESS_IO_SPACE                0x00000001L  // (ro)
#define PCI_ADDRESS_MEMORY_TYPE_MASK        0x00000006L  // (ro)
#define PCI_ADDRESS_MEMORY_PREFETCHABLE     0x00000008L  // (ro)
// ASM116 Legacy SPI control interface registers
//
#pragma pack(1)
//offset 0x00-0x03: SPI data buffer
typedef union _LSPI_DATA {
    struct {
        BYTE    Data[4];             //4 bytes
    };
    DWORD   AsDword;
} LSPI_DATA, *PLSPI_DATA;

//offset 0x04: SPI control register 0
typedef union _LSPI_CTRL0 {
    struct {
        BYTE    DataSize : 3;       //bit[2:0]: buffer size in bytes. 1-4: 1-4 bytes. other: reserved
        BYTE    WR : 1;             //bit[3]: SPI R/W control. 1: write, 0: read
        BYTE    CS : 1;             //bit[4]: SPI CS# signal. Low active
        BYTE    RUN : 1;            //bit[5]: trigger SPI controller
        BYTE    Rev : 2;            //bit[7:6]: Reserved
    };
    BYTE    AsByte;
} LSPI_CTRL0, *PLSPI_CTRL0;

//offset 0x05: SPI control register 1
typedef union _LSPI_CTRL1 {
    struct {
        BYTE    LSPI3WM : 1;        //bit[0]: Legacy SPI 3WM. 0:4-wire mode. 1:3-wire mode. LSPI_3WM will be set to 1 if power-on detection detects SPI FLASH is in 3-wire mode
        BYTE    Rev : 7;            //bit[7:1]: Reserved
    };
    BYTE    AsByte;
} LSPI_CTRL1, *PLSPI_CTRL1;

//offset 0x06: SPI arbitration
typedef union _SPI_ARBITER {
    struct {
        BYTE    REQ0 : 1;           //bit[0]: SPI request 0
        BYTE    GNT0 : 1;           //bit[1]: SPI grant 0
        BYTE    REQ1 : 1;           //bit[2]: SPI request 1
        BYTE    GNT1 : 1;           //bit[3]: SPI grant 1
        BYTE    REQ2 : 1;           //bit[4]: SPI request 2
        BYTE    GNT2 : 1;           //bit[5]: SPI grant 2
        BYTE    REQ3 : 1;           //bit[6]: SPI request 3
        BYTE    GNT3 : 1;           //bit[7]: SPI grant 3
    };
    BYTE    AsByte;
} SPI_ARBITER, *PSPI_ARBITER;

//offset 0x04 - 0x07 SPI control
typedef union _LSPI_CONTROL_ {
    struct {
        LSPI_CTRL0      Ctrl0;
        LSPI_CTRL1      Ctrl1;
        SPI_ARBITER     Arbiter;
        BYTE            Rev7;
    };
    DWORD   AsDword;
} LSPI_CONTROL, *PLSPI_CONTROL;


//ASM116 legacy SPI control registers
typedef struct _LSPI_REGISTERS {
    LSPI_DATA       Data;           //byte 0-3: Data buffer
    LSPI_CONTROL    Ctrl;           //byte 4-7: Control
} LSPI_REGISTERS, *PLSPI_REGISTERS;
#pragma pack()

// TIMEOUT
#define IDLE_TIMEOUT        1               //time out for SPI idle, 1 second
#define GRANT_TIMEOUT       3               //time out for SPI grant, 3 seconds
#define COMMAND_TIMEOUT    20               //time out for command,20 seconds




//
//ASM116 Arbitration Request Number
//
#define UTILITY_SPI_REQUEST_NUMBER      0               //Software utility uses request number 0
#define FIRMWARE_SPI_REQUEST_NUMBER     1               //Firmware uses request number 1
#define MAXIMUM_SPI_REQUEST_NUMBER      3


//
// ASM116 MMIO Index-Data Pair
//
#define ASM116_MMIO_INDEX             0xD0            //PCI offset 0xD0 ~ 0xD3: Index
#define ASM116_MMIO_INDEX_MASK        0x01L           //bit0 - 0: access MMIO registers, 1: access 8051 xData
#define ASM116_MMIO_DATA              0xD4            //PCI offset 0xD4 ~ 0xD7: Data

#define ASM116_MMIO_BASE_HW_REGISTER    0x10


//ASM116 SPI control interface offset from BAR0+0x1000
#define ASM116_SPI_CONTROL_BASE         0xB00L     //SPI controller registers base address: MMIO offset 0x1B00

#define ASM116_SPI_CONTROL_DATA         0xB00L     //SPI controller registers base address: MMIO offset 0x1B00
#define ASM116_SPI_CONTROL_CONTROL      0xB04L     //SPI controller registers base address: MMIO offset 0x1B00

#define ASM116_LSPI_MAXIMUM_LENGTH      4           //ASM116 legacy SPI supports Read/Write 4 bytes one time only

#define PCIMMIO_MEM_DEV			        "/dev/mem"
#define PCIMMIO_MEMMAP_SIZE    	        0x1000


//
// PPCI_DEVICE
//
typedef struct _PCI_DEVICE {
    BYTE    PciBusNumber;
    BYTE    PciDeviceNumber;
    BYTE    PciFunctionNumber;

   int  treeAddress;

    //Char name[256];

} PCI_DEVICE, *PPCI_DEVICE;


//
// Functions Prototype
//

 int SpiControllerInit(struct pci_dev *PciDevice);
 DWORD   SpiReadRegisterDword(struct pci_dev *PciDevice, DWORD Register);
 void    SpiWriteRegisterDword(struct pci_dev *PciDevice, DWORD Register, DWORD dValue);
 int SpiGetGrant(struct pci_dev *PciDevice, const BYTE RequestNumber);
 void    SpiReleaseGrant(struct pci_dev *PciDevice, const BYTE RequestNumber);
 int SpiIs3WireMode(struct pci_dev *PciDevice);
 void    SpiStart(struct pci_dev *PciDevice);
 void    SpiTerminate(struct pci_dev *PciDevice);
 int SpiIdle(struct pci_dev *PciDevice);
 int SpiRead(struct pci_dev *PciDevice, void *pData, BYTE Length);
 int SpiWrite(struct pci_dev *PciDevice, void *pData, BYTE Length);


#endif  //_SPICTRL_H_
