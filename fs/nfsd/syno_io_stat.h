#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifndef LINUX_NFSD_SYNO_IO_STAT_H
#define LINUX_NFSD_SYNO_IO_STAT_H
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/socket.h>

#include <linux/sunrpc/auth.h>
#ifdef MY_ABC_HERE
#include <linux/sunrpc/xdr.h>
#endif /* MY_ABC_HERE */

enum syno_nfsd_io_stat_type {
	SYNO_NFSD_IO_READ = 0,
	SYNO_NFSD_IO_WRITE,
	SYNO_NFSD_IO_TYPE_END
};

// We use `nfs_vers` to represent this enum type
enum syno_nfsd_version {
	SYNO_NFSD_VERSION_2 = 0,
	SYNO_NFSD_VERSION_3,
	SYNO_NFSD_VERSION_4,
	SYNO_NFSD_VERSION_END,
};

static inline enum syno_nfsd_version syno_nfsd_version_convert(unsigned int vers)
{
	return min((unsigned int) SYNO_NFSD_VERSION_END, vers - 2);
}

void syno_nfsd_io_total_stat_init(void);
void syno_nfsd_io_total_stat_destroy(void);
int syno_nfsd_io_total_stat_open(struct inode *inode, struct file *file);
void syno_nfsd_account_io_complete(struct sockaddr *s_addr, int version,
				   enum syno_nfsd_io_stat_type type, s64 bytes,
				   s64 latency);
ssize_t syno_nfsd_client_ctl(char *buf, size_t size);
int syno_nfsd_client_register(struct sockaddr *s_addr, int version, struct xdr_netobj *os_name);
void syno_nfsd_client_unregister(const struct sockaddr *s_addr, int version);
void syno_nfsd_clients_destroy_all(void);

void syno_nfsd_client_expire_time_set(int t);
int syno_nfsd_client_expire_time_get(void);
void syno_nfsd_client_cleaner(void);

#ifdef MY_ABC_HERE
void syno_nfsd_connection_init(void);
void syno_nfsd_max_connection_init(void);
int syno_nfsd_max_connection(void);
void syno_nfsd_connection_destroy(void);
void syno_nfsd_total_connection_reset(void);
int syno_nfsd_total_connection_stat_open(struct inode *inode, struct file *file);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
struct syno_nfsd_dummy_status {
	__be32 status;
};

#define SYNO_NFSD_USEC_TO_SEC(us)	((us >> 20))
void syno_nfsd_udc_stat_init(void);
void syno_nfsd_store_latency_into_histogram(unsigned int rpc_lat_s, unsigned int vfs_lat_s,
					 enum syno_nfsd_version nfs_vers,
					 enum syno_nfsd_io_stat_type op);
int syno_nfsd_latency_histogram_open(struct inode *inode, struct file *file);
void syno_nfsd_store_error(int errno, enum syno_nfsd_version nfs_vers);
int syno_nfsd_total_error_open(struct inode *inode, struct file *file);
#endif /* MY_ABC_HERE */

#endif /* LINUX_NFSD_SYNO_IO_STAT_H */
