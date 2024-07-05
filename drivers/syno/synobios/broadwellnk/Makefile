include /env.mak

obj-m += broadwellnk-synobios.o

broadwellnk-synobios-objs = \
	../syno_ttyS/syno_ttyS.o \
	../synobios.o \
	ds3018xs.o \
	fs1018.o \
	rs1619xs+.o \
	sa3400.o \
	sa3600.o \
	fs3600.o \
	hd3400.o \
	ds1621xs+.o \
	rs3621xs+.o \
	rs3621rpxs.o \
	rs4021xs+.o \
	ds3622xs+.o \
	../mapping.o \
	../rtc/localtime.o \
	../rtc/alarmtime.o \
	../rtc/rtc-x86.o \
	../i2c/i2c-linux.o \
	broadwellnk_common.o

ifneq (,$(filter $(PLAT_SPEC_VAR), SYNO_FEA_PORT_MAPPING_V2))
	broadwellnk-synobios-objs += \
		../led/led_9235.o \
		../led/led_1475.o \
		../led/led_trigger.o
endif
