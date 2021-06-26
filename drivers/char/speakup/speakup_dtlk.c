/*
 * speakup_dtlk.c - DoubleTalk PC driver for Linux kernel 2.3.x and speakup
 * 
 * author: Kirk Reiser <kirk@braille.uwo.ca>

    Copyright (C) 1998-99  Kirk Reiser.

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Kirk Reiser <kirk@braille.uwo.ca>
    261 Trott dr. London, Ontario, Canada. N6G 1B6
    */


/* These routines are written to control the Double Talk PC speech
   synthesizer.  They are not ment to be thought of as a device driver
   in that they do not register themselves as a chr device and there
   is no file_operations structure.  They are strictly to provide an
   interface to the DoubleTalk from the speakup screen review package.  
*/

#define KERNEL
#include <linux/config.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>		/* for verify_area */
#include <linux/errno.h>	/* for -EBUSY */
#include <linux/ioport.h>	/* for check_region, request_region */
#include <linux/delay.h>	/* for loops_per_sec */
#include <asm/segment.h>	/* for put_user_byte */
#include <asm/io.h>		/* for inb_p, outb_p, inb, outb, etc... */
#include <linux/wait.h>		/* for wait_queue */
#include <linux/vt_kern.h> /* kd_mksound */
#include <linux/init.h> /* for __init */
#include <linux/version.h> /* need to know if 2.2.x or 2.3.x */

#include <linux/speakup.h>
#include "speakup_dtlk.h" /* local header file for DoubleTalk values */

#define synth_readable() ((synth_status = inb_p(synth_port_tts)) & TTS_READABLE) 
#define synth_full() ((synth_status = inb_p(synth_port_tts)) & TTS_ALMOST_FULL) 
static int synth_port_lpc;
static unsigned int synth_portlist[] =
    { 0x25e, 0x29e, 0x2de, 0x31e, 0x35e, 0x39e, 0 };
/* 200ms delay */
static unsigned char synth_status = 0; /* speed up some checks which are done regularly */

static  inline void spk_out(const char ch)
{
int tmout = 100000;

  while (((synth_status = inb_p(synth_port_tts)) & TTS_WRITABLE) == 0); 
outb_p(ch, synth_port_tts);
      while ((((synth_status = inb_p(synth_port_tts)) & TTS_WRITABLE) != 0)
	     && (--tmout != 0) );
}

static void do_catch_up(unsigned long data)
{
unsigned long jiff_in = jiffies;

synth_stop_timer();
synth_status = inb_p(synth_port_tts);
while ((synth_sent_bytes < synth_queued_bytes) && !(synth_status & TTS_ALMOST_FULL))
  {
    spk_out(*(synth_buffer+synth_sent_bytes++));
    if (jiffies >= jiff_in+synth_jiffy_delta && *(synth_buffer+synth_sent_bytes-1) == ' ') { 
      spk_out(0x00);
      synth_delay(synth_delay_time); 
      return; 
    }
  }
if (synth_status & TTS_ALMOST_FULL)
  {
    synth_delay(synth_full_time);
    return;
  }
spk_out(0x00);
synth_sent_bytes = synth_queued_bytes = 0;
synth_timer_active = 0;
if (waitqueue_active(&synth_sleeping_list))
  wake_up_interruptible(&synth_sleeping_list);
}

static void synth_write_tts(char ch)
{

  if (ch < 0x00) return; /* don't want unprintable chars */
  if (ch == 0x0a) /* turn lf into NULL to force talking. */
    ch = 0x00;
  if (ch == SYNTH_CLEAR) {		/* clear all and wake sleeping */
    outb_p(ch, synth_port_tts);			/* output to TTS port */
    while (((synth_status = inb_p(synth_port_tts)) & TTS_WRITABLE) != 0);
    if (waitqueue_active(&synth_sleeping_list))
      wake_up_interruptible(&synth_sleeping_list);
    synth_queued_bytes = synth_sent_bytes = 0;
    return;
  }

  synth_buffer_add(ch);
  if (synth_buffering) return;
  if (synth_timer_active == 0) synth_delay( synth_trigger_time );
}

static inline void synth_immediate_tts(const char *buf, short count)
{
char ch;

  while (count--) {
    ch = *buf++;
    if (ch != SYNTH_CLEAR)
    spk_out(ch);			/* output to TTS port */
  }
}

static char __init synth_read_tts(void)
{
unsigned char ch;

  while (((synth_status = inb_p(synth_port_tts)) & TTS_READABLE) == 0);
  ch = synth_status & 0x7f;
  outb_p(ch, synth_port_tts);
  while ((inb_p(synth_port_tts) & TTS_READABLE) != 0);
return (char) ch;
}

/* interrogate the DoubleTalk PC and return its settings */
static struct synth_settings * __init synth_interrogate(void)
{
unsigned char *t;
static char buf[sizeof(struct synth_settings) + 1];
int total, i;
static struct synth_settings status;

  synth_immediate_tts("\x18\x01?", 3);

  for (total = 0, i = 0; i < 50; i++)
    {
      buf[total] = synth_read_tts();
      if (total > 2 && buf[total] == 0x7f) break;
      if (total < sizeof(struct synth_settings)) total++;
    }

  t = buf;
  status.serial_number = t[0] + t[1]*256; /* serial number is little endian */
  t += 2;

  i = 0;
  while (*t != '\r')
    {
      status.rom_version[i] = *t;
      if (i < sizeof(status.rom_version)-1) i++;
      t++;
    }
  status.rom_version[i] = 0;
  t++;

  status.mode = *t++;
  status.punc_level = *t++;
  status.formant_freq = *t++;
  status.pitch = *t++;
  status.speed = *t++;
  status.volume = *t++;
  status.tone = *t++;
  status.expression = *t++;
  status.ext_dict_loaded = *t++;
  status.ext_dict_status = *t++;
  status.free_ram = *t++;
  status.articulation = *t++;
  status.reverb = *t++;
  status.eob = *t++;
  return &status;
}

static int __init synth_dev_probe(void)
{
unsigned int port_val = 0;
int i = 0;
struct synth_settings *sp;

  printk("Probing for DoubleTalk.\n");
  if (synth_port_tts) {
    printk("probe forced to %x by kernel command line\n", synth_port_tts);
    if (synth_request_region(synth_port_tts-1, SYNTH_IO_EXTENT)) {
      printk("sorry, port already reserved\n");
      return -EBUSY;
    }
    port_val = inw(synth_port_tts-1);
    synth_port_lpc = synth_port_tts-1;
  }
  else {
    for(i=0; synth_portlist[i]; i++) {
      if (synth_request_region(synth_portlist[i], SYNTH_IO_EXTENT))
	continue;
      port_val = inw(synth_portlist[i]);
      if ((port_val &= 0xfbff) == 0x107f) {
	synth_port_lpc = synth_portlist[i];
	synth_port_tts = synth_port_lpc+1;
	break;
      }
      synth_release_region(synth_portlist[i], SYNTH_IO_EXTENT);
    }
  }

  if ((port_val &= 0xfbff) != 0x107f) {
    printk("DoubleTalk PC:  not found\n");
    return -ENODEV;
  }

  while (inw_p(synth_port_lpc) != 0x147f ); /* wait until it's ready */
  sp = synth_interrogate();
  printk("DoubleTalk PC:  %03x-%03x, ROM version %s,\n"
	 "DoubleTalk PC:  serial number %u, driver: %s\n",
	 synth_port_lpc, synth_port_lpc+SYNTH_IO_EXTENT - 1,
	 sp->rom_version, sp->serial_number, synth->version);
  synth_immediate_tts(synth->init, strlen(synth->init));
  return 0;
}

static int synth_alive(void) {
	return 1;	/* I'm *INVINCIBLE* */
}

static const char init_string[] = "\x01@doubletalk found\n\x01\x31y\n";
static const char reinit_string[] = "\x01@doubletalk found\n\x01\x31y\n";

static struct spk_variable vars[] =
{{"flush", "\x18", "_", (BUILDER|HARD_DIRECT|USE_RANGE|NO_USER), "*"},
 {"pitch", "50", "\x01_p", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,99"},
 {"caps_start", "\x01+35p", "_", 0, "*"},
 {"caps_stop", "\x01-35p", "_", 0, "*"},
 {"rate", "5", "\x01_s", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 {"tone", "1", "\x01_x", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,2"},
 {"volume", "5", "\x01_v", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 {"voice", "0", "\x01_o", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,7"},
 {"ffreq", "5", "\x01_f", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 {"punct", "7", "\x01_b", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,15"},
 END_VARS};

static char *config[] =
{"\x18", "\x01\x35\x30p", "\x01\x38\x35p", "\x01\x35\x30p", "\x01\x35s",
 "\x01\x31x", "\x01\x35v", "\x01\x30o", "\x01\x35\x66", "\x01\x37\x62"};

struct spk_synth synth_dtlk = {"dtlk", "Version-0.13", "doubletalk_pc",
			       init_string, reinit_string, 500, 50, 5, 1000,
			       vars, config, 0, synth_dev_probe, do_catch_up,
			       synth_write_tts, synth_alive};
