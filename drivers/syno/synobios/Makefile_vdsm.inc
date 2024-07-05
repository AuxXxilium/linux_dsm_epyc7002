vdsm-common-obj = \
    ../syno_ttyS/syno_ttyS.o \
    ../synobios.o \
    ../mapping.o \
    ../rtc/localtime.o \
    ../rtc/alarmtime.o \
    ../rtc/rtc-x86.o
# 為了讓其他共用此變數的 makefile 可以使用到此變數，需要 export 出去
export vdsm-common-obj
