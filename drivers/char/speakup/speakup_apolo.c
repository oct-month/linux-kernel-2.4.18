/*
 * speakup_apolo.c for linux kernels 2.2.x and speakup
 * 
 * author: Kirk Reiser <kirk@braille.uwo.ca>

    Copyright (C) 2000  Kirk Reiser.

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


/* These routines are written to control the Apolo serial speech
   synthesizer.  They are not ment to be thought of as a device driver
   in that they do not register themselves as a chr device and there
   is no file_operations structure.  They are strictly to provide an
   interface to the apolo from the speakup screen review package.  */

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
#include <linux/serial.h> /* for rs_table, serial constants and
				serial_uart_config */
#include <linux/serial_reg.h>  /* for more serial constants */
#if (LINUX_VERSION_CODE >= 0x20332)  /* v 2.3.50+ */
#include <linux/serialP.h>	/* for struct serial_state */
#endif
#include <asm/serial.h>
#include <linux/speakup.h>

#define SYNTH_CLEAR 0x18
#define PROCSPEECH '\r'
#define SPK_TIMEOUT 100			/* buffer timeout in ms */
#define NUM_DISABLE_TIMEOUTS 3		/* disable synth if n timeouts */
#define SPK_SERIAL_TIMEOUT 1000000 /* countdown values for serial timeouts */
#define SPK_XMITR_TIMEOUT 1000000 /* countdown values transmitter/dsr timeouts */
#define SPK_LO_TTY 0	/* check ttyS0 ... ttyS3 */
#define SPK_HI_TTY 3

static int apolo_alive = 0;

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static struct serial_state rs_table[] = {
	SERIAL_PORT_DFNS	/* Defined in serial.h */
};

static int timeouts = 0;	/* sequential number of timeouts */

static int wait_for_xmitr(void)
{
int check, tmout = SPK_XMITR_TIMEOUT;

  if ((apolo_alive) && (timeouts >= NUM_DISABLE_TIMEOUTS)) {
    apolo_alive = 0; 
    timeouts = 0;
    return 0; 
  }

	/* holding register empty? */
  do {
    check = inb(synth_port_tts + UART_LSR);
    if (--tmout == 0) {
      printk("APOLO:  timed out\n");
      timeouts++;
      return 0;
    }
  } while ((check & BOTH_EMPTY) != BOTH_EMPTY);

  tmout = SPK_XMITR_TIMEOUT;
	/* CTS */
	do {
		check = inb(synth_port_tts + UART_MSR);
    		if (--tmout == 0) {
    			timeouts++;
    			return 0;
    		}
	} while ((check & UART_MSR_CTS) != UART_MSR_CTS);

  timeouts = 0;
  return 1;
}

static inline int spk_serial_out(const char ch) 
{
 // int timer = 9000000; 

  if (apolo_alive && synth_port_tts) {
    if (wait_for_xmitr()) {
    outb(ch, synth_port_tts);
    /*while (inb(synth_port_tts+UART_MSR) & UART_MSR_CTS) if (--timer == 0) break;*/
    /*    outb(UART_MCR_DTR, synth_port_tts + UART_MCR);*/
    return 1;
    }
  }
  return 0;
}

#if 0
static unsigned char __init spk_serial_in(void) 
{
int c, lsr, tmout = SPK_SERIAL_TIMEOUT;

  do {
    lsr = inb(synth_port_tts + UART_LSR);
    if (--tmout == 0) return 0xff;
  } while (!(lsr & UART_LSR_DR));
  c = inb(synth_port_tts + UART_RX);
  return (unsigned char) c;
}
#endif

static void do_catch_up(unsigned long data)
{
  unsigned long jiff_in = jiffies;

synth_stop_timer();
  while (synth_sent_bytes < synth_queued_bytes)
    {
    if (!spk_serial_out(*(synth_buffer+synth_sent_bytes))) {
      outb(UART_MCR_DTR, synth_port_tts + UART_MCR);
      outb(UART_MCR_DTR | UART_MCR_RTS, synth_port_tts + UART_MCR);
      synth_delay(synth_full_time);
      return;
    }
    synth_sent_bytes++;
    if (jiffies >= jiff_in+synth_jiffy_delta && (synth_sent_bytes > 10))  
	{
	  spk_serial_out(PROCSPEECH);
	  synth_delay(synth_delay_time); 
		return; 
	}
    }

synth_sent_bytes = synth_queued_bytes = 0;
spk_serial_out(PROCSPEECH);
synth_timer_active = 0;
if (waitqueue_active(&synth_sleeping_list))
  wake_up_interruptible(&synth_sleeping_list);
}

static void synth_write_tts(char ch)
{
  if (!apolo_alive) return;
  /* if (ch < 0x00) return; // don't want unprintable chars */
  if (ch == 0x0a) /* turn lf into <cr> to force talking. */
    ch = PROCSPEECH;
  if (ch == SYNTH_CLEAR) 		/* clear all and wake sleeping */
    {
      spk_serial_out(ch);
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

static inline void synth_immediate_tts(const char *buf, short count)
{
  while (count--) spk_serial_out(*buf++);
return;
}

static int __init serprobe(int index)
{
	struct serial_state *ser=NULL;
	unsigned char test=0;

	if (synth_port_tts) {
	  for (test=0; test <= SPK_HI_TTY; test++)
	    if ( (rs_table+test)->port == synth_port_tts) {
	      ser = rs_table+test;
	      break;
	    }
	} else 	ser = rs_table + index;

	if (synth_request_region(ser->port,8))
		return -1;

	initialize_uart(ser);
	/* If we read 0xff from the LSR, there is no UART here. */
	if (inb (ser->port + UART_LSR) == 0xff) {
	  synth_release_region(ser->port,8);
	  return -1;
	}
	outb(0x0d, ser->port); /* wake it up if older BIOS */
	mdelay(1);

	apolo_alive = 1;
	if (synth_port_tts) return 0;
	synth_port_tts = ser->port;

	/* check for apolo now... */
	if (spk_serial_out(0x18)) return 0;
	/*	spk_serial_out('@');
	spk_serial_out('E');
	spk_serial_out('r');
	mdelay(10);
	if ((test = spk_serial_in()) == 0x15) {
	return 0;*/
	else printk("port %x failed\n", synth_port_tts);

	synth_release_region(ser->port,8);
	timeouts = apolo_alive = synth_port_tts = 0;
        return -1;
}

static int __init synth_dev_probe(void)
{
int i;

  printk("Probing for Apolo.\n");
  if (synth_port_tts)
    printk("Probe forced to 0x%x by kernel command line\n", synth_port_tts);
  for (i=SPK_LO_TTY; i <= SPK_HI_TTY; i++) {
    if (serprobe(i) == 0) break; /* found it */
  }

  if (apolo_alive) {
    /* found 'em */
    printk("Apolo:  %03x-%03x, Driver version %s,\n",
	   synth_port_tts, synth_port_tts + 7, synth->version);
    synth_immediate_tts(synth->init, strlen(synth->init));
    return 0;
  }

  printk("Apolo:  not found\n");
  return -ENODEV;
}

static int synth_alive(void)
{
  if (apolo_alive)
    return 1; /* already on */
  else if ((!apolo_alive) && (synth_port_tts)) {
    if (wait_for_xmitr() > 0) { /* restart */
      apolo_alive = 1;
      synth_write(synth->reinit, strlen(synth->reinit));
      return 2;  /* reenabled */
    } else printk("Apolo: can't restart synth\n");
  }
  return 0;
}

static const char init_string[] = "@R0@D0@K1@W5\rApolo found\r";
static const char reinit_string[] = "@R0@D0@K1@W5\rApolo Restarted\r";

static struct spk_variable vars[] =
{{"flush", "\x18", "_", (BUILDER|HARD_DIRECT|NO_USER), "*"},
 {"pitch", "8", "@F_", HARD_DIRECT, "0123456789abcdef"},
 {"caps_start", "cap ", "_", 0, "*"},
 {"caps_stop", "", "_", 0, "*"},
 {"rate", "5", "@W_", (NUMERIC|HARD_DIRECT|USE_RANGE), "1,9"},
 {"voice", "1", "@V_", (NUMERIC|HARD_DIRECT|USE_RANGE), "1,6"},
 {"volume", "A", "@A_", HARD_DIRECT, "0123456789abcdef"},
 {"language", "1", "@=_,", (NUMERIC|HARD_DIRECT|USE_RANGE), "1,4"},
 END_VARS};

static char *config[] =
{"\x18", "@F8", "cap ", "", "@W5", "@V1", "@AA", "@=1,"};

struct spk_synth synth_apolo = {"apolo", "Version-0.10", "apolo",
				init_string, reinit_string, 500, 50, 5, 5000,
				vars, config, 0, synth_dev_probe, do_catch_up,
				synth_write_tts, synth_alive};
