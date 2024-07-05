obj-m += ds416j-synobios.o
obj-m += ds216-synobios.o
obj-m += ds416slim-synobios.o
obj-m += ds216j-synobios.o
obj-m += rs816-synobios.o
obj-m += ds116-synobios.o
obj-m += rs217-synobios.o
obj-m += ds218j-synobios.o
obj-m += ds419slim-synobios.o

common-obj += \
		../common/common.o \
	        ../i2c/i2c-linux.o \
		../rtc/rtc-seiko-lib.o \
		../rtc/rtc-linux-seiko.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../rtc/rtc-mvebu-builtin.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		armada38x_common.o

fan-pwm-obj += fan-pwm.o
fan-resistor-obj += fan-resistor.o

ds416j-synobios-objs = ds416j.o $(fan-resistor-obj) $(common-obj)
ds216-synobios-objs = ds216.o $(fan-resistor-obj) $(common-obj)
ds416slim-synobios-objs = ds416slim.o $(fan-resistor-obj) $(common-obj)
ds216j-synobios-objs = ds216j.o $(fan-resistor-obj) $(common-obj)
rs816-synobios-objs = rs816.o $(fan-resistor-obj) $(common-obj)
ds116-synobios-objs = ds116.o $(fan-resistor-obj) $(common-obj)
rs217-synobios-objs = rs217.o $(fan-resistor-obj) $(common-obj)
ds218j-synobios-objs = ds218j.o $(fan-resistor-obj) $(common-obj)
ds419slim-synobios-objs = ds419slim.o $(fan-resistor-obj) $(common-obj)
