//***************************************************************************
//Name: spiflash.h
//
//Description:
//      Declare SPI flash ROM definition
//
//Revision History:
//2013/05/30    V1.02   James Peng      add SPI_INDEX_DEFAULT_PAGE_LENGTH_32 difinition for MXIC MX25L5121E, MX25L1021E SPI ROM
//2013/04/05    V1.0    Jesse Chang     First revision
//
/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */
//***************************************************************************

#ifndef _SPI_FLASH_H_
#define _SPI_FLASH_H_

//#include    "basetype.h"
//#include "Precomp.h"
//
// SPI Status Register
//
//The bits definition of SPI Statur Register may be different between different manufacturers
//Only the same definition bits are defined here
//Some products which support Quad SPI have 16 bits Status Register, example GD25Q512,GD25Q10...
#pragma pack(1)
//8 bits status register 1 (STSR bit 0-7)
typedef union _SPI_FLASH_STSR1 {
    struct {
        BYTE    WIP : 1;            //bit[0]: Write In Progress
        BYTE    WEL : 1;            //bit[1]: Write Enable Latch
        BYTE    BP0 : 1;            //bit[2]: Block Protect bit 0
        BYTE    BP1 : 1;            //bit[3]: Block Protect bit 1
        BYTE    Rev : 3;            //bit[6:4]: Reserved, some products are BP2, BP3,BP4. Shall set 0 to unprotected block
        BYTE    SRWD : 1;           //bit[7]: Status Register Write Protect (Status Register Protect bit 0 SRP1)
    };
    BYTE    AsByte;
} SPI_FLASH_STSR1, *PSPI_FLASH_STSR1;

//8 bits status register 2 (STSR bit 8-15)
typedef union _SPI_FLASH_STSR2 {
    struct {
        BYTE    SRP1 : 1;           //bit[8]: Status Register Protect bit 1
        BYTE    QE : 1;             //bit[9]: Quad Enable
        BYTE    Rev : 4;            //bit[13:10]: Reserved, some products are other protected bits. Shall set 0 to unprotected
        BYTE    CMP : 1;            //bit[14]: Complement Protect
        BYTE    SUS : 1;            //bit[15]: Suspend Status
    };
    BYTE    AsByte;
} SPI_FLASH_STSR2, *PSPI_FLASH_STSR2;
#pragma pack()


//
//ASM116 legacy SPI control interface does not support SPI dual I/O and clock frequency can't be setting
//Dual I/O commands and clock frequency seeting don't need to set in command table
//

//
//SPI flash ROM index
//
#pragma pack(1)
typedef struct _SPI_FLASH_INDEX {
    BYTE    FlashID[3];                     //3 bytes spi flash rom product id
    BYTE    CmdTableIndex;                  //Index of SPI falsh command table
    WORD    Capacity;                       //Capacity K Bytes
    //Characters of different SPI flash ROM
    BYTE    WriteSTSRBytes;                 //1 or 2 bytes when Write Status Register
    BYTE    EnableWriteSTSR;                //1: Enable Write Status Register command prior to Write Status Register command
    WORD    PageLength;                     //length of program page command
    char    ProductString[14];              //Product string
    WORD    MaxSEPPTime;                    //Maximum waiting Sector Erase or Page Program time (ms)
    WORD    MaxCETime;                      //Maximum waiting Chip Erase time (ms)
} SPI_FLASH_INDEX, *PSPI_FLASH_INDEX;
#pragma pack()

#define SPI_INDEX_DEFAULT_COMMAND_TABLE     0x00            //Table 0: default command table
#define SPI_INDEX_UNKNOWN_FLASH_ID          0xFF
#define SPI_INDEX_UNKNOWN_CAPACITY          512             //Flash ID is not in supported table, ASM116 supports to 512k bytes SPI flash ROM
#define SPI_FLASH_64KB                      64              //512K bits, 64K bytes SPI flash
#define SPI_FLASH_128KB                     128             //1M bits, 128K bytes SPI flash
#define SPI_FLASH_256KB                     256             //2M bits, 256K bytes SPI flash
#define SPI_FLASH_512KB                     512             //4M bits, 512K bytes SPI flash
#define SPI_FLASH_1MB                       1024            //8M bits, 1M bytes SPI flash
#define SPI_INDEX_DEFAULT_WRSR_BYTE         0x01            //Write Status Register 1 only
#define SPI_INDEX_EWSR_UNSUPPORTED          0x00            //Don't support SPI EWSR command
#define SPI_INDEX_EWSR_SUPPORTED            0x01            //EWSR is needed prior to WRSR command
#define SPI_INDEX_DEFAULT_PAGE_LENGTH       256             //Default 256 bytes page length
#define SPI_INDEX_DEFAULT_PAGE_LENGTH_32    32              //Default 32 bytes page length
#define SPI_INDEX_UNKNOWN_STRING            "Unknown"       //Unknown
#define SPI_INDEX_DEFAULT_MAX_SE_PP_TIME    1000            //1000 ms
#define SPI_INDEX_DEFAULT_MAX_CE_TIME       20000           //20 s

#define SPI_FLASH_DEFAULT_WAIT_TIME         1000            //1000 ms


//
// SPI flash ROM command table
//
#pragma pack(1)
typedef struct _SPI_FLASH_COMMAND_TABLE {
    BYTE    WriteEnableCmd;                 //Write Enable command code
    BYTE    WritDisableCmd;                 //Write Disable command code
    BYTE    ReadSTSR1Cmd;                   //Read Status Register 1 command code
    BYTE    ReadSTSR2Cmd;                   //Read Status Register 2 command code
    BYTE    WriteSTSR;                      //Write Status Register command code
    BYTE    EnableWriteSTSRCmd;             //Enable Write Status Register command code
    BYTE    ReadCmd;                        //Read command code
    BYTE    FastReadCmd;                    //Fast Read command code
    BYTE    ChipEraseCmd;                   //Chip Erase command code
    BYTE    SectorEraseCmd;                 //Sector Erase 4k bytes command code
    BYTE    PageProgramCmd;                 //Page Program command code
    BYTE    AutoAddressIncProCmd;           //Auto Address Increment Program command code for PCT SPI flash ROM only
} SPI_FLASH_COMMAND_TABLE, *PSPI_FLASH_COMMAND_TABLE;
#pragma pack()

//
// SPI flash ROM commands
//
#define SPICMD_UNSUPPORTED      0x00        //Unsupported command code
#define SPICMD_JEDEC_RDID       0x9F        //JEDEC Read Device Identification
#define SPICMD_RDID             0xAB        //Read Device Identification
#define SPICMD_WREN             0x06        //Write Enable
#define SPICMD_WRDI             0x04        //Write Disable
#define SPICMD_RDSR1            0x05        //Read Status Register 1
#define SPICMD_RDSR2            0x35        //Read Status Register 2
#define SPICMD_EWSR             0x50        //Enable Write Read Status Register
#define SPICMD_WRSR             0x01        //Write Status Register
#define SPICMD_READ             0x03        //Read data
#define SPICMD_FREAD            0x0B        //Fast Read data
#define SPICMD_CEC7             0xC7        //Chip Erase
#define SPICMD_CE60             0x60        //Chip Erase
#define SPICMD_SE20             0x20        //Sector Erase 4k bytes
#define SPICMD_SED7             0xD7        //Sector Erase 4k bytes
#define SPICMD_PP               0x02        //Page Program
#define SPICMD_AAIP             0xAF        //Auto Address Increment Program (PCT only)
//Other SPI commands that don't not used
#define SPICMD_BE52             0x52        //Block Erase 32/64k bytes
#define SPICMD_BED8             0xD8        //Block Erase 64k bytes
#define SPICMD_DP               0xB9        //Deep Power Down
#define SPICMD_RDP              0xAB        //Release from Deep Power Down
#define SPICMD_RDO              0x3B        //Read Data Dual Out
#define SPICMD_RDIO             0xBB        //Read Data Dual In/Out
#define SPICMD_RQO              0x6B        //Read Data Quad Out
#define SPICMD_RQIO             0xEB        //Read Data Quad In/Out


//
// SPI flash ROM Extension
//

typedef struct _SPI_FLASH_EXTENSION {
   // PSPI_CONTROL_EXTENSION  pSpiControlExtension;
  PLSPI_REGISTERS    SpiAbar; //Move to here
    SPI_FLASH_INDEX         SpiIndex;
    int                 Spi3WMEnabled;
    int                 InSupportedList;
} SPI_FLASH_EXTENSION, *PSPI_FLASH_EXTENSION;


int SpiFlashJedecRDID(struct pci_dev *PciDevice, BYTE *FlashID);
int SpiFlashRDID(struct pci_dev *PciDevice, BYTE *FlashID);
int SpiFlashWREN(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashWRDI(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashRDSR1(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, PSPI_FLASH_STSR1 pStsr1);
int SpiFlashRDSR2(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, PSPI_FLASH_STSR2 pStsr2);
int SpiFlashEWSR(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashWRSR1(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, SPI_FLASH_STSR1 Stsr1);
int SpiFlashWRSR2(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, SPI_FLASH_STSR1 Stsr1, SPI_FLASH_STSR2 Stsr2);
int SpiFlashCE(struct pci_dev *PciDevice ,PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashSE(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, DWORD Address);
int SpiFlashPP(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, WORD Length);
int SpiFlashREAD(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length);
int SpiFlashFREAD(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length);
int SpiFlashWaitWriteEnabled(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, WORD WaitingTime);
int SpiFlashWaitWriteCompleted(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, WORD WaitingTime);
int SpiFlashAAIP(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length);


//
// Exported Functions Prototype
//
int SpiFlashDetectSpiFlashRom(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashReadData(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, DWORD Length);
int SpiFlashUnprotectBlocks(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashChipErase(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl);
int SpiFlashSectorErase(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, DWORD Address);
int SpiFlashWriteData(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, DWORD Length);

#endif  //_SPI_FLASH_H_

