#
# Makefile for the Margi DVD-to-Go driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := margilib.o

CFLAGS_margi_cs.o	=	-DUSE_OSD -DNOINT -DDVB  -DUSE_ZV

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

list-multi	:=	margi_cs.o

margi_cs-objs	:= 	margi.o cardbase.o i2c.o dram.o osd.o audio.o \
			video.o streams.o decoder.o spu.o crc.o ringbuffy.o \
			dvb_filter.o cvdv.o

export-objs	:=	dvbdev.o

obj-m += margi_cs.o dvbdev.o dmxdev.o dvb_demux.o

include $(TOPDIR)/Rules.make

margi_cs.o: $(margi_cs-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(margi_cs-objs)

