/**************************************************************************************
*Filename: spifile.h
*Description:
*   Definitions Data structures of ASM116 SPI ROM file
*   Refer to "Asmedia116 SPI ROM Format and Control" document
*
*History:
*   2013/08/19  V3.0    Jesse Chang     Declare const for gASM116GUID
*                                       Add Customer Configuration Table structure
*                                       Delete following definitions:
*                                           ASM116_MAXIMUM_LEGACY_OPROM_LENGTH
*                                           ASM116_FIRST_FIRMWARE_STARTING_ADDRESS
*                                           ASM116_SECOND_FIRMWARE_STARTING_ADDRESS
*                                           ASM116_MAXIMUM_FIRMWARE_BINARY_LENGTH
*   2013/04/09  V2.1    Jesse Chang     Add "Length of SPI ROM File" field in Identifier
*   2013/04/02  V1.1    Jesse Chang     Add definitions of Identifier of SPI ROM file
*   2013/03/25  V1.0    Jesse Chang     First version
*
*
* Asmedia ASM116x Firmware Update Tool
*
* Copyright (C) 2014-2016 ASMedia Technology
*
***************************************************************************************/

#ifndef _SPIFILE_H
#define _SPIFILE_H

#define ASM116_GUID_LENGTH                16                  //16-bytes GUID

//
//ASM116 SPI ROM capacity
//
#define SPIROM_CAPACITY_64K         (64*1024)          //512M bits SPI ROM
#define SPIROM_CAPACITY_128K        (128*1024)         //1M bits SPI ROM
#define SPIROM_CAPACITY_256K        (256*1024)         //2M bits SPI ROM
#define SPIROM_CAPACITY_512K        (512*1024)         //4M bits SPI ROM

#define ASM116_MAXIMUM_SPIROM_CAPACITY    SPIROM_CAPACITY_512K


//
//ASM116 Register Table, little endian format
//
//#define ASM116_REGISTER_TABLE_LENGTH      256             //Length of register table is 256 bytes
#pragma pack(1)
typedef struct _REGISTER_PAIR_ {
    WORD    RegData;                                        //Updated data, little endian
    WORD    RegOffset;                                      //ASM116 MMIO register offset, little endian
} REGPAIR, *PREGPAIR;

typedef struct _REGISTER_TABLE_ {
    DWORD       FirmwareStartAddress;                       //The first DWORD is firmware starting address, little endian
    REGPAIR     RegPair[63];                                //Maximun 63 register-data pairs
} REGTBL, *PREGTBL;
#pragma pack()

#define ASM116_REGISTER_TABLE_STARTING_ADDRESS    0x00000000
#define ASM116_REGISTER_TABLE_FW_NOT_EXIST        0xFFFFFFFC         //FirmwareStartAddress = 0xFFFFFFFC if firmware does not exist
#define ASM116_REGISTER_TABLE_ENDTAG              0x55AA             //Register and Data are 0x55AA
#define ASM116_REGISTER_TABLE_LENGTH              sizeof(REGTBL)      //Length of register table is 256 bytes


//
//ASM116 firmware verification table, big endian format
//
//#define ASM116_VERIFICATION_TABLE_LENGTH  256                 //Length of verification table is 256 bytes
//GUID = { 0xF6069CD3, 0x6844, 0x4BB4, { 0x8A, 0xC8, 0x5A, 0x07, 0xD6, 0xD7, 0xCA, 0x1B } }
#pragma pack(1)
typedef struct _VERIFICATION_DATA_ {                        //48 bytes verification data
    BYTE    Signature[4];                                   //4 bytes signature ASCII character
    BYTE    GUID[16];                                       //16 bytes GUID
    DWORD   BinaryAddress;                                  //4 bytes big endian firmware binary starting address
    DWORD   BinaryLength;                                   //4 bytes big endian firmware binary length
    DWORD   BinaryCrc32;                                    //4 bytes big endian firmware binary CRC32
    BYTE     Version[8];                                        //8 bytes firmware version. Reserved now
    DWORD   Generation;                                     //4 bytes big endian firmware generation. 0x00000001 now
    BYTE    Rev1[3];                                        //3 bytes reserved for double-word aligment
    BYTE    Checksum;                                       //1 byte checksum = sum of (Signature to Reserved)
} VERIFYDATA, *PVERIFYDATA;

typedef struct _VERIFICATION_TABLE_ {                       //256 bytes verification table
    VERIFYDATA  VerifycationData1;                          //The first firmware verification data
    BYTE        Rev0[80];                                   //reserved 80 bytes
    VERIFYDATA  VerifycationData2;                          //The second firmware verification data
    BYTE        Rev1[80];                                   //reserved 80 bytes
} VERIFYTBL, *PVERIFYTBL;
#pragma pack()

#define ASM116_VERIFICATION_TABLE_STARTING_ADDRESS    0x00000100
#define VERIFICTION_SIGNATURE_LENGTH                    4                   //Signature is 4-bytes ASCII characters
#define VERIFICTION_SIGNATURE                           "ASMT"              //Signature is 4-bytes ASCII characters
#define ASM116_VERIFICATION_TABLE_LENGTH              sizeof(VERIFYTBL)   //Length of verification table is 256 bytes


//
//ASM116 Identifier of SPI ROM File, little endian format
//
#pragma pack(1)
typedef struct _IDENTIFIER_DATA_ {                          //16 bytes
    DWORD   Address;                                        //4 bytes Address
    DWORD   Length;                                         //4 bytes Length
    DWORD   Crc32;                                          //4 bytes CRC32
    DWORD   Reserved;                                       //4 bytes Reserved
} IDENTIFIERDATA, *PIDENTIFIERDATA;

typedef struct _SPIFILE_IDENTIFIER_ {                       //256 bytes Identifier
    BYTE            GUID[16];                               //16 bytes GUID
    BYTE            Rev0[16];                               //16 bytes reserved for register table
    IDENTIFIERDATA  OpromImage;                             //16 bytes OPROM image identifier
    IDENTIFIERDATA  VerificationTable;                      //16 bytes verification table identifier
    IDENTIFIERDATA  FirstFirmwareBinary;                    //16 bytes first firmware binary identifier
    IDENTIFIERDATA  SecondFirmwareBinary;                   //16 bytes second firmware binary identifier
    BYTE            Rev1[148];                              //148 bytes reserved
    DWORD           FileLength;                             //4 bytes length of SPI ROM file
    DWORD           CustomerID;                             //4 bytes customer ID
    BYTE            Rev2[3];                                //3 bytes reserved
    BYTE            Checksum;                               //1 byte checksum
} SPIFILEIDENTIFIER, *PSPIFILEIDENTIFIER;
#pragma pack()

#define ASM116_SPI_FILE_IDENTIFIER_STARTING_ADDRESS       0x00000200
#define ASM116_SPI_FILE_IDENTIFIER_LENGTH                 sizeof(SPIFILEIDENTIFIER)   //Length of Identifier is 256 bytes


//
//Customer Configuration Table
//
//GUID = { 0xF6069CD3, 0x6844, 0x4BB4, { 0x8A, 0xC8, 0x5A, 0x07, 0xD6, 0xD7, 0xCA, 0x1B } }
#pragma pack(1)
typedef struct _CONFIG_TABLE_ {
    BYTE            GUID[16];                               //0x00-0xFF: 16 bytes GUID
    BYTE            Rev10[4];                               //0x10-0x13: 4 bytes Reserved
    BYTE            AtaSerialNumber[20];                    //0x14-0x27: 20 bytes ATA Serial Number WORD 0~9
    BYTE            AtaModelNumber[40];                     //0x28-0x4F: 40 bytes ATA Model Number WORD 0~19
    BYTE            Rev50[171];                             //0x50-0xFA: 171 bytes Reserved
    BYTE            StandbyTimer;                           //0xFB     : 1 byte Standby Timer
    BYTE            Raid0StripeSize;                        //0xFC     : 1 byte RAID 0 Stripe Size
    BYTE            RevFD;                                  //0xFD     : 1 byte Reserved
    BYTE            Signature;                              //0xFE     : 1 byte signature
    BYTE            Checksum;                               //0xFF     : 1 byte checksum
} CONFIGTBL, *PCONFIGTBL;
#pragma pack()

#define ASM116_CONFIG_TABLE_STARTING_ADDRESS      0x00000300
#define ASM116_CONFIG_TABLE_SIGNATURE             0x5A
#define ASM116_CONFIG_TABLE_LENGTH                sizeof(CONFIGTBL)   //Length of record table is 256 bytes


//
//Firmware record table
//
#pragma pack(1)
typedef struct _RECORD_TABLE_ {
    BYTE    Data[4*1024];
} RECORDTBL, *PRECORDTBL;
#pragma pack()

#define ASM116_RECORD_TABLE_STARTING_ADDRESS      0x00001000
#define ASM116_RECORD_TABLE_LENGTH                sizeof(RECORDTBL)   //Length of record table is 4K bytes


//
//OPROM image region
//
#define ASM116_OPROM_STARTING_ADDRESS             0x00002000		  //OPROM image is starting address 0x002000 (8K)


//
//Firmware binary region
//
//#define ASM116_FIRST_FIRMWARE_STARTING_ADDRESS    0x00010000L     //The first firmware starting address is 0x010000 (64K)
//#define ASM116_SECOND_FIRMWARE_STARTING_ADDRESS   0x00040000L     //The first firmware starting address is 0x040000 (256K)
//#define ASM116_MAXIMUM_FIRMWARE_BINARY_LENGTH     (64*1024L)      //Maximum firmware binary length is limited to 64K bytes

//
//SPI ROM file header
//
#pragma pack(1)
typedef struct _SPIROM_HEADER_ {            //8K bytes SPI ROM header
    REGTBL              RegisterTable;              //0x000000 ~ 0x0000FF: Register table
    VERIFYTBL           VerificationTable;          //0x000100 ~ 0x0001FF: Verification table
    SPIFILEIDENTIFIER   SpiFileIdentifier;          //0x000200 ~ 0x0002FF: Identifier of SPI ROM file
    CONFIGTBL           ConfigTable;                //0x000300 ~ 0x0003FF: Customer configuration table
    BYTE                Rev[3072];                  //0x000400 ~ 0x000FFF: Reserved
    RECORDTBL           RecordTable;                //0x001000 ~ 0x001FFF: Record table
} SPIROMHEADER, *PSPIROMHEADER;
#pragma pack()

#define ASM116_SPIROM_HEADER_LENGTH        sizeof(SPIROMHEADER)

#endif  //_SPIFILE_H

