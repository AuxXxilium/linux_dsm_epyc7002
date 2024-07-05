obj-m += ds223j-synobios.o ds423-synobios.o ds223-synobios.o ds124-synobios.o

common-obj += \
		../common/common.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		../rtc/rtc-pericom-lib.o \
		../rtc/rtc-linux-pericom.o \
		../led/led_trigger_disk.o \
		rtd1619b_common.o

fan-pwm-obj += fan-pwm.o

ds223j-synobios-objs = ds223j.o $(fan-pwm-obj) $(common-obj)
ds423-synobios-objs = ds423.o $(fan-pwm-obj) $(common-obj)
ds223-synobios-objs = ds223.o $(fan-pwm-obj) $(common-obj)
ds124-synobios-objs = ds124.o $(fan-pwm-obj) $(common-obj)
