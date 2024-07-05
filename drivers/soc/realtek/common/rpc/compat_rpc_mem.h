#ifndef _LINUX_COMPAT_RPC_MEM_H
#define _LINUX_COMPAT_RPC_MEM_H

#if IS_ENABLED(CONFIG_COMPAT)

long compat_rpc_mem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#else

#define compat_rpc_mem_ioctl  NULL

#endif /* CONFIG_COMPAT */
#endif /* _LINUX_COMPAT_RPC_MEM_H */
