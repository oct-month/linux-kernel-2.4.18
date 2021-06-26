#
# Makefile for the speakup speech output system.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

DRIVERS := speakup_dtlk.c speakup_ltlk.c speakup_acntpc.c \
	speakup_acntsa.c speakup_spkout.c speakup_txprt.c \
	speakup_bns.c speakup_audptr.c speakup_decext.c \
	speakup_dectlk.c speakup_apolo.c
O_TARGET	:= spk.o
O_OBJS		:= 
export-objs	:=

ifeq ($(CONFIG_SPEAKUP),y)
O_OBJS		+= speakup.o speakup_drvcommon.o
endif
ifeq ($(CONFIG_SPEAKUP_DTLK),y)
O_OBJS		+= speakup_dtlk.o
endif
ifeq ($(CONFIG_SPEAKUP_LTLK),y)
O_OBJS		+= speakup_ltlk.o
endif
ifeq ($(CONFIG_SPEAKUP_ACNTPC),y)
O_OBJS		+= speakup_acntpc.o
endif
ifeq ($(CONFIG_SPEAKUP_ACNTSA),y)
O_OBJS		+= speakup_acntsa.o
endif
ifeq ($(CONFIG_SPEAKUP_TXPRT),y)
O_OBJS		+= speakup_txprt.o
endif
ifeq ($(CONFIG_SPEAKUP_BNS),y)
O_OBJS		+= speakup_bns.o
endif
ifeq ($(CONFIG_SPEAKUP_AUDPTR),y)
O_OBJS		+= speakup_audptr.o
endif
ifeq ($(CONFIG_SPEAKUP_DECTLK),y)
O_OBJS		+= speakup_dectlk.o
endif
ifeq ($(CONFIG_SPEAKUP_DECEXT),y)
O_OBJS		+= speakup_decext.o
endif
ifeq ($(CONFIG_SPEAKUP_APOLO),y)
O_OBJS		+= speakup_apolo.o
endif
ifeq ($(CONFIG_SPEAKUP_SPKOUT),y)
O_OBJS		+= speakup_spkout.o
endif
ifeq ($(CONFIG_SPEAKUP_KEYMAP),y)
O_OBJS += speakupmap.o
endif

obj-y += $(O_OBJS)
include $(TOPDIR)/Rules.make

speakup.o: $(DRIVERS)

speakupmap.o: speakupmap.c

speakupmap.c: speakupmap.map
	set -e ; loadkeys --mktable $< | sed -e 's/^static *//' > $@

