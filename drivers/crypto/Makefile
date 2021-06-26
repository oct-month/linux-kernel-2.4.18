# File: drivers/crypto/Makefile
#
# Makefile for the Linux cryptohardware device drivers.
#

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

mod-subdirs     :=bcm

O_TARGET := crypto.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.


subdir-$(CONFIG_CRYPTO_BROADCOM) += bcm

include $(TOPDIR)/Rules.make
