//***************************************************************************
//Name: spiflash.c
//
//Description:
//      Supported SPI flash ROM manipulative functions
//
//Revision History:
//  2014/06/13	V1.60	Jesse Chang		Modify Maximum waiting Chip Erase time to 10000 ms for Flash Index 32 - Gigadevice GD25Q40B
//                                      Add Flash Index 48 for Gigadevice GD25Q21B
//  2013/10/28  V1.20   Jesse Chang     Correct Flash Index 35 - 38 flash id error for ESMT: F25L05PA, F25L01PA, F25L02PA, F25L04PA
//                                      Add ATMEL AT25BCM512B (Flash Index is same as AT25F512B -> Flash Index 4)
//                                      Add Flash Index 46 - 47 for ATMEL AT25DF021, AT25DF041A
//  2013/10/17  V1.10   Jesse Chang     Don't need to use EWSR command in Cmd Table Index 2 for Winbond SPI flash ROM
//                                      Set SPI_INDEX_EWSR_UNSUPPORTED in Winbond SPI flash Index
//                                      Change PCT: 25VF512A from Cmd Table Index 3 to Cmd Table Index 4
//                                      Change PCT: 25VF020B from Cmd Table Index 3 to Cmd Table Index 6
//                                      Modify 2-bytes status register in Index 15 for PCT: 25VF020B, Microchip: SST25VF020B, SST25PF020B
//                                      Add Cmd Table Index 6 for PCT: 25VF020B, Microchip: SST25VF020B, SST25PF020B
//                                      Add Cmd Table Index 7 for Micron: M25P05, M25P10, M25PE10, M25P20, M25P20S, M25PE20S, M25P40
//                                      Add Flash Index 35 - 38 for ESMT: F25L05PA, F25L01PA, F25L02PA, F25L04PA
//                                      Add Flash Index 39 - 45 for Micron: M25P05, M25P10, M25PE10, M25P20, M25P20S, M25PE20S, M25P40, M25PE40, M25PE40S
//  2013/05/30  V1.02   James Peng      Only use READ command in SpiFlashReadData function
//                                      Add Flash Index 33 - 34 for MXIC: MX25L5121E, MX25L1021E
//  2013/04/06  V1.0    Jesse Chang     First revision
//
/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */
//***************************************************************************

//#include    "stdafx.h"
#include      "precomp.h"
#include    "spiflash.h"
#include    "spictrl.h"


//
// Supported SPI flash command tables
//
//typedef struct _SPI_FLASH_COMMAND_TABLE {
//    BYTE    WriteEnableCmd;                 //Write Enable command code
//    BYTE    WritDisableCmd;                 //Write Disable command code
//    BYTE    ReadSTSR1Cmd;                   //Read Status Register 1 command code
//    BYTE    ReadSTSR2Cmd;                   //Read Status Register 2 command code
//    BYTE    WriteSTSR;                      //Write Status Register command code
//    BYTE    EnableWriteSTSRCmd;             //Enable Write Status Register command code
//    BYTE    ReadCmd;                        //Read command code
//    BYTE    FastReadCmd;                    //Fast Read command code
//    BYTE    ChipEraseCmd;                   //Chip Erase command code
//    BYTE    SectorEraseCmd;                 //Sector Erase 4k bytes command code
//    BYTE    PageProgramCmd;                 //Page Program command code
//    BYTE    AutoAddressIncProCmd;           //Auto Address Increment Program command code for PCT SPI flash ROM only
//} SPI_FLASH_COMMAND_TABLE, *PSPI_FLASH_COMMAND_TABLE;
//

const SPI_FLASH_COMMAND_TABLE SUPPORTED_SPI_FLASH_COMMAND_TABLE[] = {
    //Cmd Table Index 0: default table
    //AMIC: A25L512, A25L512A, A25LS512A, A25L010, A25L010A, A25L020, A25L020C, A25L040, A25L040A
    //ATMEL: AT25F512B
    //EON: EN25F05, EN25F10, EN25LF10, EN25F20, EN25LF20, EN25F40, EN25LF40, EN25Q40
    //MXIC: MX25L512E, MX25V512E, MX25L1006E, MX25L1026E, MX25V1006E, MX25L2006E, MX25L2026E, MX25V2006E,
    //      MX25L4006E, MX25L4026E, MX25V4006E
    //      MX25L5121E, MX25L1021E
    //Pflash: Pm25LD512, Pm25LD512C, Pm25LD010, Pm25LD010C, Pm25LD020, Pm25LD020C
    //ESMT: F25L05PA, F25L01PA, F25L02PA, F25L04PA
    //Micron: M25PE40, M25PE40S
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        SPICMD_UNSUPPORTED,     //Enable Write Status Register command unsupported
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        SPICMD_UNSUPPORTED      //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 1:
    //Pflash: Pm25LV512A, Pm25LV010A, Pm25LV020, Pm25LV040, Pm25LD040, Pm25LD040C
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        SPICMD_UNSUPPORTED,     //Enable Write Status Register command unsupported
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0xD7,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        SPICMD_UNSUPPORTED      //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 2:
    //Winbond: W25X05CL, W25X10CL, W25X10BV, W25X10BL, W25X20CL, W25X20BV, W25X20BL, W25X40CL, W25X40BV, W25X40BL
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        SPICMD_UNSUPPORTED,     //Enable Write Status Register command code
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        SPICMD_UNSUPPORTED      //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 3:
    //PCT: 25VF010A, 25VF040B
    //Microchip: SST25VF010A, SST25VF040B, SST25PF040B
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        0x50,                   //Enable Write Status Register command code
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        0xAF                    //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 4:
    //PCT: 25VF512A, 25VF020
    //Microchip: SST25VF512, SST25VF512A, SST25VF020, SST25VF020A
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        0x50,                   //Enable Write Status Register command code
        0x03,                   //Read Data command code
        SPICMD_UNSUPPORTED,     //Fast Read Data command unsupported
        0x60,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        0xAF                    //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 5:
    //Gigadevice: GD25Q512, GD25Q10, GD25Q20B, GD25Q40B, GD25Q21B
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        0x35,                   //Read Status Register 2 command code
        0x01,                   //Write Status Register command code
        SPICMD_UNSUPPORTED,     //Enable Write Status Register command unsupported
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        SPICMD_UNSUPPORTED      //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 6:
    //PCT: 25VF020B
    //Microchip: SST25VF020B, SST25PF020B
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        0x35,                   //Read Status Register 2 command code
        0x01,                   //Write Status Register command code
        0x50,                   //Enable Write Status Register command code
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        0x20,                   //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        0xAF                    //Auto Address Increment Program command unsupported
    },

    //Cmd Table Index 7:
    //Micron: M25P05, M25P10, M25PE10, M25P20, M25P20S, M25PE20S, M25P40
    {
        0x06,                   //Write Enable command code
        0x04,                   //Write Disable command code
        0x05,                   //Read Status Register 1 command code
        SPICMD_UNSUPPORTED,     //Read Status Register 2 command unsupported
        0x01,                   //Write Status Register command code
        SPICMD_UNSUPPORTED,     //Enable Write Status Register command unsupported
        0x03,                   //Read Data command code
        0x0B,                   //Fast Read Data command code
        0xC7,                   //Chip Erase command code
        SPICMD_UNSUPPORTED,     //Sector Erase 4k bytes command code
        0x02,                   //Program Page command code
        SPICMD_UNSUPPORTED      //Auto Address Increment Program command unsupported
    },
};

#define NUMBER_OF_SPI_COMMAND_TABLES    (sizeof(SUPPORTED_SPI_FLASH_COMMAND_TABLE)/sizeof(SPI_FLASH_COMMAND_TABLE))
#define DEFAULT_SPI_COMMAND_TABLE       0


//
// SPI flash supported list
//
//typedef struct _SPI_FLASH_INDEX {
//    BYTE    FlashID[3];                     //3 bytes spi flash rom product id
//    BYTE    CmdTableIndex;                  //Index of SPI falsh command table
//    WORD    Capacity;                       //Capacity K Bytes
//    //Characters of different SPI flash ROM
//    BYTE    WriteSTSRBytes;                 //1 or 2 bytes when Write Status Register
//    BYTE    EnableWriteSTSR;                //1: Enable Write Status Register command prior to Write Status Register command
//    WORD    PageLength;                     //length of program page command
//    CHAR    ProductString[14];              //Product string
//} SPI_FLASH_INDEX, *PSPI_FLASH_INDEX;
//

const SPI_FLASH_INDEX SUPPORTED_SPI_FLASH_INDEX_TABLE[] = {
    //Index 0 - AMIC: A25L512, A25L512A, A25LS512A
    {
        {0x37, 0x30, 0x10},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', '2', '5', 'L', '5', '1', '2', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 1 - AMIC: A25L010, A25L010A
    {
        {0x37, 0x30, 0x11},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', '2', '5', 'L', '0', '1', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 2 - AMIC: A25L020, A25L020C
    {
        {0x37, 0x30, 0x12},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', '2', '5', 'L', '0', '2', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        6000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 3 - AMIC: A25L040, A25L040A
    {
        {0x37, 0x30, 0x13},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', '2', '5', 'L', '0', '4', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        12000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 4 - ATMEL: AT25F512B, AT25BCM512B
    {
        {0x1F, 0x65, 0x00},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', 'T', '2', '5', 'F', '5', '1', '2', 'B', '\0', 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 5 - EON: EN25F05
    {
        {0x1C, 0x31, 0x10},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'E', 'N', '2', '5', 'F', '0', '5', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 6 - EON: EN25F10, EN25LF10
    {
        {0x1C, 0x31, 0x11},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'E', 'N', '2', '5', 'F', '1', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 7 - EON: EN25F20, EN25LF20
    {
        {0x1C, 0x31, 0x12},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'E', 'N', '2', '5', 'F', '2', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        8000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 8 - EON: EN25F40, EN25LF40, EN25Q40
    {
        {0x1C, 0x31, 0x13},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'E', 'N', '2', '5', 'F', '4', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        20000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 9 - MXIC: MX25L512E, MX25V512E
    {
        {0xC2, 0x20, 0x10},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', 'X', '2', '5', 'L', '5', '1', '2', 'E', '\0', 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 10 - MXIC: MX25L1006E, MX25V1006E, MX25L1026E
    {
        {0xC2, 0x20, 0x11},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', 'X', '2', '5', 'L', '1', '0', '0', '6', 'E', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 11 - MXIC: MX25L2006E, MX25V2006E, MX25L2026E
    {
        {0xC2, 0x20, 0x12},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', 'X', '2', '5', 'L', '2', '0', '0', '6', 'E', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 12 - MXIC: MX25L4006E, MX25V4006E, MX25L4026E
    {
        {0xC2, 0x20, 0x13},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', 'X', '2', '5', 'L', '4', '0', '0', '6', 'E', '\0', 0, 0, 0},     //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 13 - PCT: 25VF512A, Microchip: SST25VF512, SST25VF512A
    {
        {0xBF, 0x48, 0xBF},                                             //3 bytes: flash id
        04,                                                             //1 byte: Cmd Table Index 4
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_SUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        01,                                                             //1 word: 1 byte program
        {'2', '5', 'V', 'F', '5', '1', '2', 'A', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 14 - PCT: 25VF010A, Microchip: SST25VF010A
    {
        {0xBF, 0x49, 0xBF},                                             //3 bytes: flash id
        03,                                                             //1 byte: Cmd Table Index 3
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_SUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        01,                                                             //1 word: 1 byte program
        {'2', '5', 'V', 'F', '0', '1', '0', 'A', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 15 - PCT: 25VF020B, Microchip: SST25VF020B, SST25PF020B
    {
        {0xBF, 0x25, 0x8C},                                             //3 bytes: flash id
        06,                                                             //1 byte: Cmd Table Index 6
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_SUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        01,                                                             //1 word: 1 byte program
        {'2', '5', 'V', 'F', '0', '2', '0', 'B', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 16 - PCT: 25VF040B, Microchip: SST25VF040B, SST25PF040B
    {
        {0xBF, 0x25, 0x8D},                                             //3 bytes: flash id
        03,                                                             //1 byte: Cmd Table Index 3
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_SUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        01,                                                             //1 word: 1 byte program
        {'2', '5', 'V', 'F', '0', '4', '0', 'B', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 17 - PCT: 25VF020, Microchip: SST25VF020, SST25VF020A
    {
        {0xBF, 0x43, 0xBF},                                             //3 bytes: flash id
        04,                                                             //1 byte: Cmd Table Index 4
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_SUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        01,                                                             //1 word: 1 byte program
        {'2', '5', 'V', 'F', '0', '2', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 18 - Pflash: Pm25LD512, Pm25LD512C
    {
        {0x7F, 0x9D, 0x20},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'D', '5', '1', '2', 'C', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 19 - Pflash: Pm25LD010, Pm25LD010C
    {
        {0x7F, 0x9D, 0x21},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'D', '0', '1', '0', 'C', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 20 - Pflash: Pm25LD020, Pm25LD020C
    {
        {0x7F, 0x9D, 0x22},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'D', '0', '2', '0', 'C', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 21 - Pflash: Pm25LV512A
    {
        {0x9D, 0x7B, 0x7F},                                             //3 bytes: flash id
        01,                                                             //1 byte: Cmd Table Index 1
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'V', '5', '1', '2', 'A', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 22 - Pflash: Pm25LV010A
    {
        {0x7F, 0x9D, 0x7C},                                             //3 bytes: flash id
        01,                                                             //1 byte: Cmd Table Index 1
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'V', '0', '1', '0', 'A', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 23 - Pflash: Pm25LV020
    {
        {0x7F, 0x9D, 0x7D},                                             //3 bytes: flash id
        01,                                                             //1 byte: Cmd Table Index 1
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'V', '0', '2', '0', 'A', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 24 - Pflash: Pm25LV040, Pm25LD040, Pm25LD040C
    {
        {0x7F, 0x9D, 0x7E},                                             //3 bytes: flash id
        01,                                                             //1 byte: Cmd Table Index 1
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'P', 'm', '2', '5', 'L', 'V', '0', '4', '0', '\0', 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        1000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 25 - Winbond: W25X05CL
    {
        {0xEF, 0x30, 0x10},                                             //3 bytes: flash id
        02,                                                             //1 byte: Cmd Table Index 2
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: EWSR needs prior to WRSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'W', '2', '5', 'X', '0', '5', 'C', 'L', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 26 - Winbond: W25X10CL, W25X10BV, W25X10BL
    {
        {0xEF, 0x30, 0x11},                                             //3 bytes: flash id
        02,                                                             //1 byte: Cmd Table Index 2
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: EWSR needs prior to WRSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'W', '2', '5', 'X', '1', '0', 'C', 'L', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 27 - Winbond: W25X20CL, W25X20BV, W25X20BL
    {
        {0xEF, 0x30, 0x12},                                             //3 bytes: flash id
        02,                                                             //1 byte: Cmd Table Index 2
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: EWSR needs prior to WRSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'W', '2', '5', 'X', '2', '0', 'C', 'L', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 28 - Winbond: W25X40CL, W25X40BV, W25X40BL
    {
        {0xEF, 0x30, 0x13},                                             //3 bytes: flash id
        02,                                                             //1 byte: Cmd Table Index 2
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                       //1 bytes: EWSR needs prior to WRSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'W', '2', '5', 'X', '4', '0', 'C', 'L', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 29 - Gigadevice: GD25Q512
    {
        {0xC8, 0x40, 0x10},                                             //3 bytes: flash id
        05,                                                             //1 byte: Cmd Table Index 5
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'G', 'D', '2', '5', 'Q', '5', '1', '2', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 30 - Gigadevice: GD25Q10
    {
        {0xC8, 0x40, 0x11},                                             //3 bytes: flash id
        05,                                                             //1 byte: Cmd Table Index 5
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'G', 'D', '2', '5', 'Q', '1', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 31 - Gigadevice: GD25Q20B
    {
        {0xC8, 0x40, 0x12},                                             //3 bytes: flash id
        05,                                                             //1 byte: Cmd Table Index 5
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'G', 'D', '2', '5', 'Q', '2', '0', 'B', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        6000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 32 - Gigadevice: GD25Q40B
    {
        {0xC8, 0x40, 0x13},                                             //3 bytes: flash id
        05,                                                             //1 byte: Cmd Table Index 5
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'G', 'D', '2', '5', 'Q', '4', '0', 'B', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },
    //Index 33 - MXIC: MX25L5121E
    {
        {0xC2, 0x22, 0x10},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH_32,                               //1 word: 32 bytes page
        {'M', 'X', '2', '5', 'L', '5', '1', '2','1', 'E', '\0', 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },
    //Index 34 - MXIC: MX25L1021E
    {
        {0xC2, 0x22, 0x11},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH_32,                               //1 word: 32 bytes page
        {'M', 'X', '2', '5', 'L', '1', '0', '2','1', 'E', '\0', 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 35 - ESMT: F25L05PA
    {
        {0x8C, 0x30, 0x10},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'F', '2', '5', 'L', '0', '5', 'P', 'A','\0', 0, 0, 0, 0, 0},   //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 36 - ESMT: F25L01PA
    {
        {0x8C, 0x30, 0x11},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'F', '2', '5', 'L', '0', '1', 'P', 'A','\0', 0, 0, 0, 0, 0},   //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 37 - ESMT: F25L02PA
    {
        {0x8C, 0x30, 0x12},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'F', '2', '5', 'L', '0', '2', 'P', 'A','\0', 0, 0, 0, 0, 0},   //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 38 - ESMT: F25L04PA
    {
        {0x8C, 0x30, 0x13},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'F', '2', '5', 'L', '0', '4', 'P', 'A','\0', 0, 0, 0, 0, 0},   //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        6000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 39 - Micron: M25P05
    {
        {0x20, 0x20, 0x10},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_64KB,                                                 //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', '0', '5', '\0', 0, 0, 0, 0, 0, 0, 0},      //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        7000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 40 - Micron: M25P10
    {
        {0x20, 0x20, 0x11},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', '1', '0', '\0', 0, 0, 0, 0, 0, 0, 0},      //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        7000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 41 - Micron: M25PE10
    {
        {0x20, 0x80, 0x11},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_128KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', 'E', '1', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 42 - Micron: M25P20, M25P20S
    {
        {0x20, 0x20, 0x12},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', '2', '0', '\0', 0, 0, 0, 0, 0, 0, 0},      //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        7000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 43 - Micron: M25PE20S
    {
        {0x20, 0x80, 0x12},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', 'E', '2', '0', 'S', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 44 - Micron: M25P40
    {
        {0x20, 0x20, 0x13},                                             //3 bytes: flash id
        7,                                                              //1 byte: Cmd Table Index 7
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', '4', '0', '\0', 0, 0, 0, 0, 0, 0, 0},      //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 45 - Micron: M25PE40, M25PE40S
    {
        {0x20, 0x80, 0x13},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'M', '2', '5', 'P', 'E', '4', '0', '\0', 0, 0, 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        10000,                                                          //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 46 - ATMEL: AT25DF021
    {
        {0x1F, 0x43, 0x00},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', 'T', '2', '5', 'D', 'F', '0', '2', '1', '\0', 0, 0, 0, 0},    //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 47 - ATMEL: AT25DF041A
    {
        {0x1F, 0x44, 0x01},                                             //3 bytes: flash id
        0,                                                              //1 byte: Cmd Table Index 0
        SPI_FLASH_512KB,                                                //1 word: capacity K bytes
        0x01,                                                           //1 byte: 1 byte Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'A', 'T', '2', '5', 'D', 'F', '0', '2', '1', 'A', '\0', 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        8000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },

    //Index 48 - Gigadevice: GD25Q21B
    {
        {0xC8, 0x41, 0x12},                                             //3 bytes: flash id
        05,                                                             //1 byte: Cmd Table Index 5
        SPI_FLASH_256KB,                                                //1 word: capacity K bytes
        0x02,                                                           //1 byte: 2 bytes Status Register
        SPI_INDEX_EWSR_UNSUPPORTED,                                     //1 bytes: Don't support SPI EWSR command
        SPI_INDEX_DEFAULT_PAGE_LENGTH,                                  //1 word: 256 bytes page
        {'G', 'D', '2', '5', 'Q', '2', '1', 'B', '\0', 0, 0, 0, 0, 0},  //14 bytes: product string
        1000,                                                           //1 word: Maximum waiting Sector Erase or Page Program time (ms)
        5000,                                                           //1 word: Maximum waiting Chip Erase time (ms)
    },
};

#define NUMBER_OF_SPI_FLASH_INDEXS      (sizeof(SUPPORTED_SPI_FLASH_INDEX_TABLE)/sizeof(SPI_FLASH_INDEX))



/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//      SPI flash command functions
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////////


//
//Procedure:    SpiFlashJedecRDID
//Description:  Use JEDEC RDID (0x9F) command to read 3 bytes flash id
//Input:    SpiControlExtension - SPI controller extension
//          FlashID - ID buffer
//Output:   ASMT_SUCCESS    - command completed
//Note:
//      The Read JEDEC Identification (RDID) instruction allows the 8-bit manufacturer identification to be read,
//  followed by two bytes of device identification.
//      Any Read Identification (RDID) instruction while an Erase or Program cycle is in progress, is not
//  decoded, and has no effect on the cycle that is in progress.
//
int		SpiFlashJedecRDID(struct pci_dev *PciDevice, BYTE *FlashID)
{
    BYTE    SpiFlashCmd;

    SpiFlashCmd = SPICMD_JEDEC_RDID;                    //0x9F

    if(verblevel)
    printk(KERN_INFO "SpiFlashJedecRDID: SPI command code = 0x%02X\n", SpiFlashCmd);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiRead(PciDevice, FlashID, 3);          //Read 3 bytes
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashRDID
//Description:  Use RDID (0xAB) + 3 dummy bytes command to read 3 bytes flash id
//Input:    SpiControlExtension - SPI controller extension
//          FlashID - ID buffer
//Output:   ASMT_SUCCESS    - Command completed
//          FALSE   - others
//Note:
//      Only
//          Pflash: Pm25LV512A,
//          PCT: 25VF512A, 25VF010A, 25VF020,
//          Microchip: SST25VF512, SST25VF512A, SST25VF010A, SST25VF020, SST25VF020A
//      must use this command to get flash ID
//      The dummy bytes for
//          PCT 25VF512A, 25VF010A, 25VF020,
//          Microchip: SST25VF512, SST25VF512A, SST25VF010A, SST25VF020, SST25VF020A
//      must be 0x00
//
int	SpiFlashRDID(struct pci_dev *PciDevice, BYTE *FlashID)
{
    BYTE    SpiFlashCmd[4];

    SpiFlashCmd[0] = SPICMD_RDP;        //0xAB
    SpiFlashCmd[1] = 0;                 //0x00
    SpiFlashCmd[2] = 0;                 //0x00
    SpiFlashCmd[3] = 0;                 //0x00
if(verblevel)
    printk(KERN_INFO "SpiFlashRDID: SPI command code = 0x%02X\n", SpiFlashCmd[0]);


   SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);      //Write 4 bytes
    SpiRead(PciDevice, FlashID, 3);           //Read 3 bytes
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//  DetectSpiFlashRom() must be called and return ASMT_SUCCESS before use following SPI flash functions
//

//
//Procedure:    SpiFlashWREN
//Description:  SPI flash write enable. Write Enable Latch (WEL) bit of Status Register 1 will be set
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - Command completed
//
int	SpiFlashWREN(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->WriteEnableCmd;              //0x06

    if(verblevel)
    printk(KERN_INFO "SpiFlashWREN: SPI command code = 0x%02X\n", SpiFlashCmd);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashWRDI
//Description:  SPI flash write disable. Write Enable Latch (WEL) bit of Status Register 1 will be reset
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//      WEL bit is automatically reset after Power-up and upon completion of the Write Status Register,
//  Page Program, Sector(Block) Erase, and Bulk(Chip) Erase instructions.
//
int	SpiFlashWRDI(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->WritDisableCmd;              //0x04

    if(verblevel)
    printk(KERN_INFO "SpiFlashWRDI: SPI command code = 0x%02X\n", SpiFlashCmd);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashRDSR1
//Description:  Read SPI flash Status Register 1
//Input:    pSpiFlashExtension  - SPI flash extension
//          pStsr1              - SPI flash Status Register 1
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//      The Status Register may be read at any time, even while a Program, Erase or Write Status Register cycle is in progress.
//
int	SpiFlashRDSR1(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, PSPI_FLASH_STSR1 pStsr1)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->ReadSTSR1Cmd;                //0x05

//#ifdef  DEBUG_FLASH_FUNCTION
//    printk(KERN_INFO "SpiFlashRDSR1: SPI command code = 0x%02X\n", SpiFlashCmd);
//#endif  //DEBUG_FLASH_FUNCTION

    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiRead(PciDevice, pStsr1, 1);           //Read 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashRDSR2
//Description:  Read SPI flash Status Register 2
//Input:    pSpiFlashExtension  - SPI flash extension
//          pStsr2              - SPI flash Status Register 2
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//      The Status Register may be read at any time, even while a Program, Erase or Write Status Register cycle is in progress.
//
int	SpiFlashRDSR2(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, PSPI_FLASH_STSR2 pStsr2)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;

    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->ReadSTSR2Cmd;                //0x35

//#ifdef  DEBUG_FLASH_FUNCTION
//    printk(KERN_INFO "SpiFlashRDSR2: SPI command code = 0x%02X\n", SpiFlashCmd);
//#endif  //DEBUG_FLASH_FUNCTION

    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiRead(PciDevice, pStsr2, 1);           //Read 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashEWSR
//Description:  Use EWSR command to enable write Status Register
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - Command completed
//          ASMT_PARAMETER_INVALID   - others
//Note:
//      EWSR command must be prior to WRSR command if SPI flash support EWSR command
//PCT: 25VF512A, 25VF010A, 25VF020B, 25VF040B, 25VF020
//Winbond: W25X05CL, W25X10CL, W25X20CL, W25X40CL
//
int	SpiFlashEWSR(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;

    if(SpiControl->SpiIndex.EnableWriteSTSR == 0)
    {
        return ASMT_PARAMETER_INVALID;
    }


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->EnableWriteSTSRCmd;          //0x50

    if(verblevel)
    printk(KERN_INFO "SpiFlashEWSR: SPI command code = 0x%02X\n", SpiFlashCmd);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashWRSR1
//Description:  Use WRSR command to write Status Register 1
//Input:    pSpiFlashExtension  - SPI flash extension
//          Stsr1               - Status Register 1
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//  Write Enable (WREN) instruction must previously have been executed.
//  EWSR command must be prior to WRSR command if SPI flash support EWSR command
//PCT: 25VF512A, 25VF010A, 25VF020B, 25VF040B, 25VF020
//Winbond: W25X05CL, W25X10CL, W25X20CL, W25X40CL
//
int	SpiFlashWRSR1(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, SPI_FLASH_STSR1 Stsr1)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd[2];

    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd[0] = pCmdTbl->WriteSTSR;                //0x01
    SpiFlashCmd[1] = Stsr1.AsByte;

    if(verblevel) {
        printk(KERN_INFO "SpiFlashWRSR1: SPI command code = 0x%02X, Write 1 byte SSTR\n", SpiFlashCmd[0]);
    }

    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 2);     //Write 2 bytes
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashWRSR2
//Description:  Use WRSR command to write Status Register 1 and 2
//Input:    pSpiFlashExtension  - SPI flash extension
//          Stsr1               - Status Register 1
//          Stsr2               - Status Register 2
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//  Write Enable (WREN) instruction must previously have been executed.
//  EWSR command must be prior to WRSR command if SPI flash support EWSR command
//PCT: 25VF512A, 25VF010A, 25VF020B, 25VF040B, 25VF020
//Winbond: W25X05CL, W25X10CL, W25X20CL, W25X40CL
//
int	SpiFlashWRSR2(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, SPI_FLASH_STSR1 Stsr1, SPI_FLASH_STSR2 Stsr2)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd[3];


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd[0] = pCmdTbl->WriteSTSR;                //0x01
    SpiFlashCmd[1] = Stsr1.AsByte;
    SpiFlashCmd[2] = Stsr2.AsByte;

    if(verblevel)
    printk(KERN_INFO "SpiFlashWRSR2: SPI command code = 0x%02X, Write 2 byte SSTR\n", SpiFlashCmd[0]);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 3);     //Write 3 bytes
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashCE
//Description:  Use Chip Erase command to erase SPI flash
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//      The Chip Erase instruction is executed only if all Block Protect (BP1, BP0) bits are 0.
//
int	SpiFlashCE(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd;


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd = pCmdTbl->ChipEraseCmd;                //0xC7 or 0x60
    if(verblevel)
    printk(KERN_INFO "SpiFlashCE: SPI command code = 0x%02X\n", SpiFlashCmd);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, &SpiFlashCmd, 1);    //Write 1 byte
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashSE
//Description:  Use Sector Erase command to erase one 4k bytes sector
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address             - Specific address
//Output:   ASMT_SUCCESS    - Command completed
//Note:
//      A Sector Erase (SE) instruction applied to a sector which is protected by the Block Protect (BP0, BP1,...) bits is not executed.
//      SPI flash decodes 24 bits only
//
int	SpiFlashSE(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, DWORD Address)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE    SpiFlashCmd[4];


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    SpiFlashCmd[0] = pCmdTbl->SectorEraseCmd;           //0x20 or 0xD7
    SpiFlashCmd[1] = (BYTE)(Address >> 16);             //bit 23:16
    SpiFlashCmd[2] = (BYTE)(Address >> 8);              //bit 15:08
    SpiFlashCmd[3] = (BYTE)Address;                     //bit 07:00

    if(verblevel) {
        printk(KERN_INFO "SpiFlashSE: SPI command code = 0x%02X, Address = 0x%04X\n", SpiFlashCmd[0], Address);
    }


    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);     //Write 4 bytes
    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashPP
//Description:  Use Page Program command to program SPI flash
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address             - Specific address
//          pData               - Data buffer
//          Length              - Programmed data length
//Output:   ASMT_SUCCESS    - Command completed
//          ASMT_PARAMETER_INVALID   - others
//Note:
//      Write Enable (WREN) instruction must previously have been executed.
//      If more than one page are sent to the device, previously latched data are discarded and the last page data bytes
//  are guaranteed to be programmed correctly within the same page.
//      A Page Program (PP) instruction applied to a page which is protected by the Block Protect (BP0, BP1,...) bits is not executed.
//      If page length = 1 and Length > 1, the programmed result is undetermined.
//
int	SpiFlashPP(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, WORD Length)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE *pByte;
    WORD    Outstanding;
    BYTE    SpiFlashCmd[4];
    BYTE    WriteLength;

    if(Length == 0)
    {
        return ASMT_PARAMETER_INVALID;
    }


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    //Write command and address
    SpiFlashCmd[0] = pCmdTbl->PageProgramCmd;           //0x02
    SpiFlashCmd[1] = (BYTE)(Address >> 16);             //bit 23:16
    SpiFlashCmd[2] = (BYTE)(Address >> 8);              //bit 15:08
    SpiFlashCmd[3] = (BYTE)Address;                     //bit 07:00

    if(verblevel)
    printk(KERN_INFO "SpiFlashPP: SPI command code = 0x%02X, Address = 0x%04X, Length = %d\n", SpiFlashCmd[0], Address, Length);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);     //Write 4 bytes

    //Write data
    Outstanding = Length;
    pByte = (BYTE *)pData;
    while(Outstanding > 0)
    {
        WriteLength = (Outstanding >= 4) ? 4 : Outstanding;     //ASM116 LSPI maximum data buffer = 4
        SpiWrite(PciDevice, pByte, WriteLength);
        Outstanding = Outstanding - WriteLength;
        pByte = pByte + WriteLength;
    }

    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashREAD
//Description:  Use READ command to read SPI flash
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address             - Specific address
//          pData               - Data buffer
//          Length              - Read length
//Output:   ASMT_SUCCESS    - Command completed
//          ASMT_PARAMETER_INVALID   - others
int	SpiFlashREAD(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE *pByte;
    int   Outstanding;
    BYTE    SpiFlashCmd[4];
    BYTE    ReadLength;

    if(Length == 0L)
    {
        return ASMT_PARAMETER_INVALID;
    }


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    //Write command and address
    SpiFlashCmd[0] = pCmdTbl->ReadCmd;                  //0x03
    SpiFlashCmd[1] = (BYTE)(Address >> 16);             //bit 23:16
    SpiFlashCmd[2] = (BYTE)(Address >> 8);              //bit 15:08
    SpiFlashCmd[3] = (BYTE)Address;                     //bit 07:00

    if(verblevel)
    printk(KERN_INFO "SpiFlashREAD: SPI command code = 0x%02X, Address = 0x%04X, Length = %X\n", SpiFlashCmd[0], Address, Length);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);     //Write 4 bytes

    //Read data
    Outstanding = Length;
    pByte = (BYTE *)pData;
    while(Outstanding > 0)
    {
        //ReadLength = (Outstanding >= 4) ? 4 : Outstanding;      //ASM116 LSPI maximum data buffer = 4
        if(Outstanding >= 4)
        {
            ReadLength = 4;
        }else
        {
            ReadLength = (BYTE)Outstanding;
        }

		SpiRead(PciDevice, pByte, ReadLength);
        Outstanding = Outstanding - ReadLength;
        pByte = pByte + ReadLength;
    }

    SpiTerminate(PciDevice);
    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashFREAD
//Description:  Use FAST READ command to read SPI flash
//Input:    pSpiFlashExtension  - SPI flash extension
//          pData               - Data buffer
//          Address             - Specific address
//          Length              - Read length
//Output:   ASMT_SUCCESS    - Command completed
//          ASMT_PARAMETER_INVALID   - others
int	SpiFlashFREAD(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE *pByte;
    int   Outstanding;
    BYTE    SpiFlashCmd[4];
    BYTE    ReadLength;
    BYTE    Dummy;

    if(Length == 0L)
    {
        return ASMT_PARAMETER_INVALID;
    }


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    //Write command and address
    SpiFlashCmd[0] = pCmdTbl->FastReadCmd;              //0x0B
    SpiFlashCmd[1] = (BYTE)(Address >> 16);             //bit 23:16
    SpiFlashCmd[2] = (BYTE)(Address >> 8);              //bit 15:08
    SpiFlashCmd[3] = (BYTE)Address;                     //bit 07:00

    if(verblevel)
    printk(KERN_INFO "SpiFlashFREAD: SPI command code = 0x%02X, Address = 0x%04X, Length = %d\n", SpiFlashCmd[0], Address, Length);


    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);     //Write 4 bytes

    //Write dummy byte
    Dummy = 0;
    SpiWrite(PciDevice, &Dummy, 1);          //Write 1 dummy byte

    //Read data
    Outstanding = Length;
    pByte = (BYTE *)pData;
    while(Outstanding > 0)
    {
        //ReadLength = (Outstanding >= 4) ? 4 : Outstanding;      //ASM116 LSPI maximum data buffer = 4
        if(Outstanding >= 4)
        {
            ReadLength = 4;
        }else
        {
            ReadLength = (BYTE)Outstanding;
        }

		SpiRead(PciDevice, pByte, ReadLength);
        Outstanding = Outstanding - ReadLength;
        pByte = pByte + ReadLength;
    }

    SpiTerminate(PciDevice);

    if(verblevel)
    printk(KERN_INFO "SpiFlashFREAD: Fast Read completed\n");


    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashWaitWriteEnabled
//Description:  Wait Status Register 1 WEL bit is set
//Input:    pSpiFlashExtension  - SPI flash extension
//          WaitingTime         - Waiting time (ms)
//Output:   ASMT_SUCCESS    - WEL bit is set
//          ASMT_TIMEOUT   - time out
int	SpiFlashWaitWriteEnabled(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, WORD WaitingTime)
{
    SPI_FLASH_STSR1 Stsr1;
    int         BoolTimeout;
    clock_t         ticks1, ticks2;

    SpiFlashRDSR1(PciDevice,SpiControl, (PSPI_FLASH_STSR1)&Stsr1);
    ticks1 = jiffies;
    ticks2 = ticks1;
    BoolTimeout = ASMT_SUCCESS;
    while( Stsr1.WEL == 0 )
    {
        SpiFlashRDSR1(PciDevice,SpiControl, (PSPI_FLASH_STSR1)&Stsr1);
        ticks2 = jiffies;
        if( (((ticks2 - ticks1)*1000)/ CLOCKS_PER_SEC ) > WaitingTime )
        {
            BoolTimeout = ASMT_TIMEOUT;
            break;
        }
    }

    if(BoolTimeout == ASMT_TIMEOUT)
    {
        if (verblevel)
        printk(KERN_INFO " SpiFlashWaitWriteEnabled WaitWriteEnabled: Timeout!!!\n");
    }


    return BoolTimeout ;
}


//
//Procedure:    SpiFlashWaitWriteCompleted
//Description:  Wait Status Register 1 WIP bit is reset
//Input:    pSpiFlashExtension  - SPI flash extension
//          WaitingTime         - Waiting time (ms)
//Output:   ASMT_SUCCESS    - WEL bit is set
//          ASMT_TIMEOUT   - time out
int	SpiFlashWaitWriteCompleted(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, WORD WaitingTime)
{
    SPI_FLASH_STSR1 Stsr1;
    int         BoolTimeout;
    clock_t         ticks1, ticks2;

    SpiFlashRDSR1(PciDevice,SpiControl, (PSPI_FLASH_STSR1)&Stsr1);
    ticks1 = jiffies;
    ticks2 = ticks1;
    BoolTimeout = ASMT_SUCCESS;
    while(  Stsr1.WIP == 1 )
    {
        SpiFlashRDSR1(PciDevice,SpiControl, (PSPI_FLASH_STSR1)&Stsr1);
        ticks2 = jiffies;
        if( (((ticks2 - ticks1)*1000)/ CLOCKS_PER_SEC )> WaitingTime )
        {
            BoolTimeout = ASMT_TIMEOUT;
            break;
        }
    }

    if(BoolTimeout == ASMT_TIMEOUT)
    {
        if(verblevel)
        printk(KERN_INFO "SpiFlashWaitWriteCompleted WaitWriteCompleted: Timeout!!!\n");
    }


    return BoolTimeout ;
}


//
//Procedure:    SpiFlashAAIP
//Description:  Auto Address Increment (AAI) Program for PCI SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address             - Specific address
//          pData               - Data buffer
//          Length              - Programmed data length
//Output:   ASMT_SUCCESS    - Command completed
//          ASMT_IO_ERROR   - others
//Note:
//      PCT: 25VF512A, 25VF010A, 25VF020B, 25VF040B, 25VF020
//      Microchip:
//      Please refer to PCI SPI flash ROM datasheet
//
int	SpiFlashAAIP(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, int Length)
{

    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    BYTE *pByte;
    int   Outstanding;
    BYTE    SpiFlashCmd[4];
    int ret = ASMT_SUCCESS;
    if(Length == 0L)
    {
        return ASMT_IO_ERROR;
    }


    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];

    if(pCmdTbl->AutoAddressIncProCmd == SPICMD_UNSUPPORTED)
    {
        return ASMT_IO_ERROR;
    }

    Outstanding = Length;
    pByte = (BYTE *)pData;

    //Write Enable
    SpiFlashWREN(PciDevice,SpiControl);
    SpiFlashWaitWriteEnabled(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME);


    //Write command and address
    SpiFlashCmd[0] = pCmdTbl->AutoAddressIncProCmd;     //0xAF
    SpiFlashCmd[1] = (BYTE)(Address >> 16);             //bit 23:16
    SpiFlashCmd[2] = (BYTE)(Address >> 8);              //bit 15:08
    SpiFlashCmd[3] = (BYTE)Address;                     //bit 07:00

    if (verblevel) {
        printk(KERN_INFO "SpiFlashAAIP: SPI command code = 0x%02X, Address = 0x%04X, Length = %d\n", SpiFlashCmd[0], Address, Length);
    }

    SpiStart(PciDevice);
    SpiWrite(PciDevice, SpiFlashCmd, 4);     //Write 4 bytes command and address
    SpiWrite(PciDevice, pByte, 1);           //Write 1 byte data
    SpiTerminate(PciDevice);

    //Wait WIP reset
    ret = SpiFlashWaitWriteCompleted(PciDevice, SpiControl, SpiControl->SpiIndex.MaxCETime);

    Outstanding--;
    pByte++;

    //Write other data
    while(Outstanding > 0)
    {
        SpiFlashCmd[0] = pCmdTbl->AutoAddressIncProCmd;     //0xAF
        SpiFlashCmd[1] = *pByte;
        SpiStart(PciDevice);
        SpiWrite(PciDevice, SpiFlashCmd, 2);     //Write command and 1 byte data
        SpiTerminate(PciDevice);
        SpiFlashWaitWriteCompleted(PciDevice, SpiControl, SpiControl->SpiIndex.MaxCETime);
        Outstanding--;
        pByte++;
    }

    //Write Disable
    SpiFlashWRDI(PciDevice, SpiControl);
    return (SpiFlashWaitWriteCompleted(PciDevice, SpiControl, SpiControl->SpiIndex.MaxCETime));

   // return ASMT_SUCCESS;
}


//
// Exported Functions
//

//
//Procedure:    SpiFlashDetectSpiFlashRom
//Description:  Detect SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - SPI flash ROM found and SPI_FLASH_EXTENSION structure is returned
//          ASMT_IO_ERROR   - SPI flash ROM is not found
//Note:
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashDetectSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{

    PSPI_FLASH_INDEX        pSpiIdxDst;
    PSPI_FLASH_INDEX        pSpiIdxSrc;
    BYTE *FlashID;
    int                 BoolFound;
    int                     i;



    pSpiIdxDst = (PSPI_FLASH_INDEX)&SpiControl->SpiIndex;
    FlashID = pSpiIdxDst->FlashID;
    //SPI JEDEC RDID
    SpiFlashJedecRDID(PciDevice, FlashID);
    if( ( (FlashID[0] == 0xFF) && (FlashID[1] == 0xFF) && (FlashID[2] == 0xFF) ) ||
        ( (FlashID[0] == 0x00) && (FlashID[1] == 0x00) && (FlashID[2] == 0x00) ) )
    {
        //SPI RDID
        SpiFlashRDID(PciDevice, FlashID);
    }
    if( ( (FlashID[0] == 0xFF) && (FlashID[1] == 0xFF) && (FlashID[2] == 0xFF) ) ||
        ( (FlashID[0] == 0x00) && (FlashID[1] == 0x00) && (FlashID[2] == 0x00) ) )
    {

        if(verblevel){
            printk(KERN_INFO "\nSpiFlashDetectSpiFlashRom: SPI flash ROM can't be detected!!!\n");
        }

        return ASMT_IO_ERROR;   //SPI flash ROM is not detected
    }

    //SPI flash ROM found
    //Search supported SPI flash table
    BoolFound = ASMT_UNMATCH;
    i = 0;
    while( (i < NUMBER_OF_SPI_FLASH_INDEXS) && (BoolFound == ASMT_UNMATCH) )
    {
        pSpiIdxSrc = (PSPI_FLASH_INDEX)&SUPPORTED_SPI_FLASH_INDEX_TABLE[i];
        if( (FlashID[0] == pSpiIdxSrc->FlashID[0]) && (FlashID[1] == pSpiIdxSrc->FlashID[1]) && (FlashID[2] == pSpiIdxSrc->FlashID[2]) )
        {
            BoolFound = ASMT_SUCCESS;

             if ( verblevel )
            printk(KERN_INFO "SPI ID[%X %X %X]CmdTableIndex[%X]\n",pSpiIdxSrc->FlashID[0],pSpiIdxSrc->FlashID[1],pSpiIdxSrc->FlashID[2],pSpiIdxSrc->CmdTableIndex);
        }
        i++;
    }

    if(BoolFound == ASMT_SUCCESS)
    {
        //Found in supported index table
        //pSpiIdxSrc points to the found SPI_FLASH_INDEX structure
        memcpy(pSpiIdxDst, pSpiIdxSrc, sizeof(SPI_FLASH_INDEX));
        SpiControl->InSupportedList = ASMT_SUCCESS;
    }
    else
    {
        //Found a SPI flash ROM that is not in supported index table
        pSpiIdxDst->CmdTableIndex = SPI_INDEX_DEFAULT_COMMAND_TABLE;
        pSpiIdxDst->Capacity = SPI_INDEX_UNKNOWN_CAPACITY;
        pSpiIdxDst->WriteSTSRBytes = SPI_INDEX_DEFAULT_WRSR_BYTE;
        pSpiIdxDst->EnableWriteSTSR = SPI_INDEX_EWSR_UNSUPPORTED;
        pSpiIdxDst->PageLength = SPI_INDEX_DEFAULT_PAGE_LENGTH;
        memcpy(pSpiIdxDst->ProductString, "Unknown", 7);
        pSpiIdxDst->ProductString[7] = '\0';
        pSpiIdxDst->MaxSEPPTime = SPI_INDEX_DEFAULT_MAX_SE_PP_TIME;
        pSpiIdxDst->MaxCETime = SPI_INDEX_DEFAULT_MAX_CE_TIME;

        SpiControl->InSupportedList = ASMT_IO_ERROR;
    }

    SpiControl->Spi3WMEnabled = SpiIs3WireMode(PciDevice);

    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashReadData
//Description:  Read data from SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          pData               - data buffer
//          Address             - SPI flash start address
//          Length              - read length
//Output:   ASMT_SUCCESS    - data read
//          ASMT_PARAMETER_INVALID   - data is not read
//Note:
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashReadData(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, DWORD Length)
{
    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;

    if(Length == 0L)
    {
        return ASMT_PARAMETER_INVALID;
    }

    pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex];
    if(pCmdTbl->FastReadCmd != SPICMD_UNSUPPORTED)
    {

        if(verblevel)
        printk(KERN_INFO "SpiFlashReadData: SPI Fast Read \n");


        //Fast Read
        //SpiFlashFREAD(pSpiFlashExtension, pData, Address, Length);
        // Read
        SpiFlashREAD(PciDevice,SpiControl,pData,Address,Length);      //use READ command only for116 legacy SPI control
    }
    else
    {

        if(verblevel) {
            printk(KERN_INFO "SpiFlashReadData: SPI Read\n");
        }

        //Read
        SpiFlashREAD(PciDevice,SpiControl, pData, Address, Length);
    }

    return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashUnprotectBlocks
//Description:  Clear Block Protect bits in Status Register
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - BP bits cleared
//          ASMT_TIMEOUT
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashUnprotectBlocks(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl)
{
    PSPI_FLASH_INDEX        pSpiIdx;
    SPI_FLASH_STSR1         Stsr1;
    SPI_FLASH_STSR2         Stsr2;

    pSpiIdx = (PSPI_FLASH_INDEX)&SpiControl->SpiIndex;
    Stsr1.AsByte = 0;
    Stsr2.AsByte = 0;
    if(pSpiIdx->EnableWriteSTSR == SPI_INDEX_EWSR_UNSUPPORTED)
    {

        if(verblevel)
        printk(KERN_INFO "SpiFlashUnprotectBlocks: SPI flash does not support EWSR command.\n");


        SpiFlashWREN(PciDevice,SpiControl);
        SpiFlashWaitWriteEnabled(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME);
        switch(pSpiIdx->WriteSTSRBytes)
        {
            case 1:
                SpiFlashWRSR1(PciDevice,SpiControl,Stsr1);
                break;
            case 2:
                SpiFlashWRSR2(PciDevice,SpiControl,Stsr1, Stsr2);
                break;
            default:
                SpiFlashWRSR1(PciDevice,SpiControl, Stsr1);
                break;
        }
       return( SpiFlashWaitWriteCompleted(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME));
    }
    else
    {
        if(verblevel)
        printk(KERN_INFO "SpiFlashUnprotectBlocks: EWSR command first.\n");

        SpiFlashEWSR(PciDevice,SpiControl);
        switch(pSpiIdx->WriteSTSRBytes)
        {
            case 1:
                SpiFlashWRSR1(PciDevice,SpiControl, Stsr1);
                break;
            case 2:
                SpiFlashWRSR2(PciDevice,SpiControl, Stsr1, Stsr2);
                break;
            default:
                SpiFlashWRSR1(PciDevice,SpiControl, Stsr1);
                break;
        }
      return  (SpiFlashWaitWriteCompleted(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME));
    }

    //return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashChipErase
//Description:  Erase all content of SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//Output:   ASMT_SUCCESS    - Erased
//          ASMT_TIMEOUT   - others
//Note:
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashChipErase(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{

    SpiFlashWREN(PciDevice,SpiControl);
    SpiFlashWaitWriteEnabled(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME);
    SpiFlashCE(PciDevice, SpiControl);
   return (SpiFlashWaitWriteCompleted(PciDevice,SpiControl, SpiControl->SpiIndex.MaxCETime));

    //return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashSectorErase
//Description:  Erase 4k bytes sector of SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address             - SPI flash sector address
//Output:   ASMT_SUCCESS    - Erased
//          ASMT_TIMEOUT   - others
//Note:
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashSectorErase(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, DWORD Address)
{

    SpiFlashWREN(PciDevice,SpiControl);
    SpiFlashWaitWriteEnabled(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME);
    SpiFlashSE(PciDevice,SpiControl, Address);
   return  (SpiFlashWaitWriteCompleted(PciDevice,SpiControl, SpiControl->SpiIndex.MaxSEPPTime));

    //return ASMT_SUCCESS;
}


//
//Procedure:    SpiFlashWriteData
//Description:  Write data into SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          pData               - data buffer
//          Address             - SPI flash start address
//          Length              - written length
//Output:   ASMT_SUCCESS    - data written
//          ASMT_PARAMETER_INVALID   - others
//Note:
//      SPI control Grant shall be gotten by caller
//
int	SpiFlashWriteData(struct pci_dev *PciDevice, PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, DWORD Length)
{
    BYTE *pByte;
    int   Outstanding;
    DWORD   ProgramAddress;
    WORD    ProgramLength;
    WORD    PageLength;

    if(Length == 0L)
    {
        return ASMT_PARAMETER_INVALID;
    }

    pByte = (BYTE *)pData;
    Outstanding = Length;
    ProgramAddress = Address;
    PageLength = SpiControl->SpiIndex.PageLength;

    if(verblevel)
        printk(KERN_INFO "SpiFlashWriteData: SPI flash Page length = %d\n", PageLength);


    while(Outstanding > 0)
    {
        if( (ProgramAddress % PageLength) == 0 )
        {
            //Program address is the multiple of SPI flash ROM Page Length
            //ProgramLength = (Outstanding >= PageLength) ? PageLength : Outstanding;
			if(Outstanding >= PageLength)
			{
				ProgramLength = PageLength;
			}else
			{
				ProgramLength = (WORD)Outstanding;
			}
        }
        else
        {
            //Program address is not the multiple of Page Length
            //Calculate byte count to next SPI flash ROM Page start address
            ProgramLength = PageLength - (ProgramAddress % PageLength);
        }
        SpiFlashWREN(PciDevice,SpiControl);
        SpiFlashWaitWriteEnabled(PciDevice,SpiControl, SPI_FLASH_DEFAULT_WAIT_TIME);
        SpiFlashPP(PciDevice,SpiControl, pByte, ProgramAddress, ProgramLength);
        SpiFlashWaitWriteCompleted(PciDevice, SpiControl, SpiControl->SpiIndex.MaxSEPPTime);
        Outstanding = Outstanding - ProgramLength;
        ProgramAddress = ProgramAddress + ProgramLength;
        pByte = pByte + ProgramLength;
    }

    return ASMT_SUCCESS;
}



/*
void SpiTestTable()
{
    PSPI_FLASH_COMMAND_TABLE    pCmdTbl;
    PSPI_FLASH_INDEX            pSpiIdx;
    UINTN                       i;

    printk(KERN_INFO "\nNumber of flash command tables = %d\n", NUMBER_OF_SPI_COMMAND_TABLES);
    printk(KERN_INFO "\nNumber of flash Index tables = %d\n", NUMBER_OF_SPI_FLASH_INDEXS);
    getch();
    for(i = 0; i < NUMBER_OF_SPI_FLASH_INDEXS; i++)
    {
        pSpiIdx = (PSPI_FLASH_INDEX)&SUPPORTED_SPI_FLASH_INDEX_TABLE[i];
        pCmdTbl = (PSPI_FLASH_COMMAND_TABLE)&SUPPORTED_SPI_FLASH_COMMAND_TABLE[pSpiIdx->CmdTableIndex];
        printk(KERN_INFO "\n");
        printk(KERN_INFO "Index %d:\n", i);
        printk(KERN_INFO "SPI falsh ID = 0x%02X, 0x%02X, 0x%02X\n", pSpiIdx->FlashID[0], pSpiIdx->FlashID[1], pSpiIdx->FlashID[2]);
        printk(KERN_INFO "Use Flash command table %d\n", pSpiIdx->CmdTableIndex);
        printk(KERN_INFO "Flash capacity = %dk\n", pSpiIdx->Capacity);
        printk(KERN_INFO "Write Status Register command bytes = %d\n", pSpiIdx->WriteSTSRBytes);
        printk(KERN_INFO "Use EWSR prior to WRSR command = %d\n", pSpiIdx->EnableWriteSTSR);
        printk(KERN_INFO "Page length = %d\n", pSpiIdx->PageLength);
        printk(KERN_INFO "Product string = %s\n", pSpiIdx->ProductString);
        printk(KERN_INFO "Maximun SE PP time = %d ms\n", pSpiIdx->MaxSEPPTime);
        printk(KERN_INFO "Maximun CE time = %d ms\n", pSpiIdx->MaxCETime);
        getch();

        printk(KERN_INFO "\n");
        printk(KERN_INFO "Command table: %d\n", pSpiIdx->CmdTableIndex);
        printk(KERN_INFO "WriteEnableCmd          = 0x%02X\n", pCmdTbl->WriteEnableCmd);
        printk(KERN_INFO "WritDisableCmd          = 0x%02X\n", pCmdTbl->WritDisableCmd);
        printk(KERN_INFO "ReadSTSR1Cmd            = 0x%02X\n", pCmdTbl->ReadSTSR1Cmd);
        printk(KERN_INFO "ReadSTSR2Cmd            = 0x%02X\n", pCmdTbl->ReadSTSR2Cmd);
        printk(KERN_INFO "WriteSTSR               = 0x%02X\n", pCmdTbl->WriteSTSR);
        printk(KERN_INFO "EnableWriteSTSRCmd      = 0x%02X\n", pCmdTbl->EnableWriteSTSRCmd);
        printk(KERN_INFO "ReadCmd                 = 0x%02X\n", pCmdTbl->ReadCmd);
        printk(KERN_INFO "FastReadCmd             = 0x%02X\n", pCmdTbl->FastReadCmd);
        printk(KERN_INFO "ChipEraseCmd            = 0x%02X\n", pCmdTbl->ChipEraseCmd);
        printk(KERN_INFO "SectorEraseCmd          = 0x%02X\n", pCmdTbl->SectorEraseCmd);
        printk(KERN_INFO "PageProgramCmd          = 0x%02X\n", pCmdTbl->PageProgramCmd);
        printk(KERN_INFO "AutoAddressIncProCmd    = 0x%02X\n", pCmdTbl->AutoAddressIncProCmd);
        getch();
    }
}
*/


