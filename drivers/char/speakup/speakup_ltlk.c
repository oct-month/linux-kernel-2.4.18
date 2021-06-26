/*
 * speakup_ltlk.c - LiteTalk driver for Linux kernel 2.3.x and speakup
 * 
 * author: Kirk Reiser <kirk@braille.uwo.ca> and Andy Berdan <ed@facade.dhs.org>

    Copyright (C) 1998-99  Kirk Reiser, and Andy Berdan

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

    Andy Berdan <ed@facade.dhs.org>
    3-337 Wharncliffe Rd. N  London, Ontario, Canada N6G 1E4
    */


/* These routines are written to control the Lite Talk speech
   synthesizer.  They are not ment to be thought of as a device driver
   in that they do not register themselves as a chr device and there
   is no file_operations structure.  They are strictly to provide an
   interface to the LiteTalk from the speakup screen review package.  
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
#include <linux/vt_kern.h> 	/* kd_mksound */
#include <linux/init.h> 	/* for __init */
#include <linux/version.h>
#include <linux/serial.h> 	/* for rs_table, serial constants & 
				   serial_uart_config */
#include <linux/serial_reg.h> 	/* for more serial constants */
#if (LINUX_VERSION_CODE >= 0x20300)  /* v 2.3.x */
#include <linux/serialP.h>	/* for struct serial_state */
#endif
#include <asm/serial.h>
#include <linux/speakup.h>

#include "speakup_dtlk.h" /* local header file for LiteTalk values */

#define synth_full() ( !(inb(synth_port_tts + UART_MSR) & UART_MSR_CTS) )
#define PROCSPEECH 0x00 /* synth process speech char */
#define SPK_SERIAL_TIMEOUT 1000000 /* countdown values for serial timeouts */
#define SPK_XMITR_TIMEOUT 3000000 /* countdown values transmitter/dsr timeouts */
#define SPK_LO_TTY 0	/* check ttyS0 ... ttyS3 */
#define SPK_HI_TTY 3
#define NUM_DISABLE_TIMEOUTS 3	/* # of timeouts permitted before disable */

static int ltlk_alive = 0;

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static struct serial_state rs_table[] __initdata = {
	SERIAL_PORT_DFNS
};

static inline int wait_for_xmitr(void)
{
	static int timeouts = 0;	/* sequential number of timeouts */
	int check, tmout = SPK_XMITR_TIMEOUT;

	if ((ltlk_alive) && (timeouts >= NUM_DISABLE_TIMEOUTS)) {
		ltlk_alive = 0; 
		timeouts = 0;
		return 0; 
	}

	/* holding register empty? */

	do {
		check = inb(synth_port_tts + UART_LSR);
    		if (--tmout == 0) {
    			printk("LiteTalk:  register timed out\n");
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
	if (ltlk_alive && synth_port_tts) {
		if (wait_for_xmitr()) {
			outb(ch, synth_port_tts);
			return 1;
		}
	}
	return 0;
}

static unsigned char __init spk_serial_in(void)
{
	int c, lsr, tmout = SPK_SERIAL_TIMEOUT;

	do {
		lsr = inb(synth_port_tts + UART_LSR);
		if (--tmout == 0) {
			printk("time out while waiting for input.\n");
			return 0xff; 
		}
	} while ((lsr & UART_LSR_DR) != UART_LSR_DR);
	c = inb(synth_port_tts + UART_RX);
	return (unsigned char) c;
}

static void do_catch_up(unsigned long data)
{
	unsigned long jiff_in = jiffies;

	synth_stop_timer();
	while (synth_sent_bytes < synth_queued_bytes)
	{
		if (!spk_serial_out(*(synth_buffer+synth_sent_bytes))) {
			synth_delay(synth_full_time);
			return;
		}
		synth_sent_bytes++;
		if (jiffies >= jiff_in+synth_jiffy_delta
		    && *(synth_buffer+synth_sent_bytes-1) == ' ') { 
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
	if (!ltlk_alive) return;
	if (ch < 0x00) return; /* don't want unprintable chars */
	if (ch == 0x0a) /* turn lf into <cr> to force talking. */
		ch = PROCSPEECH;

	/* clear all and wake sleeping */
	if (ch == SYNTH_CLEAR) {
		spk_serial_out(ch);
		if (waitqueue_active(&synth_sleeping_list))
			wake_up_interruptible(&synth_sleeping_list);
		synth_timer_active = synth_queued_bytes = synth_sent_bytes = 0;
		return;
	}

	synth_buffer_add(ch);
	if (synth_buffering) return;
	if (synth_timer_active == 0) synth_delay( synth_trigger_time );
}

static inline void synth_immediate_tts(const char *buf, short count)
{
	while (count--) spk_serial_out(*buf++);
	return;
}

/* interrogate the LiteTalk and print its settings */
static void __init synth_interrogate(void)
{
	unsigned char *t, i;
	unsigned char buf[50], rom_v[20];

	synth_immediate_tts("\x18\x01?", 3);

	for (i = 0; i < 50; i++)
	{
		buf[i] = spk_serial_in();
		if (i > 2 && buf[i] == 0x7f) break;
	}

	t = buf+2;
	i = 0;
	while (*t != '\r')
	{
		rom_v[i] = *t;
		t++; i++;
	}
	rom_v[i] = 0;

	printk("LiteTalk: ROM version: %s\n", rom_v);
	return; 
}

static int __init serprobe(int index)
{
	struct serial_state *ser = NULL;
	unsigned char test=0;

	if (synth_port_tts) {
		for (test=0; test <= SPK_HI_TTY; test++)
			if ( (rs_table+test)->port == synth_port_tts) {
				ser = rs_table+test;
				break;
			}
	} else 	ser = rs_table + index;

	/* don't do output yet... */
	if (synth_request_region(ser->port,8))
		return -1;

	initialize_uart(ser);
	/* If we read 0xff from the LSR, there is no UART here. */
	if (inb (ser->port + UART_LSR) == 0xff) {
		// printk("initial test = %x\n", test);
		synth_release_region(ser->port,8);
		return -1;
	}
	outb(0, ser->port);
	mdelay(1);
	outb('\r', ser->port);

	ltlk_alive = 1;
	/* ignore any error results, if port was forced */
	if (synth_port_tts) {
		return 0;
	}

	synth_port_tts = ser->port;

	/* check for device... */
	if (spk_serial_out(SYNTH_CLEAR)) return 0;	
	/*spk_serial_out('@');
	  mdelay(100);

	  spk_serial_out(0);

	  if ((spk_serial_in() == ('C'+ 0x80))
	  && ((test = spk_serial_in()) > ('A' + 0x80))
	  && (test < ('Z' + 0x80))
	  && (spk_serial_in() == 0x8d) ) 
	  return 0;  */

	synth_release_region(ser->port,8);
	ltlk_alive = synth_port_tts  = 0; /* try next port */
	return -1;
}

static int __init synth_dev_probe(void)
{
	int i;

	printk("Probing for LiteTalk.\n");
	if (synth_port_tts)
		printk("Probe forced to 0x%x by kernel command line\n", synth_port_tts);

	for (i=SPK_LO_TTY; i <= SPK_HI_TTY; i++) {
		if (serprobe(i) == 0) break; /* found it */
	}

	if (ltlk_alive) {
		/* found 'em */
		synth_interrogate();
		printk("LiteTalk: at  %03x-%03x, driver %s\n",
		       synth_port_tts, synth_port_tts + 7, synth->version);
		synth_write(synth->init, strlen(synth->init));
		return 0;
	}

	printk("LiteTalk:  not found\n");
	return -ENODEV;
}

static int synth_alive(void) {

	if (ltlk_alive) {
		return 1; /* already on */
	} else {
		if ((!ltlk_alive) && (synth_port_tts)) {
			if (wait_for_xmitr() > 0) { /* restart */
				ltlk_alive = 1;
				synth_write(synth->reinit, strlen(synth->reinit));
				return 2;  /* reenabled */
			} else printk("LiteTalk: can't restart synth\n");
		}
	}
	return 0;
}

static const char init_string[] = "\01@light talk found\n\x01\x31y\n\0";
static const char reinit_string[] = "\01@light talk restarted\n\x01\x31y\n\0";
/* 200ms delay */

static struct spk_variable vars[] =
{
	{ "flush", "\x18\x20", "_", (BUILDER|HARD_DIRECT|USE_RANGE|NO_USER), "*" },
	{ "pitch", "50", "\x01_p", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,99" },
	{ "caps_start", "\x01+35p", "_", 0, "*" },
	{ "caps_stop", "\x01-35p", "_", 0, "*" },
	{ "rate", "5", "\x01_s", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9" },
	{ "tone", "1", "\x01_x", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,2" },
	{ "volume", "5", "\x01_v", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9" },
	{ "voice", "0", "\x01_o", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,7" },
	{ "ffreq", "5", "\x01_f", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,9" },
	{ "punct", "7", "\x01_b", (NUMERIC|HARD_DIRECT|USE_RANGE), "0,15" },
	END_VARS
};

static char *config[] =
{ "\x18\x20", "\x01\x35\x30p", "\x01\x38\x35p", "\x01\x35\x30p", "\x01\x35s",
  "\x01\x31x", "\x01\x35v", "\x01\x30o", "\x01\x35\x66", "\x01\x37\x62" };

struct spk_synth synth_ltlk = { "ltlk", "Version-0.13", "litetalk",
				init_string, reinit_string, 500, 50, 5, 5000,
				vars, config, 0, synth_dev_probe, do_catch_up,
				synth_write_tts, synth_alive };
