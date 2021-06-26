#	$Id: Makefile,v 1.4 2001/10/03 22:47:07 hch Exp $

O_TARGET        := dummy.o

list-multi      := abi-solaris.o
abi-solaris-objs:= lfs.o solarisx86.o socket.o stat.o sysent.o

obj-$(CONFIG_ABI_SOLARIS)	+= abi-solaris.o


include $(TOPDIR)/Rules.make

abi-solaris.o: $(abi-solaris-objs)
	$(LD) -r -o $@ $(abi-solaris-objs)
