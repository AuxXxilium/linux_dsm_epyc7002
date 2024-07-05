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
#include <linux/crc32.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/reset.h>
#include "rtk_nand.h"

#if defined(CONFIG_MTD_NAND_RTK_BBTCRC)
#define CRCLEN	4
#else
#define CRCLEN	0
#endif 

#define TAGOFFSET 4

#define	BBT1	64
#define	BBT2	128

static inline struct rtk_nf *to_rtk_nand(struct nand_chip *chip)
{
        return container_of(chip, struct rtk_nf, chip);
}

static unsigned int rtk_nf_page_to_block(struct nand_chip *chip, int page)
{
	struct nand_memory_organization *memorg = &chip->base.memorg;	

        return page / memorg->pages_per_eraseblock;
}

static unsigned int rtk_nf_page_offset_in_block(struct nand_chip *chip, 
						int page)
{
	struct nand_memory_organization *memorg = &chip->base.memorg;	

        return page % memorg->pages_per_eraseblock;
}

static int rtk_nf_bbt_page_count(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	u32 byte, p = 1;

	byte = sizeof(BB_t) * nf->RBA;
	while ( (byte = byte - mtd->writesize) > 0) {
		p++;
	}

	return p;
}

#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
static int rtk_nf_bbt_crc_calculate(struct rtk_nf *nf)
{
	BB_t *bbt = nf->bbt;
	char hash_temp[64] = {0};
        u32 hash_value_temp = 0;
	int i;

	for (i=0; i<nf->RBA; i++) {
		if ((bbt[i].BB_die != BB_DIE_INIT) && 
		    (bbt[i].bad_block != BB_INIT)) {
			hash_value_temp = hash_value_temp + 
					bbt[i].BB_die + bbt[i].bad_block + 
					bbt[i].RB_die + bbt[i].remap_block;
		}
	}

	sprintf(hash_temp, "%u", hash_value_temp);

	return crc32(~0, (u8 *)hash_temp, sizeof(hash_temp));
}
#endif

int rtk_nf_get_realpage(struct nand_chip *chip, int page)
{
	struct nand_memory_organization *memorg = &chip->base.memorg;
	struct rtk_nf *nf = to_rtk_nand(chip);
	BB_t *bbt = nf->bbt;
	int offset, block;
	int i;

	offset = rtk_nf_page_offset_in_block(chip, page);
	block = rtk_nf_page_to_block(chip, page);

	for (i=0; i<nf->RBA; i++) {
		if (bbt[i].bad_block != BB_INIT) {
			if (block == bbt[i].bad_block) {
				block = bbt[i].remap_block;
				break;
			}
		}
	}

	return (block * memorg->pages_per_eraseblock) + offset;
}

static void rtk_nf_update_BBT(struct nand_chip *chip, int o_blk, 
			      int s_blk, int m_blk)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	u64 blk = nf->size;

	do_div(blk, mtd->erasesize);

	if (s_blk == 0) {
		nf->bbt[(blk-1) - m_blk].bad_block = BAD_RESERVED;
		nf->bbt[(blk-1) - m_blk].BB_die = 0;
	}
	else {
		if (o_blk != s_blk) {
			nf->bbt[(blk-1) - s_blk].bad_block = BAD_RESERVED;
			nf->bbt[(blk-1) - s_blk].BB_die = 0;
		}

		nf->bbt[(blk-1) - m_blk].bad_block = o_blk;
		nf->bbt[(blk-1) - m_blk].BB_die = 0;
	}

        return;
}

static int rtk_nf_backup_block(struct nand_chip *chip, int src_b, int map_b, 
			       int offset, int mode)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct device *dev = nf->dev;
	struct rtk_buffer *buffer = &nf->nandbuf;
	int s_page = src_b * 64;
	int b_page = map_b * 64;
	char *buf;
	int ret;
	int i;

	dev_info(dev, "RTK %s(%d) backup %d to %d.\n",
			__func__, __LINE__, src_b, map_b);

	buf = kmalloc(mtd->writesize, GFP_KERNEL);
	if (!buf) {
		ret = -2;
		goto rtk_nf_backup_block_exit;
	}

	chip->legacy.cmdfunc(chip, NAND_CMD_ERASE1, -1, b_page);

	for (i=0; i<64; i++) {
		ret = chip->ecc.read_oob(chip, s_page + i);
		if (ret == -1)
			goto rtk_nf_backup_block_exit;
		else if ((ret == 1) || ((i == offset) && (mode == NFWRITE)))
			continue;

		memcpy(buf, buffer->dataBuf, mtd->writesize);

		ret = chip->ecc.write_page(chip, buf, 0, s_page + i);
		if (ret < 0)
			goto rtk_nf_backup_block_exit;
	}
	
rtk_nf_backup_block_exit:
	if (buf)
		kfree(buf);

	return ret;
}

static int rtk_nf_find_available_reserved_block(struct nand_chip *chip)
{
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct device *dev = nf->dev;
        int i;

        for (i = 0; i < nf->RBA; i++) {
                if (nf->bbt[i].bad_block == BB_INIT) {
			return nf->bbt[i].remap_block;
                }
        }

	dev_err(dev, "RTK %s(%d) No available reserved block.\n",
		__func__, __LINE__);

        return -1;
}

static int rtk_nf_write_bbt(struct nand_chip *chip, unsigned int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	struct device *dev = nf->dev;
	u8 *tmp;
	u32 p, len, wlen;
	int ret;
	int i = 0;
#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
	u32 crc = 0;
#endif

	p = rtk_nf_bbt_page_count(chip);
	
	len = CRCLEN + sizeof(BB_t)*nf->RBA;
	tmp = kmalloc(len, GFP_KERNEL);
	if (!tmp) {
		dev_err(dev, "RTK %s(%d) alloc tmp fail.\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	chip->legacy.select_chip(chip, 0);

	chip->legacy.cmdfunc(chip, NAND_CMD_ERASE1, -1, page);
#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
	crc = rtk_nf_bbt_crc_calculate(nf);
	memcpy(tmp, &crc, 4);
#endif
	memcpy(tmp + CRCLEN, nf->bbt, sizeof(BB_t)*nf->RBA);
	
	while (len > 0) {
		memset(buffer->dataBuf, 0xff, mtd->writesize + mtd->oobsize);

		wlen = (len > mtd->writesize) ? mtd->writesize : len;
		*(buffer->dataBuf + mtd->writesize + TAGOFFSET) = BBT_TAG;
		memcpy(buffer->dataBuf, tmp, wlen);
		ret = chip->ecc.write_page(chip, NULL, 0, page + i);
		if (ret) {
			dev_info(dev, "RTK %s(%d) write bbt%d fail.(%d)\n",
				__func__, __LINE__, 
				(page == BBT1) ? 1 : 2, ret);
			goto rtk_nf_write_bbt_exit;
		}

		len = len - mtd->writesize;
		i++;
	}	

rtk_nf_write_bbt_exit:
	if (tmp)
		kfree(tmp);

	return ret;
}

static int rtk_nf_update_bbt_to_flash(struct nand_chip *chip)
{
	int ret1, ret2;

	ret1 = rtk_nf_write_bbt(chip, BBT1);

	ret2 = rtk_nf_write_bbt(chip, BBT2);

	return ((ret1 < 0) && (ret2 < 0)) ? -1 : 0;
}

int rtk_nf_bb_handle(struct nand_chip *chip, int o_blk, 
		     int page, int backup, int mode)
{
	int s_blk = rtk_nf_page_to_block(chip, page);
	int m_blk = 0;
	int offset = rtk_nf_page_offset_in_block(chip, page);
	int ret = 0;

rtk_nf_bb_handle_redo:
	m_blk = rtk_nf_find_available_reserved_block(chip);
	if (m_blk <= 0)
		return -1;

	if (backup) {
		ret = rtk_nf_backup_block(chip, s_blk, m_blk, offset, mode);
		if (ret < 0) {
			if (ret == -1) {
				rtk_nf_update_BBT(chip, 0, 0, m_blk);
				goto rtk_nf_bb_handle_redo;
			} else {
				return ret;
			}
		}
	}

	rtk_nf_update_BBT(chip, o_blk, s_blk, m_blk);

	return rtk_nf_update_bbt_to_flash(chip);
}

static void rtk_nf_bbt_sync(struct nand_chip *chip, u32 bbtid)
{
	int ret;
	
	ret = rtk_nf_write_bbt(chip, bbtid);
}

#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
static unsigned int rtk_nf_bbt_crc_check(struct rtk_nf *nf, u8 *data)
{
	u32 crc = 0;
	u32 crc_c = 0;
	BB_t *bbt = (BB_t *)(data + CRCLEN);
	u8 hash_temp[64] = {0};
        u32 hash_value_temp = 0;
	int count;
        int i;

	count = 0;

	memcpy(&crc, data, sizeof(u32));

	for (i=0; i<nf->RBA; i++) {
		if ((bbt[i].BB_die != BB_DIE_INIT) && 
		    (bbt[i].bad_block != BB_INIT)) {
			hash_value_temp = hash_value_temp + 
					bbt[i].BB_die + bbt[i].bad_block + 
					bbt[i].RB_die + bbt[i].remap_block;

			count++;
		}
	}

	sprintf(hash_temp, "%u", hash_value_temp);
	crc_c = crc32(~0, (u8 *)hash_temp, sizeof(hash_temp));

	if (crc != crc_c)
		return -1;

	return count;
}
#endif

static int rtk_nf_read_bbt_page(struct nand_chip *chip, u8 *buf, u32 page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct rtk_buffer *buffer = &nf->nandbuf;
	int ret;

	ret = chip->ecc.read_oob(chip, page);
	if (ret != 0)
		return -1;

	if (*(buffer->dataBuf + mtd->writesize + TAGOFFSET) != BBT_TAG)
		return -1;

	memcpy(buf, buffer->dataBuf, mtd->writesize);
	
	return ret;
}

static int rtk_nf_read_bbt(struct nand_chip *chip, u8 *bbt, u32 bbtid, u32 p)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 page;
	u8 *buf;
	int ret;
	int i;

	for (i=0; i<p; i++) {
		page = bbtid + i;
		buf = bbt + (mtd->writesize * i);
		ret = rtk_nf_read_bbt_page(chip, buf, page);
		if (ret != 0)
			break;
	}

	return ret;
}

int rtk_nf_scan_bbt(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rtk_nf *nf = to_rtk_nand(chip);
	struct device *dev = nf->dev;
	int c1 = -1, c2 = -1;
	u32 b;
	u32 p = 0;
	u8 *bbt1 = NULL;
	u8 *bbt2 = NULL;
	int ret = 0;

	/* calculate RBA */
	b = nf->size;
	b = b / mtd->erasesize;
	nf->RBA = b * 5;
	nf->RBA = nf->RBA / 100;

	p = rtk_nf_bbt_page_count(chip);

	nf->bbt = kmalloc(sizeof(BB_t) * nf->RBA, GFP_KERNEL);
	if (nf->bbt) {
		dev_err(dev, "RTK %s(%d) alloc bbt1 fail.\n", 
			__func__, __LINE__);
		return -ENOMEM;
	}
	memset(nf->bbt, 0xff, sizeof(BB_t) * nf->RBA);

	bbt1 = kmalloc((mtd->writesize * p), GFP_KERNEL);
	if (!bbt1) {
		dev_err(dev, "RTK %s(%d) alloc bbt1 fail.\n", 
			__func__, __LINE__);
		return -ENOMEM;
	}
	memset(bbt1, 0xff, (mtd->writesize * p));

	bbt2 = kmalloc((mtd->writesize * p), GFP_KERNEL);
	if (!bbt2) {
		dev_err(dev, "RTK %s(%d) alloc bbt2 fail.\n", 
			__func__, __LINE__);
		return -ENOMEM;
	}
	memset(bbt2, 0xff, (mtd->writesize * p));

	/* read bbt from block 1 */
	ret = rtk_nf_read_bbt(chip, bbt1, BBT1, p);
	if (ret != 0)
		dev_err(dev, "RTK %s(%d) read bbt1 fail.\n", 
			__func__, __LINE__);
#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
	else {
		c1 = rtk_nf_bbt_crc_check(nf, bbt1);
		if (c1 < 0)
			dev_err(dev, "RTK %s(%d) check bbt1 fail.\n", 
				__func__, __LINE__);
	}
#endif

	/* read bbt from block 2 */
	ret = rtk_nf_read_bbt(chip, bbt2, BBT2, p);
	if (ret != 0)
		dev_err(dev, "RTK %s(%d) read bbt2 fail.\n", 
			__func__, __LINE__);
#ifdef CONFIG_MTD_NAND_RTK_BBTCRC
	else {
		c2 = rtk_nf_bbt_crc_check(nf, bbt2);
		if (c2 < 0)
			dev_err(dev, "RTK %s(%d) check bbt2 fail.\n", 
				__func__, __LINE__);
	}
#endif

	if ((c1 == c2) && (c1 >= 0) && (c2 >= 0)) {
		dev_info(dev, "RTK %s(%d) load bbt from bbt1.\n", 
				__func__, __LINE__);
		memcpy(nf->bbt, bbt1 + CRCLEN, sizeof(BB_t)*nf->RBA);
	} 
	else if (c1 > c2) {
		dev_info(dev, "RTK %s(%d) load bbt from bbt1.\n", 
				__func__, __LINE__);
		memcpy(nf->bbt, bbt1 + CRCLEN, sizeof(BB_t)*nf->RBA);
		rtk_nf_bbt_sync(chip, BBT2);	
	}
	else if (c2 > c1) {
		dev_info(dev, "RTK %s(%d) load bbt from bbt2.\n", 
				__func__, __LINE__);
		memcpy(nf->bbt, bbt2 + CRCLEN, sizeof(BB_t)*nf->RBA);
		rtk_nf_bbt_sync(chip, BBT1);
	}
	else {
		dev_info(dev, "RTK %s(%d) load bbt fail.\n", 
				__func__, __LINE__);
		return -1;
	}
	
	return 0;
}
