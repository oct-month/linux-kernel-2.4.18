/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.2.x and 2.4.x
 * Copyright (C) 2001 Qlogic Corporation
 * (www.qlogic.com)
 *
 * Portions (C) Arjan van de Ven <arjanv@redhat.com> for Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
 
#include "settings.h"
#include "debug.h"

uint8_t qla2100_nvram_config(struct scsi_qla_host *);
uint8_t qla2200_nvram_config(struct scsi_qla_host *);
static uint16_t qla2100_get_nvram_word(struct scsi_qla_host *, uint32_t);
static uint16_t qla2100_nvram_request(struct scsi_qla_host *, uint32_t);
int qla2x00_read_nvram(adapter_state_t *, EXT_IOCTL *, int);
int qla2x00_update_nvram(adapter_state_t *, EXT_IOCTL *, int);
int qla2x00_write_nvram_word(adapter_state_t *, uint8_t, uint16_t);
#if USE_FLASH_DATABASE
static void qla2100_flash_enable_database(struct scsi_qla_host *);
static void qla2100_flash_disable_database(struct scsi_qla_host *);
static void qla2x00_write_flash_byte(struct scsi_qla_host *, uint32_t, u_char);
#endif
static void qla2100_nv_write(struct scsi_qla_host * ha, uint16_t data);
static void qla2x00_nv_deselect(struct scsi_qla_host * ha);


#if USE_FLASH_DATABASE
/*
* qla2100_get_database
*      Copies and converts flash database to driver database.
*      (may sleep)
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_get_database(struct scsi_qla_host * ha)
{
	flash_database_t *fptr;
	uint8_t status = 1;
	uint32_t addr;
	uint16_t cnt;
	uint8_t *bptr;
	uint8_t checksum;
	uint32_t b, t;

	ENTER("qla2100_get_database");

	/* Default setup. */
	ha->flash_db = FLASH_DATABASE_0;
	ha->flash_seq = 0;

	if ((fptr = (flash_database_t *) kmalloc(sizeof(flash_database_t),GFP_ATOMIC))) {
		/* Enable Flash Read/Write. */
		qla2100_flash_enable_database(ha);

		/* Start with flash database with the highest sequence number. */
		b = qla2100_read_flash_byte(ha, FLASH_DATABASE_0);
		b |= qla2100_read_flash_byte(ha, FLASH_DATABASE_0 + 1) << 8;
		b |= qla2100_read_flash_byte(ha, FLASH_DATABASE_0 + 1) << 16;
		b |= qla2100_read_flash_byte(ha, FLASH_DATABASE_0 + 1) << 24;
		t = qla2100_read_flash_byte(ha, FLASH_DATABASE_1);
		t |= qla2100_read_flash_byte(ha, FLASH_DATABASE_1 + 1) << 8;
		t |= qla2100_read_flash_byte(ha, FLASH_DATABASE_1 + 1) << 16;
		t |= qla2100_read_flash_byte(ha, FLASH_DATABASE_1 + 1) << 24;
		if (t > b) {
			ha->flash_db = FLASH_DATABASE_1;
		}

		/* Select the flash database with the good checksum. */
		for (t = 0; t < 2; t++) {
			checksum = 0;
			addr = ha->flash_db;
			bptr = (uint8_t *) fptr;
			fptr->hdr.size = sizeof(flash_database_t);

			/* Read flash database to driver. */
			for (cnt = 0; cnt < fptr->hdr.size; cnt++) {
				*bptr = (uint8_t) qla2100_read_flash_byte(ha, addr++);
				checksum += *bptr++;
				if (bptr == &fptr->hdr.spares[0] &&
				    (fptr->hdr.size > sizeof(flash_database_t) ||
				     fptr->hdr.size < sizeof(flash_hdr_t) || !fptr->hdr.version)) {
					checksum = 1;
					break;
				}
			}

			if (!checksum) {
				status = 0;
				break;
			}
			/* trying other database */
			if (ha->flash_db == FLASH_DATABASE_0) {
				ha->flash_db = FLASH_DATABASE_1;
			} else {
				ha->flash_db = FLASH_DATABASE_0;
			}
		}

		if (!status) {
			ha->flash_seq = fptr->hdr.seq;

			/* Convert flash database to driver database format. */
			if (fptr->hdr.size -= sizeof(flash_hdr_t)) {
				for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
					ha->fc_db[cnt].name[0] = fptr->node[cnt].name[0];
					ha->fc_db[cnt].name[1] = fptr->node[cnt].name[1];
					cnt, ha->fc_db[cnt].name[1], ha->fc_db[cnt].name[0]);

					ha->fc_db[cnt].loop_id = PORT_AVAILABLE;
					ha->fc_db[cnt].flag = 0;	/* v2.19.05b3 */
					if (!(fptr->hdr.size -= sizeof(flash_node_t)))
						break;
				}
			}
		}

		qla2100_flash_disable_database(ha);

		kfree(fptr);
	} else {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - flash mem", (int) ha->host_no);
		ha->mem_err++;
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_get_database: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_get_database");
	return status;
}

/*
* qla2100_save_database
*      Copies and converts driver database to flash database.
*      (may sleep)
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t qla2100_save_database(struct scsi_qla_host * ha) {
	flash_database_t *fptr;
	uint8_t status = 1;
	uint32_t addr;
	uint16_t cnt;
	uint8_t *bptr;
	uint8_t checksum;

	ENTER("qla2100_save_database");

	if ((fptr = (flash_database_t *) kmalloc(sizeof(flash_database_t),GFP_ATOMIC))) {
		/* Enable Flash Read/Write. */
		qla2100_flash_enable_database(ha);

		fptr->hdr.seq = ++ha->flash_seq;
		fptr->hdr.version = FLASH_DATABASE_VERSION;
		fptr->hdr.size = sizeof(flash_hdr_t);

		/* Copy and convert driver database to flash database. */
		for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
			if (ha->fc_db[cnt].loop_id == PORT_UNUSED)
				break;
			else {
				fptr->node[cnt].name[0] = ha->fc_db[cnt].name[0];
				fptr->node[cnt].name[1] = ha->fc_db[cnt].name[1];
				fptr->hdr.size += sizeof(flash_node_t);
			}
		}

		/* Calculate checksum. */
		checksum = 0;
		bptr = (uint8_t *) fptr;
		for (cnt = 0; cnt < fptr->hdr.size; cnt++)
			checksum += *bptr++;
		fptr->hdr.checksum = ~checksum + 1;

		/* Setup next sector address for flash */
		if (ha->flash_db == FLASH_DATABASE_0)
			addr = FLASH_DATABASE_1;
		else
			addr = FLASH_DATABASE_0;
		ha->flash_db = addr;

		/* Erase flash sector prior to write. */
		status = qla2100_erase_flash_sector(ha, addr);

		/* Write database to flash. */
		bptr = (uint8_t *) fptr;
		for (cnt = 0; cnt < fptr->hdr.size && !status; cnt++)
			status = qla2100_program_flash_address(ha, addr++, *bptr++);

		qla2100_flash_disable_database(ha);

		kfree(fptr);
	} else {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - flash", (int) ha->host_no);
		ha->mem_err++;
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_save_database: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_save_database");
	return status;
}

/*
* qla2100_program_flash_address
*      Program flash address.
*
* Input:
*      ha   = adapter block pointer.
*      addr = flash byte address.
*      data = data to be written to flash.
*
* Returns:
*      0 = success.
*/
static uint8_t qla2100_program_flash_address(struct scsi_qla_host * ha, uint32_t addr, uint8_t data) {
	uint8_t status;

	/* Write Program Command Sequence */
	qla2100_write_flash_byte(ha, 0x5555, 0xaa);
	qla2100_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2100_write_flash_byte(ha, 0x5555, 0xa0);
	qla2100_write_flash_byte(ha, addr, data);

	/* Wait for write to complete. */
	status = qla2100_poll_flash(ha, addr, data);

	if (status)
		DEBUG2(printk("qla2100_program_flash_address: **** FAILED ****\n"));

	return status;
}

/*
* qla2100_erase_flash_sector
*      Erases flash sector.
*
* Input:
*      ha   = adapter block pointer.
*      addr = sector address.
*
* Returns:
*      0 = success.
*/
static uint8_t qla2100_erase_flash_sector(struct scsi_qla_host * ha, uint32_t addr) {
	uint8_t status;

	addr &= 0x1c000;

	/* Individual Sector Erase Command Sequence */
	qla2100_write_flash_byte(ha, 0x5555, 0xaa);
	qla2100_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2100_write_flash_byte(ha, 0x5555, 0x80);
	qla2100_write_flash_byte(ha, 0x5555, 0xaa);
	qla2100_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2100_write_flash_byte(ha, addr, 0x30);

	udelay(150);

	/* Wait for erase to complete. */
	status = qla2100_poll_flash(ha, addr, 0x80);

#ifdef QL_DEBUG_LEVEL_2
	if (status)
		printk("qla2100_erase_flash_sector: **** FAILED ****\n");
#endif
	return status;
}

/*
* qla2100_poll_flash
*      Polls flash for completion.
*
* Input:
*      ha   = adapter block pointer.
*      addr = flash byte address.
*      data = data to be polled.
*
* Returns:
*      0 = success.
*/
static uint8_t qla2100_poll_flash(struct scsi_qla_host * ha, uint32_t addr, uint8_t poll_data) {
	uint8_t status = 1;
	uint8_t flash_data;
	uint32_t cnt;

	poll_data &= BIT_7;

	/* Wait for 30 seconds for command to finish. */
	for (cnt = 3000000; cnt; cnt--) {
		flash_data = (uint8_t) qla2100_read_flash_byte(ha, addr);

		if ((flash_data & BIT_7) == poll_data) {
			status = 0;
			break;
		}
		if (flash_data & BIT_5 && cnt > 2)
			cnt = 2;
		udelay(10);
		barrier();
	}

	return status;
}

/*
* qla2100_flash_enable_database
*      Setup flash for reading/writing.
*
* Input:
*      ha = adapter block pointer.
*/
static void
 qla2100_flash_enable_database(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	/* Setup bit 16 of flash address. */
	WRT_REG_WORD(&reg->nvram, NV_SELECT);

	/* Enable Flash Read/Write. */
	WRT_REG_WORD(&reg->ctrl_status, ISP_FLASH_ENABLE);

	/* Read/Reset Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0xf0);
	qla2x00_read_flash_byte(ha, FLASH_DATABASE_0);
}

/*
* qla2100_flash_disable_database
*      Disable flash and allow RISC to run.
*
* Input:
*      ha = adapter block pointer.
*/
void
 qla2100_flash_disable_database(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	/* Restore chip registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0);
	WRT_REG_WORD(&reg->nvram, 0);
}

/*
* qla2x00_write_flash_byte
*      Write byte to flash.
*
* Input:
*      ha   = adapter block pointer.
*      addr = flash byte address.
*      data = data to be written.
*/
static void
 qla2x00_write_flash_byte(struct scsi_qla_host * ha, uint32_t addr, uint8_t data) {
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->flash_address, (uint16_t) addr);
	WRT_REG_WORD(&reg->flash_data, (uint16_t) data);
}

#endif

/*
 * qla2x00_flash_enable
 *      Setup flash for reading/writing.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
 qla2x00_flash_enable(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	/* Enable Flash Read/Write. */
	WRT_REG_WORD(&reg->ctrl_status, ISP_FLASH_ENABLE);

}

/*
 * qla2x00_flash_disable
 *      Disable flash and allow RISC to run.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
 qla2x00_flash_disable(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	/* Restore chip registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0);
}

/*
 * qla2x00_read_flash_byte
 *      Reads byte from flash, but must read a word from chip.
 *
 * Input:
 *      ha   = adapter block pointer.
 *      addr = flash byte address.
 *
 * Returns:
 *      byte from flash.
 */
static uint8_t qla2x00_read_flash_byte(struct scsi_qla_host * ha, uint32_t addr) {
	device_reg_t *reg = ha->iobase;
	uint16_t data;

	WRT_REG_WORD(&reg->flash_address, (uint16_t) addr);
	data = qla2100_debounce_register(&reg->flash_data);

	return (uint8_t) data;
}

/*
 * qla2x00_get_flash_version
 *      Reads version info from flash.
 *
 * Input:
 *      ha   = adapter struct pointer.
 *
 * Returns:
 *      byte from flash.
 */
uint16_t qla2x00_get_flash_version(struct scsi_qla_host * ha) {
	uint16_t ret = QL_STATUS_SUCCESS;
	uint32_t loop_cnt = 1;	/* this is for error exit only */
	uint32_t pcir_adr;

	DEBUG3(printk("qla2x00_get_flash_version: entered.\n"););

	    qla2x00_flash_enable(ha);

	do {			/* Loop once to provide quick error exit */

		/* Match signature */
		if (!(qla2x00_read_flash_byte(ha, 0) == 0x55 && qla2x00_read_flash_byte(ha, 1) == 0xaa)) {
			/* No signature */
			DEBUG2(printk("qla2x00_get_flash_version: No matching " "signature.\n"););
			    ret = QL_STATUS_ERROR;
			break;
		}

		pcir_adr = qla2x00_read_flash_byte(ha, 0x18) & 0xff;

		/* validate signature of PCI data structure */
		if ((qla2x00_read_flash_byte(ha, pcir_adr)) == 'P' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 1)) == 'C' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 2)) == 'I' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 3)) == 'R') {

			/* Read version */
			ha->optrom_minor = qla2x00_read_flash_byte(ha, pcir_adr + 0x12);
			ha->optrom_major = qla2x00_read_flash_byte(ha, pcir_adr + 0x13);
			DEBUG3(printk("qla2x00_get_flash_version: got %d.%d.\n", ha->optrom_major, ha->optrom_minor););

		} else {
			/* error */
			DEBUG2(printk("qla2x00_get_flash_version: PCI data "
				      "struct not found. pcir_adr=%x.\n", pcir_adr););
			    ret = QL_STATUS_ERROR;
			break;
		}

	} while (--loop_cnt);

	qla2x00_flash_disable(ha);

	DEBUG3(printk("qla2x00_get_flash_version: exiting normally.\n"););
	return ret;
}

int
qla2x00_read_nvram(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	uint8_t *usr_temp, *kernel_tmp;
	uint16_t data;
	uint32_t i, cnt;
	uint32_t transfer_size;

	DEBUG3(printk("qla2x00_read_nvram: entered.\n"));

	
	if (ha->device_id == QLA2100_DEVICE_ID) {
		if (pext->ResponseLen < sizeof(nvram21_t))
			transfer_size = pext->ResponseLen / 2;
		else
			transfer_size = sizeof(nvram21_t) / 2;
	} else {
		if (pext->ResponseLen < sizeof(nvram22_t))
			transfer_size = pext->ResponseLen / 2;
		else
			transfer_size = sizeof(nvram22_t) / 2;
	}
	/* Dump NVRAM. */
	usr_temp = (uint8_t *) pext->ResponseAdr;
	for (i = 0, cnt = 0; cnt < transfer_size; cnt++, i++) {
		data = qla2100_get_nvram_word(ha, cnt);

		kernel_tmp = (uint8_t *) & data;

		if (put_user(*kernel_tmp, usr_temp))
			return -EFAULT;


		/* next byte */
		usr_temp++;
		kernel_tmp++;

		if (put_user(*kernel_tmp, usr_temp))
			return -EFAULT;

		usr_temp++;
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_read_nvram: exiting.\n"));

	return 0;
}

/*
 * qla2x00_update_nvram
 *	Write data to NVRAM.
 *
 * Input:
 *	ha = adapter block pointer.
 *	pext = pointer to driver internal IOCTL structure.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_update_nvram(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	uint8_t i, cnt;
	uint8_t *usr_tmp, *kernel_tmp;
	nvram21_t new_nv;
	uint16_t *wptr;
	uint16_t data;
	uint32_t transfer_size;
	uint8_t chksum = 0;
	int ret = 0;

	DEBUG3(printk("qla2x00_update_nvram: entered.\n"));

	if (pext->RequestLen < sizeof(nvram21_t))
		transfer_size = pext->RequestLen;
	else
		transfer_size = sizeof(nvram21_t);

	/* Read from user buffer */
	kernel_tmp = (uint8_t *) & new_nv;
	usr_tmp = (uint8_t *) pext->RequestAdr;

	ret = verify_area(VERIFY_READ, (void *) usr_tmp, transfer_size);
	if (ret) {
		DEBUG2(printk("qla2x00_update_nvram: ERROR in buffer verify READ. "
			      "RequestAdr=%p\n", pext->RequestAdr));
		return ret;
	}

	copy_from_user(kernel_tmp, usr_tmp, transfer_size);

	kernel_tmp = (uint8_t *) & new_nv;

	/* we need to checksum the nvram */
	for (i = 0; i < sizeof(nvram21_t) - 1; i++) {
		chksum += *kernel_tmp;
		kernel_tmp++;
	}

	chksum = ~chksum + 1;

	*kernel_tmp = chksum;

	/* Write to NVRAM */
	wptr = (uint16_t *) & new_nv;
	for (cnt = 0; cnt < transfer_size / 2; cnt++) {
		data = *wptr++;
		qla2x00_write_nvram_word(ha, cnt, data);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_update_nvram: exiting.\n"));

	return 0;
}

int
qla2x00_write_nvram_word(adapter_state_t * ha, uint8_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	device_reg_t *reg = ha->iobase;

	qla2100_nv_write(ha, NV_DATA_OUT);
	qla2100_nv_write(ha, 0);
	qla2100_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2100_nv_write(ha, NV_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Erase Location */
	nv_cmd = (addr << 16) | NV_ERASE_OP;
	nv_cmd <<= 5;
	for (count = 0; count < 11; count++) {
		if (nv_cmd & BIT_31)
			qla2100_nv_write(ha, NV_DATA_OUT);
		else
			qla2100_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for Erase to Finish */
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	do {
		udelay(500);
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NV_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2100_nv_write(ha, NV_DATA_OUT);
		else
			qla2100_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	do {
		udelay(500);
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NV_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2100_nv_write(ha, NV_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2100_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);

	DEBUG3(printk("qla2x00_write_nvram_word: exiting.\n"));

	return 0;
}

/*
 * NVRAM configuration for the 2200.
 *
 * Input:
 *      ha                = adapter block pointer.
 *      ha->request_ring  = request ring virtual address
 *      ha->response_ring = response ring virtual address
 *      ha->request_dma   = request ring physical address
 *      ha->response_dma  = response ring physical address
 *
 * Output:
 *      initialization control block in response_ring
 *      host adapters parameters in host adapter block
 *
 * Returns:
 *      0 = success.
 */
uint8_t
qla2200_nvram_config(struct scsi_qla_host * ha)
{
#ifdef ISP2300
	device_reg_t *reg = ha->iobase;
	uint16_t data;
#endif
	uint8_t status = 0;
	uint8_t chksum = 0;
	uint16_t cnt, base;
	caddr_t dptr1, dptr2;
	init_cb_t *icb = ha->init_cb;
	nvram22_t *nv = (nvram22_t *) ha->request_ring;
	uint16_t *wptr = (uint16_t *) ha->request_ring;

	ENTER("qla2200_nvram_config");

	if (!ha->flags.nvram_config_done) {
#ifdef ISP2300
		if (ha->device_id == QLA2312_DEVICE_ID) {
			data = RD_REG_WORD(&reg->ctrl_status);
			if ((data >> 14) == 1)
				base = 0x80;
			else
				base = 0;
			data = RD_REG_WORD(&reg->nvram);
			while (data & NV_BUSY) {
				udelay(100);
				data = RD_REG_WORD(&reg->nvram);
			}
			/* Lock resource */
			WRT_REG_WORD(&reg->host_semaphore, 0x1);
			udelay(5);
			data = RD_REG_WORD(&reg->host_semaphore);
			while ((data & BIT_0) == 0) {
				/* Lock failed */
				udelay(100);
				WRT_REG_WORD(&reg->host_semaphore, 0x1);
				udelay(5);
				data = RD_REG_WORD(&reg->host_semaphore);
			}
		} else
			base = 0;
#else
		base = 0;
#endif
		/* Verify valid NVRAM checksum. */
		for (cnt = 0; cnt < sizeof(nvram22_t) / 2; cnt++) {
			*wptr = qla2100_get_nvram_word(ha, (cnt + base));
			chksum += (uint8_t) * wptr;
			chksum += (uint8_t) (*wptr >> 8);
			wptr++;
		}
#ifdef ISP2300
		if (ha->device_id == QLA2312_DEVICE_ID) {
			/* Unlock resource */
			WRT_REG_WORD(&reg->host_semaphore, 0);
		}
#endif

#if  DEBUG_PRINT_NVRAM
		printk("qla2200_nvram_config: Contents of NVRAM ");
		printk("\n");
		qla2100_dump_buffer((uint8_t *) ha->request_ring, sizeof(nvram22_t));
#endif

		/* Bad NVRAM data, set defaults parameters. */
		if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' || nv->id[2] != 'P' ||
		    nv->id[3] != ' ' || nv->nvram_version < 1) {
			/* Reset NVRAM data. */
			DEBUG(printk("Using defaults for NVRAM: \n"));
			DEBUG(printk("checksum=0x%x, Id=%c, version=0x%x\n", chksum, nv->id[0], nv->nvram_version));
			wptr = (uint16_t *) nv;
			for (cnt = 0; cnt < sizeof(nvram21_t) / 2; cnt++)
				*wptr++ = 0;

			/*
			   * Set default initialization control block.
			 */
			nv->parameter_block_version = ICB_VERSION;
			nv->firmware_options.enable_fairness = 1;
			nv->firmware_options.enable_fast_posting = 1;
			nv->firmware_options.enable_full_login_on_lip = 1;
			nv->firmware_options.expanded_ifwcb = 1;

			nv->frame_payload_size = 1024;
			nv->max_iocb_allocation = 256;
			nv->execution_throttle = 16;
			nv->retry_count = 8;
			nv->retry_delay = 1;
			nv->port_name[0] = 32;
			nv->port_name[3] = 224;
			nv->port_name[4] = 139;
			nv->login_timeout = 4;
#ifdef ISP2200
			nv->additional_firmware_options.connection_options = P2P_LOOP;
#else
			nv->additional_firmware_options.connection_options = LOOP_P2P;
#endif
			/*
			   * Set default host adapter parameters
			 */
			nv->host_p.enable_lip_full_login = 1;
			nv->reset_delay = 5;
			nv->port_down_retry_count = 8;
			nv->maximum_luns_per_target = 8;
			status = 1;
		}

		/* Reset NVRAM data. */
		memset((caddr_t) icb,0, sizeof(init_cb_t));
		/*
		   * Copy over NVRAM RISC parameter block
		   * to initialization control block.
		 */
		dptr1 = (caddr_t) icb;
		dptr2 = (caddr_t) & nv->parameter_block_version;
		cnt = (caddr_t) & nv->additional_firmware_options - (caddr_t) & nv->parameter_block_version;
		while (cnt--)
			*dptr1++ = *dptr2++;

		dptr1 += (caddr_t) & icb->additional_firmware_options - (caddr_t) & icb->request_q_outpointer;
		cnt = (caddr_t) & nv->host_p - (caddr_t) & nv->additional_firmware_options;
		while (cnt--)
			*dptr1++ = *dptr2++;

		/* HBA node name 0 correction */
		for (cnt = 0; cnt < 8; cnt++) {
			if (icb->node_name[cnt] != 0)
				break;
		}
		if (cnt == 8) {
			for (cnt = 0; cnt < 8; cnt++)
				icb->node_name[cnt] = icb->port_name[cnt];
			icb->node_name[0] = icb->node_name[0] & ~BIT_0;
			icb->port_name[0] = icb->port_name[0] | BIT_0;
		}

		/*
		   * Setup driver firmware options.
		 */
		icb->firmware_options.enable_full_duplex = 0;
#if  QL2100_TARGET_MODE_SUPPORT
		icb->firmware_options.enable_target_mode = 1;
#else
		icb->firmware_options.enable_target_mode = 0;
#endif
		icb->firmware_options.disable_initiator_mode = 0;
		icb->firmware_options.enable_port_update_event = 1;
		icb->firmware_options.enable_full_login_on_lip = 1;
#ifdef ISP2300
		icb->firmware_options.enable_fast_posting = 0;
#endif
#ifndef FC_IP_SUPPORT
		/* Enable FC-Tape support */
		icb->firmware_options.node_name_option = 1;
		icb->firmware_options.expanded_ifwcb = 1;
		icb->additional_firmware_options.enable_fc_tape = 1;
		icb->additional_firmware_options.enable_fc_confirm = 1;
#endif
		/*
		   * Set host adapter parameters
		 */
		ha->flags.enable_target_mode = icb->firmware_options.enable_target_mode;
		ha->flags.disable_luns = nv->host_p.disable_luns;
		ha->flags.disable_risc_code_load = nv->host_p.disable_risc_code_load;
		ha->flags.set_cache_line_size_1 = nv->host_p.set_cache_line_size_1;
		ha->flags.enable_64bit_addressing = nv->host_p.enable_64bit_addressing;

		/* Enable 64bit addressing for OS/System combination supporting it   */
		/* actual NVRAM bit is: nv->cntr_flags_1.enable_64bit_addressing     */
		/* but we will ignore it and use BITS_PER_LONG macro to setup for    */
		/* 64 or 32 bit access of host memory in all x86/ia-64/Alpha systems */
		ha->flags.enable_64bit_addressing = 1;


		/* Update our PCI device dma_mask for full 64 bit mask;
		   disable 64 bit addressing if this fails */
		if (pci_set_dma_mask(ha->pdev, 0xffffffffffffffff))  {
			printk("qla2x00: failed to set 64 bit PCI DMA mask, using 32 bits\n");
			ha->flags.enable_64bit_addressing = 0;
			pci_set_dma_mask(ha->pdev, 0xffffffff);
		}

		if (ha->flags.enable_64bit_addressing)
			printk(KERN_INFO "qla2x00: 64 Bit PCI Addressing Enabled\n");
		ha->flags.enable_lip_reset = nv->host_p.enable_lip_reset;
		ha->flags.enable_lip_full_login = nv->host_p.enable_lip_full_login;
		ha->flags.enable_target_reset = nv->host_p.enable_target_reset;
		ha->flags.enable_flash_db_update = nv->host_p.enable_database_storage;
		ha->operating_mode = icb->additional_firmware_options.connection_options;

		/* new for IOCTL support of APIs */
		ha->node_name[0] = icb->node_name[0];
		ha->node_name[1] = icb->node_name[1];
		ha->node_name[2] = icb->node_name[2];
		ha->node_name[3] = icb->node_name[3];
		ha->node_name[4] = icb->node_name[4];
		ha->node_name[5] = icb->node_name[5];
		ha->node_name[6] = icb->node_name[6];
		ha->node_name[7] = icb->node_name[7];
		ha->nvram_version = nv->nvram_version;

		ha->hiwat = icb->iocb_allocation;
		ha->execution_throttle = nv->execution_throttle;

		ha->retry_count = nv->retry_count;
		ha->login_timeout = nv->login_timeout;
		/* Set minimum login_timeout to 4 seconds. */
		if (ha->login_timeout < 4)
			ha->login_timeout = 4;
		ha->port_down_retry_count = nv->port_down_retry_count;
		ha->minimum_timeout = (ha->login_timeout * ha->retry_count)
		    + ha->port_down_retry_count;
		ha->loop_reset_delay = nv->reset_delay;
		/* Will get the value from nvram. */
		ha->loop_down_timeout = LOOP_DOWN_TIMEOUT;
		ha->loop_down_abort_time = LOOP_DOWN_TIME - ha->loop_down_timeout;

		/* save HBA serial number */
		ha->serial0 = nv->port_name[5];
		ha->serial1 = nv->port_name[6];
		ha->serial2 = nv->port_name[7];
		ha->flags.link_down_error_enable = 1;
		/* save OEM related items for QLA2200s and QLA2300s */
		ha->oem_id = nv->oem_id;
		ha->oem_spare0 = nv->oem_spare0;
		for (cnt = 2; cnt < 8; cnt++)
			ha->oem_string[cnt] = nv->oem_string[cnt];

		for (cnt = 0; cnt < 8; cnt++) {
			ha->oem_part[cnt] = nv->oem_part[cnt];
			ha->oem_fru[cnt] = nv->oem_fru[cnt];
			ha->oem_ec[cnt] = nv->oem_ec[cnt];
		}

#ifdef FC_IP_SUPPORT
		for (cnt = 0; cnt < 8; cnt++)
			ha->acPortName[cnt] = nv->port_name[cnt];
#endif

#if  USE_BIOS_MAX_LUNS
		if (!nv->maximum_luns_per_target)
			ha->max_luns = MAX_LUNS;
		else if (nv->maximum_luns_per_target < MAX_LUNS)
			ha->max_luns = nv->maximum_luns_per_target;
		else
			ha->max_luns = MAX_LUNS;
#else
		ha->max_luns = MAX_LUNS;
#endif

		/*
		   * Setup ring parameters in initialization control block
		 */
		icb->request_q_outpointer = 0;
		icb->response_q_inpointer = 0;
		icb->request_q_length = REQUEST_ENTRY_CNT;
		icb->response_q_length = RESPONSE_ENTRY_CNT;
		icb->request_q_address[0] = LS_64BITS(ha->request_dma);
		icb->request_q_address[1] = MS_64BITS(ha->request_dma);
		icb->response_q_address[0] = LS_64BITS(ha->response_dma);
		icb->response_q_address[1] = MS_64BITS(ha->response_dma);

		icb->lun_enables = 0;
		icb->command_resource_count = 0;
		icb->immediate_notify_resource_count = 0;
		icb->timeout = 0;
		icb->reserved_2 = 0;

		ha->flags.nvram_config_done = 1;
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2200_nvram_config: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2200_nvram_config");
	return status;
}

/*
 * NVRAM configuration for the 2100.
 *
 * Input:
 *      ha                = adapter block pointer.
 *      ha->request_ring  = request ring virtual address
 *      ha->response_ring = response ring virtual address
 *      ha->request_dma   = request ring physical address
 *      ha->response_dma  = response ring physical address
 *
 * Output:
 *      initialization control block in response_ring
 *      host adapters parameters in host adapter block
 *
 * Returns:
 *      0 = success.
 */
uint8_t
qla2100_nvram_config(struct scsi_qla_host * ha)
{
	uint8_t status = 0;
	uint8_t chksum = 0;
	uint16_t cnt, base;
	caddr_t dptr1, dptr2;
	init_cb_t *icb = ha->init_cb;
	nvram21_t *nv = (nvram21_t *) ha->request_ring;
	uint16_t *wptr = (uint16_t *) ha->request_ring;

	ENTER("qla2100_nvram_config");

		base = 0;
		/* Verify valid NVRAM checksum. */
		for (cnt = 0; cnt < sizeof(nvram22_t) / 2; cnt++) {
			*wptr = qla2100_get_nvram_word(ha, (cnt + base));
			chksum += (uint8_t) * wptr;
			chksum += (uint8_t) (*wptr >> 8);
			wptr++;
		}

#if  DEBUG_PRINT_NVRAM
		printk("qla2200_nvram_config: Contents of NVRAM ");
		printk("\n");
		qla2100_dump_buffer((uint8_t *) ha->request_ring, sizeof(nvram22_t));
#endif

		/* Bad NVRAM data, set defaults parameters. */
		if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' || nv->id[2] != 'P' ||
		    nv->id[3] != ' ' || nv->nvram_version < 1) {
			/* Reset NVRAM data. */
			DEBUG(printk("Using defaults for NVRAM: \n"));
			DEBUG(printk("checksum=0x%x, Id=%c, version=0x%x\n", chksum, nv->id[0], nv->nvram_version));
			wptr = (uint16_t *) nv;
			for (cnt = 0; cnt < sizeof(nvram21_t) / 2; cnt++)
				*wptr++ = 0;

			/*
			   * Set default initialization control block.
			 */
			nv->parameter_block_version = ICB_VERSION;
			nv->firmware_options.enable_fairness = 1;
			nv->firmware_options.enable_fast_posting = 1;
			nv->firmware_options.enable_full_login_on_lip = 1;


			nv->frame_payload_size = 1024;
			nv->max_iocb_allocation = 256;
			nv->execution_throttle = 16;
			nv->retry_count = 8;
			nv->retry_delay = 1;
                        nv->node_name[0] = 32;
                        nv->node_name[3] = 224;
                        nv->node_name[4] = 139;
	
			nv->login_timeout = 4;
			/*
			   * Set default host adapter parameters
			 */
			nv->host_p.enable_lip_full_login = 1;
			nv->reset_delay = 5;
			nv->port_down_retry_count = 8;
			nv->maximum_luns_per_target = 8;
			status = 1;
		}

		/* Reset NVRAM data. */
		memset((caddr_t) icb,0, sizeof(init_cb_t));
		/*
		   * Copy over NVRAM RISC parameter block
		   * to initialization control block.
		 */
		dptr1 = (caddr_t) icb;
		dptr2 = (caddr_t) & nv->parameter_block_version;

		cnt = (caddr_t) & nv->host_p - (caddr_t) & nv->parameter_block_version;
		while (cnt--)
			*dptr1++ = *dptr2++;

		/* HBA node name 0 correction */
		for (cnt = 0; cnt < 8; cnt++) {
			if (icb->node_name[cnt] != 0)
				break;
		}
		if (cnt == 8) {
			for (cnt = 0; cnt < 8; cnt++)
				icb->node_name[cnt] = icb->port_name[cnt];
			icb->node_name[0] = icb->node_name[0] & ~BIT_0;
			icb->port_name[0] = icb->port_name[0] | BIT_0;
		}

		/*
		   * Setup driver firmware options.
		 */
#if  QL2100_TARGET_MODE_SUPPORT
		icb->firmware_options.enable_target_mode = 1;
#else
		icb->firmware_options.enable_target_mode = 0;
#endif
		icb->firmware_options.disable_initiator_mode = 0;
		icb->firmware_options.enable_port_update_event = 1;
		icb->firmware_options.enable_full_login_on_lip = 1;
#ifdef ISP2300
		icb->firmware_options.enable_fast_posting = 0;
#endif
#ifndef FC_IP_SUPPORT
		/* Enable FC-Tape support */
		icb->firmware_options.node_name_option = 1;
		icb->firmware_options.expanded_ifwcb = 1;
		icb->additional_firmware_options.enable_fc_tape = 1;
		icb->additional_firmware_options.enable_fc_confirm = 1;
#endif
		/*
		   * Set host adapter parameters
		 */
		ha->flags.enable_target_mode = icb->firmware_options.enable_target_mode;
		ha->flags.disable_luns = nv->host_p.disable_luns;
		ha->flags.disable_risc_code_load = nv->host_p.disable_risc_code_load;
		ha->flags.set_cache_line_size_1 = nv->host_p.set_cache_line_size_1;
		ha->flags.enable_64bit_addressing = nv->host_p.enable_64bit_addressing;

		/* Enable 64bit addressing for OS/System combination supporting it   */
		/* actual NVRAM bit is: nv->cntr_flags_1.enable_64bit_addressing     */
		/* but we will ignore it and use BITS_PER_LONG macro to setup for    */
		/* 64 or 32 bit access of host memory in all x86/ia-64/Alpha systems */
		ha->flags.enable_64bit_addressing = 1;


		/* Update our PCI device dma_mask for full 64 bit mask;
		   disable 64 bit addressing if this fails */
		if (pci_set_dma_mask(ha->pdev, 0xffffffffffffffff)) {
			ha->flags.enable_64bit_addressing = 0;
			pci_set_dma_mask(ha->pdev, 0xffffffff);
		}
	
		if (ha->flags.enable_64bit_addressing)
			printk(KERN_INFO "qla2x00: 64 Bit PCI Addressing Enabled\n");
		ha->flags.enable_lip_reset = nv->host_p.enable_lip_reset;
		ha->flags.enable_lip_full_login = nv->host_p.enable_lip_full_login;
		ha->flags.enable_target_reset = nv->host_p.enable_target_reset;
		ha->flags.enable_flash_db_update = nv->host_p.enable_database_storage;
		ha->operating_mode = icb->additional_firmware_options.connection_options;

		/* new for IOCTL support of APIs */
		ha->node_name[0] = icb->node_name[0];
		ha->node_name[1] = icb->node_name[1];
		ha->node_name[2] = icb->node_name[2];
		ha->node_name[3] = icb->node_name[3];
		ha->node_name[4] = icb->node_name[4];
		ha->node_name[5] = icb->node_name[5];
		ha->node_name[6] = icb->node_name[6];
		ha->node_name[7] = icb->node_name[7];
		ha->nvram_version = nv->nvram_version;
	
		/* empty data for OEM idents */
		ha->oem_id     = 0;
		ha->oem_spare0 = 0;
		for (cnt = 0; cnt < 8; cnt++) {
			ha->oem_string[cnt] = 0;
			ha->oem_part[cnt]   = 0;
			ha->oem_fru[cnt]    = 0;
			ha->oem_ec[cnt]     = 0;
		}

		ha->hiwat = icb->iocb_allocation;
		ha->execution_throttle = nv->execution_throttle;

		ha->retry_count = nv->retry_count;
		ha->login_timeout = nv->login_timeout;
		/* Set minimum login_timeout to 4 seconds. */
		if (ha->login_timeout < 4)
			ha->login_timeout = 4;
		ha->port_down_retry_count = nv->port_down_retry_count;
		ha->minimum_timeout = (ha->login_timeout * ha->retry_count)
		    + ha->port_down_retry_count;
		ha->loop_reset_delay = nv->reset_delay;
		/* Will get the value from nvram. */
		ha->loop_down_timeout = LOOP_DOWN_TIMEOUT;
		ha->loop_down_abort_time = LOOP_DOWN_TIME - ha->loop_down_timeout;

		/* save HBA serial number */
		ha->serial0 = nv->node_name[5];
		ha->serial1 = nv->node_name[6];
		ha->serial2 = nv->node_name[7];
		ha->flags.link_down_error_enable = 1;

#if  USE_BIOS_MAX_LUNS
		if (!nv->maximum_luns_per_target)
			ha->max_luns = MAX_LUNS-1;
		else if (nv->maximum_luns_per_target < MAX_LUNS-1)
			ha->max_luns = nv->maximum_luns_per_target;
		else
			ha->max_luns = MAX_LUNS-1;
#else
		ha->max_luns = MAX_LUNS-1;
#endif

		/*
		   * Setup ring parameters in initialization control block
		 */
		icb->request_q_outpointer = 0;
		icb->response_q_inpointer = 0;
		icb->request_q_length = REQUEST_ENTRY_CNT;
		icb->response_q_length = RESPONSE_ENTRY_CNT;
		icb->request_q_address[0] = LS_64BITS(ha->request_dma);
		icb->request_q_address[1] = MS_64BITS(ha->request_dma);
		icb->response_q_address[0] = LS_64BITS(ha->response_dma);
		icb->response_q_address[1] = MS_64BITS(ha->response_dma);

		icb->lun_enables = 0;
		icb->command_resource_count = 0;
		icb->immediate_notify_resource_count = 0;
		icb->timeout = 0;
		icb->reserved_2 = 0;

		ha->flags.nvram_config_done = 1;
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_nvram_config: **** FAILED ****\n");
#endif

	LEAVE("qla2100_nvram_config");
	return status;
}

/*
* Get NVRAM data word
*      Calculates word position in NVRAM and calls request routine to
*      get the word from NVRAM.
*
* Input:
*      ha      = adapter block pointer.
*      address = NVRAM word address.
*
* Returns:
*      data word.
*/
static uint16_t
qla2100_get_nvram_word(struct scsi_qla_host * ha, uint32_t address)
{
	uint32_t nv_cmd;
	uint16_t data;

#ifdef QL_DEBUG_ROUTINES
	uint8_t saved_print_status = ql2x_debug_print;
#endif
	DEBUG4(printk("qla2100_get_nvram_word: entered\n"););

	nv_cmd = address << 16;
	nv_cmd |= NV_READ_OP;

#ifdef QL_DEBUG_ROUTINES
	ql2x_debug_print = FALSE;
#endif
	data = qla2100_nvram_request(ha, nv_cmd);
#ifdef QL_DEBUG_ROUTINES
	ql2x_debug_print = saved_print_status;
#endif

	DEBUG4(printk("qla2100_get_nvram_word: exiting normally NVRAM data = %x\n", data););
	return data;
}

/*
* NVRAM request
*      Sends read command to NVRAM and gets data from NVRAM.
*
* Input:
*      ha     = adapter block pointer.
*      nv_cmd = Bit 26     = start bit
*               Bit 25, 24 = opcode
*               Bit 23-16  = address
*               Bit 15-0   = write data
*
* Returns:
*      data word.
*/
static uint16_t
qla2100_nvram_request(struct scsi_qla_host * ha, uint32_t nv_cmd)
{
	uint8_t cnt;
	device_reg_t *reg = ha->iobase;
	uint16_t data = 0;
	uint16_t reg_data;

	/* Send command to NVRAM. */

	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla2100_nv_write(ha, NV_DATA_OUT);
		else
			qla2100_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */

	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, NV_SELECT + NV_CLOCK);
		udelay(500);
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NV_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NV_SELECT);
		udelay(500);
	}

	/* Deselect chip. */

	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	udelay(500);

	return data;
}

static void
qla2100_nv_write(struct scsi_qla_host * ha, uint16_t data)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	udelay(500);
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_CLOCK);
	udelay(500);
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	udelay(500);
}

static void
qla2x00_nv_deselect(struct scsi_qla_host * ha)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	udelay(500);
}
