/*
 * linux/drivers/ide/cenatek.c		Version 0.05	June 9, 2000
 *
 * Copyright (C) 1999-2000		Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2002			Arjan van de Ven <arjanv@redhat.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"

#define DISPLAY_VIPER_TIMINGS

#if defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int cenatek_get_info(char *, char **, off_t, int);
extern int (*cenatek_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

#endif  /* defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS) */

byte cenatek_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * Here is where all the hard work goes to program the chipset.
 *
 */
static int cenatek_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int err			= 0;
	byte unit		= (drive->select.b.unit & 0x01);
#ifdef CONFIG_BLK_DEV_IDEDMA
	unsigned long dma_base	= hwif->dma_base;
#endif /* CONFIG_BLK_DEV_IDEDMA */

	if (!drive->init_speed)
		drive->init_speed = speed;


	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return (err);
}

static void config_chipset_for_pio (ide_drive_t *drive)
{	
	byte speed;
	
	speed = XFER_PIO_4;
	(void) cenatek_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void cenatek_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) cenatek_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	struct hd_driveid *id	= drive->id;
	byte udma_66		= 1;
	byte udma_100		= 1;
	byte speed		= 0x00;
	int  rval;

	if (udma_100)
		udma_66 = eighty_ninty_three(drive);

	if ((id->dma_ultra & 0x0020) && (udma_66) && (udma_100)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) && (udma_66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (udma_66)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra & 0x0004) {
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	(void) cenatek_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

	return rval;
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Force if Capable UltraDMA */
		dma_func = config_chipset_for_dma(drive);
	
		config_chipset_for_pio(drive);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * cenatek_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */

int cenatek_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_cenatek (struct pci_dev *dev, const char *name)
{
	unsigned long fixdma_base = pci_resource_start(dev, 4);

#if defined(DISPLAY_VIPER_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!cenatek_proc) {
		cenatek_proc = 1;
		bmide_dev = dev;
	}
#endif /* DISPLAY_VIPER_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

unsigned int __init ata66_cenatek (ide_hwif_t *hwif)
{
	return(1);
}

void __init ide_init_cenatek (ide_hwif_t *hwif)
{
	hwif->tuneproc = &cenatek_tune_drive;
	hwif->speedproc = &cenatek_tune_chipset;

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
	return;
#else

	if (hwif->dma_base) {
		hwif->dmaproc = &cenatek_dmaproc;
		if (!noautodma) {
			hwif->autodma = 1;
			hwif->highmem = 1;
		}
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_cenatek (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}
