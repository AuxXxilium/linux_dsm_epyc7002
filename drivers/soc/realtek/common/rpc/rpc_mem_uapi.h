#ifndef _RTK_RPC_MEM_UAPI_H
#define _RTK_RPC_MEM_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct rpc_mem_fd_data {
	unsigned long phyAddr;
	unsigned long ret_offset;
	unsigned long ret_size;
	int ret_fd;
};

#define RPC_MEM_IOC_MAGIC		'R'
#define RPC_MEM_IOC_EXPORT		_IOWR(RPC_MEM_IOC_MAGIC, 0, struct rpc_mem_fd_data)

#endif /* _RTK_RPC_MEM_UAPI_H */
