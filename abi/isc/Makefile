#	$Id: Makefile,v 1.1 2001/10/31 15:13:15 hch Exp $

O_TARGET        := dummy.o

list-multi      := abi-isc.o
abi-isc-objs	:= sysent.o misc.o

obj-$(CONFIG_ABI_ISC)		+= abi-isc.o


include $(TOPDIR)/Rules.make

abi-isc.o: $(abi-isc-objs)
	$(LD) -r -o $@ $(abi-isc-objs)
