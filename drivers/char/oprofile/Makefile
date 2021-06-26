
#
# Makefile for linux/drivers/net/bcm
#

O_TARGET := oprofile_foo.o
obj-$(CONFIG_OPROFILE) += oprofile_mod.o


oprofile-objs := op_init.o op_util.o oprofile.o op_nmi.o op_rtc.o oprofile_nmi.o op_syscalls.o op_events.o op_apic.o op_fixmap.o compat.o

EXTRA_CFLAGS = -DEXPECT_OK

oprofile_mod.o: $(oprofile-objs)
	$(LD) -r -o $@ $(oprofile-objs)

oprofile_nmi.o:
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -traditional oprofile_nmi.S -c -o oprofile_nmi.o


include $(TOPDIR)/Rules.make

