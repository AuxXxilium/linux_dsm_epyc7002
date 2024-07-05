ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA370)
obj-m += ds213j-synobios.o
obj-m += ds114-synobios.o
obj-m += rs214-synobios.o
obj-m += ds214se-synobios.o
obj-m += ds414slim-synobios.o
obj-m += ds115j-synobios.o
obj-m += ds216se-synobios.o
obj-m += thermal_ctrl.o
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADAXP)
obj-m += ds414-synobios.o
obj-m += ds214-synobios.o
obj-m += rs814-synobios.o
obj-m += ds214p-synobios.o
obj-m += rs815-synobios.o
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA375)
obj-m += ds215j-synobios.o
obj-m += ds115-synobios.o
endif

common-obj += \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-ricoh-lib.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-ricoh.o \
		../rtc/rtc-linux-seiko.o \
		../rtc/rtc-mv-builtin.o \
		../mapping.o \
		armada_common.o \
		mv_level_button.o

fan-pwm-obj += fan-pwm.o
fan-resister-obj += fan-resister.o

ds213j-synobios-objs = ds213j.o $(fan-resister-obj) $(common-obj)
ds114-synobios-objs = ds114.o $(fan-resister-obj) $(common-obj)
rs214-synobios-objs = rs214.o $(fan-resister-obj) $(common-obj)
ds414-synobios-objs = ds414.o $(fan-pwm-obj) $(common-obj)
ds214-synobios-objs = ds214.o $(fan-pwm-obj) $(common-obj)
rs814-synobios-objs = rs814.o $(fan-pwm-obj) $(common-obj)
ds214se-synobios-objs = ds214se.o $(fan-resister-obj) $(common-obj)
ds214p-synobios-objs = ds214p.o $(fan-pwm-obj) $(common-obj)
ds115j-synobios-objs = ds115j.o $(fan-resister-obj) $(common-obj)
ds414slim-synobios-objs = ds414slim.o $(fan-resister-obj) $(common-obj)
ds215j-synobios-objs = ds215j.o $(fan-resister-obj) $(common-obj)
ds115-synobios-objs = ds115.o $(fan-resister-obj) $(common-obj)
rs815-synobios-objs = rs815.o $(fan-pwm-obj) $(common-obj)
ds216se-synobios-objs = ds216se.o $(fan-resister-obj) $(common-obj)
