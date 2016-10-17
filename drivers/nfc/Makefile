#
# Makefile for nfc devices
#

CFLAGS_KERNEL := -fno-graphite -fno-graphite-identity -fno-loop-flatten -fno-tree-loop-linear -fno-loop-interchange -fno-loop-strip-mine -fno-loop-block -fno-loop-nest-optimize -fno-loop-parallelize-all -ftree-parallelize-loops=0 -fno-unsafe-math-optimizations -fno-strict-aliasing

obj-$(CONFIG_NFC_PN544)		+= pn544/
obj-$(CONFIG_NFC_MICROREAD)	+= microread/
obj-$(CONFIG_NFC_PN533)		+= pn533.o
obj-$(CONFIG_NFC_WILINK)	+= nfcwilink.o
obj-$(CONFIG_NFC_MEI_PHY)	+= mei_phy.o
obj-$(CONFIG_NFC_QNCI)		+= nfc-nci.o
obj-$(CONFIG_NFC_NQ)		+= nq-nci.o

ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
