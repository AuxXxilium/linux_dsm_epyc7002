/**************************************************************************************
*Filename: crc.h
*Description:
*   Calculate 32 bits CRC
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

#ifndef _CRC_H
#define _CRC_H
#include      "precomp.h"


	// unsigned long  gUiCRCTab[256];             //CRC initial table

unsigned long Reflect(unsigned long ReflectValue, unsigned char ReflectLength);
int Crc32Init(void);
//UINT32 GetCrc32(UINT8* pBuffer, UINTN BufferLength);
unsigned long GetCrc32(unsigned char* pBuffer, unsigned long BufferLength);

#endif  //_CRC_H
