/*
 * RTK NAND Flash controller driver.
 * Copyright (C) 2020 Realtek Inc.
 * Authors : PK Chuang	<pk.chuang@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/reset.h>
#include "rtk_nand.h"


#define RTK_NAME                "rtk-nand"
#define RTK_TIMEOUT		(500000)
#define RTK_RESET_TIMEOUT	(1000000)

#define NF_CTL_ENABLE		1
#define NF_CTL_DISABLE		0
#define RTK_DMABUF_LEN		1024*256
#define RTK_OOBBUF_LEN		1024

#define ECC	0
#define RAW	1
#define NoOOB	0
#define OOB	1

struct nand_flash_dev rtk_nand_flash_ids[] = {
	{"W29N04GV 4G",
                { .id = {0xef, 0xdc, 0x90, 0x95, 0x54, 0x00, 0xff, 0xff} },
                  SZ_2K, SZ_256M, SZ_128K, 0, 6, 64, NAND_ECC_INFO(6, 1),
                  2 },
	{NULL}
};

static inline struct rtk_nf *to_rtk_nand(struct nand_chip *chip)
{
	return container_of(chip, struct rtk_nf, chip);
}

static int rtk_nf_wait_down(void __iomem *regs, u64 mask, unsigned int value)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout_atomic(regs, val, (val & mask) == value, 10, 
					RTK_TIMEOUT);
	if (ret)
		return -EIO;

	return 0;
}

static void rtk_nf_reset_nandbuf(struct rtk_nf *nf)
{
	struct nand_chip *chip = &nf->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_buffer *buf = &nf->nandbuf;

	memset(buf->tempbuf, 0x0, sizeof(buf->tempbuf));

	if (buf->dataBuf)
		memset(buf->dataBuf, 0x0, mtd->writesize + mtd->oobsize);

	buf->index_r = 0;
	buf->index_w = 0;
}

static int rtk_nf_init_nandbuf(struct nand_chip *chip)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct mtd_info *mtd = mtd = nand_to_mtd(chip);
	struct rtk_buffer *buf = &nf->nandbuf;

	buf->dataBuf = (unsigned char *)dma_alloc_coherent(nf->dev, 
			mtd->writesize + mtd->oobsize, &buf->dataPhys, 
			GFP_DMA | GFP_KERNEL);
	if ( !buf->dataBuf ) {
		dev_err(nf->dev, "no enough memory for dataBuf\n");
		return -ENOMEM;
	}

	memset(buf->dataBuf, 0x0, mtd->writesize + mtd->oobsize);

	buf->index_r = 0;
	buf->index_w = 0;

	return 0;
}

static inline void rtk_nf_write_byte(struct nand_chip *chip, unsigned char val)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buf = &nf->nandbuf;

	if (buf->dataBuf)
		buf->dataBuf[buf->index_w] = val;
	else
		buf->tempbuf[buf->index_w] = val;

	buf->index_w++;
}

static inline u8 rtk_nf_read_byte(struct nand_chip *chip)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buf = &nf->nandbuf;
	u8 byte;

	if (buf->dataBuf)	
		byte = buf->dataBuf[buf->index_r];
	else
		byte = buf->tempbuf[buf->index_r];

	buf->index_r++;

	return byte;
}

static void rtk_nf_read_buf(struct nand_chip *chip, u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = rtk_nf_read_byte(chip);
}

static int rtk_nf_do_write_page(struct nand_chip *chip, int page, int raw)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	void __iomem *base = nf->regs;
	unsigned int dram_sa, dma_len, spare_dram_sa;
	struct rtk_buffer *buffer = &nf->nandbuf;
	dma_addr_t dataPhy = buffer->dataPhys;
	u32 ecc = (mtd->ecc_strength == 6) ? 0x0 : 0x1;
	int ret = 0;

	writel(NF_DATA_TL0_length0(0), base + REG_DATA_TL0);

	writel(0x1, base + REG_RND_EN);
	writel(0x85, base + REG_RND_CMD1);
	writel(0x0, base + REG_RND_DATA_STR_COL_H);
	writel((mtd->writesize >> 8), base + REG_RND_SPR_STR_COL_H);
	writel((mtd->writesize & 0xff), base + REG_RND_SPR_STR_COL_L);
	writel(NF_DATA_TL1_length1(2), base + REG_DATA_TL1);

	writel(NF_PAGE_LEN_page_len(mtd->writesize >> 9), base + REG_PAGE_LEN);

	/* Set PP */
	writel(NF_READ_BY_PP_read_by_pp(0), base + REG_READ_BY_PP);
	writel(NF_PP_CTL1_pp_start_addr(0), base + REG_PP_CTL1);
	writel(0x0, base + REG_PP_CTL0);

	/* Set command */
	writel(NF_ND_CMD_cmd(NAND_CMD_SEQIN), base + REG_ND_CMD);
	writel(NF_CMD2_cmd2(NAND_CMD_PAGEPROG), base + REG_CMD2);
	writel(NF_CMD3_cmd3(NAND_CMD_STATUS), base + REG_CMD3);

	/* set address */
	writel(NF_ND_PA0_page_addr0(page), base + REG_ND_PA0);
	writel(NF_ND_PA1_page_addr1(page >> 8), base + REG_ND_PA1);
	writel(NF_ND_PA2_addr_mode(1) | NF_ND_PA2_page_addr2(page >> 16), 
		base + REG_ND_PA2);
	writel(NF_ND_PA3_page_addr3((page >> 21) & 0x7), base + REG_ND_PA3);
	writel(0x0, base + REG_ND_CA0);
	writel(0x0, base + REG_ND_CA1);

	/* set ECC */
	writel(NF_MULTI_CHNL_MODE_edo(1), base + REG_MULTI_CHNL_MODE);
	writel(NF_ECC_STOP_ecc_n_stop(1), base + REG_ECC_STOP);
	writel(ecc, base + REG_ECC_SEL);

	dram_sa = ((uintptr_t)dataPhy >> 3);
	dma_len = mtd->writesize >> 9;
	writel(NF_DMA_CTL1_dram_sa(dram_sa), base + REG_DMA_CTL1);
	writel(NF_DMA_CTL2_dma_len(dma_len), base + REG_DMA_CTL2);

	spare_dram_sa = ( (uintptr_t)(dataPhy + mtd->writesize) >> 3);
	writel(0x60000000 | NF_SPR_DDR_CTL_spare_dram_sa(spare_dram_sa), 
		base + REG_SPR_DDR_CTL);
	writel(NF_DMA_CTL3_ddr_wr(0)|NF_DMA_CTL3_dma_xfer(1), 
		base + REG_DMA_CTL3);

	writel(NF_AUTO_TRIG_auto_trig(1) | NF_AUTO_TRIG_spec_auto_case(0) | 
		NF_AUTO_TRIG_auto_case(1), 
		base + REG_AUTO_TRIG);

	if ((ret = rtk_nf_wait_down(base + REG_AUTO_TRIG, 0x80, 0x0)) != 0) {
		goto rtk_nf_do_write_page_exit;
	}

	if ((ret = rtk_nf_wait_down(base + REG_DMA_CTL3, 0x1, 0x0)) != 0) {
		goto rtk_nf_do_write_page_exit;
	}

	writel(NF_POLL_FSTS_bit_sel(6) | NF_POLL_FSTS_trig_poll(1),
		base + REG_POLL_FSTS);	
	if ((ret = rtk_nf_wait_down(base + REG_POLL_FSTS, 0x1, 0x0)) != 0) {
		goto rtk_nf_do_write_page_exit;
	}

	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x40, 0x40)) != 0) {
		goto rtk_nf_do_write_page_exit;
	}

	if (readl(base + REG_ND_DAT) & 0x1) {
		ret = -1;
	}

rtk_nf_do_write_page_exit:
	return ret;
}

static int rtk_nf_write_page(struct nand_chip *chip, 
			     const u8 *buf, int page, int raw)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	int r_page = page;
	int ret;
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	struct nand_memory_organization *memorg = &chip->base.memorg;
	int o_blk = page / memorg->pages_per_eraseblock;

rtk_nf_write_page_retry:
	r_page = rtk_nf_get_realpage(chip, page);
#endif

	memset(buffer->dataBuf, 0xFF, mtd->writesize + mtd->oobsize);

	if (!buf) { /* write oob only */
		ret = chip->ecc.read_oob(chip, page);
		if (ret)
			return -1;

		memcpy(buffer->dataBuf + mtd->writesize, chip->oob_poi, 
			mtd->oobsize);
	} else {
		memcpy(buffer->dataBuf, buf, mtd->writesize);
	}

	ret = rtk_nf_do_write_page(chip, r_page, raw);

#if defined(CONFIG_MTD_NAND_RTK_BBM)
	if (ret) {
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 1, NFWRITE);
		if (!ret)
			goto rtk_nf_write_page_retry;
	}
#endif
	return ret;
}

static int rtk_nf_write_page_raw(struct nand_chip *chip, const u8 *buf, 
				 int oob_on, int page)
{
	return rtk_nf_write_page(chip, buf, page, 1);
}

static int rtk_nf_write_page_hwecc(struct nand_chip *chip, const u8 *buf,
				   int oob_on, int page)
{
	return rtk_nf_write_page(chip, buf, page, 0);
}

static int rtk_nf_write_oob(struct nand_chip *chip, int page)
{

	return rtk_nf_write_page(chip, NULL, page, 0);
}

static void rtk_read_oob_from_SRAM(struct mtd_info *mtd, __u8 *oobbuf)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rtk_nf *nf = to_rtk_nand(chip);
	void __iomem *base = nf->regs;
	unsigned int reg_oob;
        int i;
        char r_oobbuf[256];
        int oobuse_size = 0;
	u32 ecc = (mtd->ecc_strength == 6) ? 0x0 : 0x1;

	writel(0x0, base + REG_READ_BY_PP);
//#ifdef CONFIG_ARCH_RTD13xx
	writel(0x30 | 0x02, base + REG_SRAM_CTL);
//#else
//	writel(0x30 | 0x04, base + REG_SRAM_CTL);
//#endif

        memset(r_oobbuf, 0xFF, 256);
        memset(oobbuf, 0xFF, mtd->oobsize);

        switch (ecc) {
        case 0x0:
                oobuse_size = 6 + 10;
                break;
        case 0x1:
                oobuse_size = 6 + 20;
                break;
        default:
                oobuse_size = 6 + 10;
                break;
        }

        for (i = 0; i < (mtd->oobsize/4); i++) {
                reg_oob = readl(base+(i*4));

                r_oobbuf[i*4] = reg_oob & 0xff;
                r_oobbuf[(i*4)+1] = (reg_oob >> 8) & 0xff;
                r_oobbuf[(i*4)+2] = (reg_oob >> 16) & 0xff;
                r_oobbuf[(i*4)+3] = (reg_oob >> 24) & 0xff;
        }

        for (i = 0; i < 4; i++) {
                memcpy(oobbuf+(i*oobuse_size), r_oobbuf+(i*(mtd->oobsize/4)), 
			oobuse_size);
        }

	writel(0x0, base + REG_SRAM_CTL);
	writel(0x80, base + REG_READ_BY_PP);

        return;
}

static int rtk_nf_do_read_page(struct mtd_info *mtd, int page, 
			       int oob, int raw)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct device *dev = nf->dev;
	void __iomem *base = nf->regs;
	unsigned int dram_sa, dma_len;
	struct rtk_buffer *buffer = &nf->nandbuf;
	dma_addr_t dataPhy = buffer->dataPhys;
	u32 ecc = (mtd->ecc_strength == 6) ? 0x0 : 0x1;
	int ret;
	unsigned int eccNum = 0;
        unsigned int blank_check = 0;
        unsigned int blank_confirm = 0;

blank_confirm_read:
	writel(0x1, base + REG_RND_EN);
	writel(0x5, base + REG_RND_CMD1);
	writel(0xe0, base + REG_RND_CMD2);
	writel(0x0, base + REG_RND_DATA_STR_COL_H);
	writel((mtd->writesize >> 8), base + REG_RND_SPR_STR_COL_H);
	writel((mtd->writesize & 0xff), base + REG_RND_SPR_STR_COL_L);
	writel(NF_DATA_TL0_length0(512), base + REG_DATA_TL0);
	writel(NF_DATA_TL1_access_mode(1) | NF_DATA_TL1_length1(2), 
			base + REG_DATA_TL1);
	writel(NF_PAGE_LEN_page_len((mtd->writesize >> 9)), 
			base + REG_PAGE_LEN);

	/* set PP */
	writel(NF_READ_BY_PP_read_by_pp(1), base + REG_READ_BY_PP);
	writel(NF_PP_CTL1_pp_start_addr(0), base + REG_PP_CTL1);
	writel(0x0, base + REG_PP_CTL0);

	/* enable blank PP */
	if (blank_confirm) {
		writel(NF_BLANK_CHK_blank_ena(1) | 
			NF_BLANK_CHK_read_ecc_xnor_ena(1), 
				base + REG_BLANK_CHK);
	} else {
		writel(NF_BLANK_CHK_blank_ena(1) | 
			NF_BLANK_CHK_read_ecc_xnor_ena(0), 
				base + REG_BLANK_CHK);
	}

	/* set command */
	writel(NF_ND_CMD_cmd(NAND_CMD_READ0), base + REG_ND_CMD);
	writel(NF_CMD2_cmd2(NAND_CMD_READSTART), base + REG_CMD2);
	writel(NF_CMD3_cmd3(NAND_CMD_STATUS), base + REG_CMD3);

	/* set address */
	writel(NF_ND_PA0_page_addr0(0xff&page), base + REG_ND_PA0);
	writel(NF_ND_PA1_page_addr1(0xff&(page>>8)), base + REG_ND_PA1);
	writel(NF_ND_PA2_addr_mode(0x1) | 
		NF_ND_PA2_page_addr2(0x1f&(page>>16)), base + REG_ND_PA2);
	writel(NF_ND_PA3_page_addr3(0x7&(page>>21)), base + REG_ND_PA3);
	writel(0x0, base + REG_ND_CA0);
	writel(0x0, base + REG_ND_CA1);

	/* Set ECC */
	writel(NF_MULTI_CHNL_MODE_edo(0x1), base + REG_MULTI_CHNL_MODE);
	writel(NF_ECC_STOP_ecc_n_stop(0x1), base + REG_ECC_STOP);
	writel(ecc, base + REG_ECC_SEL);

	dram_sa = ((uintptr_t)dataPhy >> 3);
	dma_len = mtd->writesize >> 9;
	writel(NF_DMA_CTL1_dram_sa(dram_sa), base + REG_DMA_CTL1);
	writel(NF_DMA_CTL2_dma_len(dma_len), base + REG_DMA_CTL2);

	writel(NF_DMA_CTL3_ddr_wr(1)|NF_DMA_CTL3_dma_xfer(1), 
					base + REG_DMA_CTL3);


	/* Enable Auto mode */
	writel(NF_AUTO_TRIG_auto_trig(1) | NF_AUTO_TRIG_spec_auto_case(0) | 
		NF_AUTO_TRIG_auto_case(2), base + REG_AUTO_TRIG);

	if ((ret = rtk_nf_wait_down(base + REG_AUTO_TRIG, 0x80, 0x0)) != 0) {
		goto rtk_nf_do_read_page_exit;
	}

	if ((ret = rtk_nf_wait_down(base + REG_DMA_CTL3, 0x1, 0x0)) != 0) {
		goto rtk_nf_do_read_page_exit;
	}

	if (oob)
		rtk_read_oob_from_SRAM(mtd, buffer->dataBuf + mtd->writesize);

	if (blank_confirm) {
		blank_confirm = 0;

		if (readl(base + REG_ND_ECC) & 0x8) {
			dev_err(dev, "RTK %s(%d) read error, page:0x%x\n", 
				__func__, __LINE__, page);
			writel(NF_BLANK_CHK_blank_ena(1) | 
				NF_BLANK_CHK_read_ecc_xnor_ena(0), 
				base + REG_BLANK_CHK);
			ret = -1;
			goto rtk_nf_do_read_page_exit;
		} else {
			if (oob)
				memset(buffer->dataBuf + mtd->writesize
						, 0xFF, mtd->oobsize);

			writel(NF_BLANK_CHK_blank_ena(1) | 
				NF_BLANK_CHK_read_ecc_xnor_ena(0), 
				base + REG_BLANK_CHK);
			ret = 1;
			goto rtk_nf_do_read_page_exit;
		}
	} else {
		blank_check = readl(base + REG_BLANK_CHK);
		if (blank_check & 0x2) {
			writel(NF_BLANK_CHK_blank_ena(1) | 
				NF_BLANK_CHK_read_ecc_xnor_ena(0), 
				base + REG_BLANK_CHK);
			ret = 1;
			goto rtk_nf_do_read_page_exit;
		} else if (readl(base + REG_ND_ECC) & 0x8) {
			blank_confirm = 1;
			dev_err(dev, "RTK %s(%d) ecc error...blank_confirm_read.\n", 
				__func__, __LINE__);
			writel(NF_BLANK_CHK_blank_ena(1) | 
				NF_BLANK_CHK_read_ecc_xnor_ena(0), 
				base + REG_BLANK_CHK);
			goto blank_confirm_read;
		} else {
			if (readl(base + REG_ND_ECC) & 0x04) {
				eccNum = readl(base + REG_MAX_ECC_NUM)&0xff;
				if (eccNum > (mtd->ecc_strength - 2)) {
					dev_warn(dev, "RTK %s(%d) ecc over threshold.\n", 
						__func__, __LINE__);

					writel(NF_BLANK_CHK_blank_ena(1) | 
						NF_BLANK_CHK_read_ecc_xnor_ena(0), 
						base + REG_BLANK_CHK);
					ret = 2;
					goto rtk_nf_do_read_page_exit;
				}

				writel(NF_BLANK_CHK_blank_ena(1) | 
					NF_BLANK_CHK_read_ecc_xnor_ena(0), 
					base + REG_BLANK_CHK);
				ret = 0;
				goto rtk_nf_do_read_page_exit;
			}
		}
	}

rtk_nf_do_read_page_exit:
	return ret;
}

static int rtk_nf_read_page_raw(struct nand_chip *chip, u8 *p, 
				int oob_on, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	int r_page = page;
	int ret;
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	struct nand_memory_organization *memorg = &chip->base.memorg;
	int o_blk = page / memorg->pages_per_eraseblock;
	r_page = rtk_nf_get_realpage(chip, page);
#endif
	ret = rtk_nf_do_read_page(mtd, r_page, NoOOB, RAW);
	if (ret >= 0)
		memcpy(p, buffer->dataBuf, mtd->writesize);

#if defined(CONFIG_MTD_NAND_RTK_BBM)
	if (ret < 0)
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 0, NFREAD);
	else if (ret == 2) /* ecc bit over threshold */
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 1, NFREAD);
#endif

	return ret;
}

static int rtk_nf_read_page_hwecc(struct nand_chip *chip, u8 *p, 
				  int oob_on, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	int r_page = page;
	int ret;
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	struct nand_memory_organization *memorg = &chip->base.memorg;
	int o_blk = page / memorg->pages_per_eraseblock;
	r_page = rtk_nf_get_realpage(chip, page);
#endif
	ret = rtk_nf_do_read_page(mtd, r_page, NoOOB, ECC);
	if (ret >= 0)
		memcpy(p, buffer->dataBuf, mtd->writesize);

#if defined(CONFIG_MTD_NAND_RTK_BBM)
	if (ret < 0)
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 0, NFREAD);
	else if (ret == 2) /* ecc bit over threshold */
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 1, NFREAD);
#endif

	return ret;
}

static int rtk_nf_read_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
        struct rtk_buffer *buffer = &nf->nandbuf;
	int r_page = page;
	int ret;
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	struct nand_memory_organization *memorg = &chip->base.memorg;
	int o_blk = page / memorg->pages_per_eraseblock;
	r_page = rtk_nf_get_realpage(chip, page);
#endif
	ret = rtk_nf_do_read_page(mtd, r_page, OOB, ECC);
	if (ret >= 0)
		memcpy(chip->oob_poi, buffer->dataBuf + mtd->writesize, mtd->oobsize);

#if defined(CONFIG_MTD_NAND_RTK_BBM)
	if (ret < 0)
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 0, NFREAD);
	else if (ret == 2) /* ecc bit over threshold */
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 1, NFREAD);
#endif

	return ret;	
}

static int rtk_nf_do_erase_block(struct nand_chip *chip, int page)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct device *dev = nf->dev;
	void __iomem *base = nf->regs;
	int ret = 0;

	writel(NF_MULTI_CHNL_MODE_no_wait_busy(1) | NF_MULTI_CHNL_MODE_edo(1),
		base + REG_MULTI_CHNL_MODE);

	writel(NF_ND_CMD_cmd(NAND_CMD_ERASE1), base + REG_ND_CMD);
	writel(NF_CMD2_cmd2(NAND_CMD_ERASE2), base + REG_CMD2);
	writel(NF_CMD3_cmd3(NAND_CMD_STATUS), base + REG_CMD3);

	writel(NF_ND_PA0_page_addr0(page), base + REG_ND_PA0);
	writel(NF_ND_PA1_page_addr1(page>>8), base + REG_ND_PA1);
	writel(NF_ND_PA2_addr_mode(0x04) | NF_ND_PA2_page_addr2(page >> 16),
		base + REG_ND_PA2);
	writel(NF_ND_PA3_page_addr3((page >> 21) & 0x7), base + REG_ND_PA3);

	writel(NF_AUTO_TRIG_auto_trig(1) | NF_AUTO_TRIG_spec_auto_case(1) | 
		NF_AUTO_TRIG_auto_case(2), base + REG_AUTO_TRIG);
	if ((ret = rtk_nf_wait_down(base + REG_AUTO_TRIG, 0x80, 0x0)) != 0) {
		goto rtk_nf_do_erase_block_exit;
	}

	writel(NF_POLL_FSTS_bit_sel(6) | NF_POLL_FSTS_trig_poll(1), 
		base + REG_POLL_FSTS);
	if ((ret = rtk_nf_wait_down(base + REG_POLL_FSTS, 0x1, 0x0)) != 0) {
		goto rtk_nf_do_erase_block_exit;
	}

	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x40, 0x40)) != 0) {
		goto rtk_nf_do_erase_block_exit;
	}

	if (readl(base + REG_ND_DAT) & 0x1) {
		dev_err(dev, "RTK %s(%d) erase fail.\n", __func__, __LINE__);
		ret = -1;
	}

rtk_nf_do_erase_block_exit:
	return ret;
}

static int rtk_nf_erase_block(struct nand_chip *chip, int page)
{
	int r_page = page;
	int ret;
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	struct nand_memory_organization *memorg = &chip->base.memorg;
	int o_blk = page / memorg->pages_per_eraseblock;

rtk_nf_erase_block_retry:
	r_page = rtk_nf_get_realpage(chip, page);
#endif
	ret = rtk_nf_do_erase_block(chip, r_page);
#if defined(CONFIG_MTD_NAND_RTK_BBM)
	if (ret < 0) {
		ret = rtk_nf_bb_handle(chip, o_blk, r_page, 0, NFERASE);
		if (ret == 0)
			goto rtk_nf_erase_block_retry;
	}
#endif
	return ret;
}

static int rtk_nf_block_bad(struct nand_chip *chip, loff_t ofs)
{
#ifndef CONFIG_MTD_NAND_RTK_BBM
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg = &chip->base.memorg;
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	u32 ppb = memorg->pages_per_eraseblock;
	u64 blk = (u64)ofs;
	int i;

	do_div(blk, mtd->erasesize);

	/* check 1st & 2nd page */
	for (i=0; i<2; i++) {
		if (chip->ecc.read_oob(chip, blk*ppb + i) < 0)
			return -1;

		if (*(buffer->dataBuf + mtd->writesize) != 0xff)
			return -1;
	}
#endif
	return 0;
}

static void rtk_nf_read_id(struct nand_chip *chip)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	void __iomem *base = nf->regs;
	int ret, i;
	int id_chain;

	writel(6, base + REG_DATA_TL0);
	writel(0x80, base + REG_DATA_TL1);

	/* Set PP */
	writel(0x0, base + REG_READ_BY_PP);
	writel(0x01, base + REG_PP_CTL0);
	writel(0x0, base + REG_PP_CTL1);

	/* Set command */
	writel(0x90, base + REG_ND_CMD);
	writel(0x80, base + REG_ND_CTL);
	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x80, 0x0)) != 0) {
		return;
        }

	/* Set address */
	writel(0x0, base + REG_ND_PA0);
	writel(0x0, base + REG_ND_PA1);
	writel(0x7 << 5, base + REG_ND_PA2);
	writel(0x81, base + REG_ND_CTL);
	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x80, 0x0)) != 0) {
		return;
        }

	/* Enable XFER mode */
	writel(0x84, base + REG_ND_CTL);
	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x80, 0x0)) != 0) {
		return;
        }
	
	/* reset PP */
	writel(0x2, base + REG_PP_CTL0);

	/* Move data to DRAM from SRAM */
	writel(0x30, base + REG_SRAM_CTL);

	id_chain = readl(base + REG_ND_PA0);
	for (i = 0; i < 4; i++) {
		rtk_nf_write_byte(chip, (id_chain >> (8*i)) & 0xff);
	}

	id_chain = readl(base + REG_ND_PA1);
	for (i = 0; i < 4; i++) {
		rtk_nf_write_byte(chip, (id_chain >> (8*i)) & 0xff);
	}

	writel(0x0, base + REG_SRAM_CTL);

	return;
	
}

static void rtk_nf_status(struct nand_chip *chip)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buf = &nf->nandbuf;
	u32 val = 0x0;

	val |= NAND_STATUS_WP;
	val |= NAND_STATUS_READY;

	buf->dataBuf[buf->index_w] = val;
	buf->index_w++;
}

static int rtk_nf_ooblayout_ecc(struct mtd_info *mtd, int section,
                                struct mtd_oob_region *oobregion)
{
        if (section)
                return -ERANGE;

	oobregion->offset = mtd->writesize;
	oobregion->length = 6;

        return 0;
}

static int rtk_nf_ooblayout_free(struct mtd_info *mtd, int section,
                                 struct mtd_oob_region *oobregion)
{
        if (section)
                return -ERANGE;

	oobregion->length = 6;
	oobregion->offset = mtd->writesize + (section * 16);

        return 0;
}

static const struct mtd_ooblayout_ops rtk_nf_ooblayout_ops = {
        .ecc = rtk_nf_ooblayout_ecc,
        .free = rtk_nf_ooblayout_free,
};

static void rtk_nf_update_ecc_info(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (chip->ecc.mode != NAND_ECC_HW) {
		return;
	}

	if (mtd->writesize == SZ_2K)
		mtd->ecc_strength = 6;
	else if (mtd->writesize == SZ_4K)
		mtd->ecc_strength = 12;

	chip->ecc.steps	= 4;
	chip->ecc.size	= 512;
	chip->ecc.bytes	= (mtd->writesize == SZ_2K) ? 10 : 20;
	chip->ecc.strength = mtd->ecc_strength;

        mtd_set_ooblayout(mtd, &rtk_nf_ooblayout_ops);
}

static int rtk_nf_wait(struct nand_chip *chip)
{
	/* nop */
	return 0;
}

static void rtk_nf_command(struct nand_chip *chip, unsigned int command,
				int column, int page_addr)
{
	struct rtk_nf *nf = to_rtk_nand(chip);

	rtk_nf_reset_nandbuf(nf);

	switch (command) {
		case NAND_CMD_RESET:
			break;
		
		case NAND_CMD_READID:
			rtk_nf_read_id(chip);
			break;

		case NAND_CMD_READ0:
			break;

		case NAND_CMD_STATUS:
			rtk_nf_status(chip);
			break;

		case NAND_CMD_ERASE1:
			rtk_nf_erase_block(chip, page_addr);
			break;

		default:
			break;
	}
}

static void rtk_nf_select_chip(struct nand_chip *chip, int cs)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	void __iomem *base = nf->regs;
	unsigned long value = 0x0;

	switch (cs) {
		case -1:
			value = 0xff;
			break;
		case 0:
		case 1:
		case 2:
		case 3:
			value = ~(BIT(cs));
			break;
		default:
			value = ~(BIT(0));
	}

	writel(value, base + REG_PD);
}

static inline int rtk_nf_hw_init(struct rtk_nf *nf)
{
	void __iomem *base = nf->regs;
	int ret;

	/* init controller */
	writel(0x1E, base + REG_PD);
	writel(0x2, base + REG_TIME_PARA3);
	writel(0x5, base + REG_TIME_PARA2);
	writel(0x2, base + REG_TIME_PARA1);

	writel(0x0, base + REG_MULTI_CHNL_MODE);
	writel(0x0, base + REG_READ_BY_PP);

	/* reset nand */
	writel(0xff, base + REG_ND_CMD);
	writel(0x80, base + REG_ND_CTL);

	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x80, 0x0)) != 0)
		return ret;

	if ((ret = rtk_nf_wait_down(base + REG_ND_CTL, 0x40, 0x40)) != 0)
		return ret;
#if 1
	writel(readl(base + REG_NF_LOW_PWR) &~0x10, base + REG_NF_LOW_PWR);
	writel(NF_SPR_DDR_CTL_spare_ddr_ena(1) |
		NF_SPR_DDR_CTL_per_2k_spr_ena(1) |
		NF_SPR_DDR_CTL_spare_dram_sa(0), base + REG_SPR_DDR_CTL);
#endif
	return 0;
}

static int rtk_nand_clk_reset_ctrl(struct device *dev, 
				    struct rtk_nf_clk_rst *clkrst, 
				    int enable)
{
	int ret = 0;	

	if (!clkrst->clk) {
		clkrst->clk = devm_clk_get(dev, "nand");
		if (IS_ERR(clkrst->clk)) {
			printk(KERN_ERR "%s: devm_clk_get() returns %ld\n", 
					__func__, PTR_ERR(clkrst->clk));
			clkrst->clk = NULL;
			goto rtk_nand_clk_reset_ctrl_exit;
		}
	}

	if (enable == NF_CTL_ENABLE)
		ret = clk_prepare_enable(clkrst->clk);
	else
		clk_disable_unprepare(clkrst->clk);

	if (!ret)
		clk_put(clkrst->clk);

rtk_nand_clk_reset_ctrl_exit:
	return ret;
}

static int rtk_nf_enable_clk(struct device *dev, struct rtk_nf_clk_rst *clkrst)
{
	return rtk_nand_clk_reset_ctrl(dev, clkrst, NF_CTL_ENABLE);

}

static int rtk_nf_disable_clk(struct device *dev, struct rtk_nf_clk_rst *clkrst)
{
	return rtk_nand_clk_reset_ctrl(dev, clkrst, NF_CTL_DISABLE);
}

static int rtk_nf_ecc_init(struct nand_chip *chip)
{
	//struct mtd_info *mtd = nand_to_mtd(chip);

	rtk_nf_update_ecc_info(chip);

	return 0;
}

static int rtk_nf_attach_chip(struct nand_chip *chip)
{
        int ret;

        if (chip->bbt_options & NAND_BBT_USE_FLASH)
                chip->bbt_options |= NAND_BBT_NO_OOB;

        ret = rtk_nf_ecc_init(chip);
        if (ret)
                return ret;

	ret = rtk_nf_init_nandbuf(chip);
        if (ret)
                return  -ENOMEM;

        return 0;
}

static const struct nand_controller_ops rtk_nf_controller_ops = {
        .attach_chip = rtk_nf_attach_chip,
        //.setup_data_interface = rtk_nf_setup_data_interface,
};

static int rtk_nf_nand_chip_init(struct device *dev, struct rtk_nf *nf,
				  struct device_node *np)
{
	struct nand_chip *chip = &nf->chip;
	struct mtd_info *mtd;
	int ret;

	chip->controller = &nf->controller;

	nand_set_flash_node(chip, np);
	nand_set_controller_data(chip, nf);

	mtd = nand_to_mtd(chip);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;
	mtd->name = RTK_NAME;

	chip->options |= NAND_SKIP_BBTSCAN | NAND_NO_SUBPAGE_WRITE;

	chip->legacy.select_chip = rtk_nf_select_chip;
	chip->legacy.read_byte = rtk_nf_read_byte;
	chip->legacy.read_buf = rtk_nf_read_buf;
	chip->legacy.cmdfunc = rtk_nf_command;
	chip->legacy.waitfunc = rtk_nf_wait;
	chip->legacy.block_bad = rtk_nf_block_bad;

	chip->ecc.mode = NAND_ECC_HW;

	chip->ecc.write_page_raw = rtk_nf_write_page_raw;
	chip->ecc.write_page = rtk_nf_write_page_hwecc;
	chip->ecc.write_oob_raw = rtk_nf_write_oob;
	chip->ecc.write_oob = rtk_nf_write_oob;

	chip->ecc.read_page_raw = rtk_nf_read_page_raw;
	chip->ecc.read_page = rtk_nf_read_page_hwecc;
	chip->ecc.read_oob_raw = rtk_nf_read_oob;
	chip->ecc.read_oob = rtk_nf_read_oob;

	chip->bbt = NULL;

	ret = rtk_nf_hw_init(nf);
	if (ret)
		return -ENODEV;

	ret = nand_scan_with_ids(chip, 1, rtk_nand_flash_ids);
	if (ret)
		return -ENODEV;

#if defined(CONFIG_MTD_NAND_RTK_BBM)
	ret = rtk_nf_scan_bbt(chip);
	if (ret)
		return -ENODEV;
#endif

	nf->size = mtd->size >> 20;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "mtd parse partition error\n");
		nand_release(chip);
		return -ENODEV;
	}

	return 0;
}

static void rtk_nf_pad_setup(struct rtk_nf *nf)
{
	void __iomem *pad_base = nf->pad_regs;

	writel(0x55555555, pad_base+0x0);
}

static void rtk_nf_pll_setup(struct rtk_nf *nf)
{
	void __iomem *pll_base = nf->pll_regs;

	writel(0x3, pll_base+0x0);
	writel(0x4E4388, pll_base+0x8);
	writel(0x7, pll_base+0xc);
	udelay(200);

	return;
}

static void rtk_nf_gating(struct rtk_nf *nf)
{
	void __iomem *base = nf->regs;

	writel(readl(0x168 + base) | BIT(0), 0x168 + base);
	writel(readl(0x168 + base) | BIT(1), 0x168 + base);
	writel(readl(0x314 + base) | BIT(0), 0x314 + base);
	writel(readl(0x314 + base) | BIT(1), 0x314 + base);
	writel(readl(0x13c + base) | BIT(3), 0x13c + base);
	writel(readl(0x13c + base) | BIT(4), 0x13c + base);
	writel(readl(0x310 + base) | BIT(5), 0x310 + base);
	writel(readl(0x310 + base) | BIT(6), 0x310 + base);
	writel(readl(0x318 + base) | BIT(0), 0x318 + base);

	return;
}

static int rtk_nf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rtk_nf *nf;
	struct resource *res;
	int ret;

	nf = devm_kzalloc(dev, sizeof(*nf), GFP_KERNEL);
	if (!nf)
		return -ENOMEM;

	nand_controller_init(&nf->controller);
	nf->controller.ops = &rtk_nf_controller_ops;

#if defined(CONFIG_MTD_RTK_NAND_HW_SEMAPHORE)
	unsigned int addr;
	if (of_property_read_u32(np, "hw-semaphore", &addr)) {
		addr = 0x9801a63c;
		dev_err(dev, "NAND : can't find hw semaphore in dtb, \
			use default - 0x%x\n", addr);
	} else {
		dev_info(dev, "NAND : find hw semaphore in dtb, 0x%x\n", addr);
	}
	nf->hwsem_base = ioremap(addr, 1);
#endif

	ret = rtk_nf_enable_clk(dev, &nf->clkrst);
	if (ret)
		goto rtk_nf_probe_exit;

	nf->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nf->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(nf->regs)) {
		ret = PTR_ERR(nf->regs);
		dev_err(dev, "no reg base\n");
		goto rtk_nf_probe_disable_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	nf->pll_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(nf->pll_regs)) {
		ret = PTR_ERR(nf->pll_regs);
		dev_err(dev, "no pll reg base\n");
		goto rtk_nf_probe_disable_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	nf->pad_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(nf->pad_regs)) {
		ret = PTR_ERR(nf->pad_regs);
		dev_err(dev, "no pad reg base\n");
		goto rtk_nf_probe_disable_clk;
	}

	rtk_nf_pad_setup(nf);

	rtk_nf_pll_setup(nf);

	rtk_nf_gating(nf);

	platform_set_drvdata(pdev, nf);

	ret = rtk_nf_nand_chip_init(dev, nf, np);
	if (ret) {
		dev_err(dev, "failed to init nand chip\n");
		goto rtk_nf_probe_disable_clk;
	}

	return 0;

rtk_nf_probe_disable_clk:
	rtk_nf_disable_clk(dev, &nf->clkrst);

rtk_nf_probe_exit:

	return ret;
}

static int rtk_nf_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_nf *nf = platform_get_drvdata(pdev);

	nand_release(&nf->chip);

	rtk_nf_disable_clk(dev, &nf->clkrst);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rtk_nf_suspend(struct device *dev)
{
	struct rtk_nf *nf = dev_get_drvdata(dev);

	rtk_nf_disable_clk(dev, &nf->clkrst);

	return 0;
}

static int rtk_nf_resume(struct device *dev)
{
	struct rtk_nf *nf = dev_get_drvdata(dev);

	rtk_nf_enable_clk(dev, &nf->clkrst);

	rtk_nf_hw_init(nf);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rtk_nf_pm_ops, rtk_nf_suspend, rtk_nf_resume);
#endif

static const struct of_device_id rtk_nf_id_table[] = {
	{ .compatible = "realtek,rtd12xx-nf" },
	{ .compatible = "realtek,rtd13xx-nf" },
	{}
};
MODULE_DEVICE_TABLE(of, rtk_nf_id_table);

static struct platform_driver rtk_nf_driver = {
	.probe  = rtk_nf_probe,
	.remove = rtk_nf_remove,
	.driver = {
		.name  = RTK_NAME,
		.of_match_table = rtk_nf_id_table,
#ifdef CONFIG_PM_SLEEP
		.pm = &rtk_nf_pm_ops,
#endif
	},
};

module_platform_driver(rtk_nf_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PK Chuang <pk.chuang@realtek.com>");
MODULE_DESCRIPTION("RTK Parallel Nand Flash Controller Driver");
