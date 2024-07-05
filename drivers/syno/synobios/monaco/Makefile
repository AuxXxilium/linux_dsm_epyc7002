ifeq ($(SYNO_PLATFORM),STM_MONACO)
obj-m += ds216play-synobios.o
endif

common-obj += \
		../common/common.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-ricoh-lib.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-ricoh.o \
		../rtc/rtc-linux-seiko.o \
		../mapping.o \
		monaco_common.o

fan-pwm-obj += fan-pwm.o
fan-resistor-obj += fan-resistor.o

ds216play-synobios-objs = ds216play.o $(fan-resistor-obj) $(common-obj)
