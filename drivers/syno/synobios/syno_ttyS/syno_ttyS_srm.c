
#include <linux/fs.h>
int syno_ttyS_set_termios(void)
{
	return 0;
}

int save_char_from_uart(unsigned char ch, struct tty_struct *tty)
{
	return 0;
}
EXPORT_SYMBOL(save_char_from_uart);

int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty)
{
	return 0;
}
EXPORT_SYMBOL(save_current_data_from_uart);

int syno_ttyS_open(void)
{
	return 0;
}



static int syno_ttyS_write(const char* szCmd)
{
	return 0;
}

int syno_ttyS_write_with_length(const char* szCmd, const int cbCmd)
{
	return 0;
}

int synobios_lock_ttyS_write(const char *szBuf)
{
	return 0;
}

int synobios_lock_ttyS_read(char *szBuf, int size)
{
	return 0;
}

int synobios_lock_ttyS_write_and_read(char *szBuf, int size)
{
	return 0;
}

int synobios_lock_ttyS_protection(char *szCommand, char *szBuf) {
	return 0;
}
EXPORT_SYMBOL(synobios_lock_ttyS_protection);

int synobios_lock_ttyS_current(char *szCommand, char *szBuf) {
	return 0;
}
EXPORT_SYMBOL(synobios_lock_ttyS_current);

void syno_ttyS_close(void)
{
	return;
}
