/* Copyright (c) 2001-2017 Synology Inc. All rights reserved.*/
#ifndef SYNO_TTYS_H
#define SYNO_TTYS_H


#define UP_DELAY 250
#define UART_BUF_SIZE 8
typedef struct _UART2_BUFFER {
	char szBuf[UART_BUF_SIZE];
	int size;
} UART2_BUFFER;

/**
 * init open microP device in synobios.
 * default open /dev/ttyS1.
 * should call once when insert synobios module.
 *
 * @return -1: Failed
 *          0: Success
 * @example
 * <pre>
 * if (0 > syno_ttyS_open()) return -1;
 * </pre>
 *
 * @see syno_ttyS_open
 **/
int syno_ttyS_open(void);

/**
 * read data from microP device in synobios.
 * should open before read.
 * if read fail will return -1 else return data length.
 *
 * @param [OUT] szBuf Store read data
 * @param [IN]  size  Max size of szBuf
 *
 * @return -1: Failed
 *       else: data length
 * @example
 * <pre>
 * len = synobios_lock_ttyS_read(szBuf, size);
 * if (0 > len) return -1;
 * </pre>
 *
 **/
int synobios_lock_ttyS_read(char *szBuf, int size);

/**
 * write commad to microP device in synobios.
 * should open before write.
 * if write fail will return -1.
 *
 * @param [IN] szCmd The command will be written to microP
 *
 * @return -1: Failed
 *       else: Length of being written command
 * @example
 * <pre>
 * len = syno_ttyS_write(szCmd)
 * if (0 > len) return -1;
 * </pre>
 *
 * @see syno_ttyS_write
 **/
int synobios_lock_ttyS_write(const char *szBuf);

/**
 * close microP device in synobios.
 * should call when will not read, write microP in synobios anymore.
 * will be called in unload synobios module
 *
 *
 * @example
 * <pre>
 * syno_ttyS_close;
 * </pre>
 *
 * @see syno_ttyS_close
 **/
void syno_ttyS_close(void);

int synobios_lock_ttyS_write_and_read(char *szBuf, int size);

void try_wakeup_waiting_microp(void);

void set_log_switch(int logSwitch);

#define TTY_BUF_SIZE	24
#define CURRENT_DATA_LEN	4
#define PREFIX_CH		'+'
#define TTY_PROTECT_CMD_SIZE	17
#define SZ_UART_CURRENT_PREFIX	'D'
#define SZ_UART_CURRENT_SUFFIX	'X'
#define SZ_UART_CURRENT_ZERO_SUBSTITUE '*'
#define SZ_UART_CURRENT_ZERO '0'
#define TTY_NAME		"ttyS1"

#define SZ_UART_CMD_CURRENT_GET	"D"

#endif /* SYNO_TTYS_H */
