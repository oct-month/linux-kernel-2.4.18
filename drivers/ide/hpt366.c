/*
 * linux/drivers/ide/hpt366.c		Version 0.18	June. 9, 2000
 *
 * Copyright (C) 1999-2000		Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
 *
 * Note that final HPT370 support was done by force extraction of GPL.
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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"

#define DISPLAY_HPT366_TIMINGS

#define F_LOW_PCI_33      0x23
#define F_LOW_PCI_40      0x29
#define F_LOW_PCI_50      0x2d
#define F_LOW_PCI_66      0x42
#define HPT366_MAX_DEVS                 8


static struct pci_dev *hpt_devs[HPT366_MAX_DEVS];
static int n_hpt_devs;


#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

extern char *ide_dmafunc_verbose(ide_dma_action_t dmafunc);

const char *quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP LM20.5",
        NULL
};

const char *bad_ata100_5[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

const char *bad_ata66_4[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;
	unsigned int	chipset_settings_write;
	unsigned int	chipset_settings_read;
};

struct chipset_bus_clock_list_entry forty_base_hpt366 [] = {

	{	XFER_UDMA_4,	0x900fd943,	0x900fd943	},
	{	XFER_UDMA_3,	0x900ad943,	0x900ad943	},
	{	XFER_UDMA_2,	0x900bd943,	0x900bd943	},
	{	XFER_UDMA_1,	0x9008d943,	0x9008d943	},
	{	XFER_UDMA_0,	0x9008d943,	0x9008d943	},

	{	XFER_MW_DMA_2,	0xa008d943,	0xa008d943	},
	{	XFER_MW_DMA_1,	0xa010d955,	0xa010d955	},
	{	XFER_MW_DMA_0,	0xa010d9fc,	0xa010d9fc	},

	{	XFER_PIO_4,	0xc008d963,	0xc008d963	},
	{	XFER_PIO_3,	0xc010d974,	0xc010d974	},
	{	XFER_PIO_2,	0xc010d997,	0xc010d997	},
	{	XFER_PIO_1,	0xc010d9c7,	0xc010d9c7	},
	{	XFER_PIO_0,	0xc018d9d9,	0xc018d9d9	},
	{	0,		0x0120d9d9,	0x0120d9d9	}
};

struct chipset_bus_clock_list_entry thirty_three_base_hpt366 [] = {

	{	XFER_UDMA_4,	0x90c9a731,	0x90c9a731	},
	{	XFER_UDMA_3,	0x90cfa731,	0x90cfa731	},
	{	XFER_UDMA_2,	0x90caa731,	0x90caa731	},
	{	XFER_UDMA_1,	0x90cba731,	0x90cba731	},
	{	XFER_UDMA_0,	0x90c8a731,	0x90c8a731	},

	{	XFER_MW_DMA_2,	0xa0c8a731,	0xa0c8a731	},
	{	XFER_MW_DMA_1,	0xa0c8a732,	0xa0c8a732	},	/* 0xa0c8a733 */
	{	XFER_MW_DMA_0,	0xa0c8a797,	0xa0c8a797	},

	{	XFER_PIO_4,	0xc0c8a731,	0xc0c8a731	},
	{	XFER_PIO_3,	0xc0c8a742,	0xc0c8a742	},
	{	XFER_PIO_2,	0xc0d0a753,	0xc0d0a753	},
	{	XFER_PIO_1,	0xc0d0a7a3,	0xc0d0a7a3	},	/* 0xc0d0a793 */
	{	XFER_PIO_0,	0xc0d0a7aa,	0xc0d0a7aa	},	/* 0xc0d0a7a7 */
	{	0,		0x0120a7a7,	0x0120a7a7	}
};

struct chipset_bus_clock_list_entry twenty_five_base_hpt366 [] = {

	{	XFER_UDMA_4,	0x90c98521,	0x90c98521	},
	{	XFER_UDMA_3,	0x90cf8521,	0x90cf8521	},
	{	XFER_UDMA_2,	0x90cf8521,	0x90cf8521	},
	{	XFER_UDMA_1,	0x90cb8521,	0x90cb8521	},
	{	XFER_UDMA_0,	0x90cb8521,	0x90cb8521	},

	{	XFER_MW_DMA_2,	0xa0ca8521,	0xa0ca8521	},
	{	XFER_MW_DMA_1,	0xa0ca8532,	0xa0ca8532	},
	{	XFER_MW_DMA_0,	0xa0ca8575,	0xa0ca8575	},

	{	XFER_PIO_4,	0xc0ca8521,	0xc0ca8521	},
	{	XFER_PIO_3,	0xc0ca8532,	0xc0ca8532	},
	{	XFER_PIO_2,	0xc0ca8542,	0xc0ca8542	},
	{	XFER_PIO_1,	0xc0d08572,	0xc0d08572	},
	{	XFER_PIO_0,	0xc0d08585,	0xc0d08585	},
	{	0,		0x01208585,	0x01208585	}
};

struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
	{	XFER_UDMA_5,	0x1A85F442,	0x16454e31	},
	{	XFER_UDMA_4,	0x16454e31,	0x16454e31	},
	{	XFER_UDMA_3,	0x166d4e31,	0x166d4e31	},
	{	XFER_UDMA_2,	0x16494e31,	0x16494e31	},
	{	XFER_UDMA_1,	0x164d4e31,	0x164d4e31	},
	{	XFER_UDMA_0,	0x16514e31,	0x16514e31	},

	{	XFER_MW_DMA_2,	0x26514e21,	0x26514e21	},
	{	XFER_MW_DMA_1,	0x26514e33,	0x26514e33	},
	{	XFER_MW_DMA_0,	0x26514e97,	0x26514e97	},

	{	XFER_PIO_4,	0x06514e21,	0x06514e21	},
	{	XFER_PIO_3,	0x06514e22,	0x06514e22	},
	{	XFER_PIO_2,	0x06514e33,	0x06514e33	},
	{	XFER_PIO_1,	0x06914e43,	0x06914e43	},
	{	XFER_PIO_0,	0x06914e57,	0x06914e57	},
	{	0,		0x06514e57,	0x06514e57	}
};

struct chipset_bus_clock_list_entry fifty_base_hpt370[] = {
	{       XFER_UDMA_5,    0x12848242, 0x12848242      },
	{       XFER_UDMA_4,    0x12ac8242, 0x12ac8242      },
	{       XFER_UDMA_3,    0x128c8242, 0x128c8242      },
	{       XFER_UDMA_2,    0x120c8242, 0x120c8242      },
	{       XFER_UDMA_1,    0x12148254, 0x12148254      },
	{       XFER_UDMA_0,    0x121882ea, 0x121882ea      },

	{       XFER_MW_DMA_2,  0x22808242, 0x22808242      },
	{       XFER_MW_DMA_1,  0x22808254, 0x22808254      },
	{       XFER_MW_DMA_0,  0x228082ea, 0x228082ea      },

	{       XFER_PIO_4,     0x0a81f442, 0x0a81f442      },
	{       XFER_PIO_3,     0x0a81f443, 0x0a81f443      },
	{       XFER_PIO_2,     0x0a81f454, 0x0a81f454      },
	{       XFER_PIO_1,     0x0ac1f465, 0x0ac1f465      },
	{       XFER_PIO_0,     0x0ac1f48a, 0x0ac1f48a      },
	{       0,              0x0ac1f48a, 0x0ac1f48a      }
};

struct chipset_bus_clock_list_entry sixty_six_base_hpt370[] = {
	{       XFER_UDMA_5,    0x1488e673, 0x1488e673       },
	{       XFER_UDMA_4,    0x1488e673, 0x1488e673       },
	{       XFER_UDMA_3,    0x1498e673, 0x1498e673       },
	{       XFER_UDMA_2,    0x1490e673, 0x1490e673       },
	{       XFER_UDMA_1,    0x1498e677, 0x1498e677       },
	{       XFER_UDMA_0,    0x14a0e73f, 0x14a0e73f       },

	{       XFER_MW_DMA_2,  0x2480fa73, 0x2480fa73       },
	{       XFER_MW_DMA_1,  0x2480fa77, 0x2480fa77       }, 
	{       XFER_MW_DMA_0,  0x2480fb3f, 0x2480fb3f       },

	{       XFER_PIO_4,     0x0c82be73, 0x0c82be73       },
	{       XFER_PIO_3,     0x0c82be95, 0x0c82be95       },
	{       XFER_PIO_2,     0x0c82beb7, 0x0c82beb7       },
	{       XFER_PIO_1,     0x0d02bf37, 0x0d02bf37       },
	{       XFER_PIO_0,     0x0d02bf5f, 0x0d02bf5f       },
	{       0,              0x0d02bf5f, 0x0d02bf5f       }
};

struct chipset_bus_clock_list_entry thirty_three_base_hpt372[] = {
	{	XFER_UDMA_6,	0x1c81dc62, 0x1c81dc62	},
	{	XFER_UDMA_5,	0x1c6ddc62, 0x1c6ddc62 	},
	{	XFER_UDMA_4,	0x1c8ddc62, 0x1c8ddc62	},
	{	XFER_UDMA_3,	0x1c8edc62, 0x1c8edc62	},	/* checkme */
	{	XFER_UDMA_2,	0x1c91dc62, 0x1c91dc62	},
	{	XFER_UDMA_1,	0x1c9adc62, 0x1c9adc62	},	/* checkme */
	{	XFER_UDMA_0,	0x1c82dc62, 0x1c82dc62	},	/* checkme */

	{	XFER_MW_DMA_2,	0x2c829262, 0x2c829262	},
	{	XFER_MW_DMA_1,	0x2c829266, 0x2c829266	},	/* checkme */
	{	XFER_MW_DMA_0,	0x2c82922e, 0x2c82922e	},	/* checkme */

	{	XFER_PIO_4,	0x0c829c62, 0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84, 0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6, 0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26, 0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e, 0x0d029d5e	},
	{	0,		0x0d029d5e, 0x0d029d5e	}
};

struct chipset_bus_clock_list_entry fifty_base_hpt372[] = {
	{	XFER_UDMA_5,	0x12848242, 0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242, 0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242, 0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242, 0x120c8242	},
	{	XFER_UDMA_1,	0x12148254, 0x12148254	},
	{	XFER_UDMA_0,	0x121882ea, 0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242, 0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254, 0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea, 0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442, 0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443, 0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454, 0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465, 0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a, 0x0ac1f48a	},
	{	0,		0x0a81f443, 0x0a81f443	}
};

struct chipset_bus_clock_list_entry sixty_six_base_hpt372[] = {
	{	XFER_UDMA_6,	0x1c869c62, 0x1c869c62	},
	{	XFER_UDMA_5,	0x1cae9c62, 0x1cae9c62	},
	{	XFER_UDMA_4,	0x1c8a9c62, 0x1c8a9c62	},
	{	XFER_UDMA_3,	0x1c8e9c62, 0x1c8e9c62	},
	{	XFER_UDMA_2,	0x1c929c62, 0x1c929c62	},
	{	XFER_UDMA_1,	0x1c9a9c62, 0x1c9a9c62	},
	{	XFER_UDMA_0,	0x1c829c62, 0x1c829c62	},

	{	XFER_MW_DMA_2,	0x2c829c62, 0x2c829c62	},
	{	XFER_MW_DMA_1,	0x2c829c66, 0x2c829c66	},
	{	XFER_MW_DMA_0,	0x2c829d2e, 0x2c829d2e	},

	{	XFER_PIO_4,	0x0c829c62, 0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84, 0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6, 0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26, 0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e, 0x0d029d5e	},
	{	0,		0x0d029d26, 0x0d029d26	}
};

struct chipset_bus_clock_list_entry thirty_three_base_hpt374[] = {
	{	XFER_UDMA_6,	0x12808242, 0x12808242	},
	{	XFER_UDMA_5,	0x12848242, 0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242, 0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242, 0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242, 0x120c8242	},
	{	XFER_UDMA_1,	0x12148254, 0x12148254	},
	{	XFER_UDMA_0,	0x121882ea, 0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242, 0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254, 0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea, 0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442, 0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443, 0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454, 0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465, 0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a, 0x0ac1f48a	},
	{	0,		0x06814e93, 0x06814e93	}
};

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT370_ALLOW_ATA100_5		1
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
static int hpt366_get_info(char *, char **, off_t, int);
extern int (*hpt366_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;
static struct pci_dev *bmide2_dev;

static int hpt366_get_info (char *buffer, char **addr, off_t offset, int count)
{
#ifdef CONFIG_SMALL
	return 0;
#else	
	char *p		= buffer;
	u32 bibma	= bmide_dev->resource[4].start;
	u32 bibma2 	= bmide2_dev->resource[4].start;
	char *chipset_names[] = {"HPT366", "HPT366", "HPT368", "HPT370", "HPT370A"};
	u8  c0 = 0, c1 = 0;
	u32 class_rev;

	pci_read_config_dword(bmide_dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	if (class_rev>4)
	    class_rev = 4;

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	if (bmide2_dev)
		c1 = inb_p((unsigned short)bibma2 + 0x02);

	p += sprintf(p, "\n                                %s Chipset.\n", chipset_names[class_rev]);
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;/* => must be less than 4k! */
#endif	
}
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

byte hpt366_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);
byte hpt363_shared_irq;
byte hpt363_shared_pin;

static unsigned int pci_rev_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x02) ? 1 : 0);
}

static unsigned int pci_rev2_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x01) ? 1 : 0);
}
static unsigned int pci_rev3_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x02) ? 1 : 0);
}
static unsigned int pci_rev5_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x04) ? 1 : 0);
}
static unsigned int pci_rev7_check_hpt3xx (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	return ((int) (class_rev > 0x06) ? 1 : 0);
}

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 1;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static unsigned int pci_bus_clock_list (byte speed, int direction, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return (direction) ? chipset_table->chipset_settings_write : chipset_table->chipset_settings_read;
		}
	return (direction) ? chipset_table->chipset_settings_write : chipset_table->chipset_settings_read;
}

static void hpt366_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	byte regtime		= (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
			/*
			 * since the channel is always 0 it does not matter.
			 */

	unsigned int reg1	= 0;
	unsigned int reg2	= 0;
	byte drive_fast		= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(HWIF(drive)->pci_dev, regfast, &drive_fast);
	if (drive_fast & 0x02)
		pci_write_config_byte(HWIF(drive)->pci_dev, regfast, drive_fast & ~0x20);

	pci_read_config_dword(HWIF(drive)->pci_dev, regtime, &reg1);
	
	reg2 = pci_bus_clock_list(speed, direction, 
		(struct chipset_bus_clock_list_entry *) HWIF(drive)->pci_dev->sysdata);
	
	/*
	 * Disable on-chip PIO FIFO/buffer (to avoid problems handling I/O errors later)
	 */
	if (speed >= XFER_MW_DMA_0) {
		reg2 = (reg2 & ~0xc0000000) | (reg1 & 0xc0000000);
	} else {
		reg2 = (reg2 & ~0x30070000) | (reg1 & 0x30070000);
	}	
	reg2 &= ~0x80000000;

	pci_write_config_dword(HWIF(drive)->pci_dev, regtime, reg2);
}

static void hpt370_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	unsigned int list_conf	= 0;
	unsigned int drive_conf = 0;
	unsigned int conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	byte drive_pci		= 0x40 + (drive->dn * 4);
	byte new_fast, drive_fast		= 0;
	struct pci_dev *dev 	= HWIF(drive)->pci_dev;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say) 
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	new_fast = drive_fast;
	if (new_fast & 0x02)
		new_fast &= ~0x02;

#ifdef HPT_DELAY_INTERRUPT
	if (new_fast & 0x01)
		new_fast &= ~0x01;
#else
	if ((new_fast & 0x01) == 0)
		new_fast |= 0x01;
#endif
	if (new_fast != drive_fast)
		pci_write_config_byte(HWIF(drive)->pci_dev, regfast, new_fast);

	list_conf = pci_bus_clock_list(speed, direction, 
				       (struct chipset_bus_clock_list_entry *)
				       dev->sysdata);

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	
	if (speed < XFER_MW_DMA_0) {
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	}

	pci_write_config_dword(dev, drive_pci, list_conf);
}

static void hpt372_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	unsigned int list_conf	= 0;
	unsigned int drive_conf	= 0;
	unsigned int conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	byte drive_pci		= 0x40 + (drive->dn * 4);
	byte drive_fast		= 0;
	struct pci_dev *dev	= HWIF(drive)->pci_dev;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	drive_fast &= ~0x07;
	pci_write_config_byte(HWIF(drive)->pci_dev, regfast, drive_fast);
					
	list_conf = pci_bus_clock_list(speed, direction,
			(struct chipset_bus_clock_list_entry *)
					dev->sysdata);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	if (speed < XFER_MW_DMA_0)
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, drive_pci, list_conf);
}

static void hpt374_tune_chipset (ide_drive_t *drive, byte speed, int direction)
{
	hpt372_tune_chipset(drive, speed, direction);
}


static int hpt3xx_tune_chipset (ide_drive_t *drive, byte speed)
{
	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return -1;

	if (!drive->init_speed)
		drive->init_speed = speed;

	if (pci_rev7_check_hpt3xx(HWIF(drive)->pci_dev)) {
		hpt374_tune_chipset(drive, speed, 0);
	} else if (pci_rev5_check_hpt3xx(HWIF(drive)->pci_dev)) {
		hpt372_tune_chipset(drive, speed, 0);
	} else if (pci_rev3_check_hpt3xx(HWIF(drive)->pci_dev)) {
		hpt370_tune_chipset(drive, speed, 0);
	} else if (pci_rev2_check_hpt3xx(HWIF(drive)->pci_dev)) {
		hpt366_tune_chipset(drive, speed, 0);
	} else {
                hpt366_tune_chipset(drive, speed, 0);
        }
        
	drive->current_speed = speed;
	return ((int) ide_config_drive_speed(drive, speed));
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;
	byte	timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) hpt3xx_tune_chipset(drive, speed);
}

static void hpt3xx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) hpt3xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 * check_in_drive_lists(drive, bad_ata66_4)
 * check_in_drive_lists(drive, bad_ata66_3)
 * check_in_drive_lists(drive, bad_ata33)
 *
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	byte ultra66		= eighty_ninty_three(drive);
	int  rval;

	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return ((int) ide_dma_off_quietly);

	if ((id->dma_ultra & 0x0020) &&
	    (!check_in_drive_lists(drive, bad_ata100_5)) &&
	    (HPT370_ALLOW_ATA100_5) &&
	    (pci_rev_check_hpt3xx(HWIF(drive)->pci_dev)) &&
	    (ultra66)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) &&
		   (!check_in_drive_lists(drive, bad_ata66_4)) &&
		   (HPT366_ALLOW_ATA66_4) &&
		   (ultra66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) &&
		   (!check_in_drive_lists(drive, bad_ata66_3)) &&
		   (HPT366_ALLOW_ATA66_3) &&
		   (ultra66)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra && (!check_in_drive_lists(drive, bad_ata33))) {
		if (id->dma_ultra & 0x0004) {
			speed = XFER_UDMA_2;
		} else if (id->dma_ultra & 0x0002) {
			speed = XFER_UDMA_1;
		} else if (id->dma_ultra & 0x0001) {
			speed = XFER_UDMA_0;
		}
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	(void) hpt3xx_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 14) & 3) ? ide_dma_on :
			((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
	return rval;
}

int hpt3xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, quirk_drives));
}

void hpt3xx_intrproc (ide_drive_t *drive)
{
	if (drive->quirk_list) {
		/* drives in the quirk_list may not like intr setups/cleanups */
	} else {
		OUT_BYTE((drive)->ctl|2, HWIF(drive)->io_ports[IDE_CONTROL_OFFSET]);
	}
}

void hpt3xx_maskproc (ide_drive_t *drive, int mask)
{
	if (drive->quirk_list) {
		if (pci_rev_check_hpt3xx(HWIF(drive)->pci_dev)) {
			byte reg5a = 0;
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x5a, &reg5a);
			if (((reg5a & 0x10) >> 4) != mask)
				pci_write_config_byte(HWIF(drive)->pci_dev, 0x5a, mask ? (reg5a | 0x10) : (reg5a & ~0x10));
		} else {
			if (mask) {
				disable_irq(HWIF(drive)->irq);
			} else {
				enable_irq(HWIF(drive)->irq);
			}
		}
	} else {
		if (IDE_CONTROL_REG)
			OUT_BYTE(mask ? (drive->ctl | 2) : (drive->ctl & ~2), IDE_CONTROL_REG);
	}
}

void hpt370_rw_proc (ide_drive_t *drive, ide_dma_action_t func)
{
	if ((func != ide_dma_write) || (func != ide_dma_read))
		return;
	hpt370_tune_chipset(drive, drive->current_speed, (func == ide_dma_write));
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if (id->dma_mword & 0x0007) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:

		config_chipset_for_pio(drive);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * hpt366_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT366 UDMA bios chipset
 * by HighPoint|Triones Technologies, Inc.
 */
int hpt366_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	byte reg50h = 0, reg52h = 0, reg5ah = 0, dma_stat = 0;
	unsigned long dma_base = HWIF(drive)->dma_base;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = inb(dma_base+2);
			return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
		case ide_dma_lostirq:
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x50, &reg50h);
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x52, &reg52h);
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x5a, &reg5ah);
			printk("%s: (%s)  reg50h=0x%02x, reg52h=0x%02x, reg5ah=0x%02x\n",
				drive->name,
				ide_dmafunc_verbose(func),
				reg50h, reg52h, reg5ah);
			if (reg5ah & 0x10)
				pci_write_config_byte(HWIF(drive)->pci_dev, 0x5a, reg5ah & ~0x10);
			break;
		case ide_dma_timeout:
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

int hpt370_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long dma_base	= hwif->dma_base;
	byte regstate		= hwif->channel ? 0x54 : 0x50;
	byte reginfo		= hwif->channel ? 0x56 : 0x52;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = inb(dma_base+2);
			return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */

		case ide_dma_end:
			dma_stat = inb(dma_base + 2);
			if (dma_stat & 0x01) {
				udelay(20); /* wait a little */
				dma_stat = inb(dma_base + 2);
			}
			if ((dma_stat & 0x01) == 0) 
				break;

			func = ide_dma_timeout;
			/* fallthrough */

		case ide_dma_timeout:
		case ide_dma_lostirq:
			pci_read_config_byte(hwif->pci_dev, reginfo, 
					     &dma_stat); 
			printk("%s: %d bytes in FIFO\n", drive->name, 
			       dma_stat);
			pci_write_config_byte(hwif->pci_dev, regstate, 0x37);
			udelay(10);
			dma_stat = inb(dma_base);
			outb(dma_stat & ~0x1, dma_base); /* stop dma */
			dma_stat = inb(dma_base + 2); 
			outb(dma_stat | 0x6, dma_base+2); /* clear errors */
			/* fallthrough */

#ifdef HPT_RESET_STATE_ENGINE
	        case ide_dma_begin:
#endif
			pci_write_config_byte(hwif->pci_dev, regstate, 0x37);
			udelay(10);
			break;

		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

int hpt374_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long dma_base	= hwif->dma_base;
	byte mscreg		= hwif->channel ? 0x54 : 0x50;
//	byte reginfo		= hwif->channel ? 0x56 : 0x52;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = inb(dma_base+2);
#if 0  /* do not set unless you know what you are doing */
			if (dma_stat & 4) {
				byte stat = GET_STAT();
				outb(dma_base+2, dma_stat & 0xE4);
			}
#endif
			/* return 1 if INTR asserted */
			return (dma_stat & 4) == 4;
		case ide_dma_end:
		{
			byte bwsr_mask = hwif->channel ? 0x02 : 0x01;
			byte bwsr_stat, msc_stat;
			pci_read_config_byte(hwif->pci_dev, 0x6a, &bwsr_stat);
			pci_read_config_byte(hwif->pci_dev, mscreg, &msc_stat);
			if ((bwsr_stat & bwsr_mask) == bwsr_mask)
				pci_write_config_byte(hwif->pci_dev, mscreg, msc_stat|0x30);
		}
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}


#endif /* CONFIG_BLK_DEV_IDEDMA */



static void __init init_hpt37x(struct pci_dev *dev)
{
	int adjust, i;
	u16 freq;
	u32 pll;
	byte reg5bh;

	/*
	 * default to pci clock. make sure MA15/16 are set to output
	 * to prevent drives having problems with 40-pin cables.
	 */
	pci_write_config_byte(dev, 0x5b, 0x23);

	/*
	 * set up the PLL. we need to adjust it so that it's stable. 
	 * freq = Tpll * 192 / Tpci
	 */
	pci_read_config_word(dev, 0x78, &freq);
	freq &= 0x1FF;
	if (freq < 0x9c) {
		pll = F_LOW_PCI_33;
		if (pci_rev7_check_hpt3xx(dev)) {
			dev->sysdata = (void *) thirty_three_base_hpt374;
		} else if (pci_rev5_check_hpt3xx(dev)) {
			dev->sysdata = (void *) thirty_three_base_hpt372;
		} else if (dev->device == PCI_DEVICE_ID_TTI_HPT372) {
			dev->sysdata = (void *) thirty_three_base_hpt372;
		} else {
			dev->sysdata = (void *) thirty_three_base_hpt370;
		}
		printk("HPT37X: using 33MHz PCI clock\n");
	} else if (freq < 0xb0) {
		pll = F_LOW_PCI_40;
	} else if (freq < 0xc8) {
		pll = F_LOW_PCI_50;
		if (pci_rev7_check_hpt3xx(dev)) {
	//		dev->sysdata = (void *) fifty_base_hpt374;
			BUG();
		} else if (pci_rev5_check_hpt3xx(dev)) {
			dev->sysdata = (void *) fifty_base_hpt372;
		} else if (dev->device == PCI_DEVICE_ID_TTI_HPT372) {
			dev->sysdata = (void *) fifty_base_hpt372;
		} else {
			dev->sysdata = (void *) fifty_base_hpt370;
		}
		printk("HPT37X: using 50MHz PCI clock\n");
	} else {
		pll = F_LOW_PCI_66;
		if (pci_rev7_check_hpt3xx(dev)) {
	//		dev->sysdata = (void *) sixty_six_base_hpt374;
			BUG();
		} else if (pci_rev5_check_hpt3xx(dev)) {
			dev->sysdata = (void *) sixty_six_base_hpt372;
		} else if (dev->device == PCI_DEVICE_ID_TTI_HPT372) {
			dev->sysdata = (void *) sixty_six_base_hpt372;
		} else {
			dev->sysdata = (void *) sixty_six_base_hpt370;
		}
		printk("HPT37X: using 66MHz PCI clock\n");
	}
	
	/*
	 * only try the pll if we don't have a table for the clock
	 * speed that we're running at. NOTE: the internal PLL will
	 * result in slow reads when using a 33MHz PCI clock. we also
	 * don't like to use the PLL because it will cause glitches
	 * on PRST/SRST when the HPT state engine gets reset.
	 */
	if (dev->sysdata) 
		goto init_hpt37X_done;
	
	/*
	 * adjust PLL based upon PCI clock, enable it, and wait for
	 * stabilization.
	 */
	adjust = 0;
	freq = (pll < F_LOW_PCI_50) ? 2 : 4;
	while (adjust++ < 6) {
		pci_write_config_dword(dev, 0x5c, (freq + pll) << 16 |
				       pll | 0x100);

		/* wait for clock stabilization */
		for (i = 0; i < 0x50000; i++) {
			pci_read_config_byte(dev, 0x5b, &reg5bh);
			if (reg5bh & 0x80) {
				/* spin looking for the clock to destabilize */
				for (i = 0; i < 0x1000; ++i) {
					pci_read_config_byte(dev, 0x5b, 
							     &reg5bh);
					if ((reg5bh & 0x80) == 0)
						goto pll_recal;
				}
				pci_read_config_dword(dev, 0x5c, &pll);
				pci_write_config_dword(dev, 0x5c, 
						       pll & ~0x100);
				pci_write_config_byte(dev, 0x5b, 0x21);
				if (pci_rev7_check_hpt3xx(dev)) {
	//	dev->sysdata = (void *) fifty_base_hpt374;
					BUG();
				} else if (pci_rev5_check_hpt3xx(dev)) {
					dev->sysdata = (void *) fifty_base_hpt372;
				} else if (dev->device == PCI_DEVICE_ID_TTI_HPT372) {
					dev->sysdata = (void *) fifty_base_hpt372;
				} else {
					dev->sysdata = (void *) fifty_base_hpt370;
				}
				printk("HPT37X: using 50MHz internal PLL\n");
				goto init_hpt37X_done;
			}
		}
pll_recal:
		if (adjust & 1)
			pll -= (adjust >> 1);
		else
			pll += (adjust >> 1);
	} 

init_hpt37X_done:
	/* reset state engine */
	pci_write_config_byte(dev, 0x50, 0x37); 
	pci_write_config_byte(dev, 0x54, 0x37); 
	udelay(100);
}

static void __init init_hpt366 (struct pci_dev *dev)
{
	unsigned int reg1	= 0;
	byte drive_fast		= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, 0x51, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, 0x51, drive_fast & ~0x80);
	pci_read_config_dword(dev, 0x40, &reg1);
									
	/* detect bus speed by looking at control reg timing: */
	switch((reg1 >> 8) & 7) {
		case 5:
			dev->sysdata = (void *) forty_base_hpt366;
			break;
		case 9:
			dev->sysdata = (void *) twenty_five_base_hpt366;
			break;
		case 7:
		default:
			dev->sysdata = (void *) thirty_three_base_hpt366;
			break;
	}
}

unsigned int __init pci_init_hpt366 (struct pci_dev *dev, const char *name)
{
	byte test = 0;

	if (dev->resource[PCI_ROM_RESOURCE].start)
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &test);
	if (test != (L1_CACHE_BYTES / 4))
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &test);
	if (test != 0x78)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);

	pci_read_config_byte(dev, PCI_MIN_GNT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);

	pci_read_config_byte(dev, PCI_MAX_LAT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	if (pci_rev3_check_hpt3xx(dev)) {
		init_hpt37x(dev);
		hpt_devs[n_hpt_devs++] = dev;
	} else {
		init_hpt366(dev);
		hpt_devs[n_hpt_devs++] = dev;
	}
	
#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!hpt366_proc) {
		hpt366_proc = 1;
		hpt366_display_info = &hpt366_get_info;
	}
#endif /* DISPLAY_HPT366_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}


unsigned int __init ata66_hpt366 (ide_hwif_t *hwif)
{
	byte ata66	= 0;
	byte regmask	= (hwif->channel) ? 0x01 : 0x02;

	pci_read_config_byte(hwif->pci_dev, 0x5a, &ata66);
#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & 0x02) ? "33" : "66",
		PCI_FUNC(hwif->pci_dev->devfn));
#endif /* DEBUG */
	return ((ata66 & regmask) ? 0 : 1);
}

void __init ide_init_hpt366 (ide_hwif_t *hwif)
{
	hwif->tuneproc	= &hpt3xx_tune_drive;
	hwif->speedproc	= &hpt3xx_tune_chipset;
	hwif->quirkproc	= &hpt3xx_quirkproc;
	hwif->intrproc	= &hpt3xx_intrproc;
	hwif->maskproc	= &hpt3xx_maskproc;

	if (pci_rev2_check_hpt3xx(hwif->pci_dev)) {
		/* do nothing now but will split device types */
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {	
		if (pci_rev3_check_hpt3xx(hwif->pci_dev)) {
			byte reg5ah = 0;
			pci_read_config_byte(hwif->pci_dev, 0x5a, &reg5ah);
			if (reg5ah & 0x10)	/* interrupt force enable */
				pci_write_config_byte(hwif->pci_dev, 0x5a, reg5ah & ~0x10);
			/*
			 * set up ioctl for power status.
			 * note: power affects both
			 * drives on each channel
			 */
			 
			if (pci_rev7_check_hpt3xx(hwif->pci_dev)) {
				hwif->dmaproc	= &hpt374_dmaproc;
			} else if (pci_rev5_check_hpt3xx(hwif->pci_dev)) {
				hwif->dmaproc	= &hpt374_dmaproc;
			} else if (hwif->pci_dev->device == PCI_DEVICE_ID_TTI_HPT372) {
				hwif->dmaproc	= &hpt374_dmaproc;
			} else if (pci_rev3_check_hpt3xx(hwif->pci_dev)) {
				hwif->dmaproc	= &hpt370_dmaproc;
			}
		} else if (pci_rev2_check_hpt3xx(hwif->pci_dev)) {
			hwif->dmaproc	= &hpt366_dmaproc;
		} else {
			hwif->dmaproc = &hpt366_dmaproc;
		}
		if (!noautodma)
			hwif->autodma = 1;
		else
			hwif->autodma = 0;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#else /* !CONFIG_BLK_DEV_IDEDMA */
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_hpt366 (ide_hwif_t *hwif, unsigned long dmabase)
{
	byte masterdma = 0, slavedma = 0;
	byte dma_new = 0, dma_old = inb(dmabase+2);
	byte primary	= hwif->channel ? 0x4b : 0x43;
	byte secondary	= hwif->channel ? 0x4f : 0x47;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	dma_new = dma_old;
	pci_read_config_byte(hwif->pci_dev, primary, &masterdma);
	pci_read_config_byte(hwif->pci_dev, secondary, &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if (slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old) outb(dma_new, dmabase+2);

	__restore_flags(flags);	/* local CPU only */

	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_hpt374 (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_for_each_dev(findev) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			break;
		}
	}

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
	if (!dev2) {
		return;
	} else {
		byte irq = 0, irq2 = 0;
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
		pci_read_config_byte(dev2, PCI_INTERRUPT_LINE, &irq2);
		if (irq != irq2) {
			pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, irq);
			dev2->irq = dev->irq;
			printk("%s: pci-config space interrupt fixed.\n",
				d->name);
		}
	}
	d2 = d;
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d2->name, dev2->bus->number, dev2->devfn);
	ide_setup_pci_device(dev2, d2);

}

void __init fixup_device_hpt366 (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;
	unsigned char pin1 = 0, pin2 = 0;
	unsigned int class_rev;
	char *chipset_names[] = {"HPT366", "HPT366",  "HPT368",
				 "HPT370", "HPT370A", "HPT372"};

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	strcpy(d->name, chipset_names[class_rev]);

	switch(class_rev) {
		case 5:
		case 4:
		case 3:	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
				d->name, dev->bus->number, dev->devfn);
			ide_setup_pci_device(dev, d);
			return;
		default:	break;
	}

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin1);
	pci_for_each_dev(findev) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);
			hpt363_shared_pin = (pin1 != pin2) ? 1 : 0;
			hpt363_shared_irq = (dev->irq == dev2->irq) ? 1 : 0;
			if (hpt363_shared_pin && hpt363_shared_irq) {
				d->bootable = ON_BOARD;
				printk("%s: onboard version of chipset, "
					"pin1=%d pin2=%d\n", d->name,
					pin1, pin2);
#if 0
				/*
				 * This is the third undocumented detection
				 * method and is generally required for the
				 * ABIT-BP6 boards.
				 */
				pci_write_config_byte(dev2, PCI_INTERRUPT_PIN, dev->irq);
				printk("PCI: %s: Fixing interrupt %d pin %d "
					"to ZERO \n", d->name, dev2->irq, pin2);
				pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, 0);
#endif
			}
			break;
		}
	}
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d2->name, dev2->bus->number, dev2->devfn);
	ide_setup_pci_device(dev2, d2);
}

