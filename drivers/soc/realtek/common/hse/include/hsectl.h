#ifndef __HSECTL_H__
#define __HSECTL_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

struct hse_cmd {
	uint32_t size;
	uint32_t cmds[32];
};

#define HSE_IOCTL_VERSION                _IOR('H', 0x00, unsigned int)
#define HSE_IOCTL_ADD_CMD                _IOW('H', 0x01, struct hse_cmd)
#define HSE_IOCTL_START                  _IO('H', 0x02)
#define HSE_IOCTL_HW_RESET               _IO('H', 0x04)
#define HSE_IOCTL_HW_RESET_FORCE         _IO('H', 0x05)


#define HSE_ROTATE_MODE_90              0
#define HSE_ROTATE_MODE_180             1
#define HSE_ROTATE_MODE_270             2

#endif /* __HSECTL_H__ */
