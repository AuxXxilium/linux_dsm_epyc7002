include /env.mak

obj-m += v1000sofs-synobios.o

v1000sofs-synobios-objs = \
	../common/common.o \
	../syno_ttyS/syno_ttyS.o \
	../synobios.o \
	ds1621p.o \
	ds1821p.o \
	../mapping.o \
	../rtc/localtime.o \
	../rtc/alarmtime.o \
	../rtc/rtc-seiko-lib.o \
	../rtc/rtc-linux-seiko.o \
	../i2c/i2c-linux.o \
	v1000sofs_common.o \
	../led/led_trigger.o \
	../led/led_jmb585.o \
	../led/led_asm116x.o \
	../led/led_1475.o \
	../pmbus/pmbus.o
