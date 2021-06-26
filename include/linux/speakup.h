#ifndef __SPEAKUP_H
#define __SPEAKUP_H

#include <linux/version.h>
struct serial_state;
struct kbd_struct;

#define QUICK_QUIET 0x40 /* flush buffer and shut up immediately! */
#define LINE_QUIET 0x41 /* flush  buffer up to next NULL/cr and continue */

/* start f key pad keys */
#define SAY_NOTHING 0x00 /* this is a dummy command for speakup */
#define SAY_PREV_CHAR 0x01 /* say character left of this char, LINE_QUIET 0x01 */
#define SAY_CHAR 0x02 /* say this character */
#define SAY_NEXT_CHAR 0x03 /* say char right of this char */
#define SAY_PREV_WORD 0x04
#define SAY_WORD 0x05 /* say this word under reading cursor */
#define SAY_NEXT_WORD 0x06
#define SAY_PREV_LINE 0x07 /* say line above this line */
#define SAY_LINE 0x08 /* say this line */
#define SAY_NEXT_LINE 0x09
#define SAY_SCREEN 0x0a    /* + */
#define SPEAKUP_PARKED 0x0b /* - */
#define SPEAKUP_CURSORING 0x0c /* * */
#define SPEAKUP_CUT 0x0d /* / */
#define SAY_POSITION 0x0e /* dot */
#define FULL_QUIET 0x0f /* enter */
/* end of key pad keys */

#define RIGHT_EDGE 0x10 
#define SAY_PHONETIC_CHAR 0x11 /* say this character phonetically */
#define SPELL_WORD 0x12 /* spell this word letter by letter */
#define SPELL_PHONETIC_WORD 0x13
#define TOP_EDGE 0x14 /* move to top edge of screen */
#define SAY_WINDOW 0x15
#define SET_SPEED 0x16
#define SET_PITCH 0x17
#define SET_PUNCTUATION 0x18
#define SET_VOICE 0x19
#define SET_TONE 0x1a
#define BOTTOM_EDGE 0x1b
#define SPEECH_OFF 0x1c
#define SAY_ATTRIBUTES 0x1d
#define LEFT_EDGE 0x1e
#define INS_TOGGLE 0x1f
#define SAY_FROM_TOP 0x20
#define SAY_TO_BOTTOM 0x21
#define SAY_FROM_LEFT 0x22
#define SAY_TO_RIGHT 0x23
#define SAY_CHAR_NUM 0x24
#define SPEECH_KILL 0x25
#define SPEAKUP_PASTE 0x28
#define END_OF_LINE 0x29 //mv to last char on line.

/* our basic punctuation levels as array indecies */
#define NONE 0x30 /* no punctuation spoken */
#define SOME 0x31 /* some punctuation spoken */
#define MOST 0x32 /* most punctuation spoken */
#define ALL 0x33 /* you guessed it. */

/* let's develop a structure for keeping our goodies in. */
struct spk_t {
	unsigned char reading_attr;
	unsigned char old_attr;
	char parked;
	char shut_up;
	char sound;
	unsigned long reading_x, cursor_x, old_cursor_x;
	unsigned long reading_y, cursor_y, old_cursor_y;
	unsigned long reading_pos, cursor_pos, old_cursor_pos;
};

/* now some defines to make these easier to use. */
#define spk_shut_up speakup_console[currcons]->shut_up
#define spk_killed (speakup_console[currcons]->shut_up & 0x40)
#define spk_x speakup_console[currcons]->reading_x
#define spk_cx speakup_console[currcons]->cursor_x
#define spk_o_cx speakup_console[currcons]->old_cursor_x
#define spk_y speakup_console[currcons]->reading_y
#define spk_cy speakup_console[currcons]->cursor_y
#define spk_o_cy speakup_console[currcons]->old_cursor_y
#define spk_pos (speakup_console[currcons]->reading_pos)
#define spk_cp speakup_console[currcons]->cursor_pos
#define spk_o_cp speakup_console[currcons]->old_cursor_pos
#define spk_attr speakup_console[currcons]->reading_attr
#define spk_old_attr speakup_console[currcons]->old_attr
#define spk_parked speakup_console[currcons]->parked
#define spk_sound speakup_console[currcons]->sound

/* how about some prototypes! */
extern void speakup_shut_up(unsigned  int);
extern void say_attributes(int);
extern void say_curr_char(unsigned int);
extern void say_phonetic_char(unsigned int);
extern void say_prev_char(unsigned int);
extern void say_next_char(unsigned int);
extern void say_curr_word(unsigned int);
extern void say_prev_word(unsigned int);
extern void say_next_word(unsigned int);
extern void spell_word(unsigned int);
extern void say_curr_line(unsigned int);
extern void say_prev_line(unsigned int);
extern void say_next_line(unsigned int);
extern void say_screen(unsigned int);
extern void top_edge(unsigned int);
extern void bottom_edge(unsigned int);
extern void left_edge(unsigned int);
extern void right_edge(unsigned int);
extern void say_position(unsigned int);
extern void say_char_num(unsigned int);
extern void speakup_off(unsigned int);
extern void speakup_kill(unsigned int);
extern void say_from_top(unsigned int);
extern void say_to_bottom(unsigned int);
extern void say_from_left(unsigned int);
extern void say_to_right(unsigned int);
extern void speakup_status(unsigned int);
extern void function_announce(unsigned int);
extern void speakup_open(unsigned int);
extern void speakup_date(unsigned int);
extern void speakup_precheck(unsigned int);
extern void speakup_check(unsigned int);
extern int spkup_write(const char *, int);
extern void speakup_parked(unsigned int);
extern void speakup_cursoring(unsigned int);
extern void speakup_cut(unsigned int, struct tty_struct *);
extern void speakup_paste(struct tty_struct *);
extern void spk_skip(unsigned short);
extern int do_spk_ioctl(int,unsigned char *data,int,void *);
extern int is_alive(void);

/* Speakup variable structure and definitions */
#define HARD_DIRECT 0x01	/* a variable that is immediately sent to the
				   hardware synth */
#define SOFT_DIRECT 0x02	// as above, but sent to speakup
#define NUMERIC 0x04	/* ASCII-represented numerical value
			   currently requires USE_RANGE to be set */
#define USE_RANGE 0x08	// all values in ASCII range from valid are accepted
#define BUILDER 0x10 // variable must be built into the reset string
#define NO_USER 0x20	// variable is not allowed to be set by the user
#define ALLOW_BLANK 0x40	// alias-only flag to allow a blank parameter
#define MULTI_SET 0x80	// ASCII string of chars, each must be one of the valid set
#define END_VARS { NULL, NULL, NULL, 0, NULL }
#define TOGGLE "0,1"

struct spk_variable {
	char *id;	// command name
	char *param;	// value of the parameter to the command
	char *build;	/* a string, describing how to construct the
			   string sent to the synth, with the character '_' to be filled in with
			   the parameter.  eg.  "\x01_P" will convert to "\x01+30P", if param is
			   "+30" */
	int flags;	// see #defines below
	char *valid;	/* If the flag USE_RANGE is given, valid is a range
			   described by "lowval,highval".  For example, "0,20" is the range 0-20.
			   If USE_RANGE is not given, valid is a NULL terminated set of valid
			   values for param.  eg. "aeiou".  Wildcard "*" matches anything. */
};

/* speakup_drvcommon.c */
struct spk_synth {
	const char *name;
	const char *version;
	const char *proc_name;
	const char *init;
	const char *reinit;
	unsigned short delay_time;
	unsigned short trigger_time;
	unsigned char jiffy_delta;
	unsigned short full_time;
	struct spk_variable *vars;
	char **config;
	long config_map;
	int (*probe)(void);
	void (*catch_up)(unsigned long data);
	void (*write)(char c);
	int (*is_alive)(void);
};

extern struct spk_synth *synth;
extern int synth_request_region(unsigned long, unsigned long);
extern int synth_release_region(unsigned long, unsigned long);
extern unsigned char synth_jiffy_delta;
extern int synth_port_tts;
extern volatile int synth_timer_active;
#if (LINUX_VERSION_CODE < 0x20300) /* is it a 2.2.x kernel? */
extern struct wait_queue *synth_sleeping_list;
#else /* nope it's 2.3.x */
extern wait_queue_head_t synth_sleeping_list;
#endif
extern struct timer_list synth_timer;
extern unsigned short synth_delay_time; /* time to schedule handler */
extern unsigned short synth_trigger_time;
extern unsigned short synth_full_time;
extern char synth_buffering;  /* flag to indicate we're buffering */
extern unsigned char synth_buffer[];  /* guess what this is for! */
extern unsigned short synth_end_of_buffer;
extern volatile unsigned short synth_queued_bytes, synth_sent_bytes; 
extern unsigned short spkup_num_lock_on; /* a variable used by keyboard.c 
				     but updated here */

extern void initialize_uart(struct serial_state *);
extern void synth_delay(int ms);
extern void synth_stop_timer(void);
extern void synth_buffer_add(char ch);
extern void synth_write(const char *buf, size_t count);

#ifdef CONFIG_SPEAKUP
extern struct spk_t *speakup_console[];
extern void speakup_allocate(int);
extern void speakup_bs(int);
extern void speakup_con_write(int, const char *, int);
extern void speakup_con_update(int);
extern void speakup_init(int);
extern void speakup_reset(int, unsigned char);
extern void speakup_control(int, struct kbd_struct *, int);
extern int speakup_diacr(unsigned char,unsigned int);
extern void speakup_savekey(unsigned char);
#else
static inline void speakup_allocate(int currcons) {};
static inline void speakup_bs(int currcons) {};
static inline void speakup_con_write(int currcons, const char *str, int len) {};
static inline void speakup_con_update(int currcons) {};
static inline void speakup_init(int currcons) {};
static inline void speakup_reset(int fg_console, unsigned char type) {};
static inline void speakup_control(int fg_console, struct kbd_struct * kbd, int value) {};
static inline int speakup_diacr(unsigned char ch, unsigned int fg_console) {return 0;};
static inline void speakup_savekey(unsigned char ch) {};
#endif
#endif

