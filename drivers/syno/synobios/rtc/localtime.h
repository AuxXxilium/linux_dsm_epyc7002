
#ifndef __SYNOBIOS_LOCALTIME__
#define __SYNOBIOS_LOCALTIME__
#include <linux/time.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/syno.h>

struct xtm {
	u_int8_t month;    /* (1-12) */
	u_int8_t monthday; /* (1-31) */
	u_int8_t weekday;  /* (1-7) */
	u_int8_t hour;     /* (0-23) */
	u_int8_t minute;   /* (0-59) */
	u_int8_t second;   /* (0-59) */
	unsigned int dse;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
unsigned int localtime_1(struct xtm *r, time64_t time);
void localtime_2(struct xtm *r, time64_t time);
void localtime_3(struct xtm *r, time64_t time);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
unsigned int localtime_1(struct xtm *r, time_t time);
void localtime_2(struct xtm *r, time_t time);
void localtime_3(struct xtm *r, time_t time);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
#endif /* __SYNOBIOS_LOCALTIME__*/
