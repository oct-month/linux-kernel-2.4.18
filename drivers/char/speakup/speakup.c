/* speakup.c
   review functions for the speakup screen review package.
   written by: Kirk Reiser and Andy Berdan.

   Thanks to Barry Pollock for the more responsive diacritical code.

   Thanx eternal to Jim Danley for help with the extended codepage 437
   character array!  Very nice job on the /proc file system entries as
   well.

   Thanks also to Matt Campbell for a fine job on the driver code
   building in the ability to include a number of drivers in at the
   same time.

    Copyright (C) 1998  Kirk Reiser.

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
    261 Trott dr. London, Ontario, Canada. N6G 1B6 */

#define __KERNEL_SYSCALLS__
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/vt.h>
#include <linux/tty.h>
#include <linux/mm.h> /* __get_free_page() and friends */
#include <linux/errno.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>
#include <linux/unistd.h>
#include <linux/delay.h>	/* for mdelay() */
#include <linux/init.h>		/* for __init */
#include <linux/miscdevice.h>	/* for misc_register, and SYNTH_MINOR */

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <linux/version.h>
#if LINUX_VERSION_CODE >= 0x20300
#include <linux/bootmem.h>	/* for alloc_bootmem */
#endif

#include "../console_macros.h"	/* for x, y, attr and pos macros */
#include <linux/keyboard.h>	/* for KT_SHIFT, KVAL and friends */
#include <linux/kbd_kern.h> /* for vc_kbd_* and friends */
#include <linux/kbd_diacr.h>
#include <linux/kbd_ll.h>
#include <linux/ctype.h>	/* for isdigit() and friends */

#include <asm/uaccess.h> /* copy_from|to|user() and others */
#include <asm/keyboard.h>

#include <linux/speakup.h>
#include "symbols.h"

#include "cvsversion.h"

#define SPEAKUP_VERSION "Speakup v-1.00" CVSVERSION

/* these are globals from the kernel code */
extern void *kmalloc (size_t, int);
extern void kfree (const void *);
extern void put_queue(int); /* it's in keyboard.c */
extern struct tty_struct *tty;
extern int fg_console;

/* These are ours from synth drivers. */
extern void proc_speakup_synth_init (void);	// init /proc synth-specific subtree
#ifdef CONFIG_SPEAKUP_ACNTPC
extern struct spk_synth synth_acntpc;
#endif
#ifdef CONFIG_SPEAKUP_ACNTSA
extern struct spk_synth synth_acntsa;
#endif
#ifdef CONFIG_SPEAKUP_APOLO
extern struct spk_synth synth_apolo;
#endif
#ifdef CONFIG_SPEAKUP_AUDPTR
extern struct spk_synth synth_audptr;
#endif
#ifdef CONFIG_SPEAKUP_BNS
extern struct spk_synth synth_bns;
#endif
#ifdef CONFIG_SPEAKUP_DECEXT
extern struct spk_synth synth_decext;
#endif
#ifdef CONFIG_SPEAKUP_DECTLK
extern struct spk_synth synth_dectlk;
#endif
#ifdef CONFIG_SPEAKUP_DTLK
extern struct spk_synth synth_dtlk;
#endif
#ifdef CONFIG_SPEAKUP_LTLK
extern struct spk_synth synth_ltlk;
#endif
#ifdef CONFIG_SPEAKUP_SPKOUT
extern struct spk_synth synth_spkout;
#endif
#ifdef CONFIG_SPEAKUP_TXPRT
extern struct spk_synth synth_txprt;
#endif

#define MIN(a,b) ( ((a) < (b))?(a):(b) )
#define krealloc(ptr,newsize) ( kfree(ptr), ptr = kmalloc(newsize,GFP_KERNEL) )
#define toctrl(x) ( ( ((x) >= 'A') && ((x) <='Z')) ? ((x) - 'A') : \
			(((x) >= 'a') && ((x) <= 'z')) ? ((x) - 'a') : 0 )
#define allowable(c)   (   ((c) > 0x2f && (c) < 0x3a) \
			|| (((c)&0x5f) > 0x40 && ((c)&0x5f) < 0x5b) \
			|| (strchr(" \n,.'-:?!", (c)) != NULL) \
		)

static int errno;
char *spk_cfg[] = { DEFAULT_SPKUP_VARS };
long spk_cfg_map = 0;		/* which ones have been re'alloc'ed */
int synth_file_inuse = 0;
static struct spk_variable spk_vars[] = { SPKUP_VARS };
static unsigned char pitch_shift = 0;
char saved_punc_level = 0x30;
char mark_cut_flag = 0;
unsigned short mark_x = 0;
unsigned short mark_y = 0;
static char synth_name[10] = CONFIG_SPEAKUP_DEFAULT;
static struct spk_synth *synths[] = {
#ifdef CONFIG_SPEAKUP_ACNTPC
	&synth_acntpc,
#endif
#ifdef CONFIG_SPEAKUP_ACNTSA
	&synth_acntsa,
#endif
#ifdef CONFIG_SPEAKUP_APOLO
	&synth_apolo,
#endif
#ifdef CONFIG_SPEAKUP_AUDPTR
	&synth_audptr,
#endif
#ifdef CONFIG_SPEAKUP_BNS
	&synth_bns,
#endif
#ifdef CONFIG_SPEAKUP_DECEXT
	&synth_decext,
#endif
#ifdef CONFIG_SPEAKUP_DECTLK
	&synth_dectlk,
#endif
#ifdef CONFIG_SPEAKUP_DTLK
	&synth_dtlk,
#endif
#ifdef CONFIG_SPEAKUP_LTLK
	&synth_ltlk,
#endif
#ifdef CONFIG_SPEAKUP_SPKOUT
	&synth_spkout,
#endif
#ifdef CONFIG_SPEAKUP_TXPRT
	&synth_txprt,
#endif
	NULL,			/* Leave room for one dynamically registered synth. */
	NULL
};

#define LineWrapBleep 0x01
#define LineWrapMask 0xFE
#define AttributeChangeBleep 0x02
#define AttributeChangeMask 0xFD

#define punc_level (*(spk_cfg[PUNCT_LEVEL]))
#define spell_delay (*(spk_cfg[SPELL_DELAY])-0x30)
#define key_echo (*(spk_cfg[KEY_ECHO])-0x30)

/* how about a couple of arrays to index our colours and attributes */
char *fg_color[] = {
"black", "blue", "green", "cyan", "red", "magenta", "yellow", "white",
	"grey", "bright blue", "bright green", "bright cyan", "bright red",
	"bright magenta", "bright yellow", "bright white"
};

char *bg_color[] = {
	"black", "blue", "green", "cyan", "red", "magenta", "yellow", "white",
	"blinking black", "blinking blue", "blinking green", "blinking cyan",
	"blinking red", "blinking magenta", "blinking yellow", "blinking white"
};

char *phonetic[] = {
	"alpha", "beta", "charley", "delta", "echo", "fox", "gamma", "hotel",
	"india", "juleiet", "keelo", "leema", "mike", "november", "oscar",
	"papa",
	"quebec", "romeo", "seeara", "tango", "uniform", "victer", "wiskey",
	"x ray",
	"yankee", "zooloo"
};

// array of 256 char pointers (one for each ASCII character description)
// initialized to default_chars and user selectable via /proc/speakup/characters
char *characters[256];

char *default_chars[256] = {
	"null", "control-a", "control-b", "control-c", "control-d", "control-e",
	"control-f", "control-g", "control-h", "control-i", "control-j",
	"control-k",
	"control-l", "control-m", "control-n", "control-o", "control-p",
	"control-q",
	"control-r", "control-s", "control-t", "control-u", "control-v",
	"control-w",
	"control-x", "control-y", "control-z", NULL, NULL, NULL, NULL, NULL,
	"space", "bang!", "quote", "number", "dollars", "percent", "and",
	"tick",
	"left paren", "right paren", "star", "plus", "comma,", "dash", "dot",
	"slash",
	"zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
	"nine",
	"colon", "semi", "less", "equals", "greater", "question?", "at", "eigh",
	"b",
	"c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n",
	"o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
	"y", "zehd", "left bracket", "backslash", "right bracket", "caret",
	"line",
	"accent", "eigh", "b", "c", "d", "e", "f", "g", "h", "i", "j",
	"k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u",
	"v", "w", "x", "y", "zehd", "left brace", "bar", "right brace",
	"tihlduh", "cap delta", "cap see cedilla", "u oomlout", "e acute",
	"eigh circumflex", "eigh oomlout", "eigh grave", "eigh ring",
	"see cedilla",
	"e circumflex", "e oomlout", "e grave", "i oomlout", "i circumflex",
	"i grave", "cap eigh oomlout", "cap eigh ring", "cap e acute",
	"eigh e dipthong", "cap eigh cap e dipthong", "o circumflex",
	"o oomlout",
	"o grave", "u circumflex", "u grave", "y oomlout", "cap o oomlout",
	"cap u oomlout", "cents", "pounds", "yen", "peseta", "florin",
	"eigh acute",
	"i acute", "o acute", "u acute", "n tilde", "cap n tilde",
	"feminine ordinal",
	"masculin ordinal", "inverted question",
	"reversed not", "not", "half", "quarter", "inverted bang",
	"much less than", "much greater than", "dark shading", "medium shading",
	"light shading", "verticle line", "left tee", "double left tee",
	"left double tee", "double top right", "top double right",
	"double left double tee", "double vertical line",
	"double top double right",
	"double bottom double right", "double bottom right",
	"bottom double right",
	"top right", "left bottom", "up tee", "tee down", "tee right",
	"horizontal line", "cross bars", "tee double right", "double tee right",
	"double left double bottom", "double left double top",
	"double up double tee",
	"double tee double down", "double tee double right",
	"double horizontal line",
	"double cross bars", "up double tee", "double up tee",
	"double tee down",
	"tee double down", "double left bottom", "left double bottom",
	"double left top", "left double top", "double vertical cross",
	"double horizontal cross", "bottom right", "left top", "solid square",
	"solid lower half", "solid left half", "solid right half",
	"solid upper half",
	"alpha", "beta", "cap gamma", "pie", "cap sigma", "sigma", "mu", "tou",
	"cap phigh", "cap thayta", "cap ohmega", "delta", "infinity", "phigh",
	"epsilaun", "intersection", "identical to", "plus or minus",
	"equal grater than", "less than equal", "upper integral",
	"lower integral",
	"divided by", "almost equal", "degrees", "centre dot",
	"bullet", "square root", "power", "squared", "black square",
	"white space"
};

int spk_keydown = 0;
int bell_pos = 0;
static int spk_lastkey = 0;

struct spk_t *speakup_console[MAX_NR_CONSOLES];

/* cap_toggle by far is on or not, if on, cap_on 0, else 1 */
static unsigned short cap_on;
/* num lock by far is on or not, if on, spkup_num_lock 1, else 0 */
unsigned short spkup_num_lock_on=0;
 
               
#if LINUX_VERSION_CODE >= 0x020300
int
spk_setup (char *str)
{
	int ints[4];
	str = get_options (str, ARRAY_SIZE (ints), ints);
	if (ints[0] > 0 && ints[1] >= 0)
		synth_port_tts = ints[1];
	return 1;
}

int
spk_ser_setup (char *str)
{
	int lookup[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
	int ints[4];
	str = get_options (str, ARRAY_SIZE (ints), ints);
	if (ints[0] > 0 && ints[1] >= 0)
		synth_port_tts = lookup[ints[1]];
	return 1;
}

int
spk_synth_setup (char *str)
{
	size_t len = MIN (strlen (str), 9);
	memcpy (synth_name, str, len);
	synth_name[len] = '\0';
	return 1;
}
#else
void __init
spk_setup (char *str, int *ints)
{
	if (ints[0] > 0 && ints[1] >= 0)
		synth_port_tts = ints[1];
}

void __init
spk_ser_setup (char *str, int *ints)
{
	int lookup[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
	if (ints[0] > 0 && ints[1] >= 0)
		synth_port_tts = lookup[ints[1]];
}

void __init
spk_synth_setup (char *str, int *ints)
{
	size_t len = MIN (strlen (str), 9);
	memcpy (synth_name, str, len);
	synth_name[len] = '\0';
}
#endif

#if LINUX_VERSION_CODE >= 0x020300
__setup ("speakup_port=", spk_setup);
__setup ("speakup_ser=", spk_ser_setup);
__setup ("speakup_synth=", spk_synth_setup);
#endif

void
speakup_savekey (unsigned char ch)
{
	/* keydown is seperate since they are handled by two
	   seperate routines */
	if (ch)
		spk_keydown++;
	else
		spk_keydown = 0;
	// keypad slash key is not calling this function as it should -- jd
	spk_lastkey = ch;
}

static inline void
bleep (unsigned short val)
{
	int time = 0;
	val = (val + 1) * 75 + 75;
	time = *(spk_cfg[BLEEP_TIME]) - 0x30;
	if (*(spk_cfg[BLEEP_TIME] + 1) != '\0') {
		time *= 10;
		time += (*(spk_cfg[BLEEP_TIME] + 1) - 0x30);
	}
	kd_mksound (val, time);
}

static void
spk_control (int currcons, int value)
{
	/* bleep parameter */
	unsigned short bleep_sound=2;

	if (spk_shut_up || (synth == NULL))
		return;
	/* interrupt active + not shut up + not shift key (value == 0) */
	if ((*spk_cfg[NO_INTERRUPT] - 0x30) && value) {
		synth_write (synth->config[FLUSH],
			     strlen (synth->config[FLUSH]));
		synth_write (synth->config[PITCH],
			     strlen (synth->config[PITCH]));
	}

        if (!cap_on){ /* we do not like shift working */
        	if ((*spk_cfg[SAY_CONTROL] - 0x30 == 0) && (value<4))
		return;
	}
	else { /* we like shift working */
		if ((*spk_cfg[SAY_CONTROL] - 0x30 == 0) && 
			((value==1)||(value==2)||(value==3)))
		return;
	}
		
	switch (value) {
	case 0:
		if(cap_on){ /* bleep if cap toggle is on and shift key is pressed */
			 bleep(bleep_sound);
		}
		else 
			spkup_write ("shift\n", 6);
		break;
	case 1:
		spkup_write ("insert\n", 7);
		break;
	case 2:
		spkup_write ("control\n", 8);
		break;
	case 3:
		spkup_write ("ault\n", 5);
		break;
	case 10:
		spkup_write ("caps lock on\n", 13);
		break;
	case 11:
		spkup_write ("caps lock off\n", 14);
		break;
	case 12:
		spkup_write ("numm lock on\n", 13);
		break;
	case 13:
		spkup_write ("numm lock off\n", 14);
		break;
	case 14:
		spkup_write ("scroll lock on\n", 15);
		break;
	case 15:
		spkup_write ("scroll lock off\n", 16);
		break;
	}
}

static void
s2i (char *start, int length, int *dest)
{
	int i;
	for (i = 0; i < length; i++) {
		*dest *= 10;
		*dest += start[i] - 0x30;
	}
}

void
speakup_reset (int currcons, unsigned char type)
{

	spk_shut_up &= 0xfe;
	if (spk_killed)
		return;
	spk_parked &= 0xfe;
	if (*(spk_cfg[NO_INTERRUPT]) - 0x30)
		return;
	spk_keydown++;

	if (synth == NULL)
		return;
	synth_write (synth->config[FLUSH], strlen (synth->config[FLUSH]));
	if (pitch_shift) {
		synth_write (synth->config[PITCH],
			     strlen (synth->config[PITCH]));
		pitch_shift = 0;
	}
}

void
speakup_shut_up (unsigned int currcons)
{
	spk_shut_up |= 0x01;
	speakup_date (currcons);
	if (synth == NULL)
		return;
	synth_write (synth->config[FLUSH], strlen (synth->config[FLUSH]));
	synth_write (synth->config[PITCH], strlen (synth->config[PITCH]));
}

int
speakup_diacr (unsigned char ch, unsigned int currcons)
{
	static unsigned char *buf = "\0\0\0\0\0";
	static int num = 0;
	int tmp = 0;

	buf[num++] = ch;
	buf[num] = '\0';

	if ((ch == '$' || ch == 27 || (ch > 47 && ch < 58) || ch == 'x'
	     || ch == 'y' || ch == '+' || ch == '-') && num < 5)
		switch (ch) {
		case '$':
		case 0x1b:	/* cancel */
			num = 0;
			buf[0] = '\0';
			return 1;
		case 'x':
		case 'y':
			if (buf[0] == '+' || buf[0] == '-') {
				s2i (buf + 1, num - 2, &tmp);
				tmp = (buf[0] == '+') ? tmp : -tmp;

				/* set tmp to new position */
				if (ch == 'x')
					tmp = tmp + spk_x;
				else
					tmp = tmp + spk_y;
			} else {
				s2i (buf, num - 1, &tmp);
				--tmp;
			}

			/* range checking */
			if (ch == 'x') {
				if (tmp > video_num_columns)
					tmp = video_num_columns;
				if (tmp < 0)
					tmp = 0;
			} else {
				if (tmp > video_num_lines)
					tmp = video_num_lines;
				if (tmp < 0)
					tmp = 0;
			}

			/* move it, baby... */
			if (ch == 'x') {
				spk_pos -= spk_x * 2;
				/* set x */
				spk_x = tmp;
				spk_pos += tmp * 2;
			} else {
				/* zero y */
				spk_pos -= spk_y * video_size_row;
				/* set y */
				spk_y = tmp;
				spk_pos += tmp * video_size_row;
			}
			num = 0;
			buf[0] = '\0';
			return 1;	/* no more characters req'd */
	} else {
		/* no valid terminator characters or wrong key */
		num = 0;
		buf[0] = '\0';
		if (!spk_killed)
			spkup_write ("Error\n", 6);
		return -1;
	}
	return 0;		/* I want more! */
}

void
speakup_kill (unsigned int currcons)
{
	if (spk_killed) {	/* dead */
		spk_shut_up &= ~0x40;
		spkup_write ("Eyem a Lighve!\n", 15);
	} else {
		spkup_write ("You killed speak up!\n", 21);
		spk_shut_up |= 0x40;
	}
}

void
speakup_off (unsigned int currcons)
{
	char val = 0;

	if (spk_shut_up & 0x40)
		return;		/* if speech is killed don't bother. */
	if ((synth == NULL) || (val = synth->is_alive ())) {
		/* re-enables synth, if disabled */
		if (spk_shut_up & 0x80 || (val == 2)) {
			spk_shut_up &= 0x7f;
			spkup_write ("hey. That's better!\n", 20);
		} else {
			spk_shut_up |= 0x80;
			spkup_write ("You turned me off!\n", 19);
		}
	}
}

void
function_announce (unsigned int currcons)
{
	if (spk_sound & 0x40) {
		spk_sound &= 0xbf;
		spkup_write ("Function announce off.\n", 23);
	} else {
		spk_sound |= 0x40;
		spkup_write ("Function announce on.\n", 22);
	}
}

void
speakup_parked (unsigned int currcons)
{
	if (spk_parked & 0x80) {
		spk_parked = 0;
		spkup_write ("unparked!\n", 10);
	} else {
		spk_parked |= 0x80;
		spkup_write ("parked!\n", 8);
	}
}

void
speakup_cursoring (unsigned int currcons)
{
	if (spk_shut_up & 0x02) {
		spk_shut_up &= 0xfd;
		spkup_write ("cursoring off!\n", 15);
	} else {
		spk_shut_up |= 0x02;
		spkup_write ("cursoring on!\n", 14);
	}
}

void
speakup_cut (unsigned int currcons, struct tty_struct *tty)
{
	int ret;
	unsigned char args[6*sizeof(short)];
	unsigned short *arg;
	static char *buf = "speakup_cut: set_selection failed: ";

	if (mark_cut_flag) {
		/* cut */
		arg = (unsigned short *)args + 1;
		arg[0] = mark_x + 1;
		arg[1] = mark_y + 1;
		arg[2] = (unsigned short)spk_x + 1;
		arg[3] = (unsigned short)spk_y + 1;
		arg[4] = 0;	/* char-by-char selection */
		mark_cut_flag = 0;
		spkup_write ("cut\n", 4);

		if ((ret = set_selection ((const unsigned long)args+sizeof(short)-1, tty, 0))) {
			switch (ret) {
			case -EFAULT :
				printk(KERN_WARNING "%sEFAULT\n", buf);
				break;
			case -EINVAL :
				printk(KERN_WARNING "%sEINVAL\n", buf);
				break;
			case -ENOMEM :
				printk(KERN_WARNING "%sENOMEM\n", buf);
				break;
			}
		}
	} else {
		/* mark */
		mark_cut_flag = 1;
		mark_x = spk_x;
		mark_y = spk_y;
		spkup_write ("mark\n", 5);
		clear_selection();
	}
}

void
speakup_paste (struct tty_struct *tty)
{
	spkup_write ("paste\n", 6);
	paste_selection (tty);
}

void
say_attributes (int currcons)
{
	char buf[80], cnt;

	if (synth == NULL)
		return;
	spk_parked |= 0x01;
	cnt = sprintf (buf, "%s on %s\n", *(fg_color + (spk_attr & 0x0f)),
		       *(bg_color + (spk_attr >> 4)));
	synth_write (buf, cnt);
}

void
say_curr_char (unsigned int currcons)
{
	unsigned short ch;
	char buf[128];

	if (synth == NULL)
		return;
	spk_parked |= 0x01;
	spk_old_attr = spk_attr;
	ch = scr_readw ((unsigned short *) spk_pos);
	spk_attr = ((ch & 0xff00) >> 8);
	if (spk_attr != spk_old_attr && spk_sound & AttributeChangeBleep)
		bleep (spk_y);
	if ((ch & 0x00ff) > 0x40 && (ch & 0x00ff) < 0x5b) {
		pitch_shift++;
		ch = sprintf (buf, "%s %s %s", synth->config[CAPS_START],
			      characters[(unsigned char) ch],
			      synth->config[CAPS_STOP]);
	} else
		ch = sprintf (buf, " %s %s ", synth->config[PITCH],
				characters[(unsigned char) ch]);
	synth_write (buf, ch);
}

void
say_phonetic_char (unsigned int currcons)
{
	unsigned short ch;
	char buf[64];

	if (synth == NULL)
		return;
	spk_parked |= 0x01;
	spk_old_attr = spk_attr;
	ch = scr_readw ((unsigned short *) spk_pos);
	spk_attr = ((ch & 0xff00) >> 8);
	if ((ch & 0x00ff) > 0x40 && (ch & 0x00ff) < 0x5b)
		ch = ((ch - 0x41) & 0x00ff);
	else if ((ch & 0x00ff) > 0x60 && (ch & 0x00ff) < 0x7b)
		ch = ((ch - 0x61) & 0x00ff);
	else {
		say_curr_char (currcons);
		return;
	}
	ch = sprintf (buf, "%s\n", *(phonetic + ch));
	synth_write (buf, ch);
}

void
say_prev_char (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (spk_x == 0) {
		spkup_write ("left edge\n", 10);
		return;
	}
	spk_x--;
	spk_pos -= 2;
	say_curr_char (currcons);
}

void
say_next_char (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (spk_x == video_num_columns - 1) {
		spkup_write ("right edge\n", 11);
		return;
	}
	spk_x++;
	spk_pos += 2;
	say_curr_char (currcons);
}

void
say_curr_word (unsigned int currcons)
{
	unsigned long cnt = 0, tmpx = 0, tmp_pos = spk_pos;
	char buf[video_num_columns + 2];

	spk_parked |= 0x01;
	spk_old_attr = spk_attr;
	tmpx = spk_x;
	if (((char) scr_readw ((unsigned short *) tmp_pos) == 0x20)
	    && ((char) scr_readw ((unsigned short *) tmp_pos + 1) > 0x20)) {
		tmp_pos += 2;
		tmpx++;
	} else
		while ((tmpx > 0)
		       && ((scr_readw ((unsigned short *) tmp_pos - 1) & 0x00ff)
			   != 0x20)) {
			tmp_pos -= 2;
			tmpx--;
		}
	spk_attr =
	    (unsigned char) (scr_readw ((unsigned short *) tmp_pos) >> 8);
	while (tmpx < video_num_columns) {
		if ((*(buf + cnt) =
		     (char) scr_readw ((unsigned short *) tmp_pos)) == 0x20)
			break;
		tmpx++;
		tmp_pos += 2;
		cnt++;
	}
	*(buf + cnt++) = '\n';
	saved_punc_level = punc_level;
	punc_level = ALL;
	spkup_write (buf, cnt);
	punc_level = saved_punc_level;
}

void
say_prev_word (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (((scr_readw ((unsigned short *) spk_pos) & 0x00ff) > 0x20)
	    && (((scr_readw ((unsigned short *) spk_pos - 1) & 0x00ff) == 0x20)
		|| spk_x == 0)) {
		if (spk_x > 0) {
			spk_x--;
			spk_pos -= 2;
		} else {
			if (spk_y > 0) {
				spk_y--;
				spk_pos -= 2;
				spk_x = video_num_columns - 1;
				if (spk_sound & LineWrapBleep)
					bleep (spk_y);
				else
					spkup_write ("left edge.\n", 11);
			} else {
				spkup_write ("top edge.\n", 10);
				return;
			}
		}
	}

	while (!(((scr_readw ((unsigned short *) spk_pos) & 0x00ff) > 0x20)
		 &&
		 (((scr_readw ((unsigned short *) spk_pos - 1) & 0x00ff) ==
		   0x20)
		  || spk_x == 0))) {
		if (spk_x > 0) {
			spk_x--;
			spk_pos -= 2;
		} else {
			if (spk_y > 0) {
				spk_y--;
				spk_pos -= 2;
				spk_x = video_num_columns - 1;
				if (spk_sound & LineWrapBleep)
					bleep (spk_y);
				else
					spkup_write ("left edge.\n", 11);
			} else {
				spkup_write ("top edge.\n", 10);
				break;
			}
		}
	}

	say_curr_word (currcons);
}

void
say_next_word (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (((scr_readw ((unsigned short *) spk_pos) & 0x00ff) > 0x20)
	    && (((scr_readw ((unsigned short *) spk_pos - 1) & 0x00ff) == 0x20)
		|| spk_x == 0)) {
		if (spk_x < video_num_columns - 1) {
			spk_x++;
			spk_pos += 2;
		} else {
			if (spk_y < video_num_lines - 1)
				spk_y++;
			else {
				spkup_write ("bottom edge.\n", 13);
				return;
			}
			spk_x = 0;
			spk_pos += 2;
			if (spk_sound & LineWrapBleep)
				bleep (spk_y);
			else
				spkup_write ("right edge.\n", 12);
		}
	}

	while (!(((scr_readw ((unsigned short *) spk_pos) & 0x00ff) > 0x20)
		 &&
		 (((scr_readw ((unsigned short *) spk_pos - 1) & 0x00ff) ==
		   0x20)
		  || spk_x == 0))) {
		if (spk_x < video_num_columns - 1) {
			spk_x++;
			spk_pos += 2;
		} else {
			if (spk_y < video_num_lines - 1)
				spk_y++;
			else {
				spkup_write ("bottom edge.\n", 13);
				break;
			}
			spk_x = 0;
			spk_pos += 2;
			if (spk_sound & LineWrapBleep)
				bleep (spk_y);
			else
				spkup_write ("right edge.\n", 12);
		}
	}

	say_curr_word (currcons);
}

void
spell_word (unsigned int currcons)
{
	unsigned long tmpx = spk_x, tmp_pos = spk_pos;
	char *delay_str[] = { " ", ", ", ". ", ". . ", ". . . " };
	char dnum[] = { 1, 2, 2, 4, 6 };

	if (synth == NULL)
		return;
	spk_parked |= 0x01;
	if (((char) scr_readw ((unsigned short *) spk_pos) == 0x20)
	    && ((char) scr_readw ((unsigned short *) spk_pos + 1) > 0x20)) {
		spk_pos += 2;
		spk_x++;
	} else
		while ((spk_x > 0)
		       && ((scr_readw ((unsigned short *) spk_pos - 1) & 0x00ff)
			   != 0x20)) {
			spk_pos -= 2;
			spk_x--;
		}

	for (; spk_x < video_num_columns; spk_x++, spk_pos += 2) {
		if ((char) scr_readw ((unsigned short *) spk_pos) == 0x20)
			break;
		synth_write (delay_str[spell_delay - 1], dnum[spell_delay - 1]);
		say_curr_char (currcons);
	}

	spk_pos = tmp_pos;
	spk_x = tmpx;
}

void
say_curr_line (unsigned int currcons)
{
	unsigned long tmp;
	unsigned char buf[video_num_columns + 2], i = 0, not_blank = 0;

	spk_parked |= 0x01;
	spk_old_attr = spk_attr;
	spk_attr =
	    (unsigned char) (scr_readw ((unsigned short *) spk_pos) >> 8);
	for (tmp = spk_pos - (spk_x * 2);
	     tmp < spk_pos + ((video_num_columns - spk_x) * 2); tmp += 2) {
		*(buf + i) = (unsigned char) scr_readw ((unsigned short *) tmp);
		if (*(buf + i++) != 0x20)
			not_blank++;
	}
	*(buf + i++) = '\n';

	if (not_blank)
		spkup_write (buf, i);
	else
		spkup_write ("blank\n", 6);
}

void
say_prev_line (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (spk_y == 0) {
		spkup_write ("top edge.\n", 10);
		return;
	}

	spk_y--;
	spk_pos -= video_size_row;
	say_curr_line (currcons);
}

void
say_next_line (unsigned int currcons)
{
	spk_parked |= 0x01;
	if (spk_y == video_num_lines - 1) {
		spkup_write ("bottom edge.\n", 13);
		return;
	}

	spk_y++;
	spk_pos += video_size_row;
	say_curr_line (currcons);
}

static inline void
say_line_from_to (unsigned int currcons, unsigned long from, unsigned long to)
{
	unsigned long tmp;
	unsigned char buf[video_num_columns + 2], i = 0, not_blank = 0;

	spk_parked |= 0x01;
	spk_old_attr = spk_attr;
	spk_attr =
	    (unsigned char) (scr_readw ((unsigned short *) spk_pos) >> 8);
	for (tmp = origin + (spk_y * video_size_row) + (from * 2);
	     tmp < origin + (spk_y * video_size_row) + (to * 2); tmp += 2) {
		*(buf + i) = (unsigned char) scr_readw ((unsigned short *) tmp);
		if (*(buf + i++) != 0x20)
			not_blank++;
	}
	*(buf + i++) = '\n';

	if (not_blank)
		spkup_write (buf, i);
	else
		spkup_write ("blank\n", 6);
}

void
say_screen (unsigned int currcons)
{
	unsigned long tmp_pos = origin;
	unsigned char c, blank = 0;

	spk_parked |= 0x01;
	while (tmp_pos < origin + (video_num_lines * video_size_row)) {
		if ((c =
		     (unsigned char) scr_readw ((unsigned long *) tmp_pos)) ==
		    0x20)
			blank++;
		else
			blank = 0;
		tmp_pos += 2;
		if (blank > 1)
			continue;
		spkup_write (&c, 1);
/* insert a space at the end of full lines */
		if ((tmp_pos - origin) % video_size_row == 0 && blank == 0)
			spkup_write(" ", 1);
	}
}

static inline void
say_screen_from_to (unsigned int currcons, unsigned long from, unsigned long to)
{
	unsigned long tmp_pos;
	unsigned char c, blank = 0;

	spk_parked |= 0x01;
	if (from > 0)
		tmp_pos = origin + ((from - 1) * video_size_row);
	else
		tmp_pos = origin;
	if (to > video_num_lines)
		to = video_num_lines;
	while (tmp_pos < origin + (to * video_size_row)) {
		if ((c =
		     (unsigned char) scr_readw ((unsigned long *) tmp_pos)) ==
		    0x20)
			blank++;
		else
			blank = 0;
		tmp_pos += 2;
		if (blank > 1)
			continue;
		spkup_write (&c, 1);
	}
}

void
top_edge (unsigned int currcons)
{
	spk_parked |= 0x01;
	spk_pos -= spk_y * video_size_row;
	spk_y = 0;
	say_curr_line (currcons);
}

void
bottom_edge (unsigned int currcons)
{
	spk_parked |= 0x01;
	spk_pos += (video_num_lines - spk_y - 1) * video_size_row;
	spk_y = video_num_lines - 1;
	say_curr_line (currcons);
}

void
left_edge (unsigned int currcons)
{
	spk_parked |= 0x01;
	spk_pos -= spk_x * 2;
	spk_x = 0;
	say_curr_char (currcons);
}

void
right_edge (unsigned int currcons)
{
	spk_parked |= 0x01;
	spk_pos += (video_num_columns - spk_x - 1) * 2;
	spk_x = video_num_columns - 1;
	say_curr_char (currcons);
}

void
end_of_line (unsigned int currcons)
{
	spk_parked |= 0x01;
	spk_pos += (video_num_columns - spk_x - 1) * 2;
	spk_x = video_num_columns - 1;
	while (((scr_readw ((unsigned short *) spk_pos) & 0x00ff) == 0x20)
	       && spk_x > 0) {
	  spk_pos -= 2;
	  spk_x--;
	}
	say_curr_char (currcons);
}

void
say_position (unsigned int currcons)
{
	char buf[40];
	int count;

	spk_parked |= 0x01;
	count =
	    sprintf (buf, "line %ld, position %ld, t t y. %d\n", spk_y + 1,
		     spk_x + 1, currcons + 1);
	spkup_write (buf, count);
}

// Added by brianb
void
say_char_num (unsigned int currcons)
{
	char buf[32];
	unsigned short ch;

	spk_parked |= 0x01;
	ch = scr_readw ((unsigned short *) spk_pos);
	ch &= 0x0ff;
	ch = sprintf (buf, "hex %02x, decimal %d\n", ch, ch);
	spkup_write (buf, ch);
}

/* these are stub functions to keep keyboard.c happy. */

void
say_from_top (unsigned int currcons)
{
	say_screen_from_to (currcons, 0, spk_y + 1);
}

void
say_to_bottom (unsigned int currcons)
{
	say_screen_from_to (currcons, spk_y + 1, video_num_lines);
}

void
say_from_left (unsigned int currcons)
{
	say_line_from_to (currcons, 0, spk_x);
}

void
say_to_right (unsigned int currcons)
{
	say_line_from_to (currcons, spk_x, video_num_columns);
}

/* end of stub functions. */

extern int synth_init (void);
extern char synth_buffering;	/* flag to indicate we're buffering  */
unsigned short skip_count = 0;

void
spk_skip (unsigned short cnt)
{
	skip_count += cnt;
}

int
spkup_write (const char *buf, int count)
{
	static unsigned short rep_count = 0;
	static char old_ch, oldest_ch, count_buf[30], punc_buf[128];
	int in_count = count;
	char *punc_search = NULL;

	if (synth == NULL)
		return count;
	spk_keydown = 0;
	while (count--) {
		if (rep_count) {
			if (*buf == old_ch) {
				buf++;
				rep_count++;
				continue;
			} else {
				if (rep_count > 3) {
					synth_write (count_buf,
						     sprintf (count_buf,
							      " repeated %d times. \n",
							      rep_count));
				}
				rep_count = 0;
			}
		}
		if (!key_echo && (*buf == spk_lastkey))
			goto forget_about_it;
		// keypad slash is the only key that slips through and speaks when
		// key_echo is 0, not sure why -- bug
		if (((*buf == spk_lastkey) ||
		     ((punc_search =
		       strchr (*(spk_cfg + PUNC_OFFSET + (punc_level - 0x30)),
			       *buf)) != NULL))
		    && *buf != 0x00) {
			if ((*buf == spk_lastkey) && (*buf & 0x00ff) > 0x40
			    && (*buf & 0x00ff) < 0x5b) {
				/* keyboard caps character */
				pitch_shift++;
				synth_write (punc_buf,
					     sprintf (punc_buf, "%s %s %s",
						      synth->config[CAPS_START],
						      *(characters + *buf),
						      synth->
						      config[CAPS_STOP]));
			} else
				synth_write (punc_buf,
					     sprintf (punc_buf, " %s ",
						      *(characters + *buf)));
		} else if (allowable (*buf))
			synth->write (*buf);

	      forget_about_it:
		if (*buf == old_ch && *buf == oldest_ch && rep_count == 0
		    && punc_search != NULL)
			rep_count = 3;
		oldest_ch = old_ch;
		old_ch = *buf++;
	}

	spk_lastkey = 0;
	if (in_count > 3 && rep_count > 3) {
		synth_write (count_buf,
			     sprintf (count_buf, " repeated %d times. \n",
				      rep_count));
		rep_count = 0;
	}
	return 0;
}

char *
strlwr (char *s)
{
	char *p = s;
	while (*p)
		*p++ = tolower (*p);
	return s;
}

void __init
speakup_open (unsigned int currcons)
{
	int i = 0;

	strlwr (synth_name);
	while ((synth == NULL) && (synths[i] != NULL)) {
		if (strcmp (synths[i]->name, synth_name) == 0)
			synth = synths[i];
		else
			i++;
	}

	if ((synth == NULL) && (strcmp (synth_name, "none") != 0))
		printk (KERN_WARNING "Speakup:  unknown synthesizer \"%s\"\n",
			synth_name);

	if ((synth != NULL) && synth_init ()) {
		spk_shut_up |= 0x80;	/* hopefully let the system work without the synth */
		return;
	}
	synth_buffering = 0;
}

// provide a file to users, so people can send to /dev/synth

static ssize_t
speakup_file_write (struct file *fp, const char *buf,
		    size_t nbytes, loff_t * ppos)
{
	size_t count = nbytes;
	const char *ptr = buf;

	int bytes;
	char tbuf[256];

	while (count > 0) {
		bytes = MIN (count, sizeof (tbuf));
		if (copy_from_user (&tbuf, ptr, bytes))
			return -EFAULT;

		count -= bytes;
		ptr += bytes;

		synth_write (tbuf, bytes);
	}
	return (ssize_t) nbytes;
}

static int
speakup_file_ioctl (struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	return 0;		// silently ignore
}

static ssize_t
speakup_file_read (struct file *fp, char *buf, size_t nbytes, loff_t * ppos)
{
	/*   *nod* *nod*  We'll take care of that, ma'am.  *flush*   */
	return 0;
}

static int
speakup_file_open (struct inode *ip, struct file *fp)
{
	if (synth_file_inuse)
		return -EBUSY;
	else if (synth == NULL)
		return -ENODEV;

	synth_file_inuse++;
	return 0;
}

static int
speakup_file_release (struct inode *ip, struct file *fp)
{
	synth_file_inuse = 0;
	return 0;
}

#if LINUX_VERSION_CODE >= 0x20300
static struct file_operations synth_fops = {
	read:speakup_file_read,
	write:speakup_file_write,
	ioctl:speakup_file_ioctl,
	open:speakup_file_open,
	release:speakup_file_release,
};
#else
static struct file_operations synth_fops = {
	NULL,			/* seek */
	speakup_file_read,
	speakup_file_write,
	NULL,			/* readdir */
	NULL,			/* poll */
	speakup_file_ioctl,
	NULL,			/* mmap */
	speakup_file_open,
	NULL,			/* flush */
	speakup_file_release,
	NULL,
	NULL,			/* fasync */
};
#endif

void
speakup_register_devsynth (void)
{
	static struct miscdevice synth_device;

	synth_device.minor = SYNTH_MINOR;
	synth_device.name = "synth";
	synth_device.fops = &synth_fops;

	if (misc_register (&synth_device))
		printk ("speakup:  Couldn't initialize miscdevice /dev/synth.\n");
	else
		printk
		    ("speakup:  initialized device: /dev/synth, node (MAJOR 10, MINOR 25)\n");
}

static void
reset_default_chars (void)
{
	static int first_pass = 1;
	int i;

	for (i = 0; i < 256; ++i) {
		// if pointing to allocated memory, free it!
		if (first_pass || characters[i] != default_chars[i]) {
			if (!first_pass)
				kfree (characters[i]);
			characters[i] = default_chars[i];
		}
	}
	first_pass = 0;
}

#ifdef CONFIG_PROC_FS

// speakup /proc interface code

/*
Notes:
currently, user may store an unlimited character definition

Usage:
cat /proc/speakup/version

cat /proc/speakup/characters > foo
less /proc/speakup/characters
vi /proc/speakup/characters

cat foo > /proc/speakup/characters
cat > /proc/speakup/characters
echo 39 apostrophe > /proc/speakup/characters
echo 87 w > /proc/speakup/characters
echo 119 w > /proc/speakup/characters
echo defaults > /proc/speakup/characters
echo reset > /proc/speakup/characters
*/

// the /proc/speakup directory inodes
// static struct proc_dir_entry *proc_speakup_device;
// synth-specific directory created in synth driver file

// this is the handler for /proc/speakup/version
static int
speakup_version_read_proc (char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int len = sprintf (page, "%s\n", SPEAKUP_VERSION);
	*start = 0;
	*eof = 1;
	return len;
}

// this is the read handler for /proc/speakup/characters
static int
speakup_characters_read_proc (char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int i;
	int len = 0;
	off_t begin = 0;

	for (i = 0; i < 256; ++i) {
		if (characters[i])
			len +=
			    sprintf (page + len, "%d\t%s\n", i, characters[i]);
		else
			len += sprintf (page + len, "%d\tNULL\n", i);
		if (len + begin > off + count)
			break;
		if (len + begin < off) {
			begin += len;
			len = 0;
		}
	}
	if (i >= 256)
		*eof = 1;
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return ((count < begin + len - off) ? count : begin + len - off);
}

static volatile int chars_timer_active = 0;	// indicates when a timer is set
#if (LINUX_VERSION_CODE < 0x20300)	/* is it a 2.2.x kernel? */
static struct wait_queue *chars_sleeping_list = NULL;
#else				/* nope it's 2.3.x */
static DECLARE_WAIT_QUEUE_HEAD (chars_sleeping_list);
#endif
static struct timer_list chars_timer;

static inline void
chars_stop_timer (void)
{
	if (chars_timer_active)
		del_timer (&chars_timer);
}

static int strings, rejects, updates;

static void
show_char_results (unsigned long data)
{
	int len;
	char buf[80];

	chars_stop_timer ();
	// tell what happened
	len = sprintf (buf, " updated %d of %d character descriptions",
		       updates, strings);
	if (rejects)
		sprintf (buf + len, " with %d reject%s\n",
			 rejects, rejects > 1 ? "s" : "");
	else
		sprintf (buf + len, "\n");
	printk (buf);
	chars_timer_active = 0;
}

/* this is the write handler for /proc/speakup/silent */
static int
speakup_silent_write_proc (struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	unsigned int currcons = fg_console;
	char ch = 0;
	if (count < 0 || count > 2)
		goto msg_out;
	if (count)
		get_user (ch, buffer);
	if (!count || ch == '\n')
		ch = '0';
	switch (ch) {
		case '0' :
			if (spk_killed)
				speakup_kill(currcons);
			goto out;
		case '1' :
			if (!spk_killed)
				speakup_kill(currcons);
			goto out;
		case '2' :
			if (spk_killed)
			spk_shut_up &= ~0x40;
			goto out;
		case '3' :
			if (!spk_killed)
				spk_shut_up |= 0x40;
			goto out;
	}

msg_out:
	printk (KERN_ALERT "setting silent: value not in range (0,3)\n");

out:
	return count;
}

// this is the write handler for /proc/speakup/characters
static int
speakup_characters_write_proc (struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	static const int max_description_len = 72;
	static int cnt = 0, num = 0, state = 0;
	static char desc[max_description_len + 1];
	static unsigned long jiff_last = 0;
	int i;
	char ch, *s1, *s2;

	// reset certain vars if enough time has elapsed since last called
	if (jiffies - jiff_last > HZ/10) {
		state = strings = rejects = updates = 0;
	}
	jiff_last = jiffies;
	// walk through the buffer
	for (i = 0; i < count; ++i) {
		get_user (ch, buffer + i);
		switch (state) {
		case 0:	// initial state, only happens once per "write"
			// if input matches "defaults" or "reset" reset_default_chars() and return
			if ((ch == '\n' && count == 1) || strchr ("dDrR", ch)) {
				reset_default_chars ();
				printk (KERN_ALERT
					"character descriptions reset to defaults\n");
				return count;
			}
			++state;
			// intentionally fall through to next state
		case 1:	// check for comment  and skip whitespace
			if (ch == '#') {	// comment
				num = -1;	// don't count as rejected
				state = 6;	// ignore remainder of line
				break;	// and don't process
			}
			if (ch == ' ' || ch == '\t')	// skip whitespace
				break;
			if (!isdigit (ch)) {
				state = 6;
				break;
			}
			num = ch - '0';	// convert first digit from ASCII
			++state;	// now expecting only digits or whitespace
			break;
		case 2:	// building number
			if (isdigit (ch)) {
				num *= 10;
				num += ch - '0';	// convert from ASCII
				break;
			}
			if (ch != ' ' && ch != '\t') {	// not whitespace
				state = 6;
				break;
			}
			// pointing to 1st whitespace past digits -- number is complete
			if (num < 0 || num > 255) {	// not in range
				state = 6;
				break;
			}
			if (num >= 27 && num <= 31) {	// no descriptions for these
				num = -1;	// don't count as rejected
				state = 6;	// but don't process either
				break;
			}
			++state;	// now looking for 1st char of description
			break;
		case 3:	/* skipping whitespace prior to description */
			if (ch == ' ' || ch == '\t')	// skip whitespace
				break;
			if (ch == '\n') {	// reached EOL
				state = 6;
				break;
			}
			cnt = 0;	// starting new description
			desc[cnt++] = ch;
			++state;	// now looking for EOL
			break;
		case 4:	// looking for newline
			if (ch != '\n') {	// not yet
				// add char to description
				desc[cnt++] = ch;
				if (cnt < max_description_len)
					break;	// building description
				// maximum description length reached, truncate remainder
				state = 5;
			}
			// prepare to work on next string
			state = state == 5 ? state : 1;
			// description string is complete
			desc[cnt] = '\0';	// NULL terminate
			// if new description matches old, we don't need to update
			s1 = desc;	// point to new description
			s2 = characters[num];	// point to old/current
			while (*s1 == *s2) {
				if (!*s1)	// reached end of strings
					break;
				++s1;
				++s2;
			}
			if (*s1 == *s2) {	// strings match
				++strings;
				break;
			}
			// handle new description
			// is this description the default or has it already been modified?
			if (characters[num] == default_chars[num])	// original
				characters[num] =
				    (char *) kmalloc (sizeof (char) *
						      (strlen (desc) + 1),
						      GFP_KERNEL);
			else	// already redefined/allocated
				krealloc (characters[num], strlen (desc) + 1);
			if (!characters[num]) {	// allocation failed
				characters[num] = default_chars[num];	// reset to default
				return -ENOMEM;
			}
			// got mem, copy the string
			strcpy (characters[num], desc);
			++updates;
			++strings;
			break;
		case 5:	// truncating oversized description
			if (ch == '\n')
				state = 1;	// ready to start anew
			// all other chars go to the bit bucket
			break;
		case 6:	// skipping all chars while looking for newline
			// we only get here if the data is invalid
			if (ch == '\n') {
				state = 1;
				// -1 indicates chars with no description (ASCII 27-31)
				if (num != -1)
					++rejects;
				++strings;
			}
			break;
		}		// end switch
	}			// finished processing entire buffer

	chars_stop_timer ();
	init_timer (&chars_timer);
	chars_timer.function = show_char_results;
#if (LINUX_VERSION_CODE >= 0x20300)	/* it's a 2.3.x kernel */
	init_waitqueue_head (&chars_sleeping_list);
#else				/* it's a 2.2.x kernel */
	init_waitqueue (&chars_sleeping_list);
#endif

	chars_timer.expires = jiffies + HZ/20;
# if (LINUX_VERSION_CODE >= 0x20300)	/* is it a 2.3.x kernel? */
	if (!chars_timer.list.prev)
		add_timer (&chars_timer);
# else
	if (!chars_timer.prev)
		add_timer (&chars_timer);
# endif
	chars_timer_active++;
	return count;
}

static int
human_readable (char *s, struct spk_variable *var, char *buf)
{
	char *fmt = var->build;	// point to start of it's format
	int len = 0;
	char *end = s + strlen (s) - 1;	// point to end of string holding value
	char *end_fmt = fmt + strlen (fmt) - 1;	// point to end of it's format

	while (*fmt != '_' && *s == *fmt) {
		++s;
		++fmt;
	}
	// s now points to beginning of unformatted value, find the end
	while (*end_fmt != '_' && *end == *end_fmt) {
		--end;
		--end_fmt;
	}
	// end now points to last char of unformatted value
	// store value converting to human readable if necessary
	for (; s <= end; ++s) {
		if (*s < 32) {	// don't print these directly
			// convert to hex, 0x00 - 0xff
			*(buf + len++) = '\\';
			*(buf + len++) = 'x';
			if (*s > 15)
				*(buf + len++) = '1';
			else
				*(buf + len++) = '0';
			if ((*s % 16) < 10)
				*(buf + len++) = (*s % 16) + '0';
			else
				*(buf + len++) = (*s % 16) + 'a' - 10;
		} else
			*(buf + len++) = *s;
		if (*s == '\\')
			*(buf + len++) = *s;	// double up on escape char
	}
	// append a newline
	*(buf + len++) = '\n';
	return len;
}

static int
find_config_var (const char *name, char ***value,
		 struct spk_variable **var, long **cfg_map)
{
	int i;

	if (synth != NULL)
		for (i = 0; synth->vars[i].id != NULL; i++) {
			if (strcmp (synth->vars[i].id, name) == 0) {
				*value = &(synth->config[i]);
				*var = &(synth->vars[i]);
				*cfg_map = &(synth->config_map);
				return i;
			}
		}

	for (i = 0; spk_vars[i].id != NULL; i++) {
		if (strcmp (spk_vars[i].id, name) == 0) {
			*value = &(spk_cfg[i]);
			*var = &(spk_vars[i]);
			*cfg_map = &spk_cfg_map;
			return i;
		}
	}

	return -1;
}

// this is the read handler for /proc/speakup/settings
int
speakup_settings_read_proc (char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	struct proc_dir_entry *ent = data;
	char **value;
	struct spk_variable *var;
	long *cfg_map;
	int index = find_config_var (ent->name, &value, &var, &cfg_map);

	*start = 0;
	*eof = 1;

	if (index == -1)
		return sprintf (page, "%s slipped through the cracks!\n",
				ent->name);

	return human_readable (*value, var, page);
}

char *
xlate (char *s)
{
	char *p = s, c;
	size_t len = strlen (s);
	int num, num_len;

	while ((p = strchr (p, '\\'))) {
		num_len = 1;
		switch (*(p + 1)) {
		case '\\':
			break;
		case 'r':
			*p = '\r';
			break;
		case 'n':
			*p = '\n';
			break;
		case 't':
			*p = '\t';
			break;
		case 'v':
			*p = '\v';
			break;
		case 'a':
			*p = '\a';
			break;
#ifdef fix_octal
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			if (sscanf (p + 1, "%3o%n", &num, &num_len))
				*p = (char) num;
			break;
#endif
		case 'x':
			c = *(p + 2);
			if (isdigit (c))
				num = c - '0';
			else if (isxdigit (c))
				num = tolower (c) - 'a' + 10;
			else
				break;
			num *= 0x10;
			c = *(p + 3);
			if (isdigit (c))
				num += c - '0';
			else if (isxdigit (c))
				num += tolower (c) - 'a' + 10;
			else
				break;
			num_len = 3;
			*p = (char) num;
			break;
		default:
			*p = *(p + 1);
		}
		num = p - s + num_len;
		++p;
		memmove (p, p + num_len, len - num);
	}
	return s;
}

#define NOT_IN_RANGE -1
#define NOT_ONE_OF -2
#define STRING_TOO_LONG -3
#define RESET_DEFAULT -4

// this is the write handler for /proc/speakup/settings
static int
speakup_settings_write_proc (struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	struct proc_dir_entry *ent = data;
	int ret = NOT_IN_RANGE, i, limit, val = 0;
	long *cfg_map;
	char *p = NULL, *parm, *formatted, *src, *dest, **value;
	struct spk_variable *sv;
	int index = find_config_var (ent->name, &value, &sv, &cfg_map);
	int currcons = 0;

	if (count < 0 || count > 1024)
		return -EINVAL;
	if (!(parm = (char *) __get_free_page (GFP_KERNEL)))
		return -ENOMEM;
	if (copy_from_user (parm, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}
	// NULL terminate
	if (*(parm + count - 1) == '\n')
		*(parm + count - 1) = '\0';
	else
		*(parm + count) = '\0';
	xlate (parm);
	formatted = parm + strlen (parm) + 1;
	if (index == -1) {
		printk (KERN_ALERT "%s slipped through the cracks!\n",
			ent->name);
		ret = count;
		goto out;
	}
	// wildcard
	if (!sv->flags && *sv->valid == '*') {
		// arbitrary 33 char limit
		if (strlen (parm) > PUNC_CHARS_SIZE) {
			ret = STRING_TOO_LONG;
			goto msg_out;
		}
		// all is well
	} else if (*parm && (sv->flags & USE_RANGE)) {
		// check range
		if (sv->flags & NUMERIC) {
			int neg = 0;
			// atoi on user provided value
			p = parm;
			if (*p == '-') {
				++neg;
				++p;
			}
			for (val = 0; *p; ++p) {
				if (!isdigit (*p))
					goto msg_out;
				val *= 10;
				val += *p - '0';
			}
			if (neg) {
				val = -val;
				neg = 0;
			}
			// atoi on lower limit of range
			p = sv->valid;
			if (*p == '-') {
				++neg;
				++p;
			}
			for (limit = 0; *p && *p != ','; ++p) {
				// valid range?
				if (!isdigit (*p))
					goto msg_out;
				limit *= 10;
				limit += *p - '0';
			}
			if (neg) {
				limit = -limit;
				neg = 0;
			}
			if (val < limit)
				goto msg_out;
			// should be pointing to the comma in range
			if (*p == ',')
				++p;
			else
				goto msg_out;
			// atoi on upper limit of range
			/* if bell_pos, this value is video_num_columns */
			dest = p;
			if (*p == '-') {
				++neg;
				++p;
			}
			for (limit = 0; *p; ++p) {
				// valid range?
				if (!isdigit (*p))
					goto msg_out;
				limit *= 10;
				limit += *p - '0';
			}
			if (neg)
				limit = -limit;
			if (index == BELL_POS) {
				limit = video_num_columns;
				sprintf (dest, "%-d", limit);
			}
			if (val > limit)
				goto msg_out;
			// numeric val is in range
		} else {
			// range of chars
			p = sv->valid;
			// parm should be exactly 1 char
			if (*(parm + 1)) {
				ret = NOT_ONE_OF;
				goto msg_out;
			}
			*parm = tolower (*parm);
			// expecting a comma here and char in range
			if (*(p + 1) != ',' || *parm < *p || *parm > *(p + 2)) {
				ret = NOT_ONE_OF;
				goto msg_out;
			}
		}
	} else if (*parm && (sv->flags & MULTI_SET)) {
		// each char must be one of valid
		if (strlen (parm) > PUNC_CHARS_SIZE) {
			ret = STRING_TOO_LONG;
			goto msg_out;
		}
		for (p = parm; *p; ++p)
			if (!strchr (sv->valid, *p)) {
				ret = NOT_ONE_OF;
				goto msg_out;
			}
	} else if (*parm) {
		// find_is_one_of
		p = sv->valid;
		// parm should be exactly 1 char
		if (*(parm + 1)) {
			ret = NOT_ONE_OF;
			goto msg_out;
		}
		*parm = tolower (*parm);
		for (; *p; ++p)
			if (*parm == *p)
				break;
		if (!*p) {
			// no match found
			ret = NOT_ONE_OF;
			goto msg_out;
		}
	}
	// in range or doesn't matter
	if (sv->flags & (HARD_DIRECT | SOFT_DIRECT) || !sv->flags) {
		// replace string provided by user with formated string
		if (*parm) {
			// copy the format string up to the underscore
			src = sv->build;
			dest = formatted;
			while (*src && *src != '_')
				*dest++ = *src++;
			// append parm
			p = parm;
			while (*p)
				*dest++ = *p++;
			// skip over the underscore
			if (*src == '_')
				++src;
			// append remainder of format
			while (*src)
				*dest++ = *src++;
			// null terminate
			*dest = '\0';
		}
	}
	// reset to default?
	if (!*parm) {
		// copy the format string up to the underscore
		src = sv->build;
		dest = formatted;
		while (*src && *src != '_')
			*dest++ = *src++;
		// append default parameter
		p = sv->param;
		while (*p)
			*dest++ = *p++;
		// skip over the underscore
		if (*src == '_')
			++src;
		// append remainder of format
		while (*src)
			*dest++ = *src++;
		// null terminate
		*dest = '\0';
		ret = RESET_DEFAULT;
	}
	// store formatted value
	if (*cfg_map & (0x01 << index))
		krealloc (*value, strlen (formatted) + 1);
	else {
		*cfg_map |= (0x01 << index);
		*value = kmalloc (strlen (formatted) + 1, GFP_KERNEL);
	}
	strcpy (*value, formatted);
	if (sv->flags & HARD_DIRECT) {
		synth_write (formatted, strlen (formatted));
		synth_write ("\n", 1);
	}

	/* set bell position */
	if (index == BELL_POS)
		bell_pos = val;

	/* bleep settings need to be replicated through every console */
	if ((index == LINE_WRAP_BLEEP) || (index == ATTRIBUTE_BLEEP))
		for (i = 0; speakup_console[i] != NULL; ++i) {
			if (index == LINE_WRAP_BLEEP) {
				if (*parm == '1' || !*parm)
					speakup_console[i]->sound |=
					    LineWrapBleep;
				else
					speakup_console[i]->sound &=
					    LineWrapMask;
			} else {
				// ATTRIBUTE_BLEEP
				if (*parm == '1' || !*parm)
					speakup_console[i]->sound |=
					    AttributeChangeBleep;
				else
					speakup_console[i]->sound &=
					    AttributeChangeMask;
			}
		}
	if (ret == RESET_DEFAULT)
		printk (KERN_ALERT "%s reset to default value\n", sv->id);
	ret = count;
	goto out;

      msg_out:
	switch (ret) {
	case NOT_IN_RANGE:
		p = "value not in range";
		ret = count;
		break;
	case NOT_ONE_OF:
		p = "value not one of";
		ret = count;
		break;
	case STRING_TOO_LONG:
		p = "string longer than 33 chars";
		ret = count;
		break;
	}
	printk (KERN_ALERT "setting %s (%s): %s (%s)\n", ent->name, parm, p,
		sv->valid);

      out:
	free_page ((unsigned long) parm);
	return ret;
}

// this is the read handler for /proc/speakup/synth
static int
speakup_synth_read_proc (char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len = sprintf (page, "%s\n", synth_name);
	*start = 0;
	*eof = 1;
	return len;
}

// this is the write handler for /proc/speakup/synth
static int
speakup_synth_write_proc (struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	int ret = count, i = 0;
	char new_synth_name[10];
	struct spk_synth *new_synth = NULL;

	if (count < 1 || count > 9)
		return -EINVAL;
	if (copy_from_user (new_synth_name, buffer, count))
		return -EFAULT;
	// NULL terminate
	if (new_synth_name[count - 1] == '\n')
		new_synth_name[count - 1] = '\0';
	else
		new_synth_name[count] = '\0';
	strlwr (new_synth_name);
	if (!strcmp (new_synth_name, synth->name)) {
		printk (KERN_WARNING "already in use\n");
		return ret;
	}

	for (i = 0; !new_synth && synths[i]; ++i) {
		if (!strcmp (synths[i]->name, new_synth_name))
			new_synth = synths[i];
	}

	if (!new_synth && strcmp (new_synth_name, "none")) {
		printk (KERN_WARNING
			"there is no synth named \"%s\" built into this kernel\n",
			new_synth_name);
		return ret;
	}

	/* At this point, we know that new_synth_name is valid, built into the
	   kernel, and not already in use.
	   Next, do the magic to select it as the new active synth */

	// still working here.  announce attempt and leave
	printk (KERN_ALERT "attempt to change synth to %s\n", new_synth_name);
	return ret;
}

// called by proc_root_init() to initialize the /proc/speakup subtree
void __init
proc_speakup_init (void)
{
	int i;
	char path[40];
	mode_t mode;
	struct proc_dir_entry *ent;
#define PROC_SPK_DIR "speakup"

	ent = create_proc_entry (PROC_SPK_DIR, S_IFDIR, 0);
	if (!ent) {
		printk (KERN_ALERT "Unable to create /proc/speakup entry.\n");
		return;
	}

	ent = create_proc_entry (PROC_SPK_DIR "/version", S_IFREG | S_IRUGO, 0);
	ent->read_proc = speakup_version_read_proc;

	ent = create_proc_entry (PROC_SPK_DIR "/silent", S_IFREG | S_IWUGO, 0);
	ent->write_proc = speakup_silent_write_proc;

	ent =
	    create_proc_entry (PROC_SPK_DIR "/characters",
			       S_IFREG | S_IRUGO | S_IWUGO, 0);
	ent->read_proc = speakup_characters_read_proc;
	ent->write_proc = speakup_characters_write_proc;

	ent =
	    create_proc_entry (PROC_SPK_DIR "/synth",
			       S_IFREG | S_IRUGO | S_IWUGO, 0);
	ent->read_proc = speakup_synth_read_proc;
	ent->write_proc = speakup_synth_write_proc;

	if (synth != NULL) {
		for (i = 0; synth->vars[i].id != NULL; i++) {
			mode = S_IFREG | S_IRUGO;
			if (~(synth->vars[i].flags) & NO_USER)
				mode |= S_IWUGO;
			sprintf (path, PROC_SPK_DIR "/%s", synth->vars[i].id);
			ent = create_proc_entry (path, mode, 0);
			ent->read_proc = speakup_settings_read_proc;
			if (mode & S_IWUGO)
				ent->write_proc = speakup_settings_write_proc;
			ent->data = (void *) ent;
		}
	}

	for (i = 0; spk_vars[i].id != '\0'; ++i) {
		if (!strcmp (spk_vars[i].id, "punc_none"))
			continue;	// no /proc file for this one -- it's empty
		mode = S_IFREG | S_IRUGO;
		if (~(spk_vars[i].flags) & NO_USER)
			mode |= S_IWUGO;
		sprintf (path, PROC_SPK_DIR "/%s", spk_vars[i].id);
		ent = create_proc_entry (path, mode, 0);
		ent->read_proc = speakup_settings_read_proc;
		if (mode & S_IWUGO)
			ent->write_proc = speakup_settings_write_proc;
		ent->data = (void *) ent;
	}

	// initialize the synth-specific subtree
	if (synth != NULL)
		proc_speakup_synth_init ();
}

#endif				// CONFIG_PROC_FS

#if LINUX_VERSION_CODE >= 0x020300
/* version 2.3.x */
void __init
speakup_init (int currcons)
{
	unsigned char i;
	reset_default_chars ();

	speakup_console[currcons] =
	    (struct spk_t *) alloc_bootmem (sizeof (struct spk_t) + 1);

	for (i = 0; i < MIN_NR_CONSOLES; i++) {
		spk_shut_up = 0;
		spk_x = x;
		spk_y = y;
		spk_attr = spk_old_attr = attr;
		spk_pos = pos;
		spk_sound = 3;
		spk_parked = 0;
		spk_o_cp = spk_o_cy = spk_o_cx = 0;
	}
	for (; i < MAX_NR_CONSOLES; i++)
		speakup_console[i] = NULL;
	speakup_open (currcons);	/* we'll try it here. */
	printk ("%s: initialized\n", SPEAKUP_VERSION);
}

#else
/* version 2.2.x */
unsigned long __init
speakup_init (unsigned long kmem_start, unsigned int currcons)
{
	unsigned char i;

	reset_default_chars ();

	speakup_console[currcons] = (struct spk_t *) kmem_start;
	kmem_start += (sizeof (struct spk_t) * MIN_NR_CONSOLES) + 1;

	for (i = 0; i < MIN_NR_CONSOLES; i++) {
		spk_shut_up = 0;
		spk_x = x;
		spk_y = y;
		spk_attr = spk_old_attr = attr;
		spk_pos = pos;
		spk_sound = 3;
		spk_parked = 0;
		spk_o_cp = spk_o_cy = spk_o_cx = 0;
	}
	for (; i < MAX_NR_CONSOLES; i++)
		speakup_console[i] = NULL;
	speakup_open (currcons);	/* we'll try it here. */
	printk ("%s: initialized\n", SPEAKUP_VERSION);
	speakup_register_devsynth ();
	return kmem_start;
}
#endif

void
speakup_allocate (int currcons)
{

	speakup_console[currcons] =
	    (struct spk_t *) kmalloc (sizeof (struct spk_t) + 1, GFP_KERNEL);

	spk_shut_up = 0;
	spk_x = x;
	spk_y = -1;
	spk_old_attr = spk_attr = attr;
	spk_pos = pos;
	spk_sound = 3;
	spk_parked = 0;
	spk_o_cp = spk_o_cy = spk_o_cx = 0;
}

void
speakup_date (unsigned int currcons)
{
	spk_x = spk_cx = x;
	spk_y = spk_cy = y;
	spk_pos = spk_cp = pos;
	spk_old_attr = spk_attr;
	spk_attr = ((scr_readw ((unsigned short *) spk_pos) & 0xff00) >> 8);
}

void
speakup_check (unsigned int currcons)
{
	if (!spk_keydown)
		return;

	if ((x == spk_o_cx + 1 || x == spk_o_cx - 1) && y == spk_o_cy) {
		if (spk_keydown > 1 && x > spk_o_cx)
			say_prev_char (currcons);
		else
			say_curr_char (currcons);
	} else
	    if ((x == 0 && spk_o_cx == video_num_columns - 1
		 && y == spk_o_cy + 1)
		|| (x == video_num_columns - 1 && spk_o_cx == 0
		    && y == spk_o_cy - 1)) {
		say_curr_char (currcons);
	} else if (y == spk_o_cy && (y == bottom || y == top)) {
		say_curr_line (currcons);
	} else if ((y > spk_o_cy || y < spk_o_cy)) {
		say_curr_line (currcons);
	} else if (pos == spk_o_cp) {
		say_curr_char (currcons);
	} else
		say_curr_word (currcons);

	spk_o_cp = pos;
	spk_o_cx = x;
	spk_o_cy = y;
	spk_keydown = 0;
	spk_parked &= 0xfe;
	speakup_date (currcons);
}

/* These functions are the interface to speakup from the actual kernel code. */

void
speakup_bs (int currcons)
{
	if (!spk_parked)
		speakup_date (currcons);
	if ((!spk_shut_up || spk_shut_up & 0x02) && (currcons == fg_console)
	    && spk_keydown) {
		spk_keydown = 0;
		say_curr_char (currcons);
	}
}

void
speakup_con_write (int currcons, const char *str, int len)
{
	if (!spk_shut_up && (currcons == fg_console)) {
		if (bell_pos && spk_keydown && (x == bell_pos - 1))
			bleep(38);
		spkup_write (str, len);
		}
}

void
speakup_con_update (int currcons)
{
	if (speakup_console[currcons] == NULL)
		return;
	if (!spk_parked)
		speakup_date (currcons);
	if ((currcons == fg_console) && !spk_parked && spk_shut_up & 0x02)
		speakup_check (currcons);
}

void
speakup_control(int currcons, struct kbd_struct * kbd, int value)
{
        /* speakup output number 0 for altgr key with spkup_num_lock_on=1 */
        if (spkup_num_lock_on && (value==KVAL(K_ALTGR))) {
	  speakup_savekey(0); /* clear!  brzzzot */
	  put_queue('0');
	  return;
	}
 
	/* let there be speech! */
	switch (value) {
		case KVAL(K_CAPS):
			if (vc_kbd_led(kbd , VC_CAPSLOCK)){
				cap_on=1; /* record this */
				spk_control(fg_console, 10);
				}
			else
				{
				cap_on=0; /* record this */
				spk_control(fg_console, 11);
				}
			break;
		case KVAL(K_NUM):
			if (vc_kbd_led(kbd , VC_NUMLOCK)){
				spkup_num_lock_on=1;	
				spk_control(fg_console, 12);
				}
			else	{	
				spkup_num_lock_on=0;
				spk_control(fg_console, 13);
				}
			break;
		case KVAL(K_HOLD):
			if (vc_kbd_led(kbd , VC_SCROLLOCK))
				spk_control(fg_console, 14);
			else
				spk_control(fg_console, 15);
			break;
	default:
	  spk_control(currcons,  value);
	}
}

