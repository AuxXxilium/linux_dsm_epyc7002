/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */

#include "precomp.h"
#include "crc.h"
//#include <string.h>

#define VERSION "V1.0"
const BYTE  gASM116GUID[ASM116_GUID_LENGTH] = {0x2C, 0x47, 0x67, 0x21, 0xF2, 0x73, 0x90, 0x47, 0x88, 0xF4, 0x26, 0x0C, 0xB1, 0x3E, 0x3E, 0x98};
struct pci_dev *cur_dev;


struct pci_dev *Selected_pci[MAX_DEVICE_CNT]= {};
void debugTypeSize(void);

int ASM116SpiGetControlGrant(struct pci_dev *PciDevice);
void ASM116SpiReleaseControlGrant(struct pci_dev *PciDevice);
int ASM116ReadSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void *pData, DWORD Address, DWORD Length);
int ASM116EraseSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl);
int ASM116BlankCheckSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, DWORD Address, DWORD BlankCheckLength);
int ASM116UpdateSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void * pSpirom, DWORD SpiromFileLength);
int ASM116VerifySpiRomFile(void * pSpirom, DWORD SpiromFileLength);
void ASM116GetOPROMandFWver(void * pSpirom, DWORD SpiromFileLength);
int verblevel = 0;
EXPORT_SYMBOL(verblevel);
unsigned char *pMemPtr = NULL;
EXPORT_SYMBOL(pMemPtr);
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
unsigned int uiSelectedASM116x = 0;
static struct kobject *asm116XSPIObject = NULL;
#define SZ_SPI_NAME_LENGTH 128

void debugTypeSize(void)
{
    printk(KERN_INFO "DWORD--%x \n",(unsigned int)sizeof(DWORD));
    printk(KERN_INFO "WORD--%x \n",(unsigned int)sizeof(WORD));
    printk(KERN_INFO "BYTE--%x \n",(unsigned int)sizeof(BYTE));
    printk(KERN_INFO "int--%x \n",(unsigned int)sizeof(int));
    printk(KERN_INFO "LONG--%x \n",(unsigned int)sizeof(long));
    printk(KERN_INFO "float--%x \n",(unsigned int)sizeof(float));
    printk(KERN_INFO "u32--%x \n",(unsigned int)sizeof(u32));
    printk(KERN_INFO "u64--%x \n",(unsigned int)sizeof(u64));
}

//
//Procedure:    ASM116SpiGetControlGrant
//Description:  Get SPI control grant from SPI controller
//Input:    pSpiControlExtension  - SPI control extension
//Output:   ASMT_SUCCESS    - Get SPI control grant
//          ASMT_TIMEOUT   - Can't get control grant
//
int ASM116SpiGetControlGrant(struct pci_dev *PciDevice)
{
    int         GrantRetry;
    int     BoolGrant;

    //Get SPI control request grant
    GrantRetry = 0;
    BoolGrant = ASMT_TIMEOUT;
    while( (BoolGrant == ASMT_TIMEOUT) && (GrantRetry < GET_SPI_CONTROL_GRANT_RETRY_COUNT) )
    {
        BoolGrant = SpiGetGrant(PciDevice, UTILITY_SPI_REQUEST_NUMBER);
        GrantRetry++;
    }


    if(BoolGrant == ASMT_TIMEOUT)
    {
        printk(KERN_INFO "\nSpiFlashGetControlGrant: Can't get ASM116 SPI control grant!!!\n");
    }


    return BoolGrant;
}


//
//Procedure:    ASM116SpiReleaseGrant
//Description:  Release request to SPI controller
//Input:    pSpiControlExtension  - SPI control extension
//Output:   None
//
void ASM116SpiReleaseControlGrant(struct pci_dev *PciDevice)
{
    SpiReleaseGrant(PciDevice, UTILITY_SPI_REQUEST_NUMBER);
    return;
}

//
//Procedure:    ASM116ReadSpiFlashRom
//Description:  Erase SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          pData               - Data buffer
//          Address             - Specific address
//          Length              - Read length
//Output:   ASMT_SUCCESS    - Erase success
//          ASMT_Error   - others
//Note:
//      ASM 116 SPI control Grant shall be gotten by caller
//
int ASM116ReadSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void * pData, DWORD Address, DWORD Length)
{
    return(SpiFlashReadData(PciDevice,SpiControl, pData, Address, Length));
}


//
//Procedure:    ASM116BlankCheckSpiFlashRom
//Description:  Blank check for SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          Address                 - Start address
//          BlankCheckLengthLength  - Blank check length after erased
//Output:   ASMT_SUCCESS    - SPI flash ROM is blank
//          ASMT_SPI_BLANK_ERROR   - others
//
int ASM116BlankCheckSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, DWORD Address, DWORD BlankCheckLength)
{
    DWORD   i;
    BYTE *pData;
    BYTE    bData;

    //Allocate memory for blank check
    if((pData = (BYTE *)kmalloc(BlankCheckLength, GFP_KERNEL)) == NULL)
    {

        printk(KERN_INFO "\nASM116BlankCheckSpiFlashRom: Allocate memory for blank check fail!!!\n");


        for(i = 0; i < BlankCheckLength; i++)
        {
            SpiFlashReadData(PciDevice,SpiControl, &bData, (Address+i), 1L);
            if(bData != 0xFF)   //blank
            {

                printk(KERN_INFO "\nASM116BlankCheckSpiFlashRom: Blank check fail at address 0x%04X!!!\n", Address+i);

                return ASMT_SPI_BLANK_ERROR;
            }
        }
    }
    else
    {
        SpiFlashReadData(PciDevice,SpiControl, pData, Address, BlankCheckLength);
        for(i = 0; i < BlankCheckLength; i++)
        {
            if(pData[i] != 0xFF)    //blank
            {

                printk(KERN_INFO "\nASM116BlankCheckSpiFlashRom: Blank check fail at address 0x%04X!!!\n", Address+i);


                kfree(pData);
                return ASMT_SPI_BLANK_ERROR;
            }
        }
        kfree(pData);
    }

    if(verblevel)
    printk(KERN_INFO "\nASM116BlankCheckSpiFlashRom: Blank check completed\n");


    return ASMT_SUCCESS;
}

//
//Procedure:    ASM116BEraseSpiFlashRom
//Description:  Erase SPI flash ROM
//Input:    pSpiFlashExtension      - SPI flash extension
//Output:   ASMT_SUCCESS    - Erase success
//          ASMT_IO_ERROR   - others
//Note:
//      ASM 116 SPI control Grant shall be gotten by caller
//      Use SPI flash CHIP REASE command will erase all content of SPI flash ROM
//
//
int ASM116EraseSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl)
{
    DWORD   BlankCheckLength;
    int		i;
    int BoolSuccess;

    //Unprotect blocks for SPI flash ROM

   //printk(KERN_INFO "CmdTableIndex[%X]WrEn[0x%X]E[0x%X]FastRead[0x%X]\n",SpiControl->SpiIndex.CmdTableIndex,SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex].WriteEnableCmd
   //,SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex].ChipEraseCmd,SUPPORTED_SPI_FLASH_COMMAND_TABLE[SpiControl->SpiIndex.CmdTableIndex].FastReadCmd);
    i=0;
    BoolSuccess = ASMT_IO_ERROR;
    while( (i < SPI_PROGRAM_REASE_TRY_COUNT) && (BoolSuccess == ASMT_IO_ERROR) )
    {
        BoolSuccess = SpiFlashUnprotectBlocks(PciDevice,SpiControl);
        i++;
    }

    if(verblevel)
    {
        printk(KERN_INFO "ASM116EraseSpiFlashRom: Clear SPI flash ROM Status Register = %d\n", BoolSuccess);

    }

    if(BoolSuccess == ASMT_IO_ERROR)
    {

        printk(KERN_INFO "\nASM116EraseSpiFlashRom: Write SPI flash ROM Status Register fail!!!\n");

        return ASMT_IO_ERROR;
    }

    //Erase chip and blank check
    i = 0;
    BoolSuccess = ASMT_IO_ERROR;
    BlankCheckLength = SpiControl->SpiIndex.Capacity * 1024L;       //Capacity of SPI flash ROM KBytes
    while( (i < SPI_PROGRAM_REASE_TRY_COUNT) && (BoolSuccess != ASMT_SUCCESS) )
    {
        BoolSuccess = SpiFlashChipErase(PciDevice,SpiControl);

    if(verblevel)
    {
        printk(KERN_INFO "\nASM116EraseSpiFlashRom: Chip Erase status = %d\n", BoolSuccess);

    }

        if(BoolSuccess == ASMT_SUCCESS)
        {
            BoolSuccess = ASM116BlankCheckSpiFlashRom(PciDevice,SpiControl, 0L, BlankCheckLength);
            if(verblevel)
            {
                printk(KERN_INFO "\nASM116EraseSpiFlashRom: Blank Check status = %d\n", BoolSuccess);
            }


        }
        i++;
    }

    return BoolSuccess;
}

//
//Procedure:    ASM116UpdateSpiFlashRom
//Description:  Update SPI flash ROM
//Input:    pSpiFlashExtension  - SPI flash extension
//          pSpirom             - Content to update
//          SpiromFileLength    - Length
//Output:   ASMT_SUCCESS    - update success
//          ASMT_IO_ERROR   - others
//Note:
//      ASM116 SPI control Grant shall be gotten by caller
//
int ASM116UpdateSpiFlashRom(struct pci_dev *PciDevice,PSPI_FLASH_EXTENSION SpiControl, void * pSpirom, DWORD SpiromFileLength)
{
    //PSPIROMHEADER           pSpiHeader;         //ASM 116 SPI ROM file header
    //PRECORDTBL              pRecordTable;       //ASM 116 firmware record table
    DWORD   i, m;
    BYTE    bData;
    BYTE *pData;
    BYTE *pByte;
    int BoolSuccess;



    //Unprotect blocks for SPI flash ROM
    i = 0;
    BoolSuccess = ASMT_IO_ERROR;
    while( (i < SPI_PROGRAM_UPDATE_TRY_COUNT) && (BoolSuccess == ASMT_IO_ERROR) )
    {
        BoolSuccess = SpiFlashUnprotectBlocks(PciDevice,SpiControl);
        i++;
    }
    if(BoolSuccess == ASMT_IO_ERROR)
    {

        printk(KERN_INFO "\nASM116UpdateSpiFlashRom: Write SPI flash ROM Status Register fail!!!\n");


        return ASMT_IO_ERROR;
    }

    //
    //Write SPI flash ROM
    //

    //Allocate memory for data verified
    pData = NULL;   //init value
    pData = (BYTE *)kmalloc(SpiromFileLength, GFP_KERNEL);


    if(pData == NULL)
    {
        printk(KERN_INFO "\nASM116UpdateSpiFlashRom: Allocate memory for data verified fail!!!\n");
    }


    //Start write
    i = 0;
    BoolSuccess = ASMT_IO_ERROR;
    while( (i < SPI_PROGRAM_UPDATE_TRY_COUNT) && (BoolSuccess != ASMT_SUCCESS) )
    {
        //Erase chip
        BoolSuccess = SpiFlashChipErase(PciDevice,SpiControl);


        printk(KERN_INFO "ASM116UpdateSpiFlashRom: Chip Erase status = %d\n", BoolSuccess);



        //Blank check
        BoolSuccess = ASM116BlankCheckSpiFlashRom(PciDevice,SpiControl, 0L, SpiromFileLength);


        printk(KERN_INFO "ASM116UpdateSpiFlashRom: Blank Check status = %d\n", BoolSuccess);

        if(BoolSuccess == ASMT_SUCCESS)
        {
            //Write data
            BoolSuccess = SpiFlashWriteData(PciDevice,SpiControl, pSpirom, 0L, SpiromFileLength);


            printk(KERN_INFO "ASM116UpdateSpiFlashRom: Write Data status = %d\n", BoolSuccess);



        }

        //Verify data
        if(BoolSuccess == ASMT_SUCCESS)
        {
            if(pData == NULL)   //Allocate memory fail
            {
                pByte = (BYTE *)pSpirom;
                for(m = 0; m < SpiromFileLength; m++)
                {
                    SpiFlashReadData(PciDevice,SpiControl, &bData, m, 1L);
                    if(bData != *pByte)
                    {
                        printk(KERN_INFO "\nASM116UpdateSpiFlashRom: Verify Written data fail at address 0x%04X!!!\n", i);

                        BoolSuccess = ASMT_SPI_VERIFY_ERROR;
                        break;
                    }
                    pByte++;
                }
            }
            else
            {
                SpiFlashReadData(PciDevice,SpiControl, pData, 0L, SpiromFileLength);
                if(memcmp(pSpirom, pData, SpiromFileLength) != 0)
                {
                    printk(KERN_INFO "\nASM116UpdateSpiFlashRom: Verify Written data fail!!!\n");

                    BoolSuccess = ASMT_SPI_VERIFY_ERROR;
                }
            }
        }
        if(BoolSuccess == ASMT_SUCCESS)
            break;

        i++;
    }

    if(pData != NULL)
    {
        kfree(pData);
    }

    return BoolSuccess;
}

//
//Procedure:    ASM116VerifySpiRomFile
//Description:  Verify ASM116 SPI ROM file
//Input:    pSpirom             - File content to verify
//          SpiromFileLength    - Length
//Output:   ASMT_SUCCESS    - valid ASM116 SPI ROM file
//          ASMT_PARAMETER_INVALID   - invalid
//
int ASM116VerifySpiRomFile(void * pSpirom, DWORD SpiromFileLength)
{
    PSPIROMHEADER       pSpiromHeader;
    BYTE *pVerificationTable;             //0x000100 ~ 0x0001FF: Verification table
    PSPIFILEIDENTIFIER  pSpiFileIdentifier;             //0x000200 ~ 0x0002FF: Identifier
    BYTE *pOpromImage;
    BYTE *pFirmwareBinary;
    //DWORD               OpromImageLength;
    //DWORD               FirmwareBinaryLength;
    DWORD               Crc32;
    BYTE *pByte;
    unsigned int        i;
    BYTE                Checksum;

//Initial variables
    pSpiromHeader           = (PSPIROMHEADER)pSpirom;
    pSpiFileIdentifier      = (PSPIFILEIDENTIFIER)&pSpiromHeader->SpiFileIdentifier;
    pVerificationTable      = NULL;
    pOpromImage             = NULL;
    pFirmwareBinary         = NULL;

//
//1 Check the ：Identifier； in SPI ROM file
//1.1 Check the GUID. If the GUID is not matched ASM116 GUID, it is not a valid ASM116 SPI ROM file.
//1.2 Check the checksum of "Identifier". If the checksum is not correct, it can・t be updated.
//
    //Check GUID

     for ( i=0;i<ASM116_GUID_LENGTH; i ++)
    {
        if(pSpiFileIdentifier->GUID[i] != gASM116GUID[i]  )
        {

        printk(KERN_INFO "VerifyASM116SpiRomFile: Identifier GUID is not matched!!!");

        return ASMT_PARAMETER_INVALID;
        }
    }

    if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Identifier Checksum is matched!!!" );

    //Check checksum
    pByte = (BYTE *)pSpiFileIdentifier;
    Checksum = 0;
    for(i = 0; i < ASM116_SPI_FILE_IDENTIFIER_LENGTH - 1; i++)
    {
        Checksum = Checksum + (*pByte);
        pByte++;
    }
    if(pSpiFileIdentifier->Checksum != Checksum)
    {

        printk(KERN_INFO "\nVerifyASM116SpiRomFile: Identifier Checksum is not matched!!!");

        return ASMT_PARAMETER_INVALID;
    }
    if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Identifier Checksum is matched!!!" );

//2 Check the length of SPI ROM file
//The file is invalid if the total length is not same as ：Length of SPI ROM File； field.
    if(pSpiFileIdentifier->FileLength != SpiromFileLength)
    {

        printk(KERN_INFO "VerifyASM116SpiRomFile: Length of SPI ROM file in Identifier is not matched!!!");

        return ASMT_PARAMETER_INVALID;
    }

    if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Length of SPI ROM file in Identifier is matched!!!\r\n");

//
//3 Verify the OPROM image
//3.1 Determine OPROM image whether existed by OPROM Image Starting Address in ：Identifier；.
//3.2 Check the Starting Address. If it does not equal to 0x00000000, it must be 0x00002000.
//3.3 Check the CRC32 of OPROM image if it exists.
//3.4 If the CRC32 is not correct, it can・t be updated.
//

    if(pSpiFileIdentifier->OpromImage.Address == 0L)
    {
        printk(KERN_INFO "VerifyASM116SpiRomFile: OPROM Image does not exist!!!\n");
    }


    if(pSpiFileIdentifier->OpromImage.Address != 0L)
    {
        //2.2 Check the Starting Address. If it does not equal to 0x00000000, it must be 0x00002000.
        if(pSpiFileIdentifier->OpromImage.Address != ASM116_OPROM_STARTING_ADDRESS)
        {

            printk(KERN_INFO "VerifyASM116SpiRomFile: OPROM Image Starting Address is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: OPROM Image Starting Address is correct!!!\r\n" );
        //2.3 Check the CRC32 of OPROM image if it exists.
        pOpromImage = (BYTE *)pSpirom + pSpiFileIdentifier->OpromImage.Address;
        Crc32 = GetCrc32(pOpromImage, pSpiFileIdentifier->OpromImage.Length);
        if(pSpiFileIdentifier->OpromImage.Crc32 != Crc32)
        {

            printk(KERN_INFO "VerifyASM116SpiRomFile: OPROM Image CRC32 is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: OPROM Image CRC32 is correct!!!\r\n" );
    }

    if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: OpromImage Address is correct!!!\r\n");

//
//4 Verify the Verification Table
//4.1 Determine Verification Table whether existed by Verification Table Starting Address in ：Identifier；.
//4.2 Check the Starting Address. If it does not equal to 0x00000000, it must be 0x00000100.
//4.3 Check the Length. It must be 0x00000100 if it exists.
//4.4 Check the CRC32
//4.5 If the CRC32 is not correct, it can・t be updated.
//

    //Verification table must exist if firmware binary exists.
    if(pSpiFileIdentifier->VerificationTable.Address == 0L)
    {
        if(pSpiFileIdentifier->FirstFirmwareBinary.Address != 0L)
        {

            printk(KERN_INFO "VerifyASM116SpiRomFile: Error condition!!!\n");
            printk(KERN_INFO "Firmware Binary exist but Verification Table does not exist!!!.\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: FirstFirmwareBinary Address is matched!!!\r\n" );
    }
    else
    {
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: VerificationTable Address is matched!!!\r\n" );
        //3.2 Check the Starting Address. If it does not equal to 0x00000000, it must be 0x00000100.
        if(pSpiFileIdentifier->VerificationTable.Address != ASM116_VERIFICATION_TABLE_STARTING_ADDRESS)
        {

            printk(KERN_INFO "\nVerifyASM116SpiRomFile: Verification Table Starting Address is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Verification Table Starting Address is correct!!!\r\n" );
        //3.3 Check the Length. It must be 0x00000100 if it exists.
        if(pSpiFileIdentifier->VerificationTable.Length != ASM116_VERIFICATION_TABLE_LENGTH)
        {

            printk(KERN_INFO "\nVerifyASM116SpiRomFile: Verification Table Length is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Verification Table Length is correct!!!\n" );

        //3.4 Check the CRC32
        pVerificationTable = (BYTE *)pSpirom + pSpiFileIdentifier->VerificationTable.Address;
        Crc32 = GetCrc32(pVerificationTable, pSpiFileIdentifier->VerificationTable.Length);
        if(pSpiFileIdentifier->VerificationTable.Crc32 != Crc32)
        {

            printk(KERN_INFO "\nVerifyASM116SpiRomFile: Verification Table CRC32 is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
    }

//
//5 Verify the Firmware binary[
//5.1 Don・t need to verify firmware binary if Verification Table does not exist.
//5.2 Determine the first firmware binary whether existed by First Firmware Binary Starting Address.
//5.3 Check the CRC32 of first firmware binary if it exists.
//5.4 If the CRC32 is not correct, it can・t be updated.
//5.5 Repeat 4.2 to 4.4 to check second firmware binary.
//
    //Firmware binary must exist if verification table exists
    if(pSpiFileIdentifier->FirstFirmwareBinary.Address == 0L)
    {
        if(pSpiFileIdentifier->VerificationTable.Address != 0L)
        {

            printk(KERN_INFO "\nVerifyASM116SpiRomFile: Error condition!!!\n");
            printk(KERN_INFO "Verification Table exist but Firmware Binary does not exist!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
    }
    else
    {
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: FirstFirmwareBinary Address is matched!!!\r\n" );

        //4.3 Check the CRC32 of first firmware binary if it exists.
        pFirmwareBinary = (BYTE *)pSpirom + pSpiFileIdentifier->FirstFirmwareBinary.Address;
        Crc32 = GetCrc32(pFirmwareBinary, pSpiFileIdentifier->FirstFirmwareBinary.Length);
        if(pSpiFileIdentifier->FirstFirmwareBinary.Crc32 != Crc32)
        {

            printk(KERN_INFO "VerifyASM116SpiRomFile: First firmware binary CRC32 is not correct!!!\n");

            return ASMT_PARAMETER_INVALID;
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: First firmware binary CRC32 is correct!!!\n");
        //4.5 Repeat 4.2 to 4.4 to check second firmware binary.

        if(pSpiFileIdentifier->SecondFirmwareBinary.Address == 0L)
        {
			if ( verblevel )
            printk(KERN_INFO "VerifyASM116SpiRomFile: Second Firmware Binary does not exist!!!\n");
        }


        if(pSpiFileIdentifier->SecondFirmwareBinary.Address != 0L)
        {
        printk(KERN_INFO "VerifyASM116SpiRomFile: SecondFirmwareBinary Address is not matched!!!\n" );
            //4.3 Check the CRC32 of second firmware binary if it exists.
            pByte = (BYTE *)pSpirom + pSpiFileIdentifier->SecondFirmwareBinary.Address;
            Crc32 = GetCrc32(pByte, pSpiFileIdentifier->SecondFirmwareBinary.Length);
            if(pSpiFileIdentifier->SecondFirmwareBinary.Crc32 != Crc32)
            {

                printk(KERN_INFO "VerifyASM116SpiRomFile: Second firmware binary CRC32 is not correct!!!\n");

                return ASMT_PARAMETER_INVALID;
            }
        }
        if ( verblevel )
        printk(KERN_INFO "VerifyASM116SpiRomFile: Second firmware binary CRC32 is correct!!!\r\n" );
    }

    return ASMT_SUCCESS;
}
//
//Procedure:    ASM116GetOPROMVERandFWVER
//Description:  Get ASM116 SPI ROM file OPROM Ver and FW Ver
//Input:    pSpirom             - File content to verify
//          SpiromFileLength    - Length
//Output:   None
//
void ASM116GetOPROMandFWver(void * pSpirom, DWORD SpiromFileLength)
{
    PSPIROMHEADER       pSpiromHeader;
    PSPIFILEIDENTIFIER  pSpiFileIdentifier;             //0x000200 ~ 0x0002FF: Identifier

	BYTE bVer1, bVer2, bVer3, bVer4, bVer5, bVer6;

	WORD	wTemp;
	BYTE *pByte;

	//Initial variables
    pSpiromHeader           = (PSPIROMHEADER)pSpirom;
    pSpiFileIdentifier      = (PSPIFILEIDENTIFIER)&pSpiromHeader->SpiFileIdentifier;


	bVer1 = bVer2 = bVer3 = bVer4 = bVer5 = bVer6 = 0;

	wTemp = 0;

	pByte = (BYTE *)pSpiromHeader;

	//Get OPROM version
	if((pSpiFileIdentifier->OpromImage.Address == 0)||(pSpiFileIdentifier->OpromImage.Address == 0xFFFFFFFF))
	{
		if(verblevel)
		{
			printk(KERN_INFO "OPROM Version: NA\n");
		}


	}else
	{
		bVer1 = pByte[pSpiFileIdentifier->OpromImage.Address + 0x18];		//40
		bVer2 = pByte[pSpiFileIdentifier->OpromImage.Address + 0x19];		//00
		wTemp = ((WORD)(bVer2<<8) + (bVer1) + 0x12);
		bVer3 = pByte[pSpiFileIdentifier->OpromImage.Address + wTemp];
		wTemp = ((WORD)(bVer2<<8) + (bVer1) + 0x13);
		bVer4 = pByte[pSpiFileIdentifier->OpromImage.Address + wTemp];

        if(verblevel)
		printk(KERN_INFO "OPROM Version: %01x.%02x\n", bVer4, bVer3);


	}


	bVer1 = bVer2 = 0;

	//Get FW version
	if((pSpiFileIdentifier->FirstFirmwareBinary.Address == 0)||(pSpiFileIdentifier->FirstFirmwareBinary.Address == 0xFFFFFFFF))
	{
		//if(verblevel)
		{
			printk(KERN_INFO "EEPROM FW Version: NA\n");

		}
	}else
	{
		if(pSpiFileIdentifier->SecondFirmwareBinary.Address > pSpiFileIdentifier->FirstFirmwareBinary.Address)
		{
			bVer1 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x10];
			bVer2 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x11];
			bVer3 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x12];
			bVer4 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x13];
			bVer5 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x14];
			bVer6 = pByte[pSpiFileIdentifier->SecondFirmwareBinary.Address + 0x15];

			printk(KERN_INFO "EEPROM FW Version: %02x%02x%02x_%02x%02x_%02x\n", bVer1, bVer2, bVer3, bVer4, bVer5, bVer6);

		}else
		{
			bVer1 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x10];
			bVer2 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x11];
			bVer3 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x12];
			bVer4 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x13];
			bVer5 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x14];
			bVer6 = pByte[pSpiFileIdentifier->FirstFirmwareBinary.Address + 0x15];

			printk(KERN_INFO "EEPROM File FW Version: %02x%02x%02x_%02x%02x_%02x\n", bVer1, bVer2, bVer3, bVer4, bVer5, bVer6);

		}
	}
}

static ssize_t asm116X_spi_update_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    DWORD                   SpiromFileLength;
    BYTE *pSpirom;
    SPI_FLASH_EXTENSION    SpiFlashExtension;
	int                     uiResult = ASMT_SUCCESS;
    int i = 0;
    struct pci_dev *current_dev = NULL;
    struct pci_dev *pdev = NULL;
    struct kstat fileState;
    struct file *pFile = NULL;
    int iFileLength = 0;
    char filename[SZ_SPI_NAME_LENGTH] = {0}, *pVerify = NULL;
    struct path rom_path;

    memset(&rom_path, 0, sizeof(struct path));

    snprintf(filename, strlen(buf), "%s", buf);
    pFile = filp_open(filename, O_RDONLY, 0);
    if (NULL == pFile) {
        printk(KERN_INFO "An ASM116X ROM file must be designated!!!");
        goto END;
    }

    if (kern_path(filename, LOOKUP_FOLLOW, &rom_path)) {
        printk(KERN_ERR "Cannot get path from input filename %s\n", filename);
        goto END;
    }

    vfs_getattr(&rom_path, &fileState, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
    SpiromFileLength = fileState.size;

    pVerify = kmalloc(SpiromFileLength, GFP_KERNEL);
    pSpirom = kmalloc(SpiromFileLength, GFP_KERNEL);
    if (!pSpirom) {
        printk("malloc buffer for spi rom error!\n");
        goto END;
    }
    for (iFileLength = 0; iFileLength < SpiromFileLength; iFileLength++) {
        kernel_read(pFile, pSpirom + iFileLength, 1, &pFile->f_pos);
    }

    filp_close(pFile, NULL);
    pFile = NULL;

    //Verify SPI ROM file
    if(ASM116VerifySpiRomFile(pSpirom, SpiromFileLength) != ASMT_SUCCESS)
    {
        printk(KERN_INFO "Input is not a valid ASM116X SPI ROM file!!!\n");
        return count;
    }

    while ((NULL != (pdev = pci_get_device(0x1b21, 0x1164, pdev))) ||
           (NULL != (pdev = pci_get_device(0x1b21, 0x1165, pdev)))) {
        //Update to SPI flash ROM
        i++;
        if (0 != uiSelectedASM116x && (uiSelectedASM116x != i)) {
            continue;
        }
        current_dev = pdev;
        // Initialize SpiControlExtension and SpiFlashExtension
        //
        pMemPtr = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
        if (!pMemPtr) {
            printk(KERN_INFO "Cannot Update SPI flash for ");
            printk(KERN_INFO "Controller %d : PCI bus 0x%02x, device 0x%02x function 0x%02x ...\n", i,
            pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
            continue;
        }
        // ROM SPI ADDRESS OFFSET FROM MMIO BASE
        pMemPtr += 0x1000;

        printk(KERN_INFO "Update SPI flash ROM start:");
        printk(KERN_INFO "Controller %d : PCI bus 0x%02x, device 0x%02x function 0x%02x ...\n", i,
                pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);

        //1. Get SPI control Grant
        //2. Detect SPI flash ROM
        //3. Update SPI flash ROM
        //4. Release SPI control Grant
        if(ASM116SpiGetControlGrant(current_dev) == ASMT_TIMEOUT)
        {
            printk(KERN_INFO "Get SPI control Grant fail!!!");

            uiResult = ASMT_TIMEOUT;
            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);
            //printk(KERN_INFO "    Get SPI control Grant fail!!!\n");
            continue;
        }
        //Detect SPI flash ROM

        if(SpiFlashDetectSpiFlashRom(current_dev,(PSPI_FLASH_EXTENSION)&SpiFlashExtension) != ASMT_SUCCESS)
        {
            //Release SPI control Grant
            ASM116SpiReleaseControlGrant(current_dev);

            printk(KERN_INFO "Can't detect SPI flash ROM on ASM116X!!!");

            uiResult = ASMT_UNMATCH;
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);
            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //printk(KERN_INFO "    Can't detect SPI flash ROM on ASM116!!!\n");
            //continue;
        }

        if(SpiFlashExtension.InSupportedList == ASMT_IO_ERROR)
        {
            printk(KERN_INFO "Find a SPI flash ROM ID : %02Xh, %02Xh, %02Xh is not in Supported List!!!",
                SpiFlashExtension.SpiIndex.FlashID[0], SpiFlashExtension.SpiIndex.FlashID[1], SpiFlashExtension.SpiIndex.FlashID[2]);

            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);

            printk(KERN_INFO "Try to program...");
            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);

            //printk(KERN_INFO "    Find a SPI flash ROM ID : %02Xh, %02Xh, %02Xh is not in Supported List!!!\n",pSpiFlashExtension->SpiIndex.FlashID[0], pSpiFlashExtension->SpiIndex.FlashID[1], pSpiFlashExtension->SpiIndex.FlashID[2]);
            //        printk(KERN_INFO "    Try to program...\n");
            uiResult = ASMT_IO_ERROR;
        }
        else
        {
           // printk(KERN_INFO "Find a SPI flash ROM: %s, Capacity = %d Kbytes", SpiFlashExtension.SpiIndex.ProductString, SpiFlashExtension.SpiIndex.Capacity);
            printk(KERN_INFO "Find a SPI flash ROM, Capacity = %d Kbytes", SpiFlashExtension.SpiIndex.Capacity);

            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);

            //printk(KERN_INFO "    Find a SPI flash ROM: %s, Capacity = %d Kbytes\n", pSpiFlashExtension->SpiIndex.ProductString, pSpiFlashExtension->SpiIndex.Capacity);
            if(SpiromFileLength > (DWORD)SpiFlashExtension.SpiIndex.Capacity * 1024)
            {
                //Release SPI control Grant
                ASM116SpiReleaseControlGrant(current_dev);
                printk(KERN_INFO "The capacity of SPI flash ROM is less than ASM116X SPI ROM file!!!");
                //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
                //this->m_StaticShowMessage.SetWindowTextA(strTemp1);
                //printk(KERN_INFO "    The capacity of SPI flash ROM is less than ASM116 SPI ROM file!!!\n");
                //continue;
            }
        }



        uiResult =ASM116UpdateSpiFlashRom(current_dev,(PSPI_FLASH_EXTENSION)&SpiFlashExtension, pSpirom, SpiromFileLength);
        if(uiResult != ASMT_SUCCESS)
        {
            printk(KERN_INFO "Update SPI flash ROM......FAIL!!!\n\n");
            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);
            //printk(KERN_INFO "    Update SPI flash ROM......FAIL!!!\n");

        }
        else
        {
            printk(KERN_INFO "Update SPI flash ROM......PASS!!!\n\n");

            //DBGMESSAGE( _FLP_ "%s\r\n", _FL_ , strTemp1);
            //this->m_StaticShowMessage.SetWindowTextA(strTemp1);
            //printk(KERN_INFO "    Update SPI flash ROM......PASS\n");

        }

        SpiFlashReadData(current_dev,(PSPI_FLASH_EXTENSION)&SpiFlashExtension, pVerify, 0L, SpiromFileLength);
        if (0 != memcmp(pVerify, pSpirom, SpiromFileLength)) {
            printk(KERN_ERR "Updated flash does not match input flash.\n");
        }
        ASM116GetOPROMandFWver(pVerify, SpiromFileLength);
        //Release SPI control Grant
        ASM116SpiReleaseControlGrant(current_dev);

        if (pMemPtr) {
            iounmap(pMemPtr);
            pMemPtr = NULL;
        }
    }
END:
    if (pSpirom) {
        kfree(pSpirom);
    }
    if (pMemPtr) {
        iounmap(pMemPtr);
        pMemPtr = NULL;
    }
    if (pFile) {
        filp_close(pFile, NULL);
    }
    if (pVerify) {
        kfree(pVerify);
    }
    return count;
}

static ssize_t asm116X_spi_version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    SPI_FLASH_EXTENSION    SpiFlashExtension;
    BYTE *pData;
    struct pci_dev *pdev = NULL, *current_dev = NULL;
    DWORD                   SpiromFileLength;
    int i = 0;

    if (0 == uiSelectedASM116x) {
        printk(KERN_INFO "Please select a ASM116x to read spi header\n");
        goto END;
    }

    while ((NULL != (pdev = pci_get_device(0x1b21, 0x1164, pdev))) ||
           (NULL != (pdev = pci_get_device(0x1b21, 0x1165, pdev)))) {

        i++;
        if (uiSelectedASM116x != i) {
            continue;
        }
        current_dev = pdev;

        pMemPtr = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
        if (!pMemPtr) {
            printk(KERN_INFO "Cannot Show SPI flash for ");
            printk(KERN_INFO "Controller %d : PCI bus 0x%02x, device 0x%02x function 0x%02x ...\n", i,
            pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
            continue;
        }
        // ROM SPI ADDRESS OFFSET FROM MMIO BASE
        pMemPtr += 0x1000;

    	//1. Get SPI control Grant
    	//2. Detect SPI flash ROM
    	//3. Update SPI flash ROM
    	//4. Release SPI control Grant
    	if(ASM116SpiGetControlGrant(current_dev) == ASMT_TIMEOUT)
    	{
            printk(KERN_INFO "Get SPI control dev%2d Grant fail!!!\n",i);
            continue;
    	}

    	//Detect SPI flash ROM

    	if(SpiFlashDetectSpiFlashRom(current_dev,(PSPI_FLASH_EXTENSION)&SpiFlashExtension) != ASMT_SUCCESS)
    	{
    		//Release SPI control Grant
    		ASM116SpiReleaseControlGrant(current_dev);

    		printk(KERN_INFO "Can't detect SPI flash ROM dev%2d on ASM116X!!!\n", i);

    	}

    	if(SpiFlashExtension.InSupportedList == ASMT_IO_ERROR)
    	{
    		printk(KERN_INFO "Find a SPI flash ROM dev%2d ID : %02Xh, %02Xh, %02Xh is not in Supported List!!!\n",
    			i, SpiFlashExtension.SpiIndex.FlashID[0], SpiFlashExtension.SpiIndex.FlashID[1],
    			SpiFlashExtension.SpiIndex.FlashID[2]);

    	}
    	else
    	{
            if(verblevel)
            {
                //printk(KERN_INFO "Find a SPI flash ROM dev%2d: %s, Capacity = %d Kbytes", i, SpiFlashExtension.SpiIndex.ProductString, SpiFlashExtension.SpiIndex.Capacity);
            printk(KERN_INFO "Find a SPI flash ROM dev%2d, Capacity = %d Kbytes", i, SpiFlashExtension.SpiIndex.Capacity);
            }
    	}

    	SpiromFileLength = (DWORD)(SpiFlashExtension.SpiIndex.Capacity * 1024);

    	pData = NULL;
        pData = (BYTE *)kmalloc(SpiromFileLength, GFP_KERNEL);

    	SpiFlashReadData(current_dev,(PSPI_FLASH_EXTENSION)&SpiFlashExtension, pData, 0L, SpiromFileLength);

    	ASM116GetOPROMandFWver(pData, SpiromFileLength);

        if(verblevel)
    	printk(KERN_INFO "Get SPI controller dev%2d SPI ROM version done!!!\n", i);


    	//Release SPI control Grant
    	ASM116SpiReleaseControlGrant(current_dev);

        if (pMemPtr) {
            iounmap(pMemPtr);
            pMemPtr = NULL;
        }
        if(pData != NULL)
        {
            kfree(pData);
        }
    }
END:
    if (pMemPtr) {
        iounmap(pMemPtr);
        pMemPtr = NULL;
    }
    return 0;
}

static ssize_t asm116X_spi_select_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct pci_dev *pdev = NULL;
    int iCount = 0, iCurrent = 0;

    iCurrent = snprintf(buf, PAGE_SIZE, "Current selected asm116X : %u\n", uiSelectedASM116x);
    iCurrent += snprintf(buf + iCurrent, PAGE_SIZE - iCurrent, "0. All asm116X is selected (default).\n");
    while (NULL != (pdev = pci_get_device(0x1b21, 0x1164, pdev)) ||
           NULL != (pdev = pci_get_device(0x1b21, 0x1165, pdev))) {
        iCurrent += snprintf(buf + iCurrent, PAGE_SIZE - iCurrent, "%u. pcie bus %02x:%02x.%x\n", ++iCount, pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
    }
    return iCurrent;
}

static ssize_t asm116X_spi_select_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct pci_dev *pdev = NULL;
    u32 iCount=0;
    int iRet = -1;

    iRet = kstrtouint(buf, 10, &uiSelectedASM116x);
    if (0 > iRet) {
        goto END;
    }

    if (0 == uiSelectedASM116x) {
        printk(KERN_INFO "No.0 All asm116X are selected.");
        iRet = count;
        goto END;
    }

    while (NULL != (pdev = pci_get_device(0x1b21, 0x1164, pdev)) ||
           NULL != (pdev = pci_get_device(0x1b21, 0x1165, pdev))) {
        iCount++;
        if (uiSelectedASM116x == iCount) {
            printk(KERN_INFO "No.%u asm116X on pcie bus %02x:%02x.%x has been selected\n", iCount, pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
            break;
        }
    }
    if (0 != uiSelectedASM116x && (uiSelectedASM116x != iCount)) {
        printk(KERN_INFO "There is no asm116X being selected for input no.%u. All asm116X will be selected by default.\n", uiSelectedASM116x);
        uiSelectedASM116x = 0;
    }
    iRet = count;
END:
    return iRet;
}

// register function to attribute
static struct kobj_attribute asm116XSelectAttr = __ATTR( asm116X_spi_select, 0640, asm116X_spi_select_show, asm116X_spi_select_store);
static struct kobj_attribute asm116XUpdateAttr = __ATTR( asm116X_spi_update, 0640, asm116X_spi_version_show, asm116X_spi_update_store);

// put attribute to attribute group
static struct attribute *asm116XSPIAttr[] = {
    &asm116XUpdateAttr.attr,
    &asm116XSelectAttr.attr,
    NULL,   /* NULL terminate the list*/
};
static struct attribute_group asm116XSPIGroup = {
    .attrs = asm116XSPIAttr
};

static int asm116X_spi_update_init(void)
{
    int iRet = -1;
    asm116XSPIObject = kobject_create_and_add("asm116X_spi_update", kernel_kobj);
    if (!asm116XSPIObject) {
        iRet = -ENOMEM;
        goto END;
    }

    //create attributes (files)
    if(sysfs_create_group(asm116XSPIObject, &asm116XSPIGroup)){
        iRet = -ENOMEM;
        goto END;
    }

    iRet = 0;
END:
    if (0 != iRet) {
        if (asm116XSPIObject) {
            kobject_put(asm116XSPIObject);
        }
    }
    return iRet;
}

static void asm116X_spi_update_exit(void)
{
    kobject_put(asm116XSPIObject);
}


MODULE_LICENSE("GPL");
module_init(asm116X_spi_update_init);
module_exit(asm116X_spi_update_exit);
