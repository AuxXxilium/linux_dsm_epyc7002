#include "synobios.h"
int HWMONReadPmbusStatusReg(unsigned int adapter, unsigned int addr, unsigned int reg, char *SensorValue, int cValueSize);
#ifdef CONFIG_SYNO_HWMON_PMBUS
int I2CPmbusReadPowerStatus(int index, SYNO_POWER_STATUS* status);
int HWMONGetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber);
#endif /* CONFIG_SYNO_HWMON_PMBUS */
