include /env.mak

obj-m += denverton-synobios.o

denverton-synobios-objs = \
	../common/common.o \
	../syno_ttyS/syno_ttyS.o \
	../synobios.o \
	rs2418+.o \
	rs2418rp+.o \
	ds1618+.o \
	rs2818rp+.o \
	ds2419+.o \
	ds1819+.o \
	dva3219.o \
	rs820+.o \
	rs820rp+.o \
	rs1220+.o \
	rs1220rp+.o \
	dva3221.o \
	ds2419+II.o \
	../mapping.o \
	../rtc/localtime.o \
	../rtc/alarmtime.o \
	../rtc/rtc-x86.o \
	../i2c/i2c-linux.o \
	denverton_common.o

ifneq (,$(filter $(PLAT_SPEC_VAR), SYNO_FEA_PORT_MAPPING_V2))
denverton-synobios-objs += \
	../led/led_trigger.o \
	../led/led_9235.o
endif
