/*
 *	Luxsonor LS220/LS240 series DVD/Mpeg card drivers
 *
 *	(c) Copyright 2002 Red Hat <alan@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	Made possible by Luxsonor's rather nice gesture of publishing their
 *	windows code to allow people to write new drivers based on it.
 *
 *	The firmware is (c) Copyright Luxsonor and a seperate program run
 *	on the dsp not part of Linux itself.
 *
 *	Note: the hardware css is not supported as Luxsonor decided not to
 *	document it even though the chip can do all the work. The citizens
 *	of free countries will need to use software decryption to play such
 *	films via the card. US citizens should simply report to the district
 *	attorney for termination.
 */
 
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/*
 *	Firmware modules
 */
 
#include "ls220/ac3.h"
#include "ls220/ac3_240.h"
#include "ls220/ac3i2s.h"
#include "ls220/ac3i2s_240.h"

#include "ls220/mpg.h"
#include "ls220/mpg_240.h"
#include "ls220/mpgi2s.h"
#include "ls220/mpgi2s_240.h"

#include "ls220/pcm.h"
#include "ls220/pcm_240.h"
#include "ls220/pcmi2s.h"
#include "ls220/pcmi2s_240.h"

/*
 *	Board types
 */
 
#define LS220C		0
#define LS220D		1
#define LS240		2

static char *lux_names[3] = { "LS220C", "LS220D", "LS240" };

/* 
 *	TV encoder types
 */
 
#define	BT865		0
#define BT864		1
#define SAA7120		2
#define SAA7121		3
#define AD7175		4
#define AD7176		5
#define CS4953		6
#define CS4952		7
#define	HS8171		8
#define	HS8170		9

static char *lux_tv_names[10] = {
	"BT865", "BT864", "SAA7120", "SAA7121", "AD7175", 
	"AD7176", "CS4953", "CS4952", "HS8171", "HS8170"
};

/*
 *	EEPROM bytes
 */
 
#define EPROM_REGION		0x4
#define EPROM_REGION_COUNT	0x5
#define EPROM_PARENTAL		0x6
#define EPROM_BOARDTYPE		0x7
#define EPROM_CLOCKTYPE		0x8
#define EPROM_I2S		0x9
#define EPROM_SPDIF_CH          0xa
#define EPROM_SPDIF_ONOFF       0xb
#define EPROM_TVOUT		0xc
#define EPROM_LEFT_CH_POL	0xf

/*
 *	Registers
 */
 
/* -- for field lock use -- */
#define CHANGE_MPGVID_CONTROL
#define CHANGE_REG274
#define DO_NOT_TOUCH_REG324


#define	LS220_DRAM_BASE		0x200000L
#define LS240_DRAM_BASE		0x400000L

#define DRAM_BASE(dev)		((dev)->dram_base)

#define	VID_FIFO_OFF		0x1a3000L
#define VID_FIFO_LEN		0x3e000L
#define	PTS_FIFO_OFF		0x1a2800L
#define PTS_FIFO_LEN		0x800L
#define SUB_FIFO_OFF		0x1e2000L
#define OSD_FIFO_OFF		0x1e1000L
#define OSD_FIFO_LEN		0x1000L
#define LS220_VID_FIFO(dev)	(dev->dram_base+VID_FIFO_OFF)

#define	DSPMEM_BASE(dev)	(dev->dram_base+0x1f0000L)
#define	DSPPROG_BASE(dev)	(DSPMEM_BASE(dev)-dev->dram_base)
#define	DSPPROG_OFF		0x1800L
#define	DSPPROG_SIZE		0x6000L


#define	AUD_FIFO_LEN		0x2000L
#define AUD_FIFO_OFF		0x1fc000L
#define AUD_PTS_LEN		0x400L
#define AUD_PTS_OFF		0x1ffa00L

/* Subpict definition */
#define  FIFO_SIZE	8			/* PTS FIFO entry count */
#define	 HIGH_FIFO	32			/* HighLight Fifo size */ 
#define  SCRM_FIFO	0x800			/* Scramble buffer size */
#define  SPBLOCK	( 0xE000 - HIGH_FIFO - SCRM_FIFO ) 	/* 56K - 32 bytes - 2k */
#define  SubBLOCK	0xE00			/* PTS + DCI = 3.5K */
#define	 DCIBLOCK	( SubBLOCK - 8*FIFO_SIZE )
#define  PxdBLOCK	( SPBLOCK - SubBLOCK )
#define  FStart		0x20			/* PTS FIFO Start Address */

#define	SUBBASE(dev) ( DRAM_BASE(dev) + SUB_FIFO_OFF )

#define	LS220_DSP_REG		0x100L
#define LS220_MPG_REG		0x180L
#define LS220_SYNC_REG		0x200L
#define LS220_PCM_REG		0x280L
#define LS220_VID_REG		0x300L

/*  SYNC REGS */
#define	SYNC_AUD_CONTROL	(LS220_SYNC_REG+0x00)
#define	SYNC_VID_CONTROL	(LS220_SYNC_REG+0x04)
#define SYNC_WAIT_LINE		(LS220_SYNC_REG+0x0c)
#define SYNC_FRAME_PERIOD	(LS220_SYNC_REG+0x10)
#define	SYNC_STC		(LS220_SYNC_REG+0x18)
#define	PTS_FIFO_START		(LS220_SYNC_REG+0x20)
#define	PTS_FIFO_END		(LS220_SYNC_REG+0x24)
#define	PTS_FIFO_WRITE		(LS220_SYNC_REG+0x28)
#define	PTS_FIFO_READ		(LS220_SYNC_REG+0x2c)
#define SYNC_VIDEO_PTS		(LS220_SYNC_REG+0x50)
#define	SYNC_INT_CTRL		(LS220_SYNC_REG+0x74)
#define SYNC_INT_FORCE		(LS220_SYNC_REG+0x78)

/* MPEG VIDEO REGS */
#define MPGVID_CONTROL		(LS220_MPG_REG+0x0)
#define MPGVID_SETUP		(LS220_MPG_REG+0x4)
#define MPGVID_FIFO_START	(LS220_MPG_REG+0x8)
#define MPGVID_FIFO_END		(LS220_MPG_REG+0xc)
#define MPGVID_FIFO_POS		(LS220_MPG_REG+0x10)
#define MPGVID_FIFO_FORCE	(LS220_MPG_REG+0x14)
#define MPGVID_FIFO_ADDBLOCK	(LS220_MPG_REG+0x18)
#define MPGVID_FIFO_BYTES	(LS220_MPG_REG+0x1c)
#define MPGVID_FIFO_INTLEVEL	(LS220_MPG_REG+0x20)
#define MPGVID_TOTAL_BYTES	(LS220_MPG_REG+0x24)
#define MPGVID_ERROR		(LS220_MPG_REG+0x28)
#define MPGVID_MB_WIDTH		(LS220_MPG_REG+0x2c)
#define MPGVID_MB_HEIGHT	(LS220_MPG_REG+0x30)
#define MPGVID_DEBUG1		(LS220_MPG_REG+0x38)
#define MPGVID_DEBUG2		(LS220_MPG_REG+0x3c)

/* VID_REG */
#define VIDP_GPIO		(LS220_VID_REG+0x50)

/* PCM REG */
#define PCM_FREQ_CONTROL	(LS220_PCM_REG+0x0)
#define PCM_OUTPUT_CONTROL	(LS220_PCM_REG+0x4)
#define PCM_FIFO_START		(LS220_PCM_REG+0x8)
#define PCM_FIFO_END		(LS220_PCM_REG+0xc)

/* DSP REGS */
#define DSP_CODE_ADDR		(LS220_DSP_REG+0x0)

/* DSP INTERAL MEMORY */

#define DSPMEM_DRV_RET(dev)		(DSPMEM_BASE(dev)+0xfef0L)
#define	DSPMEM_ACC(dev)			(DSPMEM_BASE(dev)+0xfef5L)
#define	DSPMEM_ACC4(dev)		(DSPMEM_BASE(dev)+0xfef4L)
#define	DSPMEM_CHAL_KEY(dev)		(DSPMEM_BASE(dev)+0xfef6L)

#define DSPMEM_LOCK(dev)		(DSPMEM_BASE(dev)+0xff78L)

#define	DSPMEM_AUDIO_CONF(dev)		(DSPMEM_BASE(dev)+0xff7cL)
#define	DSPMEM_AC3_CONF(dev)		(DSPMEM_BASE(dev)+0xff80L)

#define DSPMEM_KARAOKE(dev)		(DSPMEM_BASE(dev)+0xff8c)

#define	DSPMEM_INT_MASK(dev)		(DSPMEM_BASE(dev)+0xffa4L)
#define	DSPMEM_INT_STATUS(dev)		(DSPMEM_BASE(dev)+0xffa8L)
#define	DSPMEM_INT_THREHOLD(dev)	(DSPMEM_BASE(dev)+0xffacL)

#define DSPMEM_VOLUME_LEVEL(dev)	(DSPMEM_BASE(dev)+0xffb0L)

#define	DSPMEM_PTS_START(dev)		(DSPMEM_BASE(dev)+0xffd0L)
#define	DSPMEM_PTS_END(dev)		(DSPMEM_BASE(dev)+0xffd4L)
#define	DSPMEM_PTS_WR(dev)		(DSPMEM_BASE(dev)+0xffd8L)
#define	DSPMEM_PTS_RD(dev)		(DSPMEM_BASE(dev)+0xffdcL)

#define	DSPMEM_FIFO_START(dev)		(DSPMEM_BASE(dev)+0xffe0L)
#define	DSPMEM_FIFO_END(dev)		(DSPMEM_BASE(dev)+0xffe4L)
#define	DSPMEM_FIFO_WR(dev)		(DSPMEM_BASE(dev)+0xffe8L)
#define	DSPMEM_FIFO_RD(dev)		(DSPMEM_BASE(dev)+0xffecL)


#define	DSPMEM_CMD(dev)			(DSPMEM_BASE(dev)+0xfff0L)
#define	DSPMEM_STATUS(dev)		(DSPMEM_BASE(dev)+0xfff8L)


#define	DSP_CMD_NOP		0x00
#define	DSP_CMD_AC3		0x80
#define	DSP_CMD_MPEG1		0x81
#define	DSP_CMD_MPEG2		0x82
#define	DSP_CMD_PCM		0x83

#define	DSP_CMD_PLAY		0x84
#define	DSP_CMD_STOPF		0x85
#define	DSP_CMD_PAUSE		0x86
#define	DSP_CMD_MUTE		0x87
#define	DSP_CMD_UNMUTE		0x88
#define DSP_CMD_CONFIG		0x89
#define DSP_CMD_VER		0x8a
#define DSP_CMD_STATUS		0x8b

#define DSP_CMD_VOLUME		0x8c
#define DSP_CMD_INITDONE	0x8d

#define DSP_CMD_FRAME		0xa0
#define	DSP_CMD_CLRAUTH		0xa1
#define	DSP_CMD_DECAUTH		0xa2
#define	DSP_CMD_DRVAUTH		0xa3
#define	DSP_CMD_KEYSHARE	0xa4
#define	DSP_CMD_DISCKEY		0xa5
#define	DSP_CMD_TITLEKEY	0xa6


#define I2C_CLIENTS_MAX		16


struct ls220_dev
{
	struct ls220_dev *next;
	struct pci_dev *pdev;
	void *membase;
	int type;
	u8 eprom[16];
	int has_eprom;
	int tvencoder;
	
	u32 dram_base;

	u32 audio_fifo_off;
	u32 audio_fifo_len;

	u32 audio_m_vol;
	u32 audio_m_adj;
	int audio_mute;
	int audio_ac3;
	
	u32 audio_pts;
	u32 audio_ptscount;
	u32 audio_m_total;
	u32 audio_speed;

	int audio_spdif;

	int spdif_first_play;
	int stop_read;

	/* Buffer management */
	u8 audio_buffer[2048];
	u8 *audio_p;
	u8 *audio_cp;
	u16 audio_dlen;

	int video_mode;
#define VIDEO_PAL	0
#define VIDEO_NTSC	1
	int video_mpeg1;
	int video_hw_wrap;
	int video_letter;
	int video_zoomin;
	int video_speed;
	int video_pts;
	int video_total;
	int video_remainder;
	int video_wptr;
	int vga_mode;

	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_client i2c_client;
	int i2c_rc;
	struct i2c_client *i2c_clients[I2C_CLIENTS_MAX];
};

/* FIXME - spinlock the list */
static struct ls220_dev *ls_devs;

static int old = 1;		/* Old v new style board */

/*
 *	Hardware access
 */
 
static void ls220_dv_write(struct ls220_dev *dev, u32 offset, u32 data)
{
	writel(data, dev->membase+offset);
}

static u32 ls220_dv_read(struct ls220_dev *dev, u32 offset)
{
	return readl(dev->membase+offset);
}

static void ls220_write_dram(struct ls220_dev *dev, u32 offset, u32 data)
{
	writel(data, dev->membase+offset);
}

static u32 ls220_read_dram(struct ls220_dev *dev, u32 offset)
{
	return readl(dev->membase+offset);
}

static void ls220_memsetl(struct ls220_dev *dev, u32 offset, u32 fill, int len)
{
	int i;
	for(i=0;i<len;i++)
		ls220_write_dram(dev, offset+4*i, fill);
}

/*
 *	Firmware loading.
 *
 *	This is -fun-. The LS220 doesn't have enough memory to hold all
 *	the firmware to handle the various audio formats. Instead each
 *	format has its own firmware, along with a second set for pumping it
 *	out of the S/PDIF port.
 */
 
struct firmware
{
	u32	dstoff;		/* Destination Offset */
	u32 	*firmware;	/* Firmware */
	u32	length;		/* Firmware block size */
};

static struct firmware firmware[2][6][4]=
{
	{
		{
			{ 0x1f1800, AC3Ucode1f1800, sizeof(AC3Ucode1f1800)/4 },
			{ 0x1f8000, AC3Ucode1f8000, sizeof(AC3Ucode1f8000)/4 },
			{ 0x1fe000, AC3Ucode1fe000, sizeof(AC3Ucode1fe000)/4 },
			{ 0x1fff80, AC3Ucode1fff80, sizeof(AC3Ucode1fff80)/4 }
		},
		{
			{ 0x1f1800, MPGUcode1f1800, sizeof(MPGUcode1f1800)/4 },
			{ 0x1f5c00, MPGUcode1f5c00, sizeof(MPGUcode1f5c00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, PCMUcode1f1800, sizeof(PCMUcode1f1800)/4 },
			{ 0x1f4b00, PCMUcode1f4b00, sizeof(PCMUcode1f4b00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, AC3I2SUcode1f1800, sizeof(AC3I2SUcode1f1800)/4 },
			{ 0x1f8000, AC3I2SUcode1f8000, sizeof(AC3I2SUcode1f8000)/4 },
			{ 0x1fe000, AC3I2SUcode1fe000, sizeof(AC3I2SUcode1fe000)/4 },
			{ 0x1fff80, AC3I2SUcode1fff80, sizeof(AC3I2SUcode1fff80)/4 }
		},
		{
			{ 0x1f1800, MPGI2SUcode1f1800, sizeof(MPGI2SUcode1f1800)/4 },
			{ 0x1f5c00, MPGI2SUcode1f5c00, sizeof(MPGI2SUcode1f5c00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, PCMI2SUcode1f1800, sizeof(PCMI2SUcode1f1800)/4 },
			{ 0x1f4b00, PCMI2SUcode1f4b00, sizeof(PCMI2SUcode1f4b00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 },
		}
	},
	{
		{
			{ 0x1f1800, AC3240Ucode1f1800, sizeof(AC3240Ucode1f1800)/4 },
			{ 0x1f8000, AC3240Ucode1f8000, sizeof(AC3240Ucode1f8000)/4 },
			{ 0x1fe000, AC3240Ucode1fe000, sizeof(AC3240Ucode1fe000)/4 },
			{ 0x1fff80, AC3240Ucode1fff80, sizeof(AC3240Ucode1fff80)/4 }
		},
		{
			{ 0x1f1800, MPG240Ucode1f1800, sizeof(MPG240Ucode1f1800)/4 },
			{ 0x1f5c00, MPG240Ucode1f5c00, sizeof(MPG240Ucode1f5c00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, PCM240Ucode1f1800, sizeof(PCM240Ucode1f1800)/4 },
			{ 0x1f4b00, PCM240Ucode1f4b00, sizeof(PCM240Ucode1f4b00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, AC3I2S240Ucode1f1800, sizeof(AC3I2S240Ucode1f1800)/4 },
			{ 0x1f8000, AC3I2S240Ucode1f8000, sizeof(AC3I2S240Ucode1f8000)/4 },
			{ 0x1fe000, AC3I2S240Ucode1fe000, sizeof(AC3I2S240Ucode1fe000)/4 },
			{ 0x1fff80, AC3I2S240Ucode1fff80, sizeof(AC3I2S240Ucode1fff80)/4 }
		},
		{
			{ 0x1f1800, MPGI2S240Ucode1f1800, sizeof(MPGI2S240Ucode1f1800)/4 },
			{ 0x1f5c00, MPGI2S240Ucode1f5c00, sizeof(MPGI2S240Ucode1f5c00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 }
		},
		{
			{ 0x1f1800, PCMI2S240Ucode1f1800, sizeof(PCMI2S240Ucode1f1800)/4 },
			{ 0x1f4b00, PCMI2S240Ucode1f4b00, sizeof(PCMI2S240Ucode1f4b00)/4 },
			{ 0, NULL, 0 },
			{ 0, NULL, 0 },
		}
	}
};

static void load_firmware_block(struct ls220_dev *dev, struct firmware *f)
{
	int i;

//	printk("Loading firmware block at 0x%X, size %d.\n", f->dstoff, f->length);
	if(f->dstoff)
	{
		for(i=0; i<f->length; i++)
		{
			ls220_write_dram(dev, DRAM_BASE(dev) + f->dstoff + 4 * i, f->firmware[i]);
			if(ls220_read_dram(dev, DRAM_BASE(dev) + f->dstoff + 4 * i)!=f->firmware[i])
			{
				printk("luxsonor: firmware upload error.\n");
				printk("%d: Got 0x%X want 0x%X\n",
					i, ls220_read_dram(dev, DRAM_BASE(dev) + f->dstoff + 4 * i),
					f->firmware[i]);
				return;
			}
		}
	}
}

static void load_firmware_set(struct ls220_dev *dev, int card, int mode)
{
	int i;
	for(i=0;i<3;i++)
		load_firmware_block(dev, &firmware[card][mode][i]);
}

static void ls220_load_firmware(struct ls220_dev *dev, int format)
{
	int card = 0;
	if(dev->type==LS240)
		card = 1;
	
	if(dev->eprom[EPROM_I2S]==0x03)
		format += 3;	/* Second table for i2s */
	
	format--;		/* Numbered 1 to 3 */
	
	load_firmware_set(dev, card, format);
}

/*
 *	LS220 I2C bus implementation
 */

static void ls220_i2c_init(struct ls220_dev *dev)
{
	if(!dev->eprom[EPROM_BOARDTYPE])
		ls220_dv_write(dev, 0x350, 0x4f70100);
	else
		ls220_dv_write(dev, 0x350, 0x4f70f00);
	ls220_dv_write(dev, 0x364, 0xff031f);
}

static void ls220_bit_setscl(void *data, int state)
{
	struct ls220_dev *dev = data;
	u32 reg;

	reg = ls220_dv_read(dev, 0x350);
	reg &= 0x7FFFFF;
	if(state == 0)
		reg |= 0x00800000;
	ls220_dv_write(dev, 0x350, reg);
//	printk("SCL = %d\n", state);
	ls220_dv_read(dev, 0x350);
}

static void ls220_bit_setsda(void *data, int state)
{
	struct ls220_dev *dev = data;
	u32 reg;

	reg = ls220_dv_read(dev, 0x350);
	reg &= 0xBFFFFF;
	if(state == 0)
		reg |= 0x00400000;
	ls220_dv_write(dev, 0x350, reg);
//	printk("SDA = %d\n", state);
	ls220_dv_read(dev, 0x350);
}

static int ls220_bit_getsda(void *data)
{
	struct ls220_dev *dev = data;
	if(ls220_dv_read(dev, 0x350)&0x40)
	{
//		printk("get SDA=0\n");
		return 0;
	}
//	printk("get SDA=1\n");
	return 1;
}

static void ls220_i2c_use(struct i2c_adapter *adap)
{
	MOD_INC_USE_COUNT;
}

static void ls220_i2c_unuse(struct i2c_adapter *adap)
{
	MOD_DEC_USE_COUNT;
}

static int ls220_attach_inform(struct i2c_client *client)
{
	struct ls220_dev *dev  = client->adapter->data;
	return 0;
}

static int ls220_detach_inform(struct i2c_client *client)
{
	struct ls220_dev *dev  = client->adapter->data;
	return 0;
}

static int ls220_call_i2c_clients(struct ls220_dev *dev, unsigned int cmd, void *arg)
{
	return 0;
}

/*
 *	Structures to define our hardware with the i2c core code. It
 *	will do all the i2c bus management and locking for us.
 */
 
static struct i2c_algo_bit_data ls220_i2c_algo_template = {
	setsda:		ls220_bit_setsda,
	setscl: 	ls220_bit_setscl,
	getsda: 	ls220_bit_getsda,
	udelay: 	30,
	mdelay: 	10,
	timeout:	200,
};

static struct i2c_adapter ls220_i2c_adap_template = {
	name:			"ls220",
	id:			I2C_HW_B_BT848,	/* FIXME */
	inc_use:		ls220_i2c_use,
	dec_use:		ls220_i2c_unuse,
	client_register:	ls220_attach_inform,
	client_unregister:	ls220_detach_inform,
};

static struct i2c_client ls220_i2c_client_template =  {
	name: "ls220 internal",
	id: -1,
};

/*
 *	Register our i2c bus
 */
 
static int ls220_i2c_register(struct ls220_dev *dev)
{
	/* Copy template */
	memcpy(&dev->i2c_adap, &ls220_i2c_adap_template, sizeof(struct i2c_adapter));
	memcpy(&dev->i2c_algo, &ls220_i2c_algo_template, sizeof(struct i2c_algo_bit_data));
	memcpy(&dev->i2c_client, &ls220_i2c_client_template, sizeof(struct i2c_client));
	/* Device backlinks */
	dev->i2c_algo.data = dev;
	dev->i2c_adap.data = dev;
	/* Fix up links */
	dev->i2c_adap.algo_data = &dev->i2c_algo;
	dev->i2c_client.adapter = &dev->i2c_adap;
	/* Set up */
	ls220_bit_setscl(dev, 1);
	ls220_bit_setsda(dev, 1);
	dev->i2c_rc = i2c_bit_add_bus(&dev->i2c_adap);
	return dev->i2c_rc;
}

/*
 *	I2C read interfaces
 */
 
static int ls220_new_i2c_read(struct ls220_dev *dev, u8 addr)
{
	u8 buffer;

	if(dev->i2c_rc == -1)
		BUG();
	printk("i2c recv from 0x%02X\n", addr);

	dev->i2c_client.addr = addr>>1;
	if(i2c_master_recv(&dev->i2c_client, &buffer, 1))
	{
		printk("i2c - read error.\n");
		return -EIO;
	}
	printk("Received %d\n", buffer);
	return buffer;
}

static int ls220_new_i2c_probe(struct ls220_dev *dev, u8 addr)
{
	u8 buffer;
	int err;

	if(dev->i2c_rc == -1)
		BUG();
	printk("i2c probe for 0x%02X\n", addr);

	dev->i2c_client.addr = addr>>1;
	if((err=i2c_master_send(&dev->i2c_client, &buffer, 0)))
	{
		printk(" - probe failed (%d).\n", err);
		return 0;
	}
	printk(" - found.\n");
	return 1;
}

/*=================----------------*/

/*
 *	Old built in i2c code - this is here until I find why the generic
 *	kernel code won't play
 */
 
static void iic_delay(struct ls220_dev *dev)
{
	udelay(30);
}

static void iic_startcode(struct ls220_dev *dev)
{
	u32 tmp;

	tmp = ls220_dv_read(dev, 0x350) & 0x3fffffL;
	ls220_dv_write(dev, 0x350, tmp);	// both = 1
	iic_delay(dev);

	ls220_dv_write(dev, 0x350, tmp | 0x400000L);	// SDA = 0

	iic_delay(dev);

	ls220_dv_write(dev, 0x350, tmp | 0xc00000L);	// SCL = 0
	iic_delay(dev);
}

//////////////////////////////////////////////////////////////////////////

static void iic_dataxfer(struct ls220_dev *dev, u8 val)
{
	u32 tmp;
	u8 data;
	int i;

	data = ~val;
	for (i = 0; i < 8; i++) {
		tmp = ls220_dv_read(dev, 0x350) & 0xbfffffL;
		tmp |= (u32) ((data >> (7 - i)) & 0x1) << 22;	// set SDA
		ls220_dv_write(dev, 0x350, tmp);
		iic_delay(dev);
		tmp = ls220_dv_read(dev, 0x350);
		ls220_dv_write(dev, 0x350, tmp & 0x7fffffL);	// set SCL = 1
		iic_delay(dev);
		ls220_dv_write(dev, 0x350, tmp | 0x800000L);	// set SCL = 0
		iic_delay(dev);
	}
}

//////////////////////////////////////////////////////////////////////////

static int iic_ack(struct ls220_dev *dev)
{
	u32 tmp, ack;

	tmp = ls220_dv_read(dev, 0x350);
	ls220_dv_write(dev, 0x350, tmp & 0xbfffffL);	// disable SDA = 1
	iic_delay(dev);
	ack = ls220_dv_read(dev, 0x350) & 0x40;
	ls220_dv_write(dev, 0x350, tmp & 0x3fffffL);	// SCL = 1
	iic_delay(dev);
	tmp = ls220_dv_read(dev, 0x350);
	ls220_dv_write(dev, 0x350, tmp | 0x800000L);	// set SCL = 0
	iic_delay(dev);

	if (!ack)
		return 1;
	else
		return 0;
}

//////////////////////////////////////////////////////////////////////////

static void iic_endcode(struct ls220_dev *dev)
{
	u32 tmp;

	tmp = ls220_dv_read(dev, 0x350);
	ls220_dv_write(dev, 0x350, tmp | 0x400000L);	// SDA = 0
	iic_delay(dev);
	tmp = ls220_dv_read(dev, 0x350);
	ls220_dv_write(dev, 0x350, tmp & 0x7fffffL);	// set SCL = 1
	iic_delay(dev);
	ls220_dv_write(dev, 0x350, tmp & 0x3fffffL);	// SDA = 1
	iic_delay(dev);
}

//////////////////////////////////////////////////////////////////////////

static u8 iic_dataget(struct ls220_dev *dev)
{
	u8 val, i;
	u32 tmp;

	val = 0;
	for (i = 0; i < 8; i++) {
		iic_delay(dev);
		tmp = ls220_dv_read(dev, 0x350);
		val <<= 1;
		if (tmp & 0x40)
			val = val | 0x1;

		tmp = tmp & 0x3fffffL;
		ls220_dv_write(dev, 0x350, tmp);	// set SCL = 1
		iic_delay(dev);
		ls220_dv_write(dev, 0x350, tmp | 0x800000L);	// set SCL = 0
		iic_delay(dev);
	}

	return val;
}

//////////////////////////////////////////////////////////////////////////

static void send_ack(struct ls220_dev *dev)
{
	u32 tmp;

	tmp = ls220_dv_read(dev, 0x350);
	tmp = tmp | 0xc00000L;	//    SCLK SDA
	ls220_dv_write(dev, 0x350, tmp);	// set  00
	iic_delay(dev);
	tmp = tmp & 0x7fffffL;
	ls220_dv_write(dev, 0x350, tmp);	// set  10
	iic_delay(dev);
	tmp = tmp | 0xc00000L;
	ls220_dv_write(dev, 0x350, tmp);	// set  00
	iic_delay(dev);
	tmp = tmp & 0xb00000L;
	ls220_dv_write(dev, 0x350, tmp);	// set  01
	iic_delay(dev);
}

static int i2c_readeprom(struct ls220_dev *dev, u8 addr, u8 subaddr, u8 num, u8 * pb)
{
	u8 val;
	int i;

	iic_startcode(dev);
	iic_dataxfer(dev, addr);	// write command
	iic_ack(dev);
	iic_dataxfer(dev, subaddr);
	iic_ack(dev);
	iic_startcode(dev);
	iic_dataxfer(dev, (u8) (addr | 0x1));	// read command
	iic_ack(dev);

	for (i = 0; i < num; i++) {
		pb[i] = iic_dataget(dev);	// load array here
		if (i < (num - 1))
			send_ack(dev);
		else
			iic_endcode(dev);
	}
	return 1;
}

static int ls220_i2c_probe(struct ls220_dev *dev, u8 addr)
{
	int val;
//	return ls220_new_i2c_probe(dev,addr);
	iic_startcode(dev);
	iic_dataxfer(dev, addr);	// write command
	val = iic_ack(dev);
	iic_endcode(dev);
	return val;
}

//////////////////////////////////////////////////////////////////////////

static int read_i2c(struct ls220_dev *dev, u8 addr)
{
	u32 tmp, val;
	u32 dwcnt = 0;

	val = 0xff;
	tmp = ls220_dv_read(dev, 0x350);
	ls220_dv_write(dev, 0x350, tmp | 0x4000000);	// set bit 26=1 
	iic_delay(dev);
	ls220_dv_write(dev, 0x354, addr << 24 | 0x1000000L);
	iic_delay(dev);

	while (!(ls220_dv_read(dev, 0x368) & 0x1000)) {
		iic_delay(dev);
		if (dwcnt++ > 0xffff)
			break;
	}
	val = (u8) ls220_dv_read(dev, 0x36c);
	ls220_dv_write(dev, 0x350, tmp);
	iic_delay(dev);
	return val;
}

static void send_i2c(struct ls220_dev *dev, u8 addr,u8 subaddr,u8 data)
{
	iic_startcode(dev);
	iic_dataxfer(dev, addr);
	iic_ack(dev);
	iic_dataxfer(dev, subaddr);
	iic_ack(dev);
	iic_dataxfer(dev, data);
	iic_ack(dev);
	iic_endcode(dev);
}

/*=================----------------*/

static int ls220_i2c_read(struct ls220_dev *dev, u8 addr)
{
//	return ls220_new_i2c_read(dev, addr);
	return read_i2c(dev, addr);
}


static int ls220_i2c_write(struct ls220_dev *dev, u8 addr, u8 b1, u8 b2, int both)
{
	u8 buffer[2];
	int bytes = both ? 2 : 1;
	if(dev->i2c_rc == -1)
		BUG();
	
//	printk("Write to i2c client 0x%02X - 0x%2X (0x%02X, %d)\n", addr, b1, b2, both);
	dev->i2c_client.addr = addr >> 1;
	buffer[0] = b1;
	buffer[1] = b2;
	if(i2c_master_send(&dev->i2c_client, buffer, bytes)!=bytes)
	{
		printk(KERN_ERR "i2c write failed.\n");
		return -EIO;
	}
	return 0;
}

static int ls220_load_eeprom(struct ls220_dev *dev, u8 addr, u8 subaddr, u8 len, u8 *buf)
{
	int i;
	
	i2c_readeprom(dev, addr, subaddr, len, buf);
#if 0
	if(ls220_i2c_write(dev, addr, subaddr, -1, 0)<0)
		return -EIO;
	dev->i2c_client.addr = addr >> 1;

	if(i2c_master_recv(&dev->i2c_client, buf, 16)!=16)
		return -EIO;
#endif	
#if 0
	printk("luxsonor: EEPROM ");
	for(i=0;i<16;i++)
		printk("%02X ", buf[i]);
	printk("\n");
#endif
	return 0;
}

static int ls220_detect_tvencoder(struct ls220_dev *dev)
{
	int type;
	u8 id;

	if(ls220_i2c_write(dev, 0x00, 0x0f, 0x40, 1))
		printk("i2c_write failed.\n");
	if(ls220_i2c_probe(dev, 0x80))
	{
		u8 id;
		ls220_load_eeprom(dev, 0x80, 0x3d, 1, &id);
		if(id & 0xf0)
			type = CS4953;
		else
			type = CS4952;
		return type;
	}
	if(ls220_i2c_probe(dev, 0x8A))
	{
		id = ls220_i2c_read(dev, 0x8A);
		if(((id>>5)&7) == 5)
			type = BT865;
		else
			type = BT864;
		return type;
	}
	if(ls220_i2c_probe(dev, 0x42))
	{
		ls220_load_eeprom(dev, 0x42, 0x00, 1, &id);
		if(id & 0x0f)
			type = HS8171;
		else
			type = HS8170;
		return type;
	}
	if(ls220_i2c_probe(dev, 0x8c))
		return SAA7120;
	if(ls220_i2c_probe(dev, 0xd4))
		return AD7175;
	if(ls220_i2c_probe(dev, 0x54))
		return AD7176;
	printk("No TV encoder ??\n");
	return -ENODEV;
}
				
/*
 *	The LS220 also has some other i2c like stuff on the same
 *	registers, so we have to watch our locking. 
 *
 *	FIXME:pci posting..
 */

#define G_EN	0x60000
#define G_SC	0x0200 
#define G_SD	0x0400

static void ls220_gpio_startcode(struct ls220_dev *dev)
{
	u32 r350 = ls220_dv_read(dev, 0x350);
	r350&=0xFF000000;
	r350|=G_EN;
	ls220_dv_write(dev, 0x350, r350|G_SC|G_SD);
	udelay(100);
	ls220_dv_write(dev, 0x350, r350|G_SC);
	udelay(100);
	ls220_dv_write(dev, 0x350, r350);
	udelay(100);
}

static void ls220_gpio_endcode(struct ls220_dev *dev)
{
	u32 r350 = ls220_dv_read(dev, 0x350);
	r350&=0xFF000000;
	r350|=G_EN;
	ls220_dv_write(dev, 0x350, r350);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350|G_SC);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350|G_SC);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350|G_SC|G_SD);
}

static void ls220_gpio_addr(struct ls220_dev *dev, u8 addr)
{
	int i;
	int sd;
	u32 r350 = ls220_dv_read(dev, 0x350);
	r350&=0xFF000000;
	r350|=G_EN;

	for(i=2;i>=0;i--)
	{
		if(addr&(1<<i))
			sd=G_SD;
		else 
			sd=0;
		ls220_dv_write(dev, 0x350, r350|sd);
		udelay(10);
		ls220_dv_write(dev, 0x350, r350|G_SC|sd);
		udelay(10);
		ls220_dv_write(dev, 0x350, r350|sd);
		udelay(10);
	}
	ls220_dv_write(dev, 0x350, r350);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350|G_SC);
	udelay(10);
	ls220_dv_write(dev, 0x350, r350);
	udelay(10);
} 	

static void ls220_gpio_data(struct ls220_dev *dev, u8 data)
{
	int i;
	int sd;
	u32 r350 = ls220_dv_read(dev, 0x350);
	r350&=0xFF000000;
	r350|=G_EN;

	for(i=3;i>=0;i--)
	{
		if(data&(1<<i))
			sd=G_SD;
		else 
			sd=0;
		ls220_dv_write(dev, 0x350, r350|sd);
		udelay(10);
		ls220_dv_write(dev, 0x350, r350|G_SC|sd);
		udelay(10);
		ls220_dv_write(dev, 0x350, r350|sd);
		udelay(10);
	}
}

static void ls220_send_gpio(struct ls220_dev *dev, u8 addr, u8 data)
{
	ls220_gpio_startcode(dev);
	ls220_gpio_addr(dev, addr);
	ls220_gpio_data(dev, addr);
	ls220_gpio_endcode(dev);
}

/*
 *	LS220 chip reset
 */
 
static void ls220_reset(struct ls220_dev *dev)
{
	if(dev->type == LS240)
	{
		ls220_dv_write(dev, 0x10, 1);
		ls220_dv_write(dev, 0x10, 0x30);
		ls220_dv_write(dev, 0x300, 0);
		ls220_dv_write(dev, 0x84, 0x04400000);
		udelay(10);
		ls220_dv_write(dev, 0x80, 0x66a428ec);
		udelay(10);
		ls220_dv_write(dev, 0x84, 0x80400000);
		ls220_dv_write(dev, 0x30, 0x12c5);
	}
	else
	{
		ls220_dv_write(dev, 0x10, 1);
		ls220_dv_write(dev, 0x10, 0);
	}
	
	ls220_i2c_init(dev);
}

/*
 *	LS220 Audio drivers
 */

static void ls240_dsp_poke(struct ls220_dev *dev)
{
	if(dev->type == LS240)
	{
		u32 reg = ls220_dv_read(dev, 0x10);
		reg &= ~8;
		reg |= 0x30;
		ls220_dv_write(dev, 0x10,reg);
	}
}

static int ls220_dsp_wait(struct ls220_dev *dev)
{
	int count;

	ls240_dsp_poke(dev);
	
	ls220_dv_read(dev, 0x250);

	for(count = 0; count < 4096; count ++)
	{
		if(ls220_read_dram(dev, DSPMEM_CMD(dev)) == 0x100)
			return 0;
		ls240_dsp_poke(dev);
		udelay(50);
	}
	printk(KERN_ERR "ls220: dsp not responding.\n");
	return -ETIMEDOUT;
}

static u32 vol_table[16] = {
	0x7fffff, 0x6046c5, 0x4c79a0, 0x3cbf0f,
	0x3040a5, 0x26540e, 0x1e71fe, 0x182efd,
	0x1335ad, 0xf4240, 0xc1ed8, 0x9a0ad, 
	0x7a5c3, 0x6131b, 0x4d343, 0x0
};

static void ls220_audio_set_volume(struct ls220_dev *dev, int vol)
{
	if(dev->audio_mute)
		return;
	if(vol == 0xFF)
	{
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_MUTE);
		ls220_dsp_wait(dev);
		vol = 0;
	}
	else
		dev->audio_m_vol = vol;
	if(vol > 15 || vol < 0)
		BUG();
	ls220_write_dram(dev, DSPMEM_VOLUME_LEVEL(dev), vol_table[15-vol]);
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_VOLUME);
}
 
static void ls220_dsp_init(struct ls220_dev *dev, int type)
{
	ls220_write_dram(dev, DSPMEM_AC3_CONF(dev) ,0x3400);

	if(type == 1)	/* AC3 */
	{
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_AC3);
		dev->audio_m_adj = 0x600;
	}
	else if(type == 0) /* PCM */
	{
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_PCM);
		dev->audio_m_adj = 0x600;
	}
	else if(type == 2) /* MPG1 */
	{
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_MPEG1);
		dev->audio_m_adj = 0x0;
	}
	ls220_dsp_wait(dev);
	ls220_audio_set_volume(dev, dev->audio_m_vol);
	
	dev->audio_fifo_off = ls220_read_dram(dev, DSPMEM_FIFO_START(dev));
	dev->audio_fifo_len = ls220_read_dram(dev, DSPMEM_FIFO_END(dev)) - dev->audio_fifo_off;
//	printk("FIFO at 0x%X, size %d.\n", dev->audio_fifo_off, dev->audio_fifo_len);
}
 
static void ls220_audio_init(struct ls220_dev *dev, int type)
{
	dev->audio_mute = 0;
	dev->audio_m_vol = 15;
	dev->audio_ac3 = 1;
	dev->audio_m_total = 0;
	dev->audio_pts = 0;
	dev->audio_ptscount = 0;
	dev->audio_speed = 1000;
	dev->stop_read = 0;
	ls220_dv_write(dev, 0x24, 0);

	if(old)
	{
		ls220_dv_write(dev, 0x104, 0);
		ls220_dv_write(dev, 0x100, 0x7C600);
		ls220_dv_write(dev, 0x104, 1);
		udelay(200);
		ls220_write_dram(dev, DSPMEM_FIFO_WR(dev), AUD_FIFO_OFF);
		ls220_write_dram(dev, DSPMEM_FIFO_RD(dev), AUD_FIFO_OFF);
		ls220_write_dram(dev, DSPMEM_PTS_WR(dev), AUD_PTS_OFF);
		ls220_write_dram(dev, DSPMEM_PTS_RD(dev), AUD_PTS_OFF);
	}
	ls220_dsp_init(dev, type);
	dev->audio_m_total = 0;
	dev->audio_pts = 0;
	dev->audio_ptscount = 0;

	if(type == 2)	/* Mpeg audio */
		dev->audio_ptscount = 1;
}

static void ls240_audiopcm_enable_tristate(struct ls220_dev *dev)
{
	ls220_dv_write(dev, 0x284, ls220_dv_read(dev, 0x284)|0x200000);
}

static void ls220_audio_set_info(struct ls220_dev *dev)
{
	u16 clock_chip;		/* clock chip defition */
	u16 left_ch;		/* left channel polarity */
	u16 pcm_size;		/* PCM ( output ) size */
	u16 i2s_pin;		/* I2S pin */

	clock_chip	= (u16)dev->eprom[EPROM_CLOCKTYPE];
	left_ch		= (u16)dev->eprom[EPROM_LEFT_CH_POL]<<3;
	pcm_size	= 0;

	if(dev->eprom[EPROM_I2S] == 0x3)
		i2s_pin = 0x40;
	else
		i2s_pin = 0;

	ls220_write_dram(dev, DSPMEM_AUDIO_CONF(dev), i2s_pin|pcm_size|left_ch|clock_chip);
}

static void ls220_audio_set_spdif(struct ls220_dev *dev, int onoff)
{
	u32 data;

	dev->audio_spdif = onoff;

	data = ls220_read_dram(dev, DSPMEM_AUDIO_CONF(dev));
	data &= 0x7F;
	if(onoff)
		data |= 0x80;
	ls220_write_dram(dev, DSPMEM_AUDIO_CONF(dev), data);
}

static int ls220_audio_write(struct ls220_dev *dev, const char *buf, int len)
{
	return 0;
}

static void ls220_play_audio(struct ls220_dev *dev, int speed)
{
	dev->audio_speed = speed;
	if(speed == 1000)
	{
		/*
	 	 *	Normal speed - engage hardware synchronization
		 */
		if(dev->type == LS240)
		{
			ls220_dv_write(dev, 0x280, 0x8);
			ls220_dv_write(dev, 0x280, 0x9);
			ls220_dv_write(dev, 0x29C, 0x30008235);
			ls220_dv_write(dev, 0x200, 0x419);
			ls220_dv_write(dev, 0x200, 0x3b);
		}
		else
		{
			u32 tmp = ls220_dv_read(dev, SYNC_AUD_CONTROL);
			ls220_dv_write(dev, SYNC_AUD_CONTROL, tmp|0x20);
		}
		ls220_dv_write(dev, DSPMEM_CMD(dev), DSP_CMD_PLAY);
		ls220_dsp_wait(dev);
		/* Locking needed on 0x350 */
		if(dev->eprom[EPROM_BOARDTYPE] == 3 && !dev->audio_spdif)
			ls220_dv_write(dev, 0x350, ls220_dv_read(dev, 0x350)|0x010100);
		if(!(dev->eprom[EPROM_SPDIF_CH]&0x02) && dev->audio_spdif && dev->spdif_first_play)
		{
			dev->spdif_first_play = 0;
			ls220_dv_write(dev, DSPMEM_CMD(dev), DSP_CMD_INITDONE);
			ls220_dsp_wait(dev);
		}
	}
}
	
static void ls220_audio_set_speed(struct ls220_dev *dev, int speed)
{
	dev->audio_speed = speed;
}

static void ls220_audio_set_config(struct ls220_dev *dev, int sixchannel)
{
	if(dev->audio_ac3)
	{
		if(sixchannel && !(dev->eprom[EPROM_SPDIF_CH]&0x1))
			ls220_write_dram(dev, DSPMEM_AC3_CONF(dev), 0x340F);
		else
		{
			switch(sixchannel)
			{
				case 0:		/* Want prologic */
				case 1:		/* Board doesnt do 6 */
				case 3: 	/* Default to prologic */
					ls220_write_dram(dev, DSPMEM_AC3_CONF(dev), 0x3400);
					break;
				case 2:		/* No six channels but use 2 channel scheme */
					ls220_write_dram(dev, DSPMEM_AC3_CONF(dev), 0x3402);
					break;
			}
		}
	}
	else
	{
		/* PCM mode */
		if(sixchannel > 1)
			ls220_write_dram(dev, DSPMEM_AC3_CONF(dev), sixchannel);
	}
}

static void ls220_audio_set_type(struct ls220_dev *dev, int type)
{
	dev->audio_ac3 = type;
}


static void ls220_audio_stop(struct ls220_dev *dev)
{
	dev->stop_read = 0;
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_MUTE);
	ls220_dsp_wait(dev);
	ls220_write_dram(dev, DSPMEM_PTS_WR(dev), AUD_PTS_OFF);
	ls220_write_dram(dev, DSPMEM_PTS_RD(dev), AUD_PTS_OFF);

	if(dev->audio_ac3 == 1)
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_AC3);
	else if(dev->audio_ac3 == 0)
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_PCM);
	else if(dev->audio_ac3 == 2)
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_MPEG1);
	ls220_dsp_wait(dev);
	ls220_audio_set_volume(dev, dev->audio_m_vol);

	dev->audio_m_total = 0;
	dev->audio_pts = 0;
	dev->audio_ptscount = 0;
	if(dev->audio_ac3 == 2)
		dev->audio_ptscount = 1;	
}

static void ls220_audio_mute(struct ls220_dev *dev)
{
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_STOPF);
	ls220_dsp_wait(dev);
}

static void ls220_audio_pause(struct ls220_dev *dev)
{
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_PAUSE);
	ls220_dsp_wait(dev);
}

static void ls220_audio_continue(struct ls220_dev *dev)
{
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_PLAY);
	ls220_dsp_wait(dev);
}

static u32 ls220_audio_report_frame(struct ls220_dev *dev)
{
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_FRAME);
	ls220_dsp_wait(dev);
	return ls220_read_dram(dev, DSPMEM_STATUS(dev));
}

static u32 ls220_audio_report_status(struct ls220_dev *dev)
{
	ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_STATUS);
	ls220_dsp_wait(dev);
	return ls220_read_dram(dev, DSPMEM_STATUS(dev));
}
	
static void ls220_audio_onoff(struct ls220_dev *dev, int onoff)
{
	dev->audio_mute = onoff;
	if(!dev->audio_mute)
		ls220_audio_set_volume(dev, dev->audio_m_vol);
	else
	{
		ls220_write_dram(dev, DSPMEM_VOLUME_LEVEL(dev), vol_table[15]);
		ls220_write_dram(dev, DSPMEM_CMD(dev), DSP_CMD_VOLUME);
		ls220_dsp_wait(dev);
	}
}

static void ls220_audio_after_seek(struct ls220_dev *dev)
{
	if(old)
	{
		ls220_write_dram(dev, DSPMEM_FIFO_WR(dev), AUD_FIFO_OFF);
		ls220_write_dram(dev, DSPMEM_FIFO_RD(dev), AUD_FIFO_OFF);
	}
	ls220_write_dram(dev, DSPMEM_FIFO_WR(dev), dev->audio_fifo_off);
	ls220_write_dram(dev, DSPMEM_FIFO_RD(dev), dev->audio_fifo_off);
	ls220_write_dram(dev, DSPMEM_PTS_WR(dev), AUD_PTS_OFF);
	ls220_write_dram(dev, DSPMEM_PTS_RD(dev), AUD_PTS_OFF);
}

int ls220_audio_write_pts(struct ls220_dev *dev, u32 pts, u16 aoff)
{
	u32 rdptr;
	u32 wrptr;
	u32 space;
	u32 val;

	rdptr = ls220_dv_read(dev, DSPMEM_PTS_RD(dev));
	wrptr = ls220_dv_read(dev, DSPMEM_PTS_WR(dev));
	space = (wrptr  > rdptr) ? (AUD_PTS_LEN + rdptr - wrptr) : (rdptr - wrptr);

	if(space < 0x8)
		return 0;
	
	if(dev->type != LS240)
	{
		if(!dev->audio_pts)
		{
			val = ls220_dv_read(dev, 0x200);
			ls220_dv_write(dev, 0x200, val&0xFBF);	/* Audio master */
		}
	}
	ls220_write_dram(dev, DRAM_BASE(dev)+wrptr, pts);
	wrptr+=4;
	ls220_write_dram(dev, DRAM_BASE(dev)+wrptr, dev->audio_m_total+aoff);
	wrptr+=4;
	if(wrptr >= AUD_PTS_OFF+AUD_PTS_LEN)
		wrptr -= AUD_PTS_LEN;
	ls220_dv_write(dev, DSPMEM_PTS_WR(dev), wrptr);
	dev->audio_pts = pts;
	return 1;
}

static void ls220_audio_stop_dsp(struct ls220_dev *dev)
{
	ls220_dv_write(dev, 0x104, 0);
	ls220_dv_write(dev, SYNC_INT_CTRL, 0x8000);
}

static void ls220_audio_enable_dsp(struct ls220_dev *dev)
{
	ls220_dv_write(dev, 0x100, 0x7c600);
	ls220_dv_write(dev, 0x104, 0x1);
	udelay(100);
	if(dev->type == LS240)
		ls220_dv_write(dev, SYNC_INT_CTRL, 0x38000001);
	else
		ls220_dv_write(dev, SYNC_INT_CTRL, 0xE8001);
}

static void ls220_audio_setuclock(struct ls220_dev *dev, int type)
{
	if(!old)
		return;
#if 0	
	if(type && dev->eprom[EPROM_CLOCKTYPE]==1)
		ls220_write_dram(dev, DSPMEM_UCLOCK(dev), 1);
	else
		ls220_write_dram(dev, DSPMEM_UCLOCK(dev), 0);	
#endif	
}

static void ls220_audio_set_karaoke(struct ls220_dev *dev, int mode)
{
	if(mode < 0 || mode > 3)
		BUG();
	/* bit 0 = vocal 1 enable bit 1 = vocal 2 enable */
	ls220_write_dram(dev, DSPMEM_KARAOKE(dev), mode);
}

static u32 ls220_audio_send_data(struct ls220_dev *dev, u8 *p, u32 len, u16 pts, int scramble, u16 aoff)
{
	if(dev->audio_speed != 1000)
		return len;
	/* TODO */
	return 0;
}
				
static u32 ls220_audio_mpeg_packet(struct ls220_dev *dev, u8 *mbuf, u8 *pp, u32 used)
{
	/* mbuf is a 2K packet, pp is a pes len ptr */
	u8 aid;
	u16 pes_len;
	u32 curpts = 0;
	u8 scramble;
	int m_ext = 0;
	u16 ext_pes_len = 0;
	int m_exthead = 0;

	memcpy(dev->audio_buffer, mbuf, 2048);
	dev->audio_p = dev->audio_buffer + (pp - mbuf);
	
	aid = dev->audio_p[-1]&7;

	if(dev->stop_read)
	{
		dev->audio_p+=2;
		scramble = dev->audio_p[0]&0x30;
		pes_len = dev->audio_p[2];
		dev->audio_p+=3;
		if((dev->audio_p[0] & 0xf0)==0x20 && pes_len > 0)	/* Found PTS */
		{
			curpts = dev->audio_p[1]<<8 | dev->audio_p[2];
			curpts = (curpts << 14) | ((dev->audio_p[3]<<8 | dev->audio_p[4])>>2);
			curpts |= (dev->audio_p[0] & 0xE) << 28;
		}
		dev->audio_p += pes_len;
		goto tryit;
	}
	if((dev->audio_p[-1]&0xF0) == 0xD0)
	{
		m_exthead = 1;
		m_ext = 1;
	}
	dev->audio_dlen = dev->audio_p[0]<<8 | dev->audio_p[1];
	dev->audio_p+=2;
	dev->audio_cp = dev->audio_p+dev->audio_dlen;	/* Find A_PKT end */
	if (dev->audio_cp < dev->audio_buffer + 0x0800)
	{
		if((((dev->audio_cp[0]<<8) | dev->audio_cp[1])&0xFFFFFFF0) != 0x01c0)
			m_ext = 1;
	}
	scramble = dev->audio_p[0]&0x30;
	pes_len = dev->audio_p[2];
	dev->audio_p+=3;
	
	dev->audio_dlen = dev->audio_dlen -3 - pes_len;

	if(((*dev->audio_p & 0xf0) == 0x20) && (pes_len > 0))
	{
		curpts = dev->audio_p[1]<<8|dev->audio_p[2];
		curpts <<= 14;
		curpts |= (dev->audio_p[3]<< 8 | dev->audio_p[4])>>2;
		curpts |= (dev->audio_p[0]&0x0E)<<28;
	}

	dev->audio_p+=pes_len;

	if(m_exthead)
	{
		dev->audio_cp = dev->audio_p;
		dev->audio_dlen = 0;
		curpts = 0;
	}

	if(m_ext)
	{
		dev->audio_p = dev->audio_cp;
		while(dev->audio_p < dev->audio_buffer + 0x800)
		{
			while(((dev->audio_p[0]<<8 | dev->audio_p[1]) & 0xFFFF00) != 0x100)
			{
				if(dev->audio_p++ >= dev->audio_buffer + 0x800)
					goto tryit;
			}
	
			if((dev->audio_p[3]&0xF0) != 0xC0)
			{
				dev->audio_p += 4;
				ext_pes_len = dev->audio_p[0]<<8 | dev->audio_p[1];
				dev->audio_p += ext_pes_len;
				continue;
			}
			if((((dev->audio_p[0]<<24)|(dev->audio_p[1]<<1)|(dev->audio_p[2]<<8)|dev->audio_p[3]) & 0xFFFFFFF0) != 0x1c0)
			{
				dev->audio_p+=4;
				ext_pes_len = dev->audio_p[0] << 8 | dev->audio_p[1];
				dev->audio_p+=2;
				pes_len = dev->audio_p[2];
				dev->audio_p+=3;
				ext_pes_len = ext_pes_len - 3 - pes_len;
		
				if(m_exthead)
				{
					if((dev->audio_p[0]&0xF0) == 0x20 && pes_len > 0)
					{
						curpts = dev->audio_p[1]<<8|dev->audio_p[2];
						curpts <<= 14;
						curpts |= dev->audio_p[3]<<8|dev->audio_p[4];
						curpts |= (dev->audio_p[0]&0xE)<<28;
					}
					m_exthead = 0;
				}
		
				dev->audio_p += pes_len;
		
				memmove(dev->audio_cp, dev->audio_p, ext_pes_len);
				dev->audio_dlen += ext_pes_len;
				dev->audio_cp += ext_pes_len;
				dev->audio_p += ext_pes_len;
			}
		}
	}
tryit:
	if(dev->audio_dlen == 0)
	{
		dev->stop_read = 0;
		return used;
	}
	if(ls220_audio_send_data(dev, dev->audio_p, dev->audio_dlen, curpts, scramble, 0))
	{
		dev->stop_read = 0;
		return used;
	}
	else
	{
		dev->stop_read = 1;
		return 0;
	}
}

/*
 *	Video side drivers
 */
 
static void ls240_zoomvideo_enable_tristate(struct ls220_dev *dev)
{
	ls220_dv_write(dev, 0x37c, ls220_dv_read(dev, 0x37c)&0xe0ffffff);
}        

static void ls220_video_set_vpm(struct ls220_dev *dev, int type)
{
	/* TODO */
}

static void ls220_configure_tvout(struct ls220_dev *dev, int tvmode)
{
	static u8 bt865_pal[]={
		0x00, 0x60, 0x7e, 0xfe, 0x54, 0x01, 0xff, 0x01,
		0xd5, 0x73, 0xa8, 0x22, 0x55, 0xa4, 0x05, 0x55,
		0x27, 0x40
	};
	int i;

	switch(dev->tvencoder)
	{
		case BT865:
			for(i=0;i<18;i++)
				send_i2c(dev, 0x8A, 0xD8+2*i, bt865_pal[i]);
			break;
		default:
			printk("Sorry only PAL BT865 is supported right now.\n");
	}
}

/*
 *	Video gamma table from Luxsonor
 */

static u32 gamma[][8] = {
	{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0},
	{0x0,0x0,0x1000000,0x05040302,0x07070606,0x0b0a0908,0x0b0c0b0b,0xe0e0d},
	{0x0,0x03020200,0x07060505,0x0a090808,0x0b0b0b0a,0x0d0d0c0c,0x0e0e0e0d,0x010f0f0f},
	{0x03000000,0x09080706,0x0c0b0b0b,0x0e0e0d0d,0x0f0f0f0f,0x10100f0f,0x1010100f,0x010f1010},
	{0x08050100,0x0e0d0d0b,0x11101010,0x12121112,0x12121212,0x12121212,0x11111111,0x020f1011},
	{0x0d0a0500,0x13121210,0x15151515,0x16161616,0x15151616,0x14141415,0x12131313,0x020f1112},
	{0x120e0900,0x18171615,0x19191919,0x1919191a,0x17181919,0x16161617,0x13141415,0x020f1212},
	{0x16130d00,0x1c1c1b19,0x1d1d1d1e,0x1d1d1d1e,0x1a1b1c1c,0x1818191a,0x14151616,0x020f1213},
	{0x1b171000,0x21201f1e,0x21212122,0x20202021,0x1c1d1e1f,0x191a1a1c,0x15161718,0x020f1314},
	{0x1f1b1400,0x24242322,0x24252525,0x22232324,0x1e1f2121,0x1b1c1c1e,0x16171819,0x020f1315}
};

/*
 *	U chroma values from Luxsonor
 */
 
static u32 chroma_U[][7] = {
	{0x2,0x0,0x0,0x0,0x0,0x0,0x0},
	{0x3f3f3f02,0x3f3f3f3f,0x0,0x0,0x0,0x01010100,0x02020201},
	{0x3e3d3d02,0x3e3e3e3e,0x3f3f3f3f,0x0,0x01000000,0x03020201,0x04040403},
	{0x3c3c3b02,0x3d3d3d3c,0x3f3e3e3e,0x3f3f,0x02010000,0x04040302,0x07060605},
	{0x3b3a3a02,0x3c3c3c3b,0x3e3e3d3d,0x3f3f,0x02010000,0x06050403,0x09080807},
	{0x39393a02,0x3b3b3a3a,0x3e3d3d3c,0x3f3f3e,0x03020100,0x07060504,0x0c0b0a08},
	{0x38373a02,0x3a3a3938,0x3d3c3c3b,0x3f3e3e,0x04020100,0x09080605,0x0e0d0c0a},
	{0x36363a02,0x39393837,0x3d3c3b3a,0x3f3e3d,0x04030100,0x0a090706,0x100f0e0c},
	{0x35343a02,0x38383736,0x3c3b3a39,0x3f3e3d,0x05030100,0x0c0a0807,0x1011100e},
	{0x33323a02,0x37363534,0x3b3a3938,0x3f3e3d3c,0x06040200,0x0e0c0a08,0x10141210}
};

/*
 *	V chroma values from Luxsonor (remember mpeg is YUV)
 */
static u32 chroma_V[][7] = {
	{0x023a0202,0x02020202,0x02020202,0x02020202,0x02020202,0x02020202,0x02020202},
	{0x380002,0x0,0x01010101,0x02020101,0x03030202,0x04040303,0x05050404},
	{0x3f3e3e07,0x3f3f,0x01000000,0x02020101,0x04040302,0x06060505,0x08080707},
	{0x3d3c3c0a,0x3f3e3e3d,0x3f,0x02020101,0x06050403,0x09080706,0x0c0b0a0a},
	{0x3b3a3a0a,0x3e3d3c3c,0x3f3e,0x02020100,0x07060503,0x0b0a0908,0x0f0e0d0c},
	{0x39383a0a,0x3d3c3b3a,0x3f3e3d,0x02020100,0x08070603,0x0e0c0b0a,0x1012100f},
	{0x373e3a0a,0x3b3a3938,0x3f3e3d3c,0x03020100,0x0a080604,0x100e0d0b,0x10151312},
	{0x363c3a0a,0x3a393837,0x3f3e3d3b,0x03020000,0x0b090704,0x12100f0d,0x10181614},
	{0x343a3a0a,0x39383635,0x3e3d3c3a,0x03020000,0x0c0a0804,0x1513100e,0x10181917},
	{0x323a3a0a,0x38363533,0x3e3c3b39,0x0302003f,0x0e0b0905,0x17151210,0x10181c1a}
};

static void ls220_video_clear_screen(struct ls220_dev *dev)
{
	ls220_dv_write(dev, 0x3D0, 0);	/* Wipe OSD */
	if(dev->video_mode == VIDEO_NTSC || dev->video_mpeg1)
	{
		ls220_memsetl(dev, 0x200000, 0x0, 0x54600);
		ls220_memsetl(dev, 0x254600, 0x80808080, 0x2a300);
		ls220_memsetl(dev, 0x27e900, 0x0, 0x54600);
		ls220_memsetl(dev, 0x2d2f00, 0x80808080, 0x2a300);
		ls220_memsetl(dev, 0x2fd200, 0x0, 0x54600);
		ls220_memsetl(dev, 0x351800, 0x80808080, 0x2a300);
	}
	else
	{
		ls220_memsetl(dev, 0x200000, 0x0, 0x65400);
		ls220_memsetl(dev, 0x265400, 0x80808080, 0x32a00);
		ls220_memsetl(dev, 0x297e00, 0x0, 0x65400);
		ls220_memsetl(dev, 0x2fd200, 0x80808080, 0x32a00);
		ls220_memsetl(dev, 0x32fc00, 0x0, 0x65400);
		ls220_memsetl(dev, 0x395000, 0x80808080, 0x32a00);
	}
}

static void ls220_video_init(struct ls220_dev *dev)
{
	dev->video_hw_wrap = 0xF00000;
//	ls220_video_set(dev);
	ls220_video_clear_screen(dev);
//	ls220_video_set_gamma(dev, 1);
}

static void ls220_video_reset(struct ls220_dev *dev)
{
// FILL ME IN 
}

static void ls220_video_set_letter(struct ls220_dev *dev, int onoff)
{
}

static void ls220_video_set_speed(struct ls220_dev *dev, int speed)
{
}

static void ls220_video_release(struct ls220_dev *dev)
{
	ls220_video_clear_screen(dev);
	ls220_video_reset(dev);
}

static void ls220_video_still(struct ls220_dev *dev)
{
	u32 r180;

	ls220_dv_write(dev, 0x200, 0x7b);
	r180 = ls220_dv_read(dev, MPGVID_CONTROL) & 0x1E0;
	ls220_dv_write(dev, MPGVID_CONTROL, r180|4);	/* Start pointer */
	ls220_video_set_vpm(dev, dev->vga_mode);
	ls220_video_set_letter(dev, dev->video_letter);
	if(dev->type == LS240)
		ls220_dv_write(dev, SYNC_INT_CTRL, 0x88004401);
	else
		ls220_dv_write(dev, SYNC_INT_CTRL, 0x224401);
	ls220_dv_write(dev, 0x278, 0x4000);
}

static void ls220_video_play(struct ls220_dev *dev, int speed)
{
	u32 reg;

	if(dev->type < LS240)	/* might be wrong */
	{
		if(/* ?? MENU ?> */ !dev->video_mpeg1)
		{
			if(ls220_dv_read(dev, 0x145fc) == 0x01)
				ls220_dv_write(dev, 0x145fc, 0x02);
		}
	}

	reg = ls220_dv_read(dev, 0x200);
	ls220_dv_write(dev, 0x200, reg |0x01);

	reg = ls220_dv_read(dev, MPGVID_CONTROL);
	reg &= 0x1E0;
	ls220_dv_write(dev, MPGVID_CONTROL, reg|6);

	ls220_video_set_vpm(dev, dev->vga_mode);

	if(!dev->video_zoomin)
	{
		ls220_video_set_letter(dev, dev->video_letter);
		if(dev->type == LS240)
			ls220_dv_write(dev, SYNC_INT_CTRL, 0x38000001);
		else
			ls220_dv_write(dev, SYNC_INT_CTRL, 0x0e8001);
	}
	else
	{
		if(dev->type == LS240)
			ls220_dv_write(dev, SYNC_INT_CTRL, 0xc8000001);
		else
			ls220_dv_write(dev, SYNC_INT_CTRL, 0x328001);
	}
	ls220_video_set_speed(dev, dev->video_speed);
}

static void ls220_video_stop(struct ls220_dev *dev)
{
	u32 reg;

	reg = ls220_dv_read(dev, SYNC_INT_CTRL);

	if(dev->type == LS240)
		ls220_dv_write(dev, SYNC_INT_CTRL, reg&0xFFFFFFFE);
	else
		ls220_dv_write(dev, SYNC_INT_CTRL, reg&0xFFFFFE);

	reg = ls220_dv_read(dev, 0x200);
	ls220_dv_write(dev, 0x200, reg & 0xFFFE);
	
	reg = ls220_dv_read(dev, MPGVID_CONTROL);
	ls220_dv_write(dev, MPGVID_CONTROL, reg & 0x1E0);

	ls220_dv_write(dev, 0x194, VID_FIFO_OFF);

	dev->video_total = 0;
	dev->video_remainder = 0;
	dev->video_wptr = VID_FIFO_OFF;
	dev->video_pts = 0;

	reg = ls220_dv_read(dev, MPGVID_CONTROL);
	ls220_dv_write(dev, MPGVID_CONTROL, (reg&0x1E0) | 1);
}

static void ls220_video_pause(struct ls220_dev *dev)
{
	u32 reg;

	if(dev->video_speed != 1000)
		ls220_dv_write(dev, 0x19C, 0x02);
	reg = ls220_dv_read(dev, 0x200);
	ls220_dv_write(dev, 0x200, reg&0xFFFE);
	ls220_dv_write(dev, SYNC_INT_CTRL, 0x28000);
}

static unsigned long ls220_video_stc(struct ls220_dev *dev)
{
	return ls220_dv_read(dev, 0x218);
}

static void ls220_video_fastforward(struct ls220_dev *dev)
{
	u32 reg;

	dev->video_speed = 2000;
	if(dev->type == LS240)
	{
		ls220_dv_write(dev, 0x274, 0x28000001);
		ls220_dv_write(dev, 0x268, (ls220_dv_read(dev, 0x268) & 0xA0)|1);
	}
	else
	{
		ls220_dv_write(dev, 0x274, 0xa8001);
		ls220_dv_write(dev, 0x268, (ls220_dv_read(dev, 0x268) & 0x20)|1);
	}
}
	
/*
 *	Called when we reset and reload the DSP
 */
static void ls220_change_dsp(struct ls220_dev *dev, int audio, int vga)
{
	ls220_audio_init(dev, audio);
	/* FIXME - reset video vars */
	ls220_video_reset(dev);
	ls220_video_set_vpm(dev, vga);
	ls220_load_firmware(dev, audio);
	ls220_audio_set_info(dev);

	if(dev->eprom[EPROM_SPDIF_CH/*CHECK ME*/] && !(dev->eprom[EPROM_SPDIF_CH]&0x2))
	{
		/* SPDIF */
		ls220_audio_set_spdif(dev, 1);
	}
	else
		ls220_audio_set_spdif(dev, 0);
	ls220_video_reset(dev);
	ls220_video_init(dev);
	ls220_audio_init(dev, 1);
}

/*
 *	IRQ handling
 */

static void ls220_intr(int irq, void *dev_id, struct pt_regs *unused)
{
	struct ls220_dev *dev = dev_id;
	u32 r20, r14;
	
	r20 = ls220_dv_read(dev, 0x20);
	if(r20 == 0)
		return;
	
	r14 = ls220_dv_read(dev, 0x14);
	if(!(r14 & 0x0104))
		return;
	
	ls220_dv_write(dev, 0x20, r20&0xfffffefb);
	
	if(r14 & 0x4)
	{
		;	/* User event */
	}
	if(r14 & 0x100)
	{
		;	/* Video line reached */
	}

	/* Check - should we clear both bits or clear the r14 value ?? */
	ls220_dv_write(dev, 0x18, ls220_dv_read(dev, 0x18)|0x0104);
	ls220_dv_write(dev, 0x20, r20 | 0x0104);
}

/*
 *	Hardware setup
 */
 
static int ls220_hw_init(struct ls220_dev *dev)
{
	/* Set up base registers */
	if(dev->type == LS240)
		dev->dram_base = LS240_DRAM_BASE;
	else
		dev->dram_base = LS220_DRAM_BASE;
	if(ls220_i2c_register(dev))
		return -ENODEV;
	/* Initialise video side */
	if(ls220_i2c_probe(dev, 0xA0))
		ls220_load_eeprom(dev, 0xA0, 0, 16, dev->eprom);
	ls220_video_reset(dev);
	if((dev->tvencoder = ls220_detect_tvencoder(dev))<0)
	{
		i2c_bit_del_bus(&dev->i2c_adap);
		return -ENODEV;
	}
	printk(KERN_INFO "luxsonor: Found attached %s TV encoder.\n", 
		lux_tv_names[dev->tvencoder]);
	/* Initialise audio side */
	ls220_audio_init(dev, 1);
	/* Now reset and bring up */
	ls220_reset(dev);
	/* Set up the VMI */
	ls220_video_set_vpm(dev, 0/* For now.. */);
	if(ls220_i2c_probe(dev, 0xA0))
	{
		ls220_load_eeprom(dev, 0xA0, 0, 16, dev->eprom);
		dev->has_eprom = 1;
		if(dev->eprom[EPROM_BOARDTYPE] == 1)
		{
			ls220_send_gpio(dev, 6, 5);	/* chrontel setup */
			ls220_send_gpio(dev, 3, 0xf);
		}
	}
	else
	{
		dev->has_eprom = 0;
		dev->eprom[EPROM_TVOUT] = 0;	/* NTSC default */
	}

	ls220_load_firmware(dev, 1);		/* Default Microcode */

	ls220_audio_set_info(dev);

	if(dev->eprom[EPROM_SPDIF_CH/*CHECK ME*/] && !(dev->eprom[EPROM_SPDIF_CH]&0x2))
	{
		/* SPDIF */
		ls220_audio_set_spdif(dev, 1);
	}
	else
		ls220_audio_set_spdif(dev, 0);
	/* Reset again */
	ls220_video_reset(dev);
	ls220_audio_init(dev, 1);
	ls220_configure_tvout(dev, 0);
	return 0;
}

static int ls220_init_one(struct pci_dev *pdev, const struct pci_device_id *ident)
{
	struct ls220_dev *dev = kmalloc(sizeof(struct ls220_dev), GFP_KERNEL);
	if(dev == NULL)
		return -ENOMEM;
	memset(dev, 0, sizeof(*dev));
	dev->pdev = pdev;	
	dev->type = ident->driver_data;

	dev->i2c_rc = -1;

	if(pci_enable_device(pdev)<0)
	{
		kfree(pdev);
		return -ENODEV;
	}

	pci_set_drvdata(pdev, dev);

	if(request_irq(pdev->irq, ls220_intr, SA_SHIRQ, "ls220", dev)<0)
	{
		printk(KERN_ERR "ls220: unable to obtain interrupt.\n");
		pci_set_drvdata(pdev, NULL);
		kfree(dev);
		return -EBUSY;
	}
	dev->membase = ioremap(pdev->resource[0].start, pci_resource_len(pdev, 0));
	if(dev->membase == NULL)
	{
		printk(KERN_ERR "ls220: unable to map device.\n");
		free_irq(pdev->irq, dev);
		pci_set_drvdata(pdev, NULL);
		kfree(dev);
		return -ENOMEM;
	}

	pci_set_master(pdev);

	printk(KERN_INFO "luxsonor %s at 0x%lX for %ld bytes, IRQ %d.\n",
		lux_names[dev->type], pdev->resource[0].start,
		pci_resource_len(pdev, 0), pdev->irq);

	if(ls220_hw_init(dev) < 0)
	{
		free_irq(pdev->irq, dev);
		if(dev->i2c_rc != -1)
			i2c_bit_del_bus(&dev->i2c_adap);
		pci_set_drvdata(pdev, NULL);
		kfree(dev);
		return -ENODEV;
	}	
	if(dev->type == LS240)
	{
		ls240_zoomvideo_enable_tristate(dev);
		ls240_audiopcm_enable_tristate(dev);
	}


	dev->next = ls_devs;
	ls_devs = dev;
	
	return 0;
}

static void __devexit ls220_remove_one(struct pci_dev *pdev)
{
	struct ls220_dev *dev = pci_get_drvdata(pdev);
	free_irq(dev->pdev->irq, dev);
	if(dev->i2c_rc != -1)
		i2c_bit_del_bus(&dev->i2c_adap);
	iounmap(dev->membase);
	pci_disable_device(dev->pdev);
}

/*
 *	This code and tables ensures we are notified if there is a
 *	luxsonor card, either at boot or in the event of a PCI hotplug
 */
 
static struct pci_device_id luxsonor_table[] __devinitdata = {
	{ 0x1287, 0x001F, PCI_ANY_ID, PCI_ANY_ID, LS220C },	/* 220C */
	{ 0x1287, 0x001E, PCI_ANY_ID, PCI_ANY_ID, LS220D },	/* 220D */
	{ 0x1287, 0x0020, PCI_ANY_ID, PCI_ANY_ID, LS240  },	/* 240 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, luxsonor_table);

static struct pci_driver luxsonor_driver = 
{
	name:		"ls220",
	id_table:	luxsonor_table,
	probe:		ls220_init_one,
	remove:		__devexit_p(ls220_remove_one),
	/* No power management yet */
};

static int __init luxsonor_init_module(void)
{
	return pci_module_init(&luxsonor_driver);
}

static void __exit luxsonor_cleanup_module(void)
{
	pci_unregister_driver(&luxsonor_driver);
}

module_init(luxsonor_init_module);
module_exit(luxsonor_cleanup_module);

MODULE_AUTHOR("Alan Cox <alan@redhat.com>");
MODULE_LICENSE("GPL");
