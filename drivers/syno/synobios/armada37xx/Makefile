include /env.mak

obj-m += ds219j-synobios.o
obj-m += ds219se-synobios.o
obj-m += ds119j-synobios.o
obj-m += ds120j-synobios.o

common-obj += \
		../common/common.o \
	        ../i2c/i2c-linux.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-seiko.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		armada37xx_common.o

fan-pwm-obj += fan-pwm.o
fan-resistor-obj += fan-resistor.o

ifneq (,$(filter $(PLAT_SPEC_VAR), SYNO_FEA_PORT_MAPPING_V2))
	common-obj += \
		../led/led_9170.o
endif

ds219j-synobios-objs = ds219j.o $(fan-resistor-obj) $(common-obj)
ds219se-synobios-objs = ds219se.o $(fan-resistor-obj) $(common-obj)
ds119j-synobios-objs = ds119j.o $(fan-resistor-obj) $(common-obj)
ds120j-synobios-objs = ds120j.o $(fan-resistor-obj) $(common-obj)
