# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_SYNO_LSP_RTD1619B), y)

ccflags-y += -I$(srctree)/drivers/soc/realtek/common/mem_allocator/rtk/inc

endif # CONFIG_SYNO_LSP_RTD1619B
obj-$(CONFIG_TEE) += tee.o
tee-objs += tee_core.o
tee-objs += tee_shm.o
tee-objs += tee_shm_pool.o
ifeq ($(CONFIG_SYNO_LSP_RTD1619B), y)
ifeq ($(CONFIG_ION_RTK_DHC_HEAP),y)
obj-$(CONFIG_OPTEE) += tee_mem_api.o
endif
endif # CONFIG_SYNO_LSP_RTD1619B
obj-$(CONFIG_OPTEE) += optee/
obj-$(CONFIG_AMDTEE) += amdtee/
