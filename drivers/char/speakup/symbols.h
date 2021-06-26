#ifndef _SPEAKUP_SYMBOLS_H
#define _SPEAKUP_SYMBOLS_H

#include <linux/speakup.h>

#define PUNC_CHARS_SIZE 33
#define PUNC_CHARS "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"

#define DEFAULT_SPKUP_VARS \
/* line_wrap_bleep */	"1", \
/* attribute_bleep */	"1", \
/* bleep_time (ms) */	"3", \
/* spell_delay */	"1", \
/* punc_level */	"1", \
/* say_control */	"0", \
/* no_interrupt */	"0", \
/* punc_none */ "", \
/* punc_some */	"/$%&@", \
/* punc_most */	"($%&#@=+*^<>|\\)", \
/* punc_all */ PUNC_CHARS, \
/* key_echo */	"1", \
/* bell_pos */	"0"

// alias the necessary hardware constants off to generalized constants
#define FLUSH 0
#define PITCH 1
#define CAPS_START 2
#define CAPS_STOP 3

#define LINE_WRAP_BLEEP 0
#define ATTRIBUTE_BLEEP 1
#define BLEEP_TIME 2
#define SPELL_DELAY 3
#define PUNCT_LEVEL 4
#define SAY_CONTROL 5
#define NO_INTERRUPT 6
#define PUNC_NONE 7
#define PUNC_SOME 8
#define PUNC_MOST 9
#define PUNC_ALL 10
#define KEY_ECHO 11
#define BELL_POS 12

// beginning of the 4 punc levels
#define PUNC_OFFSET PUNC_NONE

#define SPKUP_VARS \
{"line_wrap_bleep", "1", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), TOGGLE}, \
{"attribute_bleep", "1", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), TOGGLE}, \
{"bleep_time", "3", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), "1,9"}, \
{"spell_delay", "1", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), "1,5"}, \
{"punc_level", "1", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), "0,3"}, \
{"say_control", "0", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), TOGGLE}, \
{"no_interrupt", "0", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), TOGGLE}, \
{"punc_none", "", "_", (SOFT_DIRECT|NO_USER), ""}, \
{"punc_some", "/$%&", "_", (MULTI_SET|SOFT_DIRECT), PUNC_CHARS}, \
{"punc_most", "($%&#@=+*^<>|\\)", "_", (MULTI_SET|SOFT_DIRECT), PUNC_CHARS}, \
{"punc_all", PUNC_CHARS, "_", (SOFT_DIRECT|NO_USER), ""}, \
{"key_echo", "1", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), TOGGLE}, \
{"bell_pos", "0", "_", (NUMERIC|SOFT_DIRECT|USE_RANGE), "0,200"}, \
END_VARS
#endif
