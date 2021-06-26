/*
 * speakup_acntsa.c - Accent SA driver for Linux kernel 2.3.x and speakup
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

/* These routines are written to control the Accent SA speech
   synthesizer by Aicom.  They are not ment to be thought of as a device driver
   in that they do not register themselves as a chr device and there
   is no file_operations structure.  They are strictly to provide an
   interface to the Accent-SA from the speakup screen review package.  
*/

#define KERNEL
#include <linux/config.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>		/* for verify_area */
#include <linux/errno.h>	/* for -EBUSY */
#include <linux/ioport.h>	/* for check_region, request_region */
#include <linux/delay.h>	/* for loops_per_sec */
/* #include <linux/poll.h> */
#include <asm/segment.h>	/* for put_user_byte */
/* #include <asm/uaccess.h> */
#include <asm/io.h>		/* for inb_p, outb_p, inb, outb, etc... */
#include <linux/wait.h>		/* for wait_queue */
#include <linux/vt_kern.h>	/* kd_mksound */
#include <linux/init.h>		/* for __init */
#include <linux/version.h>
#include <linux/serial.h>	/* for rs_table, serial constants & 
				   serial_uart_config */
#include <linux/serial_reg.h>	/* for more serial constants */
#if (LINUX_VERSION_CODE >= 0x20300)	/* v 2.3.x */
#include <linux/serialP.h>	/* for struct serial_state */
#endif
#include <asm/serial.h>

#include <linux/speakup.h>
#include "speakup_acnt.h"	/* local header file for Accent values */

#define synth_full() (inb_p(synth_port_tts) == 'F')
#define SPK_SERIAL_TIMEOUT 1000000	/* countdown values for serial timeouts */
#define SPK_XMITR_TIMEOUT 1000000	/* countdown values transmitter/dsr timeouts */
#define SPK_LO_TTY 0		/* check ttyS0 ... ttyS3 */
#define SPK_HI_TTY 3
#define NUM_DISABLE_TIMEOUTS 3	/* # of timeouts permitted before disable */

static int acntsa_alive = 0;
			    /* 160 ms delay and ctrl-x as flush */
#ifdef OLDSUCKYWAY
#define INIT_LEN 45
static char INIT_STRING[] =
    "\x1bT2\x1b=M\x1bN1\x1bRD\x1bOp\x1bOL\x1bOi\x1b=B\x1bM2\nAccent S A found\n";
#define REINIT_LEN 49
static char *REINIT_STRING =
    "\x1bT2\x1b=M\x1bN1\x1bRB\x1bOp\x1bOL\x1bOi\x1b=B\x1bM3\nAccent S A restarted\n";
#endif

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static struct serial_state rs_table[] __initdata = {
	SERIAL_PORT_DFNS
};

static int timeouts = 0;	/* sequential number of timeouts */

static int
wait_for_xmitr (void)
{
	int check, tmout = SPK_XMITR_TIMEOUT;

	if ((acntsa_alive) && (timeouts >= NUM_DISABLE_TIMEOUTS)) {
		acntsa_alive = 0;
		return 0;
	}

	/* holding register empty? */
	do {
		check = inb_p (synth_port_tts + UART_LSR);
		if (--tmout == 0) {
			printk ("Accent-SA:  timed out\n");
			timeouts++;
			return 0;
		}
	} while ((check & BOTH_EMPTY) != BOTH_EMPTY);

	tmout = SPK_XMITR_TIMEOUT;
	/* CTS */
	do {
		check = inb_p (synth_port_tts + UART_MSR);
		if (--tmout == 0) {
			timeouts++;
			return 0;
		}
	} while ((check & UART_MSR_CTS) != UART_MSR_CTS);

	timeouts = 0;
	return 1;
}

static inline int
spk_serial_out (const char ch)
{
	if (acntsa_alive && synth_port_tts) {
		if (wait_for_xmitr ()) {
			outb_p (ch, synth_port_tts);
			return 1;
		}
	}
	return 0;
}

#if 0
static unsigned char __init
spk_serial_in (void)
{
	int lsr, tmout = SPK_SERIAL_TIMEOUT;
	int c;

	do {
		lsr = inb_p (synth_port_tts + UART_LSR);
		if (--tmout == 0)
			return 0xff;
	} while (!(lsr & UART_LSR_DR));
	c = inb_p (synth_port_tts + UART_RX);
	return (unsigned char) c;
}
#endif

static void
do_catch_up (unsigned long data)
{
	unsigned long jiff_in = jiffies;

	synth_stop_timer ();
	while ((synth_sent_bytes < synth_queued_bytes) && !synth_full ()) {
		if (!spk_serial_out (*(synth_buffer + synth_sent_bytes))) {
			synth_delay (synth_full_time);
			return;
		}
		synth_sent_bytes++;
		if (jiffies >= jiff_in + synth_jiffy_delta
		    && *(synth_buffer + synth_sent_bytes - 1) == ' ') {
			spk_serial_out ('\r');
			synth_delay (synth_delay_time);
			return;
		}
	}

	synth_sent_bytes = synth_queued_bytes = 0;
	spk_serial_out ('\r');
	synth_timer_active = 0;
	if (waitqueue_active (&synth_sleeping_list))
		wake_up_interruptible (&synth_sleeping_list);
}

static void
synth_write_tts (char ch)
{
	if (!acntsa_alive)
		return;
	if (ch < 0x00)
		return;		/* don't want unprintable chars */
	if (ch == 0x0a)		/* turn lf into <cr> to force talking. */
		ch = 0x0D;
	if (ch == SYNTH_CLEAR) {	/* clear all and wake sleeping */
		spk_serial_out (ch);
		if (waitqueue_active (&synth_sleeping_list))
			wake_up_interruptible (&synth_sleeping_list);
		synth_timer_active = synth_queued_bytes = synth_sent_bytes = 0;
		return;
	}

	synth_buffer_add (ch);
	if (synth_buffering)
		return;
	if (synth_timer_active == 0)
		synth_delay (synth_trigger_time);
}

static inline void
synth_immediate_tts (const char *buf, size_t count)
{
	while (count--)
		spk_serial_out (*buf++);
	return;
}

/*
 *	Setup initial baud/bits/parity.  Swiped from serial.c (console section)
 *	Return non-zero if we didn't find a serial port.
 */
static int __init
serprobe (int index)
{
	struct serial_state *ser = NULL;
	unsigned char test = 0;

	if (synth_port_tts) {
		for (test = 0; test <= SPK_HI_TTY; test++)
			if ((rs_table + test)->port == synth_port_tts) {
				ser = rs_table + test;
				break;
			}
	} else
		ser = rs_table + index;

	/* don't do output yet... */
	if (synth_request_region (ser->port, 8))
		return -1;

	initialize_uart(ser);
	/* If we read 0xff from the LSR, there is no UART here. */
	if (inb (ser->port + UART_LSR) == 0xff) {
		synth_release_region (ser->port, 8);
		return -1;
	}
	outb (0x0d, ser->port);
	mdelay (1);

	acntsa_alive = 1;
	/* ignore any error results, if port was forced */
	if (synth_port_tts)
		return 0;

	synth_port_tts = ser->port;
	/* check for accent s.a now... */
	if (spk_serial_out (0x18))
		return 0;

	synth_release_region (ser->port, 8);
	timeouts = acntsa_alive = synth_port_tts = 0;	/* not ignoring */
	return -1;
}

static int __init
synth_dev_probe (void)
{
	int i = 0;

	printk ("Probing for Accent-SA.\n");
	if (synth_port_tts)
		printk ("probe forced to 0x%x by kernel command line\n",
			synth_port_tts);

	/* check ttyS0-ttyS3 */
	for (i = SPK_LO_TTY; i <= SPK_HI_TTY; i++) {
		if (serprobe (i) == 0)
			break;	/* found it */
	}

	if (acntsa_alive) {
		printk ("Acnt-SA: %03x-%03x, Driver Version %s,\n",
			synth_port_tts, synth_port_tts + 7, synth->version);
		synth_immediate_tts ("\x1b=R\r", 5);
		synth_write (synth->init, strlen (synth->init));
		return 0;
	}

	printk ("Accent-SA:  not found\n");
	return -ENODEV;
}

static int
synth_alive (void)
{
	if (acntsa_alive)
		return 1;	/* already on */
	else if ((!acntsa_alive) && (synth_port_tts)) {
		if (wait_for_xmitr () > 0) {	/* restart */
			acntsa_alive = 1;
			synth_write (synth->reinit, strlen (synth->reinit));
			return 2;	/* reenabled */
		} else
			printk ("Accent-SA: can't restart synth\n");
	}
	return 0;
}

static const char init_string[] =
    "\x1bT2\x1b=M\x1bOi\x1bN1\x1bRB\nAccent S A Found\n";
static const char reinit_string[] =
    "\x1bT2\x1bOi\x1b=M\x1bN1\x1bR9\nAccent S A restarted\n";

static struct spk_variable vars[] =
    { {"flush", "\x18", "_", (BUILDER | HARD_DIRECT | USE_RANGE | NO_USER),
       "*"},
{"pitch", "5", "\x1bP_", (NUMERIC | HARD_DIRECT | USE_RANGE), "0,9"},
{"caps_start", "\x1bP8", "_", 0, "*"},
{"caps_stop", "\x1bP5", "_", 0, "*"},
{"rate", "9", "\x1bR_", HARD_DIRECT, "0123456789abcdefgh"},
{"tone", "5", "\x1bV_", (NUMERIC | HARD_DIRECT | USE_RANGE), "0,9"},
{"volume", "9", "\x1b\x41_", (NUMERIC | HARD_DIRECT | USE_RANGE), "0,9"},
END_VARS
};

static char *config[] =
    { "\x18", "\x1bP5", "\x1bP8", "\x1bP5", "\x1bR9", "\x1bV5",
"\x1b\x41\x39" };

struct spk_synth synth_acntsa = { "acntsa", "Version-0.14", "accent_sa",
	init_string, reinit_string, 400, 5, 3, 1000,
	vars, config, 0, synth_dev_probe, do_catch_up,
	synth_write_tts, synth_alive
};
