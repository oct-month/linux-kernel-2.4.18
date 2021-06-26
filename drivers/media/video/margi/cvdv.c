/* 
    cvdv.c

    Copyright (C) Christian Wolff 
                  Marcus Metzler for convergence integrated media.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

      /////////////////////////////////////////////////////////////////////
     //                                                                 //
    //   Driver for the Convergence Digital Video decoder card (pci)   //
   //   with L64017, L64021, PCM1723, and Bt864/Bt865 chipset         //
  //   (c) Christian Wolff 19990209 for convergence integrated media //
 //                                                                 //
/////////////////////////////////////////////////////////////////////

// Convergence CV2300i
#define __NO_VERSION__

#include <linux/module.h>
#include "cvdv.h"
#include "ost/osd.h"
#include "i2c.h"

  //////////////////////  
 // global variables //
//////////////////////

// Our major device number
unsigned int major_device_number;


// my little random function for memory test
uint16_t rnd_seed;
uint16_t rnd(uint16_t range)
{				// returns random 0..(range-1) range<=872
	uint32_t b = 75 * (rnd_seed + 1) - 1;
	rnd_seed = (uint16_t) (b & 0xFFFF);
	return ((b * range) / 0xFFFF) - ((b / 0xFFFF) * range);
}
void rnd_omize(void)
{
	rnd_seed = (uint16_t) jiffies;
}

static char *cimlogo[] = {
".............................................",
".............................................",
"......................###....................",
".....................#####...................",
".....................######..................",
"..............#......#####...................",
"...........#####....######...................",
".........########...######...................",
"........########....######...................",
".......#########...######....................",
"......########.....######...####.............",
".....#######.......#####...#####.............",
".....######.......######...######............",
"....#######.......######....######...........",
"....######........######....######...........",
"....#####........######......#####...........",
"...######........######......#####...........",
"...#####.........######......######..........",
"...#####.........#####.......######..........",
"...#####........######........#####..........",
"...#####........######.......######..........",
"...#####........#####.........#####..........",
"...#####.......######........######..........",
"...#####.......######........#####...........",
"...######.......####.........#####...........",
"....#####........##.........######...........",
"....######..................######...........",
"....######.................######............",
".....#######..............######.....#####...",
".....########............#######....#######..",
"......#########........########.....#######..",
".......#######################......########.",
"........#####################.......#######..",
"..........#################.........#######..",
"............#############............#####...",
"...............#.#####.................##....",
".............................................",
"............................................."
};

    /////////////////////////////////////////////
   //                                         //
  //  Controlling the L64021 MPEG-2 Decoder  //
 //                                         //
/////////////////////////////////////////////

int OSDTest(struct cvdv_cards *card)
{
	int i, j, col, x0, y0, x1, y1,aspx;
	uint8_t b;


	if (!card->OSD.open)
		return -2;

	OSDQuery(card, &x0, &y0, &x1, &y1, &aspx);
	OSDShow(card);
	OSDSetColor(card, 0, 0, 0, 0, 0, 0, 0);
	OSDSetColor(card, 1, 128, 255, 255, 0, 0, 0);
	for ( i = 0; i < cimlogo_width; i++){
		for ( j = 0; j < cimlogo_height; j++){
			b = cimlogo[j][i];
			col = (b == '#') ? 1: 0;
			OSDSetPixel(card, x0+i, y0+j, col);
		}
	}

	return 0;
}


void SetVideoSystem(struct cvdv_cards *card)
{
	uint8_t reg;

	// set the hsync and vsync generators in the L64017 according to the video standard
	reg = read_indexed_register(card, IIO_VIDEO_CONTROL1);
	reg &= ~0x03;
	switch (card->videomode) {
	case PAL:		// 864*625*50Hz = 27MHz, 25fps
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x41 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x96);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0x13);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x54);
		reg |= VMS_PAL;
		break;
	case PALN:
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0xa1 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x96);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0x13);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x54);
		reg |= VMS_PAL;
		break;

	case PALNc:
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x81 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x8c);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0x28);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0xed);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x43);
		reg |= VMS_PAL;
		break;

	case NTSC:		// 858*525*59.94006Hz = 27MHz, 29.97fps
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x01 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x1c);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x3e);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0xf8);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0xe0);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x43);
		reg |= VMS_NTSC;
		break;

	case PALM:
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x01 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x4e);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0x4a);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0xe1);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x43);
		reg |= VMS_PAL;
		break;

	case NTSC60:		// 857*525*60.010002Hz = 27MHz, 30fps
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x21 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x1c);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x3e);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0xf8);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0xe0);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x43);
		reg |= VMS_NTSC;
		break;

	case PALM60:
		I2CWrite(card, card->i2c_addr, CS_CONTROL0, 0x61 | 0x0a);
		I2CWrite(card, card->i2c_addr, CS_CONTROL1, 0x04);
		I2CWrite(card, card->i2c_addr, CS_SC_AMP, 0x15);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH0, 0x4e);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH1, 0x4a);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH2, 0xe1);
		I2CWrite(card, card->i2c_addr, CS_SC_SYNTH3, 0x43);
		reg |= VMS_PAL;
		break;

	case PAL60:
		break;
	}
	write_indexed_register(card, IIO_VIDEO_CONTROL1, reg);
	// set the pixel generators according to the video standard
	L64021Setup(card);
}

int SetVideoAttr(struct cvdv_cards *card, uint16_t vattr)
{
	uint8_t video_compression_mode;
	uint8_t tv_system;
	uint8_t aspect_ratio;
	uint8_t display_mode;
	uint8_t line_21_switch_1;
	uint8_t line_21_switch_2;
	uint8_t source_picture_resolution;
	uint8_t source_picture_letterboxed;
	uint8_t reserved;
	uint8_t film_camera_mode;
	uint16_t hsize, vsize;
	if (vattr != card->lastvattr) {
		video_compression_mode = (vattr >> 14) & 0x03;
		tv_system = (vattr >> 12) & 0x03;
		aspect_ratio = (vattr >> 10) & 0x03;
		display_mode = (vattr >> 8) & 0x03;
		line_21_switch_1 = (vattr >> 7) & 0x01;
		line_21_switch_2 = (vattr >> 6) & 0x01;
		source_picture_resolution = (vattr >> 3) & 0x07;
		source_picture_letterboxed = (vattr >> 2) & 0x01;
		reserved = (vattr >> 1) & 0x01;
		film_camera_mode = (vattr >> 0) & 0x01;
		card->videomode =
			((tv_system == 0) ? NTSC : ((tv_system == 1) ? 
						    PAL : PAL));	
		SetVideoSystem(card);
		hsize =
			((source_picture_resolution == 0) ? 720
			 : ((source_picture_resolution == 1) ? 702 : 352));
		vsize = ((source_picture_resolution == 3)
			 ? ((tv_system == 0) ? 240 : 288)
			 : ((tv_system == 0) ? 480 : 576));
		if (DecoderOpen
		    (card, hsize, vsize, ((aspect_ratio) ? 3 : 2),
		     ((video_compression_mode) ? 0 : 1),
		     source_picture_letterboxed, tv_system)) {	
			MDEBUG(0,
			       ": Video Decoder Open failed: On-card memory insufficient for frame stores\n");
		}
		card->lastvattr = vattr;
	} else {
		MDEBUG(0,
		       ": Video attribute not set, equal to previous one.\n");
	}
	return 0;
}

int SetAudioAttr(struct cvdv_cards *card, uint16_t aattr)
{
	uint8_t audio_coding_mode;
	uint8_t multichannel_extension;
	uint8_t audio_type;
	uint8_t audio_application_mode;
	uint8_t quantization_drc;
	uint8_t fs;
	uint8_t reserved;
	uint8_t num_audio_ch;
	if (aattr) {
		if (aattr != card->lastaattr) {
			audio_coding_mode = (aattr >> 13) & 0x07;
			multichannel_extension = (aattr >> 12) & 0x01;
			audio_type = (aattr >> 10) & 0x03;
			audio_application_mode = (aattr >> 8) & 0x03;
			quantization_drc = (aattr >> 6) & 0x03;
			fs = (aattr >> 4) & 0x03;
			reserved = (aattr >> 3) & 0x01;
			num_audio_ch = (aattr >> 0) & 0x07;
			switch (audio_coding_mode) {
			case 0:	// AC-3
				card->setup.audioselect = audio_AC3;
				break;
			case 2:	// MPEG Audio
				card->setup.audioselect = audio_MPEG;
				break;
			case 3:	// MPEG Audio with ext.
				card->setup.audioselect = audio_MPEG_EXT;
				break;
			case 4:	// Linear Pulse Code Modulation LPCM
				card->setup.audioselect = audio_LPCM;
				break;
			case 6:	// DTS
				card->setup.audioselect = audio_DTS;
				break;
			case 7:	// SDDS
				card->setup.audioselect = audio_SDDS;
				break;
			}
			DecoderPrepareAudio(card);
			AudioInit(card, ((fs) ? 96 : 48),
				  ((audio_application_mode == 2) ? 1 : 0));
		} else {
			MDEBUG(0,
			       ": Audio attribute not set, equal to previous one.\n");
		}
	} else {
		card->setup.audioselect = audio_none;
		DecoderPrepareAudio(card);
	}
	card->lastaattr = aattr;
	return 0;
}

int Prepare(struct cvdv_cards *card)
{
	int err, h;
	struct StreamSetup *setup = &card->setup;

	if (!card->ChannelBuffersAllocated) {
			
		DecoderStreamReset(card);
		if (setup->streamtype == stream_none) {
			setup->streamtype = stream_PS; 
		}
		
		if (setup->audioselect == audio_none) {
			setup->audioselect = audio_MPEG;
		}

		DecoderPrepareAudio(card);
		AudioMute(card, 1);
		DecoderPrepareVideo(card);
		VideoSetBackground(card, 1, 0, 0, 0);	// black

		switch (setup->streamtype) {
		default:
		case stream_none:	// unknown stream!
			MDEBUG(0,
			       ": Video Decoder Prepare failed: unknown stream type\n");
			return -ENODEV;	// not an MPEG stream!
		case stream_ES:	// Elementary Stream
			err = DecoderPrepareES(card);
			break;
		case stream_PES:	// Packetized Elementary Stream
			err = DecoderPreparePES(card);
			break;
		case stream_PS:	// MPEG-1 System Stream / MPEG-2 Program Stream
			err = DecoderPreparePS(card, -1, 0, 0, 0, 0, 0);
			break;
		case stream_DVD:	// DVD Stream
			err = DecoderPreparePS(card, 0, 0, 0, 0, 3, 1);
			break;
		}
		if (err) {	// insufficient memory
			MDEBUG(0,
			       ": Video Decoder Prepare failed: no kernel memory, please reboot if possible\n");
			CloseCard(card);
			return -ENODEV;
		}
	}

	// Set up the Video Decoder as we have the stream information
	if ((!card->FrameBuffersAllocated)
	    && (card->ChannelBuffersAllocated) && (card->stream.sh.valid)) {
		//  Automatic PAL/NTSC-switch according to MPEG-Source
		h = card->stream.vsize;
		if (h < 480)
			h *= 2;	// catch quarter sized images
		printk(KERN_INFO LOGNAME ": Video mode: %s\n",
		       ((h == 480) ? "NTSC" : "PAL"));
		card->videomode = ((h == 480) ? NTSC : PAL);
		SetVideoSystem(card);
		// Open the Video Decoder with the parameters retreived from the stream
		if (
		    (err =
		     DecoderOpen(card, card->stream.hsize,
				 card->stream.vsize,
				 card->stream.sh.aspectratio,
				 !card->stream.MPEG2, 0,
				 (card->stream.hsize > 480)))) {	// TODO: include vbvbuffersize
			MDEBUG(0,
			       ": Video Decoder Open failed: %s\n",
			       ((err == 1) ?
				"Picture size too big (>1440 pixel wide)" :
				"On-card memory insufficient for frame stores"));
			CloseCard(card);
			return -ENODEV;	// picture too big or insufficient memory
		}
		MDEBUG(1, ": Ready to go\n");
		card->startingV = 1;	// tell the card to start playing as soon as ES-buffers are sufficiently full
		card->startingA = 1;	// tell the card to start playing as soon as ES-buffers are sufficiently full
	}
	

	return 0;
}

int SetSCRstart(struct cvdv_cards *card, uint32_t SCR_base)
{
	uint32_t SCR_compare;
	uint32_t SCR_compareA;
	uint32_t SCR_compareV;
	if (card->startingV) {
		MDEBUG(0, ": SCR in DVD Pack: 0x%08X\n",
		       SCR_base);
		card->startingV = 0;
		card->startingA = 0;
		DecoderMaskByte(card, 0x007, 0xD2, 0xD2);	// Set 0x010, halt SCR counter
		SCR_compare = SCR_base + 000;
		if (SCR_base < 900)
			SCR_base = 0;
		else
			SCR_base -= 900;
		//DecoderWriteDWord(card,0x009,SCR_base);  // Set SCR counter
		DecoderWriteByte(card, 0x009, SCR_base & 0xFF);	// Set SCR counter
		DecoderWriteByte(card, 0x00A, (SCR_base >> 8) & 0xFF);
		DecoderWriteByte(card, 0x00B, (SCR_base >> 16) & 0xFF);
		DecoderWriteByte(card, 0x00C, (SCR_base >> 24) & 0xFF);
		DecoderMaskByte(card, 0x011, 0x03, 0x02);	// compare, not capture
		MDEBUG(0, ": SCR compare value: 0x%08X\n",
		       SCR_compare);
		//DecoderWriteDWord(card,0x00D,SCR_compare);  // Set Compare register
		DecoderWriteByte(card, 0x00D, SCR_compare & 0xFF);	// Set Compare register
		DecoderWriteByte(card, 0x00E, (SCR_compare >> 8) & 0xFF);
		DecoderWriteByte(card, 0x00F, (SCR_compare >> 16) & 0xFF);
		DecoderWriteByte(card, 0x010, (SCR_compare >> 24) & 0xFF);
		//DecoderWriteDWord(card,0x014,SCR_compare);  // Set audio compare reg.
		DecoderWriteByte(card, 0x014, SCR_compare & 0xFF);	// Set audio compare reg.
		DecoderWriteByte(card, 0x015, (SCR_compare >> 8) & 0xFF);
		DecoderWriteByte(card, 0x016, (SCR_compare >> 16) & 0xFF);
		DecoderWriteByte(card, 0x017, (SCR_compare >> 24) & 0xFF);
		DecoderSetByte(card, 0x013, 0x03);	// Video and Audio start on cmp.
		//DecoderSetVideoPanic(card,0,DecoderGetVideoESSize(card)/4);  // video panic at 25 percent
		VideoSetBackground(card, 1, 0, 0, 0);	// black
		SCR_base = DecoderReadByte(card, 0x009);
		SCR_base =
		    SCR_base | ((uint32_t) DecoderReadByte(card, 0x00A) << 8);
		SCR_base =
		    SCR_base | ((uint32_t) DecoderReadByte(card, 0x00B) << 16);
		SCR_base =
		    SCR_base | ((uint32_t) DecoderReadByte(card, 0x00C) << 24);
		SCR_compareA = DecoderReadByte(card, 0x014);
		SCR_compareA =
		    SCR_compareA | ((uint32_t) DecoderReadByte(card, 0x015) <<
				    8);
		SCR_compareA =
		    SCR_compareA | ((uint32_t) DecoderReadByte(card, 0x016) <<
				    16);
		SCR_compareA =
		    SCR_compareA | ((uint32_t) DecoderReadByte(card, 0x017) <<
				    24);
		SCR_compareV = DecoderReadByte(card, 0x00D);
		SCR_compareV =
		    SCR_compareV | ((uint32_t) DecoderReadByte(card, 0x00E) <<
				    8);
		SCR_compareV =
		    SCR_compareV | ((uint32_t) DecoderReadByte(card, 0x00F) <<
				    16);
		SCR_compareV =
		    SCR_compareV | ((uint32_t) DecoderReadByte(card, 0x010) <<
				    24);
		if (DecoderReadByte(card, 0x013) & 0x03)
			MDEBUG(1,": SCR 0x%08X, videocmp=0x%08X, audiocmp=0x%08X %02X\n",
			       SCR_base, SCR_compareV, SCR_compareA,
			       DecoderReadByte(card, 0x013));
		DecoderMaskByte(card, 0x007, 0xD2, 0xC2);	// Del 0x010, SCR counter run
	}
	return 0;
}

int DecoderWriteBlock(struct cvdv_cards *card, uint8_t * data, int size,
		      int initial, int setSCR)
{
	//int a,v,as,vs,ap,vp;
	int res;
	uint32_t SCR_base;
	int co = 0;
	//  uint32_t SCR_compare;
	res = 0;
	
	Prepare(card);

	if (size > 0) {

		if (!card->use_ringA)
			MargiSetBuffers(card, NBBUF*CHANNELBUFFERSIZE,0);
		
		if (card->startingDVDV || card->startingDVDA)
			setSCR = 1;

		if (initial) {
			DecoderStreamReset(card);
			//TODO stop and start channel interface
			setSCR = 1;
		}

		if (setSCR) {
			SCR_base = ParseSCR(data);
			SetSCR(card, SCR_base);
		}
		card->DMAABusy = 0;
		while (((res = MargiPushA(card, size, data)) < size) 
		       && co < 1000) {	
			data+=res;
			size-=res;
			co++;
			MDEBUG(2,
			       ": DecoderWriteBlock - buffers only filled with %d instead of %d bytes\n",res, size);
			if (card->DMAABusy){
				interruptible_sleep_on(&card->wqA);	
			}
		}

		if (card->startingDVDV) {
			card->startingDVDV = 0;
			card->startingV = 1;
			DecoderStartDecode(card);
		}
		if (card->startingDVDA) {
			card->startingDVDA = 0;
			card->startingA = 1;
			AudioSetPlayMode(card, MAUDIO_PLAY);
		}
	}
	return 0;
}





    //////////////////////////////
   //                          //
  //  Char Device Procedures  //
 //                          //
//////////////////////////////
static long margi_write(struct cvdv_cards *card, const char *data,
                      unsigned long count, int nonblock)
{

	int res;
	long int out=0;
	int free;

	free = ring_write_rest(&(card->rbufA));

	if (card != NULL) {
		card->nonblock = nonblock;
		if (count > 0) {	// Do we have data?
			if ((res = Prepare(card)))
				return res;
			if (!card->use_ringA)
				MargiSetBuffers(card, NBBUF*CHANNELBUFFERSIZE,
						0);
			if (!nonblock && 
			    !wait_event_interruptible(
				    card->wqA, 
				    ring_write_rest(&(card->rbufA)) >count )){
				
				out = MargiPushA(card, count,
						 data);
			} else {
				out = MargiPushA(card, count, data);
			}
		}
		return out;
	} else {
		MDEBUG(0,
		       ": Video Decoder Prepare failed: device with this minor number not found\n");
		return -ENODEV;	// device with this minor number not found
	}
}	


static long margi_write_audio(struct cvdv_cards *card, const char *data,
                      unsigned long count, int nonblock)
{
	struct StreamSetup *setup = &card->setup;

	int res;
	long int out=0;
	int free;

	free = ring_write_rest(&(card->rbufB));

	if (card != NULL) {
		card->nonblock = nonblock;
	
		if (count > 0) {	// Do we have data?
			if ((res = Prepare(card)))
				return res;
			if ((setup->streamtype == stream_ES)
			    || (setup->streamtype == stream_PES)){
				if (!card->use_ringB)
					MargiSetBuffers(card, NBBUF*
                                                         CHANNELBUFFERSIZE,1);
				if (!nonblock && 
				    !wait_event_interruptible(
					    card->wqB, 
					    ring_write_rest(&(card->rbufB))
					    > count)){
					out = MargiPushB(card, count,
							 data);
				} else {
					out = MargiPushB(card, count, data);
				}
			}
		}
		return out;
	} else {
		MDEBUG(0,
		       ": Video Decoder Prepare failed: device with this minor number not found\n");
		return -ENODEV;	// device with this minor number not found
	}
}	

void pes_write(uint8_t *buf, int count, void *priv)
{
	struct cvdv_cards *card = (struct cvdv_cards *) priv;
	
	margi_write(card, buf, count, 0);
}


static ssize_t PSwrite(struct file *file, const char *data, size_t count,
		       loff_t * offset)
{
	struct cvdv_cards *card =
	    minorlist[MINOR(file->f_dentry->d_inode->i_rdev) % MAXDEV];	// minor number modulo 16
	return margi_write(card, data, count, file->f_flags&O_NONBLOCK);
}

static unsigned int PSpoll(struct file *file, poll_table * table)
{
	struct cvdv_cards *card =
	    minorlist[MINOR(file->f_dentry->d_inode->i_rdev) % MAXDEV];	// minor number modulo 16
	if (card != NULL) {
		poll_wait(file, &card->wqA , table);
		if  (  !card->rbufA.buffy || ring_write_rest(&(card->rbufA)) )
			return (POLLOUT | POLLWRNORM);	
		else {
			return 0;
		}
	} else
		return POLLERR;
}

static unsigned int poll_audio(struct file *file, poll_table * table)
{
	struct cvdv_cards *card =
	    minorlist[MINOR(file->f_dentry->d_inode->i_rdev) % MAXDEV];	// minor number modulo 16
	if (card != NULL) {
		poll_wait(file, &card->wqB, table);
		if  (  !card->rbufB.buffy || ring_write_rest(&(card->rbufB)) )
			return (POLLOUT | POLLWRNORM);	
		else {
			return 0;
		}
	} else
		return POLLERR;
}

static int
OSD_DrawCommand(struct cvdv_cards *card,osd_cmd_t *dc)
{

	switch (dc->cmd) {
	case OSD_Close:
		MDEBUG(1,": OSD Close\n");
		return OSDClose(card);
	case OSD_Open:	// Open(x0,y0,x1,y1,BitPerPixel(2/4/8),mix(0..15))
		return OSDOpen(card, dc->x0,
			       dc->y0, dc->x1,
			       dc->y1,
			       dc->color & 0x0F,
			       (dc->color >> 4) &
			       0x0F);
	case OSD_Show:
		return OSDShow(card);
	case OSD_Hide:
		return OSDHide(card);
	case OSD_Clear:
		return OSDClear(card);
	case OSD_Fill:	// Fill(color)
		return OSDFill(card, dc->color);
	case OSD_SetColor:    // SetColor(color,R(x0),G(y0),B(x1),opacity(y1))
		return (OSDSetColor
			(card, dc->color, dc->x0,
			 dc->y0, dc->x1, 0,
			 (dc->y1 != 255),
			 (dc->y1 == 0)) >= 0);
	case OSD_SetPalette:// SetPalette(firstcolor{color},lastcolor{x0},data)
		return OSDSetPalette(card,
				     dc->color,
				     dc->x0, (uint8_t *)
				     dc->data);
	case OSD_SetTrans:	// SetTrans(transparency{color})
		return OSDSetTrans(card,
				   (dc->color >> 4)
				   & 0x0F);
	case OSD_SetPixel:	// SetPixel(x0,y0,color)
		return OSDSetPixel(card, dc->x0,
				   dc->y0,
				   dc->color);
	case OSD_GetPixel:	// GetPixel(x0,y0);
		return OSDGetPixel(card, dc->x0,
				   dc->y0);
	case OSD_SetRow:	// SetRow(x0,y0,x1,(uint8_t*)data)
		return OSDSetRow(card, dc->x0,
				 dc->y0, dc->x1,
				 (uint8_t *) dc->data);
	case OSD_SetBlock:	// SetBlock(x0,y0,x1,y1,(uint8_t*)data)
		return OSDSetBlock(card, dc->x0,
				   dc->y0, dc->x1,
				   dc->y1,
				   dc->color,
				   (uint8_t *)
				   dc->data);
	case OSD_FillRow:	// FillRow(x0,y0,x1,color)
		return OSDFillRow(card, dc->x0,
				  dc->y0, dc->x1,
				  dc->color);
	case OSD_FillBlock:	// FillRow(x0,y0,x1,y1,color)
		return OSDFillBlock(card, dc->x0,
				    dc->y0, dc->x1,
				    dc->y1,
				    dc->color);
	case OSD_Line:	// Line(x0,y0,x1,y1,color);
		return OSDLine(card, dc->x0,
			       dc->y0, dc->x1,
			       dc->y1, dc->color);
	case OSD_Query:	// Query(x0,y0,x1,y1,aspect(color:11)
		return OSDQuery(card, &dc->x0,
				&dc->y0, &dc->x1,
				&dc->y1,
				&dc->color);
	case OSD_Test:
		return OSDTest(card);
	default:
		return -EINVAL;
	}
}


static int PSioctl(struct inode *inode, struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	struct cvdv_cards *card = minorlist[MINOR(inode->i_rdev) % MAXDEV];	// minor number modulo 16
	osd_cmd_t *dc;
	struct decodercmd *command;
	uint16_t attr;

	if (card != NULL) {
		if (_IOC_TYPE(cmd) == CVDV_IOCTL_MAGIC)
			switch (_IOC_NR(cmd)) {
			case IOCTL_DRAW:	// Drawing commands
				dc = (osd_cmd_t *) arg;
				return OSD_DrawCommand(card,dc);
				break;
			case IOCTL_DECODER:
				command = (struct decodercmd *) arg;
				switch (command->cmd) {

				case Decoder_CSS:
				  /*
					return DecoderCSS(card,
							  command->param1,
							  command->data1);
				  */
				  break;

				case Decoder_Set_Videosystem:
					MDEBUG(1,": -- Decoder_Set_Videosystem\n");
					card->videomode =
					    (videosystem) command->param1;
					SetVideoSystem(card);
					return 0;
					break;

				case Decoder_Set_Streamtype:
					MDEBUG(1,": -- Decoder_Set_Streamtype\n");
					card->setup.streamtype =
					    (stream_type) command->param1;
					return 0;
					break;

				case Decoder_Set_Audiotype:
					MDEBUG(1,": -- Decoder_Set_Audiotype\n");
					card->setup.audioselect =
					    (audio_type) command->param1;
					DecoderPrepareAudio(card);
					return 0;
					break;

				case Decoder_Set_VideoStreamID:
					MDEBUG(1,": -- Decoder_Set_VideoStreamID\n");
					card->setup.videoID =
					    command->param1;
					DecoderPrepareVideo(card);
					return 0;
					break;

				case Decoder_Set_AudioStreamID:
					MDEBUG(1,": -- Decoder_Set_AudioStreamID 0x%02X 0x%02X\n",
					       command->param1,command->param2);
					card->setup.audioID =
					    command->param1;
					card->setup.audioIDext =
					    command->param2;
					attr = card->lastaattr;
					DecoderSelectAudioID(card);
					card->lastaattr = attr;
					return 0;
					break;

				case Decoder_Still_Put:
					return DecoderShowStill(card,
								command->
								param1,
								command->
								param2,
								command->
								data1,
								command->
								data2);
					break;

				case Decoder_Still_Get:
					return DecoderGetStill(card,
							       &command->
							       param1,
							       &command->
							       param2,
							       command->
							       data1,
							       command->
							       data2);
					break;

				case Decoder_Pause:	// pause{param1}  0=run 1=pause 2=toggle
					if (command->param1 == 2) {
						if (card->paused)
							DecoderUnPause
							    (card);
						else
							DecoderPause(card);
					} else {
						if (!command->param1)
							DecoderUnPause
							    (card);
						else
							DecoderPause(card);
					}
					return 0;

					/* Too buggy 				
				case Decoder_FFWD:	// pause{param1}  =normal 1=ffwd 2=toggle
					if (command->param1 == 2) {
						if (card->videoffwd)
							card->videoffwd = 0;
						else
							card->videoffwd = 3;
					} else {
						if (!command->param1)
							card->videoffwd = 0;
						else
							card->videoffwd = 3;
					}
					return 0;

				case Decoder_Slow:	// pause{param1}  =normal 1=slow 2=toggle
					if (command->param1 == 2) {
						if (card->videoslow)
							card->videoslow = 0;
						else
							card->videoslow = 4;
					} else {
						if (!command->param1)
							card->videoslow = 0;
						else
							card->videoslow = 4;
					}
					return 0;
					*/
				case Decoder_Highlight:	// active{param1}, color information(SL_COLI or AC_COLI){data1[4]}, button position(BTN_POSI){data2[6]}
					return DecoderHighlight(card,
								command->
								param1,
								command->
								data1,
								command->
								data2);
				case Decoder_SPU:	// stream{param1}, active{param2}
					return DecoderSPUStream(card,
								command->
								param1,
								command->
								param2);
				case Decoder_SPU_Palette:	// length{param1}, palette{data1}
					return DecoderSPUPalette(card,
								 command->
								 param1,
								 command->
								 data1);
				case Decoder_GetNavi:	// data1 will be filled with PCI or DSI pack, and 1024 will be returned
					return DecoderGetNavi(card,
							      command->
							      data1);
				case Decoder_SetKaraoke:	// Vocal1{param1}, Vocal2{param2}, Melody{param3} 
					return DecoderKaraoke(card,
							      command->
							      param1,
							      command->
							      param2,
							      command->
							      param3);
				case Decoder_Set_Videoattribute:
					MDEBUG(1,": -- Decoder_Set_Videoattribute\n");
					if (!card->ChannelBuffersAllocated) {
						DecoderStreamReset(card);
						MargiFlush(card);

						card->setup.streamtype =
						    stream_DVD;
						card->setup.videoID = 0;
						DecoderPrepareVideo(card);
						DecoderPreparePS(card, 0,
								 0, 2, 2,
								 3, 1);
					}

					SetVideoAttr(card,
						     command->param1);
					card->startingDVDV = 1;	
// tell the card to start playing as soon as ES-buffers are sufficiently full
					return 0;
				case Decoder_Set_Audioattribute:
					MDEBUG(1,": -- Decoder_Set_Audioattribute\n");
					SetAudioAttr(card,
						     command->param1);
					card->startingDVDA =
					    ((card->setup.audioselect !=
					      audio_none)
					     && (card->setup.audioselect != audio_disable));	// tell the card to start playing as soon as ES-buffers are sufficiently full
					return 0;
				case Decoder_WriteBlock:	// DVD-Sector{data1}, sectorsize{param1{2048}}, initialsector{param2{bool}}, set_SCR{param3}
					return DecoderWriteBlock(card,
								 command->
								 data1,
								 command->
								 param1,
								 command->
								 param2,
								 command->
								 param3);
				default:
					return -EINVAL;
				}
			default:
				return -EINVAL;
		} else
			return -EINVAL;
	} else {
		MDEBUG(0,
		       ": Video Decoder Prepare failed: device with this minor number not found\n");
		return -ENODEV;	// device with this minor number not found
	}
}


static int PSmmap(struct file *file, struct vm_area_struct *vm)
{
	return -ENODEV;
}



static int margi_open(struct cvdv_cards *card, int flags)
{
	int closed;

	printk("Open card = %p\n", card);

	if (card != NULL) {
		MDEBUG(1, ": -- open \n");
		CloseCard(card);
		OSDClose(card);
	
		printk("Card and OSD closed.\n");
#ifdef NOINT
		card->timer.function = Timerfunction;
		card->timer.data=(unsigned long) card;
		card->timer.expires=jiffies+1;
		add_timer(&card->timer);
#endif
		printk("Timer added.\n");

		if (card->open)
			MDEBUG(0,": PSopen - already open\n");
		closed = 1;
		if (card->open)
			closed = 0;
		if (closed) {	// first open() for this card?
			printk("Freeing buffers.\n");
			MargiFreeBuffers(card);
			printk("Fade to black.\n");
			VideoSetBackground(card, 1, 0, 0, 0);	// black
		}
		printk("Go\n");
		card->open++;
		return 0;
	} else {
		MDEBUG(0,
		       ": Video Decoder Prepare failed: device with this minor number not found\n");
		return -ENODEV;	// device with this minor number not found
	}

}


static int PSopen(struct inode *inode, struct file *file)
{
	struct cvdv_cards *card = minorlist[MINOR(inode->i_rdev) % MAXDEV];
#ifdef DVB
	if(card)
		card->audiostate.AVSyncState=true;
#endif	
	return margi_open(card, file->f_flags);
}


static int  all_margi_close(struct cvdv_cards *card)
{

	if (card != NULL) {
		MDEBUG(1, ": -- PSrelease\n");
		if (card->open <= 0)
			MDEBUG(1,": PSrelease - not open\n");
		card->open--;
		
		if (!card->open) {
			MDEBUG(1,": PSrelease - last close\n");
			CloseCard(card);	// close immediately
		}
		return 0;
	} else {
		MDEBUG(0,": Video Decoder Prepare failed:\n");
		return -ENODEV;	// device with this minor number not found
	}
	
}

static int PSrelease(struct inode *inode, struct file *file)
{
	struct cvdv_cards *card = minorlist[MINOR(inode->i_rdev) % MAXDEV];	// minor number modulo 16
	return all_margi_close(card);
}

    //////////////////////////
   //                      //
  //  Char Device Hookup  //
 //                      //
//////////////////////////

// Hookups for a write-only device, that accepts MPEG-2 Program Stream
struct file_operations cvdv_fileops = {
	owner:	 THIS_MODULE,
	write:   PSwrite,
	poll:    PSpoll,		
	ioctl:   PSioctl,
	mmap:    PSmmap,
	open:    PSopen,
	release: PSrelease,
};


#ifdef DVB

static inline int
num2type(struct cvdv_cards *card, int num)
{
        if (!card->dvb_devs)
                return -2;
        if (num>=card->dvb_devs->num)
                return -2;
        return card->dvb_devs->tab[num];
}

static int 
dvbdev_open(struct dvb_device *dvbdev, int num, 
            struct inode *inode, struct file *file)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        int type=num2type(card, num);
        int ret=0;

        if (type<0)
                return -EINVAL;

        if (card->users[num] >= card->dvb_devs->max_users[num])
                return -EBUSY;

	if ((file->f_flags&O_ACCMODE)!=O_RDONLY) 
                if (card->writers[num] >= card->dvb_devs->max_writers[num])
		        return -EBUSY;

	switch (type) {
	case DVB_DEVICE_VIDEO_0:
                card->video_blank=true;
		card->audiostate.AVSyncState=true;
                card->videostate.streamSource=VIDEO_SOURCE_DEMUX;
		margi_open(card, file->f_flags);
	       break;

	case DVB_DEVICE_AUDIO_0:
		card->audiostate.AVSyncState=true;
                card->audiostate.streamSource=AUDIO_SOURCE_DEMUX;
                break;

	case DVB_DEVICE_DEMUX_0:
                if ((file->f_flags&O_ACCMODE)!=O_RDWR)
                        return -EINVAL;
                ret=DmxDevFilterAlloc(&card->dmxdev, file);
                break;

	case DVB_DEVICE_DVR_0:
		card->audiostate.AVSyncState=true;
		card->setup.streamtype =  stream_PES;
		margi_open(card, file->f_flags);
                ret=DmxDevDVROpen(&card->dmxdev, file);
                break;
	
	case DVB_DEVICE_OSD_0:
                break;
	default:
                return -EINVAL;
	}
	if (ret<0) 
	        return ret;
	if ((file->f_flags&O_ACCMODE)!=O_RDONLY)
		card->writers[num]++;
        card->users[num]++;
        return ret;
}

static int 
dvbdev_close(struct dvb_device *dvbdev, int num, 
             struct inode *inode, struct file *file)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        int type=num2type(card, num);
        int ret=0;

        if (type<0)
                return -EINVAL;

	switch (type) {
	case DVB_DEVICE_VIDEO_0:
	case DVB_DEVICE_AUDIO_0:
		if (card->open)
			all_margi_close(card);
		break;

	case DVB_DEVICE_DEMUX_0:
                ret=DmxDevFilterFree(&card->dmxdev, file);
                break;

	case DVB_DEVICE_DVR_0:
                ret=DmxDevDVRClose(&card->dmxdev, file);
		if (card->open)
			all_margi_close(card);
                break;
	case DVB_DEVICE_OSD_0:
                break;
	default:
                return -EINVAL;
	}
	if (ret<0) 
	        return ret;
	if ((file->f_flags&O_ACCMODE)!=O_RDONLY)
		card->writers[num]--;
        card->users[num]--;
        return ret;
}


static ssize_t 
dvbdev_write(struct dvb_device *dvbdev, int num,
             struct file *file, 
             const char *buf, size_t count, loff_t *ppos)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        int type=num2type(card, num);

	switch (type) {
	case DVB_DEVICE_VIDEO_0:
                if (card->videostate.streamSource!=VIDEO_SOURCE_MEMORY)
                        return -EPERM;
                return margi_write(card, buf, count, 
				   file->f_flags&O_NONBLOCK);

	case DVB_DEVICE_AUDIO_0:
                if (card->audiostate.streamSource!=AUDIO_SOURCE_MEMORY)
                        return -EPERM;
		if ( card->setup.streamtype !=  stream_PES )
                        return -EPERM;

		return margi_write_audio(card, buf, count, 
					 file->f_flags&O_NONBLOCK);

	case DVB_DEVICE_DVR_0:
                return DmxDevDVRWrite(&card->dmxdev, file, buf, count, ppos);
	default:
	        return -EOPNOTSUPP;
	}
        return 0;
}

static ssize_t 
dvbdev_read(struct dvb_device *dvbdev, int num, 
            struct file *file, char *buf, size_t count, loff_t *ppos)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        int type=num2type(card, num);

	switch (type) {
	case DVB_DEVICE_VIDEO_0:
		break;
	case DVB_DEVICE_AUDIO_0:
                break;
	case DVB_DEVICE_DEMUX_0:
                return DmxDevRead(&card->dmxdev, file, buf, count, ppos);
	case DVB_DEVICE_DVR_0:
                return DmxDevDVRRead(&card->dmxdev, file, buf, count, ppos);
	case DVB_DEVICE_CA_0:
                break;
	default:
	        return -EOPNOTSUPP;
	}
        return 0;
}




static int 
dvbdev_ioctl(struct dvb_device *dvbdev, int num, 
             struct file *file, unsigned int cmd, unsigned long arg)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        void *parg=(void *)arg;
        int type=num2type(card, num);
	uint16_t attr;

	switch (type) {
	case DVB_DEVICE_VIDEO_0:
                if (((file->f_flags&O_ACCMODE)==O_RDONLY) &&
                    (cmd!=VIDEO_GET_STATUS))
                        return -EPERM;

                switch (cmd) {

                case VIDEO_STOP:
			DecoderPause(card);                   
		        card->videostate.playState = VIDEO_STOPPED;
			if (card->videostate.videoBlank)
				VideoSetBackground(card, 1, 0, 0, 0);  
						

			return 0; 

                case VIDEO_PLAY:
                     
                        if (card->videostate.streamSource==
			    VIDEO_SOURCE_MEMORY) {
			  	if (card->videostate.playState==VIDEO_FREEZED){
					DecoderUnPause(card);	
				} else {
					DecoderUnPause(card);	
				} 
                        }
                        break;

                case VIDEO_FREEZE:
			DecoderPause(card);                   
                        break;

                case VIDEO_CONTINUE:
		        if (card->videostate.playState==VIDEO_FREEZED) {
				DecoderUnPause(card);                   
			} 
                        break;

                case VIDEO_SELECT_SOURCE:
                        card->videostate.streamSource=(videoStreamSource_t) arg;
                        break;

                case VIDEO_SET_BLANK:
                        card->videostate.videoBlank=(boolean) arg;
                        break;

                case VIDEO_GET_STATUS:
                        if(copy_to_user(parg, &card->videostate, 
					sizeof(struct videoStatus)))
                                return -EFAULT;
                        break;

                case VIDEO_GET_EVENT:
                        return -EOPNOTSUPP;

                case VIDEO_SET_DISPLAY_FORMAT:
                {
                        videoDisplayFormat_t format=(videoDisplayFormat_t) arg;
                        uint16_t val=0;
                        
                        switch(format) {
                        case VIDEO_PAN_SCAN:
                                val=VID_PAN_SCAN_PREF;
                                break;

                        case VIDEO_LETTER_BOX:
                                val=VID_VC_AND_PS_PREF;
                                break;

                        case VIDEO_CENTER_CUT_OUT:
                                val=VID_CENTRE_CUT_PREF;
                                break;

                        default:
                                return -EINVAL;
                        }

                        card->videostate.videoFormat=format;
                        return 0;
                }
                
                case VIDEO_STILLPICTURE:
		{ 
		        struct videoDisplayStillPicture pic;

                        if(copy_from_user(&pic, parg, 
                                          sizeof(struct videoDisplayStillPicture)))
                                return -EFAULT;

                        break;
		}

                case VIDEO_FAST_FORWARD:
                        if (card->videostate.streamSource !=
			    VIDEO_SOURCE_MEMORY)
				return -EPERM;
			card->videoffwd = 3;
                        break;

                case VIDEO_SLOWMOTION:
                        if (card->videostate.streamSource!=VIDEO_SOURCE_MEMORY)
                                return -EPERM;
                        card->videoslow = arg;

                        break;

                case VIDEO_GET_CAPABILITIES:
                {
			int cap=VIDEO_CAP_MPEG1|
				VIDEO_CAP_MPEG2|
				VIDEO_CAP_SYS|
				VIDEO_CAP_PROG|
				VIDEO_CAP_SPU|
				VIDEO_CAP_NAVI|
				VIDEO_CAP_CSS;

                        
                        if (copy_to_user(parg, &cap, 
					sizeof(cap)))
                                return -EFAULT;
                        break;
                }

		case VIDEO_SET_STREAMTYPE:
		{
			int f = -1;
			switch(arg){
			case VIDEO_CAP_MPEG1:
			case VIDEO_CAP_MPEG2:
				f = stream_PES;
				break;
				
			case VIDEO_CAP_SYS:
			case VIDEO_CAP_PROG:
				f = stream_PS;
				break;
				
			case VIDEO_CAP_SPU:
			case VIDEO_CAP_NAVI:
			case VIDEO_CAP_CSS:
				f = stream_DVD;
			}   
			card->setup.streamtype =  f;
	
		}			
		break;

		case VIDEO_SET_ID:
			card->setup.videoID = arg;
			DecoderPrepareVideo(card);
			break;

		case VIDEO_SET_SYSTEM:
			card->videomode = (videosystem) arg;
			SetVideoSystem(card);
			break;

		case VIDEO_SET_HIGHLIGHT:
		{
			uint8_t data1[4];
			uint8_t data2[6];
			videoHighlight_t vh;

                        if(copy_from_user(&vh, parg, sizeof(videoHighlight_t)))
                                return -EFAULT;

			data1[0] = vh.contrast1;
			data1[1] = vh.contrast2;
			data1[2] = vh.color1;
			data1[3] = vh.color2;
			data2[0] = vh.ypos & 0xFF;
			data2[1] = (uint8_t) ((vh.ypos >> 1) & 0xFF);
			data2[2] = (uint8_t) ((vh.ypos >> 2) & 0xFF);
			data2[3] = vh.xpos & 0xFF;
			data2[4] = (uint8_t) ((vh.xpos >> 1) & 0xFF);
			data2[5] = (uint8_t) ((vh.xpos >> 2) & 0xFF);
			return DecoderHighlight(card, vh.active, data1, data2);
			break;
		}

		case VIDEO_SET_SPU:
		{
			videoSPU_t spu;

                        if(copy_from_user(&spu, parg, sizeof(videoSPU_t)))
                                return -EFAULT;

			return DecoderSPUStream(card, spu.streamID, spu.active);
			break;
		}

		case VIDEO_SET_SPU_PALETTE:
		{
			videoSPUPalette_t spup;
                        
			if(copy_from_user(&spup, parg, sizeof(videoSPUPalette_t)))
                                return -EFAULT;

			return DecoderSPUPalette(card, spup.length, spup.palette);
			break;
		}

		case VIDEO_GET_NAVI:
		{
			videoNaviPack_t navi;

			navi.length = DecoderGetNavi(card, (u8 *)&(navi.data));
			if(copy_to_user(parg, &navi, sizeof(videoNaviPack_t)))
                                return -EFAULT;
		}
		break;

		case VIDEO_SET_ATTRIBUTES:
		{
			if (!card->ChannelBuffersAllocated) {
				DecoderStreamReset(card);
				MargiFlush(card);

				card->setup.streamtype = stream_DVD;
				card->setup.videoID = 0;
				DecoderPrepareVideo(card);
				DecoderPreparePS(card, 0, 0, 2, 2, 3, 1);
			}

			SetVideoAttr(card, arg);
			card->startingDVDV = 1;	
		}
		break;

                default:
                        return -ENOIOCTLCMD;
		}
                return 0;
	
	case DVB_DEVICE_AUDIO_0:
                if (((file->f_flags&O_ACCMODE)==O_RDONLY) &&
                    (cmd!=AUDIO_GET_STATUS))
                        return -EPERM;

                switch (cmd) {

                case AUDIO_STOP:
                        if (card->audiostate.streamSource!=AUDIO_SOURCE_MEMORY)
				break;
			AudioStopDecode(card);
			card->audiostate.playState=AUDIO_STOPPED;
                        break;

                case AUDIO_PLAY:
                        if (card->audiostate.streamSource!=AUDIO_SOURCE_MEMORY)
				break;
			AudioSetPlayMode(card, MAUDIO_PLAY);
                        card->audiostate.playState=AUDIO_PLAYING;
		        break;

                case AUDIO_PAUSE:
		        card->audiostate.playState=AUDIO_PAUSED;
			AudioSetPlayMode(card, MAUDIO_PAUSE);
                        break;

                case AUDIO_CONTINUE:
		        if (card->audiostate.playState==AUDIO_PAUSED) {
			        card->audiostate.playState=AUDIO_PLAYING;
				AudioSetPlayMode(card, MAUDIO_PLAY);
			} 
		        break;

                case AUDIO_SELECT_SOURCE:
                        card->audiostate.streamSource=
				(audioStreamSource_t) arg;
		        break;

                case AUDIO_SET_MUTE:
                {
			AudioMute(card, arg);
                        card->audiostate.muteState=(boolean) arg;
                        break;
		}

                case AUDIO_SET_AV_SYNC:
			card->videosync=(boolean) arg;
                        card->audiostate.AVSyncState=(boolean) arg;
                        break;

                case AUDIO_SET_BYPASS_MODE:
		        return -EINVAL;

                case AUDIO_CHANNEL_SELECT:
		        card->audiostate.channelSelect=(audioChannelSelect_t) arg;
                        
                        switch(card->audiostate.channelSelect) {
                        case AUDIO_STEREO:
                                break;

                        case AUDIO_MONO_LEFT:
                                break;

                        case AUDIO_MONO_RIGHT:
                                break;

                        default:
                                return -EINVAL;
			}
			return 0;

                case AUDIO_GET_STATUS:
                        if(copy_to_user(parg, &card->audiostate, 
					sizeof(struct audioStatus)))
                                return -EFAULT;
                        break;

                case AUDIO_GET_CAPABILITIES:
                {
                        int cap=AUDIO_CAP_LPCM|
                                AUDIO_CAP_MP1|
                                AUDIO_CAP_MP2|
				AUDIO_CAP_AC3;
                        
                        if (copy_to_user(parg, &cap, 
					sizeof(cap)))
                                return -EFAULT;
                }
		break;


		case AUDIO_SET_STREAMTYPE:
		{
			int f = -1;

			switch(arg){
			case AUDIO_CAP_DTS:
			case AUDIO_CAP_MP3:
			case AUDIO_CAP_AAC:
			case AUDIO_CAP_SDDS:
			case AUDIO_CAP_OGG:
				f = audio_none;    
				break;

			case AUDIO_CAP_LPCM:
				f = audio_LPCM;
				break;

			case AUDIO_CAP_MP1:
			case AUDIO_CAP_MP2:
				f = audio_MPEG;
				break;

			case AUDIO_CAP_AC3:
				f = audio_AC3;
				break;
			}
			
			card->setup.audioselect = (audio_type) f;
			DecoderPrepareAudio(card);
			break;
                }

		case AUDIO_SET_ID:
			if (arg < 0 || arg >32) arg = 0;
			card->setup.audioID = arg;
			arg = 0;
		case AUDIO_SET_EXT_ID:
			if (arg < 0 || arg >32) arg = 0;
			card->setup.audioIDext = arg;

			attr = card->lastaattr;
			DecoderSelectAudioID(card);
			card->lastaattr = attr;
			break;

		case AUDIO_SET_MIXER:
		        return -EINVAL;
		
		case AUDIO_SET_ATTRIBUTES:
			SetAudioAttr(card,arg);
			card->startingDVDA = ((card->setup.audioselect != audio_none)
					      && (card->setup.audioselect != 
						  audio_disable));
			break;

		
		case AUDIO_SET_KARAOKE:
		{
			break;
		}

		default:
                        return -ENOIOCTLCMD;
                }
                break;

        case DVB_DEVICE_DEMUX_0:
                return DmxDevIoctl(&card->dmxdev, file, cmd, arg);
		break;

	case DVB_DEVICE_OSD_0:
	 	{
			switch (cmd) {
			case OSD_SEND_CMD:
			{
				osd_cmd_t doc;
				
				if(copy_from_user(&doc, parg, 
						  sizeof(osd_cmd_t)))
					return -EFAULT;
				return OSD_DrawCommand(card, &doc);
			}
			default:
				return -EINVAL;
			}
			break;
		}
	default:
                return -EOPNOTSUPP;
        }
        return 0;
}

static unsigned int 
dvbdev_poll(struct dvb_device *dvbdev, int num, 
            struct file *file, poll_table * wait)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;
        int type=num2type(card, num);

	switch (type) {
        case DVB_DEVICE_DEMUX_0:
                return DmxDevPoll(&card->dmxdev, file, wait);

        case DVB_DEVICE_VIDEO_0:
                return PSpoll(file, wait);
                
        case DVB_DEVICE_AUDIO_0:
                return poll_audio(file, wait);

        case DVB_DEVICE_CA_0:
                break;

	default:
	        return -EOPNOTSUPP;
        }

        return 0;
}


static int 
dvbdev_device_type(struct dvb_device *dvbdev, unsigned int num)
{
        struct cvdv_cards *card=(struct cvdv_cards *) dvbdev->priv;

        return num2type(card, num);
}
#endif

/******************************************************************************
 * driver registration 
 ******************************************************************************/


#ifdef DVB

#define INFU 32768



static dvb_devs_t mdvb_devs = {
        9,
        { 
                DVB_DEVICE_VIDEO_0, DVB_DEVICE_AUDIO_0,
		-1, -1,
                DVB_DEVICE_DEMUX_0, DVB_DEVICE_DVR_0,
		-1, -1,
		DVB_DEVICE_OSD_0,
        },
        { INFU, INFU, INFU, INFU, INFU, 1, 1, INFU, 1 },
        { 1, 1, 1, 1, INFU, 1, 1, 1, 1}
};


static int 
dvb_start_feed(dvb_demux_feed_t *dvbdmxfeed)
{
        dvb_demux_t *dvbdmx=dvbdmxfeed->demux;
        struct cvdv_cards * card = (struct cvdv_cards *)dvbdmx->priv;
 
        if (!dvbdmx->dmx.frontend || !card)
                return -EINVAL;
	
        if (dvbdmxfeed->type == DMX_TYPE_TS) {
	        if ((dvbdmxfeed->ts_type & TS_DECODER) 
		    && (dvbdmxfeed->pes_type<DMX_TS_PES_OTHER)) {
		        switch (dvbdmx->dmx.frontend->source) {
			case DMX_MEMORY_FE: 
			        if (dvbdmxfeed->ts_type & TS_DECODER)
				       if (dvbdmxfeed->pes_type<2 && 
                                           dvbdmx->pids[0]!=0xffff &&
					    dvbdmx->pids[1]!=0xffff) {
					       
					       setup_ts2pes( &card->tsa, 
							     &card->tsv,
							     dvbdmx->pids,
							     dvbdmx->pids+1, 
							     pes_write,
							     (void *)card);

                                               dvbdmx->playing=1;
				       }
				break;
			default:
				return -EINVAL;
				break;
			}
		} 
        }
        
        if (dvbdmxfeed->type == DMX_TYPE_SEC) {
                int i;

	        for (i=0; i<dvbdmx->filternum; i++) {
		        if (dvbdmx->filter[i].state!=DMX_STATE_READY)
			        continue;
			if (dvbdmx->filter[i].type!=DMX_TYPE_SEC)
			        continue;
			if (dvbdmx->filter[i].filter.parent!=
			    &dvbdmxfeed->feed.sec)
			        continue;

			dvbdmxfeed->feed.sec.is_filtering=1;
			dvbdmx->filter[i].state=DMX_STATE_GO;
                }
	}

        return 0;
}


static int 
dvb_stop_feed(dvb_demux_feed_t *dvbdmxfeed)
{
        dvb_demux_t *dvbdmx=dvbdmxfeed->demux;
        struct cvdv_cards * card = (struct cvdv_cards *)dvbdmx->priv;
        if (!card)
                return -EINVAL;

        if (dvbdmxfeed->type == DMX_TYPE_TS) {
		if ((dvbdmxfeed->ts_type & TS_DECODER) 
		    && (dvbdmxfeed->pes_type<=1)) {
			if (dvbdmx->playing) {
				free_ipack(&card->tsa);
				free_ipack(&card->tsv);
				DecoderPause(card);
				dvbdmx->playing=0;
			}
		} 

	}
        if (dvbdmxfeed->type == DMX_TYPE_SEC) {
                int i;
		
	        for (i=0; i<dvbdmx->filternum; i++)
		        if (dvbdmx->filter[i].state==DMX_STATE_GO && 
			    dvbdmx->filter[i].filter.parent==
			    &dvbdmxfeed->feed.sec) {
			        dvbdmx->filter[i].state=DMX_STATE_READY;
                }
		
	}
        return 0;
}

static uint16_t get_pid(uint8_t *pid)
{
	uint16_t pp = 0;

	pp = (pid[0] & PID_MASK_HI)<<8;
	pp |= pid[1];

	return pp;
}


static int 
dvb_write_to_decoder(dvb_demux_feed_t *dvbdmxfeed, uint8_t *buf, size_t count)
{
        dvb_demux_t *dvbdmx=dvbdmxfeed->demux;
        struct cvdv_cards * card = (struct cvdv_cards *)dvbdmx->priv;
	uint16_t pid = 0;
	int off = 0;

	ipack *p;

        if (!card)
                return -EINVAL;
	
	pid = get_pid(buf+1);
			
	if (pid == *(card->tsa.pid)) p = &(card->tsa);
	else if (pid == *(card->tsv.pid)) p = &(card->tsv);
	else return 0;

        if (dvbdmxfeed->pes_type>1)
                return -1;
        if (!(buf[3]&0x10)) // no payload?
                return -1;

	if (count != TS_SIZE) return -1;

	if ( buf[3] & ADAPT_FIELD) {  // adaptation field?
		off = buf[4] + 1;
	}
	

	if (pid == *(card->tsa.pid)){
		MDEBUG(0,"AUDIO count: %d  off: %d\n",count,off);
		margi_write_audio(card, buf+off+4, TS_SIZE-4-off, 0);
	} else {
		MDEBUG(0,"VIDEO count: %d  off: %d\n",count,off);
		margi_write(card, buf+off+4, TS_SIZE-4-off, 0);
	}

//	ts_to_pes( p, buf); // don't need count (=188)
        return 0;
}

int dvb_register(struct cvdv_cards *card)
{
        int i,ret;
        struct dvb_device *dvbd=&card->dvb_dev;
	
	dvb_demux_t *dvbdemux = (dvb_demux_t *)&card->demux;

        if (card->dvb_registered)
                return -1;
        card->dvb_registered=1;

        card->audiostate.AVSyncState=0;
        card->audiostate.muteState=0;
        card->audiostate.playState=AUDIO_STOPPED;
        card->audiostate.streamSource=AUDIO_SOURCE_MEMORY;
        card->audiostate.channelSelect=AUDIO_STEREO;
        card->audiostate.bypassMode=0;

        card->videostate.videoBlank=0;
        card->videostate.playState=VIDEO_STOPPED;
        card->videostate.streamSource=VIDEO_SOURCE_MEMORY;
        card->videostate.videoFormat=VIDEO_FORMAT_4_3;
        card->videostate.displayFormat=VIDEO_CENTER_CUT_OUT;

        // init and register demuxes
	memcpy(card->demux_id, "demux0_0", 9);
        card->demux_id[7] = 1+0x30;
        dvbdemux->priv = (void *) card;
	dvbdemux->filternum = 32;
	dvbdemux->feednum = 32;
	dvbdemux->start_feed = dvb_start_feed;
	dvbdemux->stop_feed = dvb_stop_feed;
	dvbdemux->write_to_decoder = dvb_write_to_decoder;
                
	dvbdemux->dmx.vendor="CIM";
	dvbdemux->dmx.model="sw";
	dvbdemux->dmx.id=card->demux_id;
	dvbdemux->dmx.capabilities=(DMX_TS_FILTERING|
				    DMX_SECTION_FILTERING|
				    DMX_MEMORY_BASED_FILTERING);
	
	DvbDmxInit(&card->demux);

	card->dmxdev.filternum=32;
	card->dmxdev.demux=&dvbdemux->dmx;
	card->dmxdev.capabilities=0;

	DmxDevInit(&card->dmxdev);
        
        card->mem_frontend.id="mem_frontend";
        card->mem_frontend.vendor="memory";
        card->mem_frontend.model="sw";
        card->mem_frontend.source=DMX_MEMORY_FE;
        ret=dvbdemux->dmx.add_frontend(&dvbdemux->dmx, 
                                        &card->mem_frontend);
        if (ret<0)
                return ret;
	ret=dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, 
					   &card->mem_frontend);
        if (ret<0)
                return ret;

        // init and register dvb device structure
        dvbd->priv=(void *) card;
        dvbd->open=dvbdev_open;
        dvbd->close=dvbdev_close;
        dvbd->write=dvbdev_write;
        dvbd->read=dvbdev_read;
        dvbd->ioctl=dvbdev_ioctl;
        dvbd->poll=dvbdev_poll;
        dvbd->device_type=dvbdev_device_type;
        
        for (i=0; i<DVB_DEVS_MAX; i++) 
                card->users[i]=card->writers[i]=0;

        card->dvb_devs=0;
	card->dvb_devs=&mdvb_devs;
	
        return dvb_register_device(dvbd);
}

void dvb_unregister(struct cvdv_cards *card)
{
	dvb_demux_t *dvbdemux=&card->demux;

        dvbdemux->dmx.close(&dvbdemux->dmx);
        dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &card->mem_frontend);
        DmxDevRelease(&card->dmxdev);
        DvbDmxRelease(&card->demux);
        dvb_unregister_device(&card->dvb_dev);
}
#endif
