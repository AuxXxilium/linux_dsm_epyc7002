ccflags-y := -DSYNO_PLATFORM=\"EPYC7002\"
obj-$(CONFIG_SYNO_SYNOBIOS) += synobios.o

synobios-objs = \
	../common/common.o \
	../syno_ttyS/syno_ttyS.o \
	../synobios.o \
	../mapping.o \
	../rtc/localtime.o \
	../rtc/alarmtime.o \
	../rtc/rtc-seiko-lib.o \
	../rtc/rtc-linux-seiko.o \
	../i2c/i2c-linux.o \
	../led/led_trigger.o \
	../pmbus/pmbus.o \
	epyc7002_common.o \
	fs6400n.o \
	fs6410.o \
	sa6400.o \
	sa6200.o \
	sc6200.o
