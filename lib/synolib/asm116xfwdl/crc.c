/**************************************************************************************
*Filename: crc.c
*Description:
*   Calculate 32 bits CRC. Modify from 2105 MPTool function
*
*History:
*   2013/04/02  V1.1    Jesse Chang     Modify BufferLength in GetCrc32 to UINT32
*   2012/07/24  V1.0    Jesse Chang     First version
*
*
* Asmedia ASM116x Firmware Update Tool
*
* Copyright (C) 2014-2016 ASMedia Technology
*
***************************************************************************************/
//#include "stdafx.h"
#include    "crc.h"

unsigned long  gUiCRCTab[256];             //CRC initial table

//
//Procedure:    Reflect
//Description:  Reflect 32 bits value. Only called by Crc32Init
//Input:    ReflectValue    - Value to reflect
//          ReflectLength   - Length to reflect
//Output:   Reflected value
//
unsigned long Reflect(unsigned long ReflectValue, unsigned char ReflectLength)
{
//Used only by Init_CRC32_Table()
    unsigned long  value = 0;
    unsigned int   i;

    // Swap bit 0 for bit 7
    // bit 1 for bit 6, etc.
    for(i = 1; i < (unsigned int)(ReflectLength + 1); i++)
    {
        if(ReflectValue & 1)
        {
            value |= 1L << (ReflectLength - i);
        }
        ReflectValue >>= 1;
    }
    return value;
}


//
//Procedure:    Crc32Init
//Description:  Init CRC32 table
//Input:    None
//Output:   None
//
int Crc32Init(void)
{
    unsigned long  ulPolynomial = 0x04c11db7L;
    unsigned int   i, j;

    // 256 values representing ASCII character codes.
    for(i = 0; i <= 0xFF; i++)
    {
        gUiCRCTab[i] = Reflect(i, 8) << 24;
        for (j = 0; j < 8; j++)
        {
            gUiCRCTab[i] = (gUiCRCTab[i] << 1) ^ (gUiCRCTab[i] & (1L << 31) ? ulPolynomial : 0);
            //gUiCRCTab[i] = (gUiCRCTab[i] << 1) ^ (gUiCRCTab[i] & (0x80000000L) ? ulPolynomial : 0);
        }
        gUiCRCTab[i] = Reflect(gUiCRCTab[i], 32);
    }
    return ASMT_SUCCESS;
}


//
//Procedure:    GetCrc32
//Description:  Get 32 bits CRC.
//Input:    pBuffer         - Data buffer to calculate CRC32
//          BufferLength    - Data buffer length
//Output:   32 bits CRC
//
unsigned long GetCrc32(unsigned char* pBuffer, unsigned long BufferLength)
{
    unsigned long  crc = 0xFFFFFFFF;
    unsigned long  len;

    Crc32Init();                //Initial CRC32 table
    len = BufferLength;
    // Perform the algorithm on each character
    // in the string, using the lookup table values.
    while(len--)
    {
        crc = (crc >> 8) ^ gUiCRCTab[(crc & 0xFF) ^ *pBuffer++];
    }
    // Exclusive OR the result with the beginning value.
    return (crc^0xFFFFFFFF);
}



