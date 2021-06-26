#	$Id: Makefile,v 1.3 2001/10/31 15:08:46 hch Exp $

O_TARGET        := dummy.o

list-multi      := abi-cxenix.o
abi-cxenix-objs	:= sysent.o misc.o stubs.o signal.o pathconf.o utsname.o

obj-$(CONFIG_ABI_SCO)	+= abi-cxenix.o


include $(TOPDIR)/Rules.make

abi-cxenix.o: $(abi-cxenix-objs)
	$(LD) -r -o $@ $(abi-cxenix-objs)
