obj-m += ds418j-synobios.o
obj-m += ds418-synobios.o
obj-m += ds218play-synobios.o
obj-m += ds118-synobios.o
obj-m += ds218-synobios.o
obj-m += eds19-synobios.o
obj-m += rs819-synobios.o
obj-m += ds220j-synobios.o
obj-m += ds420j-synobios.o

common-obj += \
		../common/common.o \
	        ../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-rtk-builtin.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		rtd1296_common.o

fan-pwm-obj += fan-pwm.o
fan-resistor-obj += fan-resistor.o

syno_power_outage-obj += ../syno_power_outage/syno_power_outage.o

ds418j-synobios-objs = ds418j.o $(fan-pwm-obj) $(common-obj)
ds418-synobios-objs = ds418.o $(fan-pwm-obj) $(common-obj)
ds218play-synobios-objs = ds218play.o $(fan-pwm-obj) $(common-obj)
ds118-synobios-objs = ds118.o $(fan-pwm-obj) $(common-obj)
ds218-synobios-objs = ds218.o $(fan-pwm-obj) $(common-obj)
eds19-synobios-objs = eds19.o $(common-obj) $(syno_power_outage-obj)
rs819-synobios-objs = rs819.o $(fan-pwm-obj) $(common-obj)
ds220j-synobios-objs = ds220j.o $(fan-pwm-obj) $(common-obj)
ds420j-synobios-objs = ds420j.o $(fan-pwm-obj) $(common-obj)
