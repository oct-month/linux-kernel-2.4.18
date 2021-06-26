/*
 * speakup_dectlk.c - Dectalk Express driver for Linux kernel 2.3.x and speakup
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

/* These routines are written to control the Dectalk Express speech
   synthesizer by Digital Equipment Corp.  They are not ment to be
   thought of as a device driver in that they do not register
   themselves as a chr device and there is no file_operations
   structure.  They are strictly to provide an interface to the
   Dectalk Express from the speakup screen review package.  */

#define KERNEL
#include <linux/version.h>
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
#include <linux/vt_kern.h>	/* kd_mksound */
#include <linux/init.h>		/* for __init */
#include <linux/serial.h>	/* for rs_table, serial constants & 
				   serial_uart_config */
#include <linux/serial_reg.h>	/* for more serial constants */
#if LINUX_VERSION_CODE >= 0x020200
#include <linux/serialP.h>	/* for struct serial_state */
#endif
#include <asm/serial.h>
#include <linux/speakup.h>

#define SYNTH_CLEAR 0x03	/* synth flush char */
#define PROCSPEECH 0x0b		/* start synth processing char */
static int full_status = 0;
#define synth_full() (full_status = (inb_p(synth_port_tts) == 0x13))
#define SPK_SERIAL_TIMEOUT 1000000	/* countdown values for serial timeouts */
#define SPK_XMITR_TIMEOUT 1000000	/* countdown values transmitter/dsr timeouts */
#define SPK_LO_TTY 0		/* check ttyS0 ... ttyS3 */
#define SPK_HI_TTY 3
#define NUM_DISABLE_TIMEOUTS 3	/* # of timeouts permitted before disable */

static int dectlk_alive = 0;
			    /* 160 ms delay and ctrl-x as flush */
#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static struct serial_state rs_table[] __initdata = {
	SERIAL_PORT_DFNS
};

static int timeouts = 0;	/* sequential number of timeouts */

static int
wait_for_xmitr (void)
{
	int check;
	unsigned int tmout = SPK_XMITR_TIMEOUT;

	if ((dectlk_alive) && (timeouts >= NUM_DISABLE_TIMEOUTS)) {
		dectlk_alive = 0;
		timeouts = 0;
		return 0;
	}

	/* holding register empty? */
	do {
		check = inb_p(synth_port_tts + UART_LSR);
		if (--tmout == 0) {
			printk ("Dectalk Express:  timed out\n");
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
	if (dectlk_alive && synth_port_tts) {
		if (wait_for_xmitr ()) {
			outb_p (ch, synth_port_tts);
			return 1;
		}
	}
	return 0;
}

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
			spk_serial_out (PROCSPEECH);
			synth_delay (synth_delay_time);
			return;
		}
	}

	if (full_status) {
		synth_delay (synth_full_time);
		return;
	}
	synth_sent_bytes = synth_queued_bytes = 0;
	synth_trigger_time = 50;
	spk_serial_out (PROCSPEECH);
	synth_timer_active = 0;
	if (waitqueue_active (&synth_sleeping_list))
		wake_up_interruptible (&synth_sleeping_list);
}

static inline void
synth_immediate_tts (const char *buf, size_t count)
{
	while (count--)
		spk_serial_out (*buf++);
	return;
}

static void
synth_write_tts (char ch)
{
/* int i = SPK_SERIAL_TIMEOUT; */
	static char last = '[';

	if (!dectlk_alive)
		return;
	if (ch < 0x00)
		return;		/* don't want unprintable chars */
	if (ch == 0x0a)		/* turn lf into <cr> to force talking. */
		ch = 0x0D;
	if (strchr ("-'", ch))
		ch = ' ';
	if (ch == SYNTH_CLEAR) {	/* clear all and wake sleeping */
		spk_serial_out (ch);
		/*while(spk_serial_in() != 0x01 && i-- > 0); */
		synth_trigger_time = 100;
		if (waitqueue_active (&synth_sleeping_list))
			wake_up_interruptible (&synth_sleeping_list);
#if LINUX_VERSION_CODE >= 0x020300
		if (synth_timer.list.prev)
			synth_stop_timer ();
#else
		if (synth_timer.prev)
			synth_stop_timer ();
#endif
		synth_timer_active = synth_queued_bytes = synth_sent_bytes = 0;
		return;
	}

	synth_buffer_add (ch);
	if ((last != '[') && strchr (",_.@!?/-:", ch))
		synth_buffer_add (PROCSPEECH);
	last = ch;
	if (synth_buffering)
		return;
	if (synth_timer_active == 0)
		synth_delay (synth_trigger_time);
	return;
}

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
	mdelay (1);
	outb (0x0d, ser->port);

	dectlk_alive = 1;
	/* ignore any error results, if port was forced */
	if (synth_port_tts)
		return 0;

	synth_port_tts = ser->port;
	/* check for dectalk express now... */
	if (spk_serial_out (0x03)) {
		while (spk_serial_in () != 0x01) ;
		return 0;
	}

	synth_release_region (ser->port, 8);
	timeouts = dectlk_alive = synth_port_tts = 0;	/* not ignoring */
	return -1;
}

static int __init
synth_dev_probe (void)
{
	int i = 0;

	printk ("Probing for Dectalk Express.\n");
	if (synth_port_tts)
		printk ("probe forced to 0x%x by kernel command line\n",
			synth_port_tts);

	/* check ttyS0-ttyS3 */
	for (i = SPK_LO_TTY; i <= SPK_HI_TTY; i++) {
		if (serprobe (i) == 0)
			break;	/* found it */
	}

	if (dectlk_alive) {
		/* found 'em */
		printk ("Dectalk Express: %03x-%03x, Driver Version %s,\n",
			synth_port_tts, synth_port_tts + 7, synth->version);
		synth_immediate_tts (synth->init, strlen (synth->init));
		return 0;
	}

	printk ("Dectalk Express:  not found\n");
	return -ENODEV;
}

static int
synth_alive (void)
{
	if (dectlk_alive)
		return 1;	/* already on */
	else if ((!dectlk_alive) && (synth_port_tts)) {
		if (wait_for_xmitr () > 0) {	/* restart */
			dectlk_alive = 1;
			synth_write (synth->reinit, strlen (synth->reinit));
			return 2;	/* reenabled */
		} else
			printk ("Dectalk Express: can't restart synth\n");
	}
	return 0;
}

static const char init_string[] =
    "[:ra 300][:dv ap 100][:punc n][:pe -380]Dectalk Express found\n";
static const char reinit_string[] =
    "[:ra 300][:dv ap 100][:punc n][:pe -380]Dectalk restarted\n";

static struct spk_variable vars[] =
    { {"flush", "\x03", "_", (BUILDER | HARD_DIRECT | USE_RANGE | NO_USER),
       "*"},
{"pitch", "100", "[:dv ap _]", (NUMERIC | HARD_DIRECT | USE_RANGE), "50,350"},
{"caps_start", "[:dv ap 222]", "_", 0, "*"},
{"caps_stop", "[:dv ap 100]", "_", 0, "*"},
{"rate", "300", "[:ra _]", (NUMERIC | HARD_DIRECT | USE_RANGE), "75,650"},
{"volume", "65", "[:dv gv _]", (NUMERIC | HARD_DIRECT | USE_RANGE), "0,80"},
{"voice", "p", "[:n_]", HARD_DIRECT, "phfdburwkv"},
{"punct", "n", "[:pu _]", HARD_DIRECT, "nsa"},
END_VARS
};

static char *config[] =
    { "\x03", "[:dv ap 100]", "[:dv ap 222]", "[:dv ap 100]", "[:ra 300]",
	"[:dv gv 65]", "[:np]", "[:pu n]"
};

struct spk_synth synth_dectlk = { "dectlk", "Version-0.12", "dec_express",
	init_string, reinit_string, 500, 50, 5, 1000,
	vars, config, 0, synth_dev_probe, do_catch_up,
	synth_write_tts, synth_alive
};
