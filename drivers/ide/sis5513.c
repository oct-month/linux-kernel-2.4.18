/*
 * linux/drivers/ide/sis5513.c		Version 0.14	July 24, 2002
 *
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2002		Lionel Bouton <Lionel.Bouton@inet6.fr>, Maintainer
 * May be copied or modified under the terms of the GNU General Public License
 *
 *
 * Thanks :
 *
 * SiS Taiwan		: for direct support and hardware.
 * Daniela Engert	: for initial ATA100 advices and numerous others.
 * John Fremlin, Manfred Spraul :
 *			  for checking code correctness, providing patches.
 *
 *
 * Original tests and design on the SiS620/5513 chipset.
 * ATA100 tests and design on the SiS735/5513 chipset.
 * ATA16/33 design from specs
 * ATA133 support for SiS961/962 by L.C. Chang <lcchang@sis.com.tw>
 */

/*
 * TODO:
 *	- Get ridden of SisHostChipInfo[] completness dependancy.
 *	- Get ATA-133 datasheets, implement ATA-133 init code.
 *	- Study drivers/ide/ide-timing.h.
 *	- Are there pre-ATA_16 SiS5513 chips ? -> tune init code for them
 *	  or remove ATA_00 define
 *	- More checks in the config registers (force values instead of
 *	  relying on the BIOS setting them correctly).
 *	- Further optimisations ?
 *	  . for example ATA66+ regs 0x48 & 0x4A
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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"

/* When DEBUG is defined it outputs initial PCI config register
   values and changes made to them by the driver */   
//#define DEBUG
/* When BROKEN_LEVEL is defined it limits the DMA mode
   at boot time to its value */
// #define BROKEN_LEVEL XFER_SW_DMA_0
#define DISPLAY_SIS_TIMINGS

/* Miscellaneaous flags */
#define SIS5513_LATENCY		0x01

/* registers layout and init values are chipset family dependant */
/* 1/ define families */
#define ATA_00		0x00
#define ATA_16		0x01
#define ATA_33		0x02
#define ATA_66		0x03
#define ATA_100a	0x04 // SiS730 is ATA100 with ATA66 layout
#define ATA_100		0x05
#define ATA_133a	0x06 // SiS961b with 133 support
#define ATA_133		0x07 // SiS962
/* 2/ variable holding the controller chipset family value */
static unsigned char chipset_family;


/*
 * Debug code: following IDE config registers' changes
 */
#ifdef DEBUG
/* Copy of IDE Config registers 0x00 -> 0x7f
   Fewer might be used depending on the actual chipset */
static unsigned char ide_regs_copy[0x80];

static byte sis5513_max_config_register(void) {
	switch(chipset_family) {
		case ATA_00:
		case ATA_16:	return 0x4f;
		case ATA_33:	return 0x52;
		case ATA_66:
		case ATA_100a:
		case ATA_100:
		case ATA_133a:  return 0x57;
		case ATA_133:	return 0x7f;
		default:	return 0x57;
	}
}

/* Read config registers, print differences from previous read */
static void sis5513_load_verify_registers(struct pci_dev* dev, char* info) {
	int i;
	byte reg_val;
	byte changed=0;
	byte max = sis5513_max_config_register();

	printk("SIS5513: %s, changed registers:\n", info);
	for(i=0; i<=max; i++) {
		pci_read_config_byte(dev, i, &reg_val);
		if (reg_val != ide_regs_copy[i]) {
			printk("%0#x: %0#x -> %0#x\n",
			       i, ide_regs_copy[i], reg_val);
			ide_regs_copy[i]=reg_val;
			changed=1;
		}
	}

	if (!changed) {
		printk("none\n");
	}
}

/* Load config registers, no printing */
static void sis5513_load_registers(struct pci_dev* dev) {
	int i;
	byte max = sis5513_max_config_register();

	for(i=0; i<=max; i++) {
		pci_read_config_byte(dev, i, &(ide_regs_copy[i]));
	}
}

/* Print a register */
static void sis5513_print_register(int reg) {
	printk(" %0#x:%0#x", reg, ide_regs_copy[reg]);
}

/* Print valuable registers */
static void sis5513_print_registers(struct pci_dev* dev, char* marker) {
	int i;
	byte max = sis5513_max_config_register();

	sis5513_load_registers(dev);
	printk("SIS5513 %s\n", marker);
	printk("SIS5513 dump:");
	for(i=0x00; i<0x40; i++) {
		if ((i % 0x10)==0) printk("\n             ");
		sis5513_print_register(i);
	}
	for(; i<49; i++) {
		sis5513_print_register(i);
	}
	printk("\n             ");

	for(; i<=max; i++) {
		sis5513_print_register(i);
	}
	printk("\n");
}
#endif


/*
 * Devices supported
 */
static const struct {
	const char *name;
	unsigned short host_id;
	unsigned char chipset_family;
	unsigned char flags;
} SiSHostChipInfo[] = {
	{ "SiS752",	PCI_DEVICE_ID_SI_752,	ATA_133,	0 },
	{ "SiS751",	PCI_DEVICE_ID_SI_751,	ATA_133,	0 },
	{ "SiS750",	PCI_DEVICE_ID_SI_750,	ATA_133,	0 },
	{ "SiS748",	PCI_DEVICE_ID_SI_748,	ATA_133,	0 },
	{ "SiS746",	PCI_DEVICE_ID_SI_746,	ATA_133,	0 },
	{ "SiS745",	PCI_DEVICE_ID_SI_745,	ATA_133,	0 },
	{ "SiS740",	PCI_DEVICE_ID_SI_740,	ATA_100,	0 },
	{ "SiS735",	PCI_DEVICE_ID_SI_735,	ATA_100,	SIS5513_LATENCY },
	{ "SiS730",	PCI_DEVICE_ID_SI_730,	ATA_100a,	SIS5513_LATENCY },
	{ "SiS652",	PCI_DEVICE_ID_SI_652,	ATA_133,	0 },
	{ "SiS651",	PCI_DEVICE_ID_SI_651,	ATA_133,	0 },
	{ "SiS650",	PCI_DEVICE_ID_SI_650,	ATA_133,	0 },
	{ "SiS648",	PCI_DEVICE_ID_SI_648,	ATA_133,	0 },
	{ "SiS646",	PCI_DEVICE_ID_SI_646,	ATA_133,	0 },
	{ "SiS645",	PCI_DEVICE_ID_SI_645,	ATA_133,	0 },
	{ "SiS635",	PCI_DEVICE_ID_SI_635,	ATA_100,	SIS5513_LATENCY },
	{ "SiS640",	PCI_DEVICE_ID_SI_640,	ATA_66,		SIS5513_LATENCY },
	{ "SiS630",	PCI_DEVICE_ID_SI_630,	ATA_66,		SIS5513_LATENCY },
	{ "SiS620",	PCI_DEVICE_ID_SI_620,	ATA_66,		SIS5513_LATENCY },
	{ "SiS550",	PCI_DEVICE_ID_SI_550,	ATA_100a,	0},
	{ "SiS540",	PCI_DEVICE_ID_SI_540,	ATA_66,		0},
	{ "SiS530",	PCI_DEVICE_ID_SI_530,	ATA_66,		0},
	{ "SiS5600",	PCI_DEVICE_ID_SI_5600,	ATA_33,		0},
	{ "SiS5598",	PCI_DEVICE_ID_SI_5598,	ATA_33,		0},
	{ "SiS5597",	PCI_DEVICE_ID_SI_5597,	ATA_33,		0},
	{ "SiS5591",	PCI_DEVICE_ID_SI_5591,	ATA_33,		0},
	{ "SiS5513",	PCI_DEVICE_ID_SI_5513,	ATA_16,		0},
	{ "SiS5511",	PCI_DEVICE_ID_SI_5511,	ATA_16,		0},
};

/* Cycle time bits and values vary accross chip dma capabilities
   These three arrays hold the register layout and the values to set.
   Indexed by chipset_family and (dma_mode - XFER_UDMA_0) */
static byte cycle_time_offset[] = {0,0,5,4,4,0,0};
static byte cycle_time_range[] = {0,0,2,3,3,4,4};
static byte cycle_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{3,2,1,0,0,0,0},
	{7,5,3,2,1,0,0},
	{7,5,3,2,1,0,0},
	{11,7,5,4,2,1,0},
	{15,10,7,5,3,2,1},
	{15,10,7,5,3,2,1}
};
/* CRC Valid Setup Time vary accross IDE clock setting 33/66/100/133
   See SiS962 data sheet for more detail */
static byte cvs_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{2,1,1,0,0,0,0},
	{4,3,2,1,0,0,0},
	{4,3,2,1,0,0,0},
	{6,4,3,1,1,1,0},
	{9,6,4,2,2,2,2},
	{9,6,4,2,2,2,2}
};
/* Initialize time, Active time, Recovery time vary accross
   IDE clock settings. These 3 arrays hold the register value
   for PIO0/1/2/3/4 and DMA0/1/2 mode in order */
static byte ini_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{2,1,0,0,0,1,0,0},
	{4,3,1,1,1,3,1,1},
	{4,3,1,1,1,3,1,1},
	{6,4,2,2,2,4,2,2},
	{9,6,3,3,3,6,3,3},
	{9,6,3,3,3,6,3,3}
};
static byte act_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,9,9,2,2,7,2,2},
	{19,19,19,5,4,14,5,4},
	{19,19,19,5,4,14,5,4},
	{28,28,28,7,6,21,7,6},
	{38,38,38,10,9,28,10,9},
	{38,38,38,10,9,28,10,9}
};
static byte rco_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,2,0,2,0,7,1,1},
	{19,5,1,5,2,16,3,2},
	{19,5,1,5,2,16,3,2},
	{30,9,3,9,4,25,6,4},
	{40,12,4,12,5,34,12,5},
	{40,12,4,12,5,34,12,5}
};

static struct pci_dev *host_dev = NULL;


/*
 * Printing configuration
 */
#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int sis_get_info(char *, char **, off_t, int);
extern int (*sis_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static char* cable_type[] = {
	"80 pins",
	"40 pins"
};

static char* recovery_time[] ={
	"12 PCICLK", "1 PCICLK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLCK",
	"6 PCICLK", "7 PCICLCK",
	"8 PCICLK", "9 PCICLCK",
	"10 PCICLK", "11 PCICLK",
	"13 PCICLK", "14 PCICLK",
	"15 PCICLK", "15 PCICLK"
};

static char* active_time[] = {
	"8 PCICLK", "1 PCICLCK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLK",
	"6 PCICLK", "12 PCICLK"
};

static char* cycle_time[] = {
	"Reserved", "2 CLK",
	"3 CLK", "4 CLK",
	"5 CLK", "6 CLK",
	"7 CLK", "8 CLK",
	"9 CLK", "10 CLK",
	"11 CLK", "12 CLK",
	"13 CLK", "14 CLK",
	"15 CLK", "16 CLK"
};

static char* chipset_capability[] = {
	"ATA", "ATA 16",
	"ATA 33", "ATA 66",
	"ATA 100", "ATA 100",
	"ATA 133", "ATA 133"
};

/* Generic add master or slave info function */
static char* get_drives_info (char *buffer, byte pos)
{
	byte reg00, reg01, reg10, reg11; /* timing registers */
	char* p = buffer;

/* Postwrite/Prefetch */
	if (chipset_family < ATA_133) {
	pci_read_config_byte(bmide_dev, 0x4b, &reg00);
	p += sprintf(p, "Drive %d:        Postwrite %s \t \t Postwrite %s\n",
		     pos, (reg00 & (0x10 << pos)) ? "Enabled" : "Disabled",
		     (reg00 & (0x40 << pos)) ? "Enabled" : "Disabled");
	p += sprintf(p, "                Prefetch  %s \t \t Prefetch  %s\n",
		     (reg00 & (0x01 << pos)) ? "Enabled" : "Disabled",
		     (reg00 & (0x04 << pos)) ? "Enabled" : "Disabled");

	pci_read_config_byte(bmide_dev, 0x40+2*pos, &reg00);
	pci_read_config_byte(bmide_dev, 0x41+2*pos, &reg01);
	pci_read_config_byte(bmide_dev, 0x44+2*pos, &reg10);
	pci_read_config_byte(bmide_dev, 0x45+2*pos, &reg11);
	}

/* UDMA */
	if (chipset_family >= ATA_33) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (reg01 & 0x80)  ? "Enabled" : "Disabled",
			     (reg11 & 0x80) ? "Enabled" : "Disabled");

		p += sprintf(p, "                UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg01 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg01 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:  p += sprintf(p, cycle_time[reg01 & 0x0F]); break;
			case ATA_133:
			default:	p += sprintf(p, "133+ ?"); break;
		}
		p += sprintf(p, " \t UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg11 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg11 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:  p += sprintf(p, cycle_time[reg11 & 0x0F]); break;
			case ATA_133:
			default:	p += sprintf(p, "133+ ?"); break;
		}
		p += sprintf(p, "\n");
	}

/* Data Active */
	p += sprintf(p, "                Data Active Time   ");
	switch(chipset_family) {
		case ATA_00:
		case ATA_16: /* confirmed */
		case ATA_33:
		case ATA_66:
		case ATA_100a: p += sprintf(p, active_time[reg01 & 0x07]); break;
		case ATA_100:
		case ATA_133a: p += sprintf(p, active_time[(reg00 & 0x70) >> 4]); break;
		case ATA_133:
		default: p += sprintf(p, "133+ ?"); break;
	}
	p += sprintf(p, " \t Data Active Time   ");
	switch(chipset_family) {
		case ATA_00:
		case ATA_16:
		case ATA_33:
		case ATA_66:
		case ATA_100a: p += sprintf(p, active_time[reg11 & 0x07]); break;
		case ATA_100:
		case ATA_133a: p += sprintf(p, active_time[(reg10 & 0x70) >> 4]); break;
		case ATA_133:
		default: p += sprintf(p, "133+ ?"); break;
	}
	p += sprintf(p, "\n");

/* Data Recovery */
	/* warning: may need (reg&0x07) for pre ATA66 chips */
	if (chipset_family < ATA_133) {
	p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
		     recovery_time[reg00 & 0x0f], recovery_time[reg10 & 0x0f]);
	}

	return p;
}

static char* get_masters_info(char* buffer)
{
	return get_drives_info(buffer, 0);
}

static char* get_slaves_info(char* buffer)
{
	return get_drives_info(buffer, 1);
}

/* Main get_info, called on /proc/ide/sis reads */
static int sis_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	byte reg;
	u16 reg2, reg3;

	p += sprintf(p, "\nSiS 5513 ");
	switch(chipset_family) {
		case ATA_00: p += sprintf(p, "Unknown???"); break;
		case ATA_16: p += sprintf(p, "DMA 16"); break;
		case ATA_33: p += sprintf(p, "Ultra 33"); break;
		case ATA_66: p += sprintf(p, "Ultra 66"); break;
		case ATA_100a:
		case ATA_100: p += sprintf(p, "Ultra 100"); break;
		case ATA_133a:
		case ATA_133:
		default: p+= sprintf(p, "Ultra 133+"); break;
	}
	p += sprintf(p, " chipset\n");
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");

/* Status */
	pci_read_config_byte(bmide_dev, 0x4a, &reg);
	pci_read_config_word(bmide_dev, 0x50, &reg2);
	pci_read_config_word(bmide_dev, 0x52, &reg3);
	p += sprintf(p, "Channel Status: ");
	if (chipset_family < ATA_66) {
		p += sprintf(p, "%s \t \t \t \t %s\n",
			     (reg & 0x04) ? "On" : "Off",
			     (reg & 0x02) ? "On" : "Off");
	} else if (chipset_family < ATA_133) {
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg & 0x02) ? "On" : "Off",
			     (reg & 0x04) ? "On" : "Off");
	} else {
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg2 & 0x02) ? "On" : "Off",
			     (reg3 & 0x02) ? "On" : "Off");
	}

/* Operation Mode */
	pci_read_config_byte(bmide_dev, 0x09, &reg);
	p += sprintf(p, "Operation Mode: %s \t \t \t %s \n",
		     (reg & 0x01) ? "Native" : "Compatible",
		     (reg & 0x04) ? "Native" : "Compatible");

/* 80-pin cable ? */
	if (chipset_family >= ATA_133) {
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg2 & 0x01) ? cable_type[1] : cable_type[0],
			     (reg3 & 0x01) ? cable_type[1] : cable_type[0]);
	} else if (chipset_family > ATA_33) {
		pci_read_config_byte(bmide_dev, 0x48, &reg);
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg & 0x10) ? cable_type[1] : cable_type[0],
			     (reg & 0x20) ? cable_type[1] : cable_type[0]);
	}

/* Prefetch Count */
	if (chipset_family < ATA_133) {
		pci_read_config_word(bmide_dev, 0x4c, &reg2);
		pci_read_config_word(bmide_dev, 0x4e, &reg3);
		p += sprintf(p, "Prefetch Count: %d \t \t \t \t %d\n",
			     reg2, reg3);
	}

	p = get_masters_info(p);
	p = get_slaves_info(p);

	return p-buffer;
}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS) */


byte sis_proc = 0;
extern char *ide_xfer_verbose (byte xfer_rate);


/*
 * Configuration functions
 */
/* Enables per-drive prefetch and postwrite */
static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte reg4bh		= 0;
	byte rw_prefetch	= (0x11 << drive->dn);

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp, drive %d\n", drive->dn);
	sis5513_load_verify_registers(dev, "config_drive_art_rwp start");
#endif

	if (drive->media != ide_disk)
		return;
	pci_read_config_byte(dev, 0x4b, &reg4bh);

	if ((reg4bh & rw_prefetch) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp end");
#endif
}


/* Set per-drive active and recovery time */
static void config_art_rwp_pio (ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte			timing, drive_pci, test1, test2;
	u32			test3;

	unsigned short eide_pio_timing[6] = {600, 390, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif

	config_drive_art_rwp(drive);
	pio = ide_get_best_pio_mode(drive, 255, pio, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			(xfer_pio > 0) &&
			(drive->id->eide_pio_iordy > eide_pio_timing[xfer_pio]);
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

#ifdef DEBUG
	printk("SIS5513: config_drive_art_rwp_pio, drive %d, pio %d, timing %d\n",
	       drive->dn, pio, timing);
#endif

	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		/* check SiS962 remapping control */
		if (reg54h & 0x40000000)
			drive_pci = 0x70;
		else
			drive_pci = 0x40;
		switch(drive->dn) {
			case 0:		drive_pci += 0x0; break;
			case 1:		drive_pci += 0x4; break;
			case 2:		drive_pci += 0x8; break;
			case 3:		drive_pci += 0xc; break;
			default:	return;
		}
	} else {
		switch(drive->dn) {
			case 0:		drive_pci = 0x40; break;
			case 1:		drive_pci = 0x42; break;
			case 2:		drive_pci = 0x44; break;
			case 3:		drive_pci = 0x46; break;
			default:	return;
		}
	}

	/* register layout changed with newer ATA100 chips */
	if (chipset_family < ATA_100) {
		pci_read_config_byte(dev, drive_pci, &test1);
		pci_read_config_byte(dev, drive_pci+1, &test2);

		/* Clear active and recovery timings */
		test1 &= ~0x0F;
		test2 &= ~0x07;

		switch(timing) {
			case 4:		test1 |= 0x01; test2 |= 0x03; break;
			case 3:		test1 |= 0x03; test2 |= 0x03; break;
			case 2:		test1 |= 0x04; test2 |= 0x04; break;
			case 1:		test1 |= 0x07; test2 |= 0x06; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
		pci_write_config_byte(dev, drive_pci+1, test2);
	} else if (chipset_family < ATA_133) {
		switch(timing) { /*   active  recovery
					  v     v */
			case 4:		test1 = 0x30|0x01; break;
			case 3:		test1 = 0x30|0x03; break;
			case 2:		test1 = 0x40|0x04; break;
			case 1:		test1 = 0x60|0x07; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
	} else {
		pci_read_config_dword(dev, drive_pci, &test3);
		test3 &= 0xc0c00fff;
		if (test3 & 0x08) {
			test3 |= (unsigned long)ini_time_value[ATA_133-ATA_00][timing] << 12;
			test3 |= (unsigned long)act_time_value[ATA_133-ATA_00][timing] << 16;
			test3 |= (unsigned long)rco_time_value[ATA_133-ATA_00][timing] << 24;
		} else {
			test3 |= (unsigned long)ini_time_value[ATA_100-ATA_00][timing] << 12;
			test3 |= (unsigned long)act_time_value[ATA_100-ATA_00][timing] << 16;
			test3 |= (unsigned long)rco_time_value[ATA_100-ATA_00][timing] << 24;
		}
		pci_write_config_dword(dev, drive_pci, test3);
	}

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "config_drive_art_rwp_pio start");
#endif
}

static int config_chipset_for_pio (ide_drive_t *drive, byte pio)
{
	byte speed;

	switch(pio) {
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}

	config_art_rwp_pio(drive, pio);
	drive->current_speed = speed;
	return ide_config_drive_speed(drive, speed);
}

static int sis5513_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte			drive_pci, reg;
	u32			regdw;

#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset start");
	printk("SIS5513: sis5513_tune_chipset, drive %d, speed %d\n",
	       drive->dn, speed);
#endif

#ifdef BROKEN_LEVEL
#ifdef DEBUG
	printk("SIS5513: BROKEN_LEVEL activated, speed=%d -> speed=%d\n", speed, BROKEN_LEVEL);
#endif
	if (speed > BROKEN_LEVEL) speed = BROKEN_LEVEL;
#endif

	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		/* check SiS962 remapping control */
		if (reg54h & 0x40000000)
			drive_pci = 0x70;
		else
			drive_pci = 0x40;
		switch(drive->dn) {
			case 0:		drive_pci += 0x0; break;
			case 1:		drive_pci += 0x4; break;
			case 2:		drive_pci += 0x8; break;
			case 3:		drive_pci += 0xc; break;
			default:	return ide_dma_off;
		}
		pci_read_config_dword(dev, (unsigned long)drive_pci, &regdw);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if (speed < XFER_UDMA_0) {
			regdw &= 0xfffffffb;
			pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
		}
	
	} else {
		switch(drive->dn) {
			case 0:		drive_pci = 0x40; break;
			case 1:		drive_pci = 0x42; break;
			case 2:		drive_pci = 0x44; break;
			case 3:		drive_pci = 0x46; break;
			default:	return ide_dma_off;
		}
		pci_read_config_byte(dev, drive_pci+1, &reg);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if ((speed < XFER_UDMA_0) && (chipset_family > ATA_16)) {
			reg &= 0x7F;
			pci_write_config_byte(dev, drive_pci+1, reg);
		}
	}

	/* Config chip for mode */
	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			if (chipset_family >= ATA_133) {
				regdw |= 0x04;
				regdw &= 0xfffff00f;
				/* check if ATA133 enable */
				if (regdw & 0x08) {
					regdw |= (unsigned long)cycle_time_value[ATA_133-ATA_00][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_133-ATA_00][speed-XFER_UDMA_0] << 8;
				} else {
				/* if ATA133 disable, we should not set speed above UDMA5 */
					if (speed > XFER_UDMA_5)
						speed = XFER_UDMA_5;
					regdw |= (unsigned long)cycle_time_value[ATA_100-ATA_00][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_100-ATA_00][speed-XFER_UDMA_0] << 8;
				}
				pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
			} else {
				/* Force the UDMA bit on if we want to use UDMA */
				reg |= 0x80;
				/* clean reg cycle time bits */
				reg &= ~((0xFF >> (8 - cycle_time_range[chipset_family]))
					 << cycle_time_offset[chipset_family]);
				/* set reg cycle time bits */
				reg |= cycle_time_value[chipset_family-ATA_00][speed-XFER_UDMA_0]
					<< cycle_time_offset[chipset_family];
				pci_write_config_byte(dev, drive_pci+1, reg);
			}
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4: return((int) config_chipset_for_pio(drive, 4));
		case XFER_PIO_3: return((int) config_chipset_for_pio(drive, 3));
		case XFER_PIO_2: return((int) config_chipset_for_pio(drive, 2));
		case XFER_PIO_1: return((int) config_chipset_for_pio(drive, 1));
		case XFER_PIO_0:
		default:	 return((int) config_chipset_for_pio(drive, 0));	
	}
	drive->current_speed = speed;
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "sis5513_tune_chipset end");
#endif
	return ((int) ide_config_drive_speed(drive, speed));
}

static void sis5513_tune_drive (ide_drive_t *drive, byte pio)
{
	(void) config_chipset_for_pio(drive, pio);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * ((id->hw_config & 0x4000|0x2000) && (HWIF(drive)->udma_four))
 */
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);

	byte			speed = 0;

	byte unit		= (drive->select.b.unit & 0x01);
	byte udma_66		= eighty_ninty_three(drive);

#ifdef DEBUG
	printk("SIS5513: config_chipset_for_dma, drive %d, ultra %x, udma_66 %x\n",
	       drive->dn, id->dma_ultra, udma_66);
#endif

	if ((id->dma_ultra & 0x0040) && ultra && udma_66 && (chipset_family >= ATA_133a))
		speed = XFER_UDMA_6;
	else if ((id->dma_ultra & 0x0020) && ultra && udma_66 && (chipset_family >= ATA_100a))
		speed = XFER_UDMA_5;
	else if ((id->dma_ultra & 0x0010) && ultra && udma_66 && (chipset_family >= ATA_66))
		speed = XFER_UDMA_4;
	else if ((id->dma_ultra & 0x0008) && ultra && udma_66 && (chipset_family >= ATA_66))
		speed = XFER_UDMA_3;
	else if ((id->dma_ultra & 0x0004) && ultra && (chipset_family >= ATA_33))
		speed = XFER_UDMA_2;
	else if ((id->dma_ultra & 0x0002) && ultra && (chipset_family >= ATA_33))
		speed = XFER_UDMA_1;
	else if ((id->dma_ultra & 0x0001) && ultra && (chipset_family >= ATA_33))
		speed = XFER_UDMA_0;
	else if (id->dma_mword & 0x0004)
		speed = XFER_MW_DMA_2;
	else if (id->dma_mword & 0x0002)
		speed = XFER_MW_DMA_1;
	else if (id->dma_mword & 0x0001)
		speed = XFER_MW_DMA_0;
	else if (id->dma_1word & 0x0004)
		speed = XFER_SW_DMA_2;
	else if (id->dma_1word & 0x0002)
		speed = XFER_SW_DMA_1;
	else if (id->dma_1word & 0x0001)
		speed = XFER_SW_DMA_0;
	else
		return ((int) ide_dma_off_quietly);

	outb(inb(hwif->dma_base+2)|(1<<(5+unit)), hwif->dma_base+2);

	sis5513_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 15) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id		= drive->id;
	ide_dma_action_t dma_func	= ide_dma_off_quietly;

	(void) config_chipset_for_pio(drive, 5);

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x003F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive, 0);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if ((ide_dmaproc(ide_dma_good_drive, drive)) &&
			   (id->eide_dma_time > 150)) {
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, 0);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		(void) config_chipset_for_pio(drive, 5);
	}

	return HWIF(drive)->dmaproc(dma_func, drive);
}

/* initiates/aborts (U)DMA read/write operations on a drive. */
int sis5513_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			config_drive_art_rwp(drive);
			config_art_rwp_pio(drive, 5);
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/* Chip detection and general config */
unsigned int __init pci_init_sis5513 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *host;
	int i = 0;

	/* Find the chip */
	for (i = 0; i < ARRAY_SIZE(SiSHostChipInfo) && !host_dev; i++) {
		host = pci_find_device (PCI_VENDOR_ID_SI,
					SiSHostChipInfo[i].host_id,
					NULL);
		if (!host)
			continue;

		host_dev = host;
		chipset_family = SiSHostChipInfo[i].chipset_family;
	
		/* check 100/133 chipset family */
		if (chipset_family == ATA_133) {
			u32 reg54h;
			u16 reg02h;
			pci_read_config_dword(dev, 0x54, &reg54h);
			pci_write_config_dword(dev, 0x54, (reg54h & 0x7fffffff));
			pci_read_config_word(dev, 0x02, &reg02h);
			pci_write_config_dword(dev, 0x54, reg54h);
			/* devid 5518 here means SiS962 or later
			   which supports ATA133 */
			if (reg02h != 0x5518) {
				byte reg49h;
				unsigned long sbrev;
				/* SiS961 family */
				outl(0x80001008, 0x0cf8);
				sbrev = inl(0x0cfc);
				pci_read_config_byte(dev, 0x49, &reg49h);
				if (((sbrev & 0xff) == 0x10) && (reg49h & 0x80))
					chipset_family = ATA_133a;
				else
					chipset_family = ATA_100;
			}
		}
		printk(SiSHostChipInfo[i].name);
		printk("    %s controller", chipset_capability[chipset_family]);
		printk("\n");

#ifdef DEBUG
		sis5513_print_registers(dev, "pci_init_sis5513 start");
#endif

		if (SiSHostChipInfo[i].flags & SIS5513_LATENCY) {
			byte latency = (chipset_family == ATA_100)? 0x80 : 0x10; /* Lacking specs */
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);
		}
	}

	/* Make general config ops here
	   1/ tell IDE channels to operate in Compabitility mode only
	   2/ tell old chips to allow per drive IDE timings */
	if (host_dev) {
		byte reg;
		u16 regw;
		switch(chipset_family) {
			case ATA_133:
				/* SiS962 operation mode */
				pci_read_config_word(dev, 0x50, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x50, regw&0xfff7);
				pci_read_config_word(dev, 0x52, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x52, regw&0xfff7);
				break;
			case ATA_133a:
			case ATA_100:
				/* Set compatibility bit */
				pci_read_config_byte(dev, 0x49, &reg);
				if (!(reg & 0x01)) {
					pci_write_config_byte(dev, 0x49, reg|0x01);
				}
				break;
			case ATA_100a:
			case ATA_66:
				/* On ATA_66 chips the bit was elsewhere */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x04)) {
					pci_write_config_byte(dev, 0x52, reg|0x04);
				}
				break;
			case ATA_33:
				/* On ATA_33 we didn't have a single bit to set */
				pci_read_config_byte(dev, 0x09, &reg);
				if ((reg & 0x0f) != 0x00) {
					pci_write_config_byte(dev, 0x09, reg&0xf0);
				}
			case ATA_16:
				/* force per drive recovery and active timings
				   needed on ATA_33 and below chips */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x08)) {
					pci_write_config_byte(dev, 0x52, reg|0x08);
				}
				break;
			case ATA_00:
			default: break;
		}

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_PROC_FS)
		if (!sis_proc) {
			sis_proc = 1;
			bmide_dev = dev;
			sis_display_info = &sis_get_info;
		}
#endif
	}
#ifdef DEBUG
	sis5513_load_verify_registers(dev, "pci_init_sis5513 end");
#endif
	return 0;
}

unsigned int __init ata66_sis5513 (ide_hwif_t *hwif)
{
	byte ata66 = 0;

	if (chipset_family >= ATA_133) {
		u16 regw = 0;
		u16 reg_addr = hwif->channel ? 0x52: 0x50;
		pci_read_config_word(hwif->pci_dev, reg_addr, &regw);
		ata66 = (regw & 0x8000) ? 0 : 1;
	} else if (chipset_family >= ATA_66) {
		byte reg48h = 0;
		byte mask = hwif->channel ? 0x20 : 0x10;
		pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);
		ata66 = (reg48h & mask) ? 0 : 1;
	}
        return ata66;
}

void __init ide_init_sis5513 (ide_hwif_t *hwif)
{

	hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &sis5513_tune_drive;
	hwif->speedproc = &sis5513_tune_chipset;

	if (!(hwif->dma_base))
		return;

	if (host_dev) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		if (chipset_family > ATA_16) {
			hwif->autodma = noautodma ? 0 : 1;
			hwif->dmaproc = &sis5513_dmaproc;
		} else {
#endif
			hwif->autodma = 0;
#ifdef CONFIG_BLK_DEV_IDEDMA
		}
#endif
	}
	return;
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_sis5513 (struct pci_dev *dev, ide_pci_device_t *d)
{
	if (dev->resource[0].start != 0x01F1)
		ide_register_xp_fix(dev);

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
}
