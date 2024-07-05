/*
 * Asmedia ASM116 Firmware Update Tool
 *
 * Copyright (C) 2014-2016 ASMedia Technology
 */

#ifndef _ASM116_H_
#define _ASM116_H_


#define ASMedia_VENDOR_ID       0x1b21

#define ASM116_DEVICE_1062      0x1062
#define ASM116_DEVICE_1064      0x1064
#define ASM116_DEVICE_1164      0x1164
#define ASM116_DEVICE_1165      0x1165
#define ASM116_DEVICE_1166      0x1166



#define VID_REG                     0x00    /* offset 0x00-02: Vendor ID */
#define DID_REG                     0x02    /* offset 0x02-04: Device ID */
#define PCI_COMMAND_REG             0x04    /* offset 0x04: Command register */
#define PCI_IO_SPACE_ENABLED        0x01    /* bit 0: IO space enable */
#define PCI_MEMORY_SPACE_ENABLED    0x02    /* bit 1: Memory space enable */
#define PCI_BUS_MASTER_ENABLED      0x04    /* bit 2: Bus master enable */
#define REVISION_REG                0x08    /* offset 0x08: Revision ID register */
#define PROGRAMMING_INTERFACE_REG   0x09    /* offset 0x09: Programming interface */
#define SUB_CLASS_REG               0x0A    /* offset 0x0A: Sub class code */
#define BASE_CLASS_REG              0x0B    /* offset 0x0B: Base class code */
#define BAR0_REG                    0x10    /* offset 0x10-13: Base Address 0 */
#define IDE_PRI_COMMAND_BASE_REG    BAR0_REG
#define BAR1_REG                    0x14    /* offset 0x14-17: Base Address 1 */
#define IDE_PRI_CONTROL_BASE_REG    BAR1_REG
#define BAR2_REG                    0x18    /* offset 0x18-1B: Base Address 2 */
#define IDE_SEC_COMMAND_BASE_REG    BAR2_REG
#define BAR3_REG                    0x1C    /* offset 0x1C-1F: Base Address 3 */
#define IDE_SEC_CONTROL_BASE_REG    BAR3_REG
#define BAR4_REG                    0x20    /* offset 0x20-23: Base Address 4 */
#define IDE_BM_BASE_REG             BAR4_REG
#define BAR5_REG                    0x24    /* offset 0x24-27: Base Address 5 */
#define SATA_AHCI_BASE_REG          BAR5_REG
#define SVID_REG                    0x2C    /* offset 0x2C-2D: Subsystem Vendor ID */
#define SDID_REG                    0x2E    /* offset 0x2E-2F: Subsystem Device ID */
#define EXPANSION_ROM_BASE_REG      0x30    /* offset 0x30-33: Expansion ROM Base Address */
#define INTERRUPT_LINE_REG          0x3C    /* offset 0x3C: Interrupt line (IRQ) */
#define INTERRUPT_PIN_REG           0x3D    /* offset 0x3D: Interrupt Pin (INTA,B,C,D) */

extern unsigned char   *pMemPtr;

#endif  /* _ASM116_H_ */

