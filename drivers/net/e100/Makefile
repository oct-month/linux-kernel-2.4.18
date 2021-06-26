#
# Makefile for the Intel's E100 ethernet driver
#

obj-$(CONFIG_NET_E100) += e100.o

e100-objs := e100_main.o e100_config.o e100_proc.o e100_phy.o \
	     e100_eeprom.o e100_test.o

include $(TOPDIR)/Rules.make

e100.o: $(e100-objs)
	$(LD) -r -o $@ $(e100-objs)

