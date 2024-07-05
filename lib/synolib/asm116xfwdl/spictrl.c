
//***************************************************************************
//Name: spictrl.c
//
//Description:
//      Asmedia ASM116 legacy SPI ROM access functions
//
//Revision History:
//2013/08/20    V2.03  James Peng      add write grant of  XDATA RAM.
//2013/04/03    V1.0    Jesse Chang     First revision
//
/*
 * Asmedia ASM116x Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */
//***************************************************************************

#include      "precomp.h"
#include      "spictrl.h"





//
//Procedure:    SpiReadRegisterDword
//Description:  Read ASM116 legacy SPI controller Dword register
//Input:    pSpiControlExtension    - SPI Controller Extension
//          Register                - MMIO address of register
//Output:   Return read Dword value
//Note:
//      Address can be not Dword alignment when Read/Write Dword memory, but we limit it must be Dword alignment.
//
DWORD SpiReadRegisterDword(struct pci_dev *PciDevice,DWORD Register)
{
    // DWORD dValue = 0;
    DWORD RegisterOffset = 0;

    RegisterOffset = Register;

    //RegisterOffset = (DWORD)Register - (DWORD)(pSpiControlExtension->Abar);
    //Offset = RegisterOffset % 4;
    RegisterOffset = RegisterOffset & (~0x03);

    //Write Index
    //pci_write_long(PciDevice, ASM116_MMIO_INDEX, RegisterOffset);

    //Read Data
    //dValue = pci_read_long(PciDevice,ASM116_MMIO_DATA);
//    if ( verblevel )
//    {
//        printk(KERN_INFO "MMIO  Read: %08X, OFFSET: %08X\n\n", pMemPtr, RegisterOffset);
//    }

//    dValue = *((DWORD *)&pMemPtr[RegisterOffset]);


    return readl(pMemPtr + Register);
}



//
//Procedure:    SpiWriteRegisterDword
//Description:  Write ASM116 legacy SPI controller Dword register
//Input:    pSpiControlExtension    - SPI Controller Extension
//          Register                - MMIO address of register
//          dValue                  - Written value
//Output:   None
//Note:
//      Address can be not Dword alignment when Read/Write Dword memory, but we limit it must be Dword alignment.
//
void SpiWriteRegisterDword(struct pci_dev *PciDevice, DWORD Register, DWORD dValue)
{
    DWORD RegisterOffset = 0;


    RegisterOffset = Register;
    RegisterOffset = RegisterOffset & (~0x03);

    //Write Index
    //pci_write_long( PciDevice, ASM116_MMIO_INDEX, RegisterOffset);

    //Write Data
    //pci_write_long(PciDevice, ASM116_MMIO_DATA, dValue);


//    if ( verblevel )
//    {
//        printk(KERN_INFO "MMIO Write: %08X, OFFSET: %08X\n\n", pMemPtr, RegisterOffset);
//    }

//    *((DWORD *)&pMemPtr[RegisterOffset]) = dValue;

    writel(dValue, pMemPtr + Register);

    return;
}


//
//Procedure:    SpiGetGrant
//Description:  Get the authority to use SPI controller
//Input:    pSpiControlExtension    - SPI Controller Extension
//          RequestNumber           - Request number
//Output:   ASMT_SUCCESS            - Get authority
//          ASMT_PARAMETER_INVALID  - SPI controller can't be used now
//          ASMT_TIMEOUT            -Time out
//Note:
//      Software utility uses request number 0
//
int SpiGetGrant(struct pci_dev *PciDevice, const BYTE RequestNumber)
{
        LSPI_CONTROL    Ctrl;
        clock_t         ticks1, ticks2;

        if(RequestNumber > MAXIMUM_SPI_REQUEST_NUMBER)
        {
                return ASMT_PARAMETER_INVALID;
        }

        Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
        switch(RequestNumber)
        {
            case 0:
                    Ctrl.Arbiter.REQ0 = 1;
                    break;
            case 1:
                    Ctrl.Arbiter.REQ1 = 1;
                    break;
            case 2:
                    Ctrl.Arbiter.REQ2 = 1;
                    break;
            case 3:
                    Ctrl.Arbiter.REQ3 = 1;
                    break;
            default:
                    break;
        }
        SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);
         //Wait Grant
        ticks1 = jiffies;
        ticks2 = ticks1;
        while( ((ticks2 - ticks1) / CLOCKS_PER_SEC) < GRANT_TIMEOUT )
        {
                Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
                switch(RequestNumber)
                {
                case 0:
                        if(Ctrl.Arbiter.GNT0 == 1)
                        {
                                return ASMT_SUCCESS;
                        }
                        break;
                case 1:
                        if(Ctrl.Arbiter.GNT1 == 1)
                        {
                                return ASMT_SUCCESS;
                        }
                        break;
                case 2:
                        if(Ctrl.Arbiter.GNT2 == 1)
                        {
                                return ASMT_SUCCESS;
                        }
                        break;
                case 3:
                        if(Ctrl.Arbiter.GNT3 == 1)
                        {
                                return ASMT_SUCCESS;
                        }
                        break;
                default:
                        break;
                }
                ticks2 = jiffies;
        }

        //time out
        //Release request
      if(verblevel)
      {
        printk(KERN_INFO "\nSpiGetGrant: Get SPI control grant timeout!!!\n");
        printk(KERN_INFO "    Request number = %d,  SPI Arbiter = 0x%02X\n", RequestNumber, Ctrl.Arbiter.AsByte);
      }

        SpiReleaseGrant(PciDevice, RequestNumber);

        return ASMT_TIMEOUT;   //time out
}


//
//Procedure:    SpiReleaseGrant
//Description:  Release the authority of SPI controller
//Input:    pSpiControlExtension    - SPI Controller Extension
//          RequestNumber           - Request number
//Output:   None
//Note:
//      Software utility uses request number 0
//
void SpiReleaseGrant(struct pci_dev *PciDevice, const BYTE RequestNumber)
{
        LSPI_CONTROL    Ctrl;

        if(RequestNumber > MAXIMUM_SPI_REQUEST_NUMBER)
        {
                return;
        }

          Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
        switch(RequestNumber)
        {
        case 0:
                Ctrl.Arbiter.REQ0 = 0;
                break;
        case 1:
                Ctrl.Arbiter.REQ1 = 0;
                break;
        case 2:
                Ctrl.Arbiter.REQ2 = 0;
                break;
        case 3:
                Ctrl.Arbiter.REQ3 = 0;
                break;
        default:
                break;
        }
         SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);
        return;
}


//
//Procedure:    SpiIs3WireMode
//Description:  Is 3-wires mode enabled for SPI controller?
//Input:    pSpiControlExtension    - SPI Controller Extension
//Output:   ASMT_SUCCESS    - enabled
//          ASMT_PARAMETER_INVALID   - disabled
//Note:
//          3-wires mode is enabled/disabled by hardware controller when power-on detection
//
int SpiIs3WireMode(struct pci_dev *PciDevice)
{
        LSPI_CONTROL    Ctrl;

        Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);

        return ( (Ctrl.Ctrl1.LSPI3WM == 1) ? ASMT_SUCCESS : ASMT_PARAMETER_INVALID);
}


//
//Procedure:    SpiStart
//Description:  Pull down CS# signal to start SPI transaction
//Input:    pSpiControlExtension    - SPI Controller Extension
//Output:   None
//
void SpiStart(struct pci_dev *PciDevice)
{
        LSPI_CONTROL    Ctrl;

          Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
       //     printk(KERN_INFO "\nSpiStart[0x%X]",Ctrl.AsDword);
        Ctrl.Ctrl0.CS = 0;
          SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);
       // printk(KERN_INFO "[0x%X]\n",Ctrl.AsDword);
        return;
}


//
//Procedure:    SpiTerminate
//Description:  Pull up CS# signal to terminate SPI transaction
//Input:    pSpiControlExtension    - SPI Controller Extension
//Output:   None
//
void SpiTerminate(struct pci_dev *PciDevice)
{
        LSPI_CONTROL    Ctrl;

          Ctrl.AsDword = SpiReadRegisterDword(PciDevice,ASM116_SPI_CONTROL_CONTROL);
       // printk(KERN_INFO "\nSpiTerminate[0x%X]",Ctrl.AsDword);
        Ctrl.Ctrl0.CS = 1;
        SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);
       // printk(KERN_INFO "[0x%X]\n",Ctrl.AsDword);
        return;
}


//
//Procedure:    SpiIdle
//Description:  Check SPI transmission whether is done
//Input:    pSpiControlExtension    - SPI Controller Extension
//Output:   ASMT_SUCCESS    - idle
//          ASMT_TIMEOUT   - timeout still running
//Note:
//      Transaction done doesn't mean that SPI ROM works completely for erase, page program command
//
int SpiIdle(struct pci_dev *PciDevice)
{
        LSPI_CONTROL    Ctrl;
        clock_t         ticks1, ticks2;

        ticks1 = jiffies;
        ticks2 = ticks1;
        while( ((ticks2 - ticks1) / CLOCKS_PER_SEC) < IDLE_TIMEOUT )
        {
                 Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
                if(Ctrl.Ctrl0.RUN == 0)
                {
                        return ASMT_SUCCESS;
                }
                else
                {
                        ticks2 = jiffies;
                }
        }


        printk(KERN_INFO "\nSpiIdle: timeout!!!\n");
        printk(KERN_INFO "    SpiIdle: SPI Ctrl Register 0 = 0x%02X\n", Ctrl.Ctrl0.AsByte);


        return ASMT_TIMEOUT;   //Time out
}


//
//Procedure:    SpiRead
//Description:  Read data from SPI flash ROM
//Input:    pSpiControlExtension    - SPI Controller Extension
//          pData                   - Data buffer pointer
//          Length                  - Data length
//Output:   ASMT_SUCCESS    - Read data is stored in pData
//          ASMT_TIMEOUT   - Otherwise and content of pdata is unknow
//
int SpiRead(struct pci_dev *PciDevice, void *pData, BYTE Length)
{
        LSPI_DATA       Data;
        LSPI_CONTROL    Ctrl;
        BYTE        i;
        BYTE *pByte;

        if(SpiIdle(PciDevice) == ASMT_TIMEOUT)
        {
                return ASMT_TIMEOUT;
        }

        if((Length > ASM116_LSPI_MAXIMUM_LENGTH) || (Length < 1))
        {
                return ASMT_PARAMETER_INVALID;
        }

        Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
        Ctrl.Ctrl0.AsByte = 0;
        Ctrl.Ctrl0.DataSize = Length;
        Ctrl.Ctrl0.WR = 0;      //Read
        Ctrl.Ctrl0.CS = 0;      //Keep CS# signal to low
        Ctrl.Ctrl0.RUN = 1;
         SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);

        if(SpiIdle(PciDevice) == ASMT_SUCCESS)
        {
                 Data.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_DATA);

                pByte = (BYTE *)pData;
                for(i = 0; i < Length; i++)
                {
                        *pByte = Data.Data[i];
                        pByte ++;
                }
                return ASMT_SUCCESS;
        }
        else
        {
                return ASMT_TIMEOUT;   //time out
        }
}


//
//Procedure:    SpiWrite
//Description:  Write data to SPI flash ROM
//Input:    pSpiControlExtension    - SPI Controller Extension
//          pData                   - Data buffer pointer
//          Length                  - Data length
//Output:   ASMT_SUCCESS    - Write data success
//          ASMT_TIMEOUT   - Otherwise
//
int SpiWrite(struct pci_dev *PciDevice, void *pData, BYTE Length)
{
        LSPI_DATA       Data;
        LSPI_CONTROL    Ctrl;
        BYTE        i;
        BYTE *pByte;

        if(SpiIdle(PciDevice) == ASMT_TIMEOUT)
        {
                return ASMT_TIMEOUT;
        }

        if((Length > ASM116_LSPI_MAXIMUM_LENGTH) || (Length < 1))
        {
                return ASMT_PARAMETER_INVALID;
        }

        Data.AsDword = 0L;
        pByte = (BYTE *)pData;
        //printk(KERN_INFO "SpiWrite");
        for(i = 0; i < Length; i++)
        {
                Data.Data[i] = *pByte;
             //printk(KERN_INFO "[0x%X]",Data.Data[i]);
                pByte ++;
        }
         SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_DATA, Data.AsDword);

        Ctrl.AsDword = SpiReadRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL);
        Ctrl.Ctrl0.AsByte = 0;
        Ctrl.Ctrl0.DataSize = Length;
        Ctrl.Ctrl0.WR = 1;      //Write
        Ctrl.Ctrl0.CS = 0;      //Keep CS# signal to low
        Ctrl.Ctrl0.RUN = 1;
        SpiWriteRegisterDword(PciDevice, ASM116_SPI_CONTROL_CONTROL, Ctrl.AsDword);

        return (SpiIdle(PciDevice));
}



