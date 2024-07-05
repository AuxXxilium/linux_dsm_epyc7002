ccflags-$(CONFIG_REALTEK_RPC)	+= -Idrivers/soc/realtek/common
ccflags-$(CONFIG_REALTEK_RPC)	+= -Idrivers/soc/realtek/common/include
ccflags-y 			+= -Idrivers/video/fbdev/rtk
ccflags-y 			+= -Idrivers/soc/realtek/common/mem_allocator/rtk/inc
obj-$(CONFIG_REALTEK_RPC)	+= rpc_mem.o rtk_rpc_intr.o rtk_rpc_kern.o rtk_rpc_poll.o rtk_rpc.o
ifdef CONFIG_COMPAT
obj-$(CONFIG_REALTEK_RPC)	+= compat_rpc_mem.o
endif
