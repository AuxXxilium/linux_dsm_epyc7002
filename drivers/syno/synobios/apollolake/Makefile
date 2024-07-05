include /env.mak

obj-m += apollolake-synobios.o

apollolake-synobios-objs = \
       ../common/common.o \
       ../syno_ttyS/syno_ttyS.o \
       ../synobios.o \
	   ds918p.o \
	   ds218p.o \
	   ds718p.o \
	   ds418play.o \
	   ds419p.o \
	   ds1019p.o \
	   ds719p.o \
	   ds620slim.o \
	   rs419p.o \
       ../mapping.o \
       ../rtc/localtime.o \
       ../rtc/alarmtime.o \
       ../rtc/rtc-seiko-lib.o \
       ../rtc/rtc-linux-seiko.o \
       ../i2c/i2c-linux.o \
       apollolake_common.o

ifneq (,$(filter $(PLAT_SPEC_VAR), SYNO_FEA_PORT_MAPPING_V2))
	apollolake-synobios-objs += \
       ../led/led_trigger.o
endif
