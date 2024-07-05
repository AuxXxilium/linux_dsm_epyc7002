#
# Makefile for the Realtek codec drivers.
#
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/mem_allocator/rtk/inc
obj-$(CONFIG_RTD13XX_RTK_CODEC)		+= puwrap.o
obj-$(CONFIG_RTD13XX_VE1_CODEC)		+= ve1/
obj-$(CONFIG_RTD13XX_IMAGE_CODEC)	+= jdi/
