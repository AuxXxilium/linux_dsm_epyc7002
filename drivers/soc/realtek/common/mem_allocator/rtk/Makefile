ccflags-y += -I$(srctree)/drivers/soc/realtek/common/mem_allocator
ccflags-y += -I$(srctree)/mm

obj-$(CONFIG_ION_RTK_DHC_HEAP) += rtk_ion.o
rtk_ion-objs := src/pool_common.o
rtk_ion-objs += src/pool_gen.o
rtk_ion-objs += src/pool_cma.o
rtk_ion-objs += src/carveout_heap.o
rtk_ion-objs += src/dev.o
rtk_ion-objs += src/ioctl.o
rtk_ion-objs += src/protected.o
obj-$(CONFIG_DEBUG_FS) += src/debugfs.o

obj-$(CONFIG_ION_RTK_LEGACY_DEVICE) += src/dev_legacy.o

obj-$(CONFIG_ION_RTK_DHC_HEAP) += test/
