obj-m += ds220play-synobios.o
obj-m += ds220-synobios.o

common-obj += \
		../common/common.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-seiko.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		rtd1619_common.o

fan-pwm-obj += fan-pwm.o
fan-resistor-obj += fan-resistor.o

syno_power_outage-obj += ../syno_power_outage/syno_power_outage.o

ds220play-synobios-objs = ds220play.o $(fan-resistor-obj) $(common-obj)
ds220-synobios-objs = ds220.o $(fan-resistor-obj) $(common-obj)
