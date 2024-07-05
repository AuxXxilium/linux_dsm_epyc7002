int mpc_i2c_write(int target, const unsigned char *data, int length, int restart, int offset);
int mpc_i2c_read(int target, unsigned char *data, int length, int restart, int offset);
int mpc_i2c_init(void);
