/*
 * speakup_acntpc.c - Accent PC driver for Linux kernel 2.3.x and speakup
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

/* These routines are written to control the Accent PC speech
   synthesizer by Aicom.  They are not ment to be thought of as a device driver
   in that they do not register themselves as a chr device and there
   is no file_operations structure.  They are strictly to provide an
   interface to the Accent-pc from the speakup screen review package.  
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
#include <linux/version.h>

#include <linux/speakup.h>
#include "speakup_acnt.h" /* local header file for Accent values */

#define synth_readable() (inb_p(synth_port_control) & SYNTH_READABLE) 
#define synth_writable() (inb_p(synth_port_control) & SYNTH_WRITABLE) 
#define synth_full() (inb_p(synth_port_tts) == 'F') 
static int synth_port_control;
static unsigned int synth_portlist[] =
    { 0x2a8, 0 };
                            /* 160 ms delay and ctrl-x as flush */

static void do_catch_up(unsigned long data)
{
unsigned long jiff_in = jiffies;

synth_stop_timer();
while ((synth_sent_bytes < synth_queued_bytes) && !synth_full())
  {
    while (synth_writable()); 
    outb_p(*(synth_buffer+synth_sent_bytes++), synth_port_tts);
    if (jiffies >= jiff_in+synth_jiffy_delta && *(synth_buffer+synth_sent_bytes-1) == ' ') { 
      while (synth_writable());
      outb_p('\r', synth_port_tts);
      synth_delay(synth_delay_time); 
      return; 
    }
  }
if (synth_full())
  {
    synth_delay(synth_full_time);
    return;
  }
while (synth_writable());
outb_p('\r', synth_port_tts);
synth_sent_bytes = synth_queued_bytes = synth_timer_active = 0;
if (waitqueue_active(&synth_sleeping_list))
  wake_up_interruptible(&synth_sleeping_list);
return;
}

static void synth_write_tts(char ch)
{

  if (ch < 0x00) return; /* don't want unprintable chars */
  if (ch == 0x0a) /* turn lf into <cr> to force talking. */
    ch = 0x0D;
  if (ch == SYNTH_CLEAR) {		/* clear all and wake sleeping */
    outb_p(ch, synth_port_tts);	/* output to data port */
    if (waitqueue_active(&synth_sleeping_list))
      wake_up_interruptible(&synth_sleeping_list);
    synth_timer_active = synth_queued_bytes = synth_sent_bytes = 0;
    return;
  }
      synth_buffer_add(ch);
      if (synth_buffering) return;
      if (synth_timer_active == 0) synth_delay( synth_trigger_time );
      return;
}

static int __init synth_dev_probe(void)
{
unsigned int port_val = 0;
int i = 0;

  printk("Probing for Accent PC.\n");
  if (synth_port_tts) {
    printk("probe forced to %x by kernel command line\n", synth_port_tts);

    if (synth_request_region(synth_port_tts-1, SYNTH_IO_EXTENT)) {
      printk("sorry, port already reserved\n");
      return -EBUSY;
    }
    
    port_val = inw(synth_port_tts-1);
    synth_port_control = synth_port_tts-1;
  } else {
    for(i=0; synth_portlist[i]; i++) {
      if (synth_request_region(synth_portlist[i], SYNTH_IO_EXTENT)) {
       	printk("request_region:  failed with 0x%x, %d\n",
        	synth_portlist[i], SYNTH_IO_EXTENT);
	continue;
      }

      port_val = inw(synth_portlist[i]);
      if ((port_val &= 0xfffc) == 0x53fc) { /* 'S' and out&input bits */
	synth_port_control = synth_portlist[i];
	synth_port_tts = synth_port_control+1;
	break;
      }
    }
  }

  if ((port_val &= 0xfffc) != 0x53fc) { /* 'S' and out&input bits */
    printk("Accent PC:  not found\n");
    synth_release_region(synth_portlist[i], SYNTH_IO_EXTENT);
    return -ENODEV;
  }

  printk("Accent-PC:  %03x-%03x, driver version %s,\n",
	 synth_port_control,	synth_port_control+SYNTH_IO_EXTENT-1, 
	 synth->version);
  synth_write(synth->init, strlen(synth->init));
  return 0;
}

static int synth_alive(void)
{
return 1;
}

static const char init_string[] = "\x1b=X \x1bOi\x1bT2\x1b=M\x1bN1\x1bR9\nAccent PC Found\n";
static const char reinit_string[] = "";

static struct spk_variable vars[] =
{{"flush", "\x18", "_", (BUILDER|HARD_DIRECT|USE_RANGE|NO_USER), "*"},
 {"pitch", "5", "\x1bP_", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 {"caps_start", "\x1bP8", "_", 0, "*"},
 {"caps_stop", "\x1bP5", "_", 0, "*"},
 {"rate", "9", "\x1bR_", HARD_DIRECT, "0123456789abcdefgh"},
 {"tone", "5", "\x1bV_", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 {"volume", "9", "\x1b\x41_", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9"},
 END_VARS};

static char *config[] =
{"\x18", "\x1bP5", "\x1bP8", "\x1bP5", "\x1bR9", "\x1bV5", "\x1b\x41\x39"};

struct spk_synth synth_acntpc = {"acntpc", "Version-0.9", "accent_pc",
				 init_string, reinit_string, 500, 50, 5, 1000,
				 vars, config, 0, synth_dev_probe, do_catch_up,
				 synth_write_tts, synth_alive};
