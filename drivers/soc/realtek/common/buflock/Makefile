MODULE_NAME := buflock

ccflags-y := -I$(obj)/inc
ccflags-y += -I$(obj)/uapi
ccflags-y += -DMODULE_NAME='"$(MODULE_NAME)"'
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/
ccflags-y += -I$(srctree)/drivers/soc/realtek/common/mem_allocator/rtk/inc

obj-y   := $(MODULE_NAME).o
$(MODULE_NAME)-objs := src/buflock.o
