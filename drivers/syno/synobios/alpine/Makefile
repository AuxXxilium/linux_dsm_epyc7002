ifeq ($(SYNO_PLATFORM),ALPINE)
obj-m += ds2015xs-synobios.o
obj-m += ds1515-synobios.o
obj-m += ds715-synobios.o
obj-m += ds215+-synobios.o
obj-m += ds416-synobios.o
obj-m += ds1817-synobios.o
obj-m += ds1517-synobios.o
obj-m += rs1219-synobios.o
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
		alpine_common.o

fan-pwm-obj += fan-pwm.o

ds2015xs-synobios-objs = ds2015xs.o $(fan-pwm-obj) $(common-obj)
ds1515-synobios-objs = ds1515.o $(fan-pwm-obj) $(common-obj)
ds715-synobios-objs = ds715.o $(fan-pwm-obj) $(common-obj)
ds215+-synobios-objs = ds215+.o $(fan-pwm-obj) $(common-obj)
ds416-synobios-objs = ds416.o $(fan-pwm-obj) $(common-obj)
ds1817-synobios-objs = ds1817.o $(fan-pwm-obj) $(common-obj)
ds1517-synobios-objs = ds1517.o $(fan-pwm-obj) $(common-obj)
rs1219-synobios-objs = rs1219.o $(fan-pwm-obj) $(common-obj)
