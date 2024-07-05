int linuxI2CCharRead(int target, u8 *data, int length, int offset);
int linuxI2CCharWrite(int target, u8 *data, int length, int offset);
int linuxI2CSmbusRegRead(int bus_no, u16 addr, u8 reg, u16 *data);
void I2cBusChange(int new_bus);
