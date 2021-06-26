#
# Makefile for the Cisco iSCSI client
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := iscsi.o

EXTRA_CFLAGS := -I../../scsi

obj-y   :=  iscsi_main.o iscsi-login.o iscsiAuthClient.o iscsiAuthClientGlue.o md5.o

obj-m   := $(O_TARGET)

include $(TOPDIR)/Rules.make
