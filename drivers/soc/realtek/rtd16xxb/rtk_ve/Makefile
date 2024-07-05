#
# Makefile for the Realtek codec drivers.
#
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/mem_allocator/rtk/inc
obj-$(CONFIG_RTD16XXB_RTK_CODEC)		+= puwrap.o
obj-$(CONFIG_RTD16XXB_VE1_CODEC)		+= ve1/
obj-$(CONFIG_RTD16XXB_IMAGE_CODEC)	+= jdi/
