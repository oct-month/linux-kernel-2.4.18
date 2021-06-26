#	$Id: Makefile,v 1.2 2001/10/31 15:11:54 hch Exp $

O_TARGET        := dummy.o

list-multi      := abi-util.o
abi-util-objs	:= plist.o

obj-$(CONFIG_ABI)	+= abi-util.o


include $(TOPDIR)/Rules.make

abi-util.o: $(abi-util-objs)
	$(LD) -r -o $@ $(abi-util-objs)
