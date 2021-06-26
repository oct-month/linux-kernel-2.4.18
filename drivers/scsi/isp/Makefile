#
# %W% %G%
#
# Copyright (c) 2001 by Matthew Jacob
#
#	This software is free software; you can redistribute it and/or
#	modify it under the terms of the GNU Library General Public
#	License as published by the Free Software Foundation; version 2.
#
#	This software is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#	Library General Public License for more details.
#
#	You should have received a copy of the GNU Library General Public
#	License along with this software; if not, write to the Free
#	Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#	The author may be reached via electronic communications at
#
#		mjacob@feral.com
#
#	or, via United States Postal Address
#
#		Matthew Jacob
#		Feral Software
#		PMB #825
#		5214-F Diamond Hts. Blvd
#		San Francisco, CA, 94131
#
#

O_TARGET			:= isp_drv.o
list-multi			:= isp_mod.o

obj-$(CONFIG_SCSI_NEWISP)	+= isp_mod.o

HDRS=	asm_1000.h asm_1040.h asm_1080.h asm_12160.h asm_2100.h asm_2200.h \
	asm_2300.h isp_inline.h isp_linux.h isp_target.h isp_tpublic.h \
	ispmbox.h ispreg.h ispvar.h

SRCS=	isp.c isp_linux.c isp_pci.c isp_target.c

ISP_OBJS = isp_linux.o isp_pci.o isp.o

# Override our module desitnation
MOD_DESTDIR = $(shell cd .. && $(CONFIG_SHELL) $(TOPDIR)/scripts/pathdown.sh)
MOD_TARGET = isp_mod.o

EXTRA_CFLAGS = -I..
include $(TOPDIR)/Rules.make


isp_mod.o: $(ISP_HDRS) $(ISP_SRCS) $(ISP_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(ISP_OBJS)
