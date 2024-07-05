#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/dcache.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/percpu.h>
#include <linux/ratelimit.h>
#include <linux/socket.h>
#include <linux/sunrpc/addr.h>
#include <linux/jiffies.h>
#ifdef MY_ABC_HERE
#include <linux/list.h>
#endif /* MY_ABC_HERE */

#include "syno_io_stat.h"
#ifdef MY_ABC_HERE
#include "state.h"
#endif /* MY_ABC_HERE */
#include "nfsd.h"
#include "netns.h"

#define SYNO_NFSD_CLIENT_EXPIRE_TIME 86400 // 1 day

#define syno_io_stat_lock() 	preempt_disable()
#define syno_io_stat_unlock()	preempt_enable()
static atomic64_t g_syno_client_id;

static int g_syno_client_expire_time = SYNO_NFSD_CLIENT_EXPIRE_TIME;

/*
 * We need this lock to destory client_entry even it been
 * hold by some connections due to it expired.
 */
static DEFINE_MUTEX(g_syno_delete_client_mutex);

struct syno_nfsd_io_stat {
	u64 io_count[SYNO_NFSD_IO_TYPE_END];
	u64 io_bytes[SYNO_NFSD_IO_TYPE_END];
	u64 io_latency[SYNO_NFSD_IO_TYPE_END];
	u64 max_io_latency[SYNO_NFSD_IO_TYPE_END];
};

struct syno_total_nfsd_io_stat {
	struct syno_nfsd_io_stat io_stat[SYNO_NFSD_VERSION_END];
};

struct syno_total_nfsd_io_stat __percpu *g_syno_io_stat = NULL;

struct syno_nfsd_client_addr {
	sa_family_t family;
	enum syno_nfsd_version nfs_vers;
	union {
		struct in6_addr sin6_addr;
		struct in_addr sin_addr;
	};
};

struct syno_nfsd_client_entry {
	struct hlist_bl_node client_hash;
	struct syno_nfsd_client_addr addr;
	atomic_t holder; // for connection
	struct kref refs; // for entry refs.
	struct syno_nfsd_io_stat __percpu *vfs_io;
	/* debugging info directory under nfsd/syno_clients/ : */
	struct dentry *info_dentry;
	unsigned long last_used; // jiffies.
};

#define SYNO_NFSD_CLIENT_NRHASH 256U
#define SYNO_NFSD_CLIENT_NRHASH_MASK ((SYNO_NFSD_CLIENT_NRHASH - 1))
static struct hlist_bl_head g_syno_nfsd_client_hosts[SYNO_NFSD_CLIENT_NRHASH];

static struct syno_nfsd_client_entry *alloc_client(const struct syno_nfsd_client_addr *addr);
static inline void put_client(struct syno_nfsd_client_entry *entry);
static struct syno_nfsd_client_entry *client_find(const struct syno_nfsd_client_addr addr);
static void release_client_from_inode(struct inode *inode);
static struct syno_nfsd_client_entry *__get_client_from_inode(struct inode *inode);
static struct syno_nfsd_client_entry *get_client_from_inode(struct inode *inode);
static void __client_rmdir(struct dentry *dentry);
static void client_rmdir(struct syno_nfsd_client_entry *entry);
static struct dentry *client_mkdir(struct nfsd_net *nn,
				   struct syno_nfsd_client_entry *entry);
static int sockaddr_to_client_addr(const struct sockaddr *s_addr,
				   enum syno_nfsd_version nfs_vers,
				   struct syno_nfsd_client_addr *addr);

static int client_info_open(struct inode *inode, struct file *file);
static int client_io_stat_open(struct inode *inode, struct file *file);
static int client_register(struct nfsd_net *nn,
			   const struct syno_nfsd_client_addr addr,
			   struct xdr_netobj *os_name);
#ifdef MY_ABC_HERE
static void __nfsd_connection_reg(const struct xdr_netobj *os_name, struct syno_nfsd_client_addr addr);
static void inc_connection(void);
static void dec_connection(void);
#endif /* MY_ABC_HERE */

static inline bool is_should_expire_version(enum syno_nfsd_version nfs_vers)
{
	return (nfs_vers == SYNO_NFSD_VERSION_2) || (nfs_vers == SYNO_NFSD_VERSION_3);
}

void syno_nfsd_client_expire_time_set(int t)
{
	g_syno_client_expire_time = t;
}
int syno_nfsd_client_expire_time_get(void)
{
	return g_syno_client_expire_time;
}

static int io_total_stat_show(struct seq_file *m, void *v)
{
	int cpu;
	int type = 0;
	enum syno_nfsd_version nfs_vers;
	struct syno_total_nfsd_io_stat stat;
	memset(&stat, 0, sizeof(stat));

	if (!g_syno_io_stat)
		return -EIO;

	for_each_possible_cpu(cpu) {
		struct syno_total_nfsd_io_stat *ptr = per_cpu_ptr(g_syno_io_stat, cpu);

		for (nfs_vers = SYNO_NFSD_VERSION_2; nfs_vers < SYNO_NFSD_VERSION_END; ++nfs_vers) {
			for (type = SYNO_NFSD_IO_READ; type < SYNO_NFSD_IO_TYPE_END; ++type) {
				stat.io_stat[nfs_vers].io_count[type] +=
					ptr->io_stat[nfs_vers].io_count[type];
				stat.io_stat[nfs_vers].io_bytes[type] +=
					ptr->io_stat[nfs_vers].io_bytes[type];
				stat.io_stat[nfs_vers].io_latency[type] +=
					ptr->io_stat[nfs_vers].io_latency[type];
				stat.io_stat[nfs_vers].max_io_latency[type] = max(
					stat.io_stat[nfs_vers].max_io_latency[type],
					ptr->io_stat[nfs_vers].max_io_latency[type]);
			}
		}
	}

	for (nfs_vers = SYNO_NFSD_VERSION_2; nfs_vers < SYNO_NFSD_VERSION_END; ++nfs_vers) {
		for (type = SYNO_NFSD_IO_READ; type < SYNO_NFSD_IO_TYPE_END; ++type)
			seq_printf(m, "%llu %llu %llu %llu\n",
				   stat.io_stat[nfs_vers].io_count[type],
				   stat.io_stat[nfs_vers].io_bytes[type],
				   stat.io_stat[nfs_vers].io_latency[type],
				   stat.io_stat[nfs_vers].max_io_latency[type]);
	}
	return 0;
}

int syno_nfsd_io_total_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, io_total_stat_show, NULL);
}

void syno_nfsd_io_total_stat_init(void)
{
	int i;
	atomic64_set(&g_syno_client_id, 0);
	g_syno_io_stat = alloc_percpu(*g_syno_io_stat);
	if (g_syno_io_stat) {
		for_each_possible_cpu(i)
			memset(per_cpu_ptr(g_syno_io_stat, i), 0, sizeof(struct syno_total_nfsd_io_stat));
	}

	for (i = 0; i < ARRAY_SIZE(g_syno_nfsd_client_hosts); i++)
		INIT_HLIST_BL_HEAD(&g_syno_nfsd_client_hosts[i]);
}

void syno_nfsd_io_total_stat_destroy(void)
{
	free_percpu(g_syno_io_stat);
	g_syno_io_stat = NULL;
}

static inline void update_total_io_stat(enum syno_nfsd_version nfs_vers,
					enum syno_nfsd_io_stat_type type,
					s64 bytes, s64 latency)
{
	if (!g_syno_io_stat)
		return;
	syno_io_stat_lock();
	__this_cpu_inc(g_syno_io_stat->io_stat[nfs_vers].io_count[type]);
	__this_cpu_add(g_syno_io_stat->io_stat[nfs_vers].io_bytes[type], bytes);
	__this_cpu_add(g_syno_io_stat->io_stat[nfs_vers].io_latency[type], latency);
	__this_cpu_write(g_syno_io_stat->io_stat[nfs_vers].max_io_latency[type],
			 max((u64)latency,
			      __this_cpu_read(g_syno_io_stat->io_stat[nfs_vers].max_io_latency[type])));
	syno_io_stat_unlock();
}

static inline void update_client_io_stat(struct sockaddr *s_addr,
					 enum syno_nfsd_version nfs_vers,
					 enum syno_nfsd_io_stat_type type,
					 s64 bytes, s64 latency)
{
	int ret;
	struct syno_nfsd_client_addr addr;
	struct syno_nfsd_client_entry *entry;

	ret = sockaddr_to_client_addr(s_addr, nfs_vers, &addr);
	if (ret)
		return;
retry:
	entry = client_find(addr);
	if (!entry) {
		struct nfsd_net *nn = syno_nfsd_net_get();
		if (!nn)
			return;
		ret = client_register(nn, addr, NULL);
		if (ret)
			return;
		goto retry;
	}

	syno_io_stat_lock();
	__this_cpu_inc(entry->vfs_io->io_count[type]);
	__this_cpu_add(entry->vfs_io->io_bytes[type], bytes);
	__this_cpu_add(entry->vfs_io->io_latency[type], latency);
	__this_cpu_write(entry->vfs_io->max_io_latency[type],
			 max((u64)latency, __this_cpu_read(entry->vfs_io->max_io_latency[type])));
	syno_io_stat_unlock();
	entry->last_used = jiffies;
	put_client(entry);

	return;
}

static int sockaddr_to_client_addr(const struct sockaddr *s_addr, enum syno_nfsd_version nfs_vers,
				   struct syno_nfsd_client_addr *addr)
{
	const struct sockaddr_in *sin;
	const struct sockaddr_in6 *sin6;

	if (!s_addr || !addr)
		return -EINVAL;

	sin = (const struct sockaddr_in *)s_addr;
	sin6 = (const struct sockaddr_in6 *)s_addr;
	addr->family = s_addr->sa_family;
	addr->nfs_vers = nfs_vers;

	switch (addr->family) {
	case AF_INET:
		addr->sin_addr = sin->sin_addr;
		break;
	case AF_INET6:
		addr->sin6_addr = sin6->sin6_addr;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

void syno_nfsd_account_io_complete(struct sockaddr *s_addr, int version,
				   enum syno_nfsd_io_stat_type type, s64 bytes,
				   s64 latency)
{
	enum syno_nfsd_version nfs_vers = syno_nfsd_version_convert(version);
	if (nfs_vers == SYNO_NFSD_VERSION_END)
		return;
	update_total_io_stat(nfs_vers, type, bytes, latency);
	update_client_io_stat(s_addr, nfs_vers, type, bytes, latency);
}

/*
 * hash function copy from fs/lockd/host.c
 */
static inline unsigned int __hash32(const __be32 n)
{
	unsigned int hash = (__force u32)n ^ ((__force u32)n >> 16);
	return hash ^ (hash >> 8);
}

static inline unsigned int
__client_addr_hash(const struct syno_nfsd_client_addr *addr)
{
	unsigned int hash;
	if (addr->family == AF_INET6) {
		hash = __hash32(addr->sin6_addr.s6_addr32[0]) ^
		       __hash32(addr->sin6_addr.s6_addr32[1]) ^
		       __hash32(addr->sin6_addr.s6_addr32[2]) ^
		       __hash32(addr->sin6_addr.s6_addr32[3]);
	} else {
		hash = __hash32(addr->sin_addr.s_addr);
	}
	return (hash ^ __hash32(addr->nfs_vers)) & SYNO_NFSD_CLIENT_NRHASH_MASK;
}

static struct hlist_bl_head *
client_addr_hash(const struct syno_nfsd_client_addr *addr)
{
	return &g_syno_nfsd_client_hosts[__client_addr_hash(addr)];
}

static inline int cmp_client_addr(const struct syno_nfsd_client_addr x,
				  const struct syno_nfsd_client_addr y)
{
	if (x.nfs_vers < y.nfs_vers)
		return 1;
	else if (x.nfs_vers > y.nfs_vers)
		return -1;

	if (x.family < y.family)
		return 1;
	else if (x.family > y.family)
		return -1;

	if (x.family == AF_INET) {
		if (x.sin_addr.s_addr < y.sin_addr.s_addr)
			return 1;
		else if (x.sin_addr.s_addr > y.sin_addr.s_addr)
			return -1;
	} else {
		return ipv6_addr_cmp(&x.sin6_addr, &y.sin6_addr);
	}
	return 0;
}

/*
 * You should aquire RCU lock before calling this function,
 * and increment refs count before unlock RCU.
 */
static struct syno_nfsd_client_entry *
client_lookup_rcu(const struct syno_nfsd_client_addr addr)
{
	struct hlist_bl_head *b = client_addr_hash(&addr);
	struct hlist_bl_node *node;
	struct syno_nfsd_client_entry *entry;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "suspicious nfsd_client_lookup_rcu() usage");
	hlist_bl_for_each_entry_rcu(entry, node, b, client_hash) {
		if (!cmp_client_addr(entry->addr, addr))
			return entry;
	}
	return NULL;
}

static struct syno_nfsd_client_entry *
client_find(const struct syno_nfsd_client_addr addr)
{
	struct syno_nfsd_client_entry *entry;
	rcu_read_lock();
	entry = client_lookup_rcu(addr);
	if (!entry || !kref_get_unless_zero(&entry->refs)) {
		entry = NULL;
	}
	rcu_read_unlock();
	return entry;
}

static int client_register(struct nfsd_net *nn,
			   const struct syno_nfsd_client_addr addr,
			   struct xdr_netobj *os_name)
{
	bool create = false;
	int ret;
	struct syno_nfsd_client_entry *entry;
	struct syno_nfsd_client_entry *new = NULL;
	struct hlist_bl_node *node;
	struct hlist_bl_head *b = client_addr_hash(&addr);

	__nfsd_connection_reg(os_name, addr);

	// Check first to prevent allocate un-necessary entry.
	rcu_read_lock();
	entry = client_lookup_rcu(addr);
	if (unlikely(entry)) {
		if (kref_get_unless_zero(&entry->refs)) {
			rcu_read_unlock();
			goto found;
		}
		// someone is releasing entry now, we start to create one.
	}
	rcu_read_unlock();

	new = alloc_client(&addr);
	if (!new) {
		ret = -ENOMEM;
		goto out;
	}
	new->info_dentry = client_mkdir(nn, new);
	if (IS_ERR(new->info_dentry)) {
		ret = PTR_ERR((void *)new->info_dentry);
		new->info_dentry = NULL;
		goto out;
	}

retry:
	rcu_read_lock();
	entry = client_lookup_rcu(addr);
	if (unlikely(entry)) {
		if (!kref_get_unless_zero(&entry->refs)) {
			rcu_read_unlock();
			goto retry;
		}
		rcu_read_unlock();
		goto found;
	}

	hlist_bl_lock(b);
	hlist_bl_for_each_entry_rcu(entry, node, b, client_hash) {
		if (cmp_client_addr(entry->addr, addr) != 0)
			continue;

		hlist_bl_unlock(b);
		if (!kref_get_unless_zero(&entry->refs)) {
			rcu_read_unlock();
			goto retry;
		}
		rcu_read_unlock();
		goto found;
	}
	rcu_read_unlock();
	hlist_bl_add_head_rcu(&new->client_hash, b);
	hlist_bl_unlock(b);
	create = true;
	entry = new;
	new = NULL;
#ifdef MY_ABC_HERE
	inc_connection();
#endif /* MY_ABC_HERE */
found:
	ret = 0;
	atomic_inc(&entry->holder);
	// entry is already hold by someone, we dont need to get refs during this register.
	if (!create)
		put_client(entry);
out:
	client_rmdir(new);
	put_client(new);
	return ret;
}

int syno_nfsd_client_register(struct sockaddr *s_addr, int version, struct xdr_netobj *os_name)
{
	int ret;
	struct syno_nfsd_client_addr addr;
	struct nfsd_net *nn = syno_nfsd_net_get();
	enum syno_nfsd_version nfs_vers = syno_nfsd_version_convert(version);

	if (!s_addr || !nn || nfs_vers == SYNO_NFSD_VERSION_END)
		return -EINVAL;

	ret = sockaddr_to_client_addr(s_addr, nfs_vers, &addr);
	if (ret)
		return ret;
	return client_register(nn, addr, os_name);
}

static void client_unregister(const struct syno_nfsd_client_addr addr)
{
	struct syno_nfsd_client_entry *entry;

	mutex_lock(&g_syno_delete_client_mutex);
	entry = client_find(addr);
	if (!entry)
		goto unlock;
	if (atomic_dec_return(&entry->holder) > 0)
		goto put_client;
#ifdef MY_ABC_HERE
	dec_connection();
#endif /* MY_ABC_HERE */
	client_rmdir(entry);
	put_client(entry);

put_client:
	put_client(entry);
unlock:
	mutex_unlock(&g_syno_delete_client_mutex);
}

void syno_nfsd_client_unregister(const struct sockaddr *s_addr, int version)
{
	int ret;
	struct syno_nfsd_client_addr addr;
	enum syno_nfsd_version nfs_vers = syno_nfsd_version_convert(version);

	if (!s_addr || nfs_vers == SYNO_NFSD_VERSION_END)
		return;
	ret = sockaddr_to_client_addr(s_addr, nfs_vers, &addr);
	if (ret)
		return;
	client_unregister(addr);
}

static struct syno_nfsd_client_entry *
alloc_client(const struct syno_nfsd_client_addr *addr)
{
	int i;
	struct syno_nfsd_client_entry *entry;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;
	INIT_HLIST_BL_NODE(&entry->client_hash);
	entry->vfs_io = alloc_percpu(*entry->vfs_io);
	if (!entry->vfs_io) {
		kfree(entry);
		return NULL;
	}
	for_each_possible_cpu(i)
		memset(per_cpu_ptr(entry->vfs_io, i), 0, sizeof(struct syno_nfsd_io_stat));
	entry->addr = *addr;
	kref_init(&entry->refs);
	atomic_set(&entry->holder, 0);
	entry->last_used = jiffies;
	return entry;
}

static void release_client(struct kref *kref)
{
	struct syno_nfsd_client_entry *entry =
		container_of(kref, struct syno_nfsd_client_entry, refs);
	struct hlist_bl_head *b = client_addr_hash(&entry->addr);

	WARN_ON(entry->info_dentry);

	hlist_bl_lock(b);
	if (!hlist_bl_unhashed(&entry->client_hash))
		hlist_bl_del_rcu(&entry->client_hash);
	hlist_bl_unlock(b);

	synchronize_rcu();
	free_percpu(entry->vfs_io);
	kfree(entry);
}

static inline void put_client(struct syno_nfsd_client_entry *entry)
{
	if (!entry)
		return;
	might_sleep();
	kref_put(&entry->refs, release_client);
}

static int parse_client_addr(const char *buf, size_t size,
			     struct syno_nfsd_client_addr *client, bool *is_add)
{
	unsigned int op;
	unsigned int family;
	unsigned int vers;
	const char *family_ptr = strchr(buf, ',');
	const char *vers_ptr = (!family_ptr) ? NULL : strchr(family_ptr + 1, ',');
	const char *addr_ptr = (!vers_ptr) ? NULL : strchr(vers_ptr + 1, ',');
	enum syno_nfsd_version nfs_vers = SYNO_NFSD_VERSION_END;

	if (!addr_ptr || !client || !is_add)
		return -EINVAL;

	if (3 != sscanf(buf, "%u,%u,%u", &op, &family, &vers))
		return -EINVAL;
	if (op != 1 && op != 2)
		return -EINVAL;
	*is_add = (op == 1);

	nfs_vers = syno_nfsd_version_convert(vers);
	if (nfs_vers == SYNO_NFSD_VERSION_END)
		return -EINVAL;

	client->family = family;
	client->nfs_vers = nfs_vers;

	switch (client->family) {
	case AF_INET:
		if (in4_pton(addr_ptr + 1, size - (addr_ptr - buf + 1),
			     (u8 *)&client->sin_addr.s_addr, -1, NULL) != 1)
			return -EINVAL;
		break;
	case AF_INET6:
		if (in6_pton(addr_ptr + 1, size - (addr_ptr - buf + 1),
			     (u8 *)&client->sin6_addr.s6_addr, -1, NULL) != 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

ssize_t syno_nfsd_client_ctl(char *buf, size_t size)
{
	int ret;
	struct syno_nfsd_client_addr addr;
	bool is_add = false;
	struct nfsd_net *nn = syno_nfsd_net_get();

	if (!nn)
		return -EINVAL;

	if (!size)
		return 0;

	ret = parse_client_addr(buf, size, &addr, &is_add);
	if (ret)
		return ret;

	if (is_add) {
		ret = client_register(nn, addr, NULL);
	} else {
		client_unregister(addr);
		ret = 0;
	}
	return ret;
}

void syno_nfsd_clients_destroy_all(void)
{
	int i;
	struct hlist_bl_head *b;
	struct hlist_bl_node *node;
	struct hlist_bl_node *tmp;
	struct syno_nfsd_client_entry *entry;

	mutex_lock(&g_syno_delete_client_mutex);
	for (i = 0; i < ARRAY_SIZE(g_syno_nfsd_client_hosts); ++i) {
		b = &g_syno_nfsd_client_hosts[i];
		hlist_bl_for_each_entry_safe(entry, node, tmp, b, client_hash) {
			client_rmdir(entry);
			put_client(entry);
		}
	}
	mutex_unlock(&g_syno_delete_client_mutex);
}

void syno_nfsd_client_cleaner(void)
{
	int i;
	struct hlist_bl_head *b;
	struct hlist_bl_node *node;
	struct hlist_bl_node *tmp;
	struct syno_nfsd_client_entry *entry;
	unsigned long expire_jiffie =
		msecs_to_jiffies(syno_nfsd_client_expire_time_get() * HZ);

	mutex_lock(&g_syno_delete_client_mutex);
	for (i = 0; i < SYNO_NFSD_CLIENT_NRHASH; ++i) {
		b = &g_syno_nfsd_client_hosts[i];
		hlist_bl_for_each_entry_safe(entry, node, tmp, b, client_hash) {
			if (!kref_get_unless_zero(&entry->refs))
				continue;
			if (!is_should_expire_version(entry->addr.nfs_vers) ||
			    time_after(entry->last_used + expire_jiffie,
				       jiffies)) {
				put_client(entry);
				continue;
			}
			client_rmdir(entry);
			put_client(entry);
			put_client(entry);
		}
	}
	mutex_unlock(&g_syno_delete_client_mutex);
}

/*
 * below operations are copy from fs/nfsd/nfsctl.c nfsd_client_mkdir
 */
static struct syno_nfsd_client_entry *
__get_client_from_inode(struct inode *inode)
{
	struct syno_nfsd_client_entry *entry = inode->i_private;
	entry = inode->i_private;
	if (entry)
		kref_get(&entry->refs);
	return entry;
}

static struct syno_nfsd_client_entry *get_client_from_inode(struct inode *inode)
{
	struct syno_nfsd_client_entry *entry;
	inode_lock_shared(inode);
	entry = __get_client_from_inode(inode);
	inode_unlock_shared(inode);
	return entry;
}

static int __do_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode,
		      struct syno_nfsd_client_entry *entry)
{
	struct inode *inode;

	inode = nfsd_get_inode(dir->i_sb, mode);
	if (!inode)
		return -ENOMEM;
	if (entry) {
		inode->i_private = entry;
		kref_get(&entry->refs);
	}
	d_add(dentry, inode);
	inc_nlink(dir);
	fsnotify_mkdir(dir, dentry);
	return 0;
}

static struct dentry *do_mkdir(struct dentry *parent,
			       struct syno_nfsd_client_entry *entry, char *name)
{
	int ret;
	struct inode *dir = parent->d_inode;
	struct dentry *dentry = NULL;

	inode_lock(dir);
	dentry = d_alloc_name(parent, name);
	if (!dentry) {
		ret = -ENOMEM;
		goto out;
	}
	ret = __do_mkdir(d_inode(parent), dentry, S_IFDIR | 0600, entry);
	if (ret)
		goto out;

	ret = 0;
out:
	inode_unlock(dir);
	if (ret) {
		dput(dentry);
		dentry = ERR_PTR(ret);
	}
	return dentry;
}

static void release_client_from_inode(struct inode *inode)
{
	struct syno_nfsd_client_entry *entry = inode->i_private;

	inode->i_private = NULL;
	put_client(entry);
}

static void remove_file(struct inode *dir, struct dentry *dentry)
{
	int ret;

	release_client_from_inode(d_inode(dentry));
	dget(dentry);
	ret = simple_unlink(dir, dentry);
	d_delete(dentry);
	dput(dentry);
	WARN_ON_ONCE(ret);
}

static void remove_files(struct dentry *root)
{
	struct dentry *dentry, *tmp;

	list_for_each_entry_safe(dentry, tmp, &root->d_subdirs, d_child) {
		if (!simple_positive(dentry)) {
			WARN_ON_ONCE(1); /* I think this can't happen? */
			continue;
		}
		remove_file(d_inode(root), dentry);
	}
}

static int create_files(struct dentry *root, const struct tree_descr *files)
{
	int ret;
	struct inode *dir = d_inode(root);
	struct inode *inode;
	struct dentry *dentry;
	int i;

	inode_lock(dir);
	for (i = 0; files->name && files->name[0]; i++, files++) {
		if (!files->name)
			continue;
		dentry = d_alloc_name(root, files->name);
		if (!dentry) {
			ret = -ENOMEM;
			goto out;
		}
		inode = nfsd_get_inode(d_inode(root)->i_sb,
				       S_IFREG | files->mode);
		if (!inode) {
			dput(dentry);
			ret = -ENOMEM;
			goto out;
		}
		inode->i_fop = files->ops;
		inode->i_private = __get_client_from_inode(dir);
		d_add(dentry, inode);
		fsnotify_create(dir, dentry);
	}
	ret = 0;
out:
	if (ret)
		remove_files(root);
	inode_unlock(dir);
	return ret;
}

static void __client_rmdir(struct dentry *dentry)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct inode *inode = d_inode(dentry);
	int ret;

	inode_lock(dir);
	remove_files(dentry);
	release_client_from_inode(inode);
	dget(dentry);
	ret = simple_rmdir(dir, dentry);
	WARN_ON_ONCE(ret);
	fsnotify_rmdir(dir, dentry);
	d_delete(dentry);
	dput(dentry);
	inode_unlock(dir);
}

static void client_rmdir(struct syno_nfsd_client_entry *entry)
{
	if (!entry)
		return;
	if (entry->info_dentry)
		__client_rmdir(entry->info_dentry);
	entry->info_dentry = NULL;
}

static const struct file_operations client_info_fops = {
	.open = client_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations client_io_stat_fops = {
	.open = client_io_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct tree_descr syno_client_files[] = {
	[0] = { "info", &client_info_fops, S_IRUSR },
	[1] = { "io_stat", &client_io_stat_fops, S_IRUSR },
	[2] = { "" },
};

static struct dentry *client_mkdir(struct nfsd_net *nn,
				   struct syno_nfsd_client_entry *entry)
{
	struct dentry *dentry;
	char name[32];
	int ret;
	s64 id;

	id = atomic64_inc_return(&g_syno_client_id);

	sprintf(name, "%lld", id);

	dentry = do_mkdir(nn->nfsd_syno_client_dir, entry, name);
	if (IS_ERR(dentry))
		goto out;

	ret = create_files(dentry, syno_client_files);
	if (ret) {
		__client_rmdir(dentry);
		dentry = ERR_PTR(ret);
		goto out;
	}

	ret = 0;
out:
	return dentry;
}

static int client_info_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct syno_nfsd_client_entry *entry;

	entry = get_client_from_inode(inode);
	if (!entry)
		return -ENXIO;
	if (entry->addr.family == AF_INET)
		seq_printf(m, "address: %pI4\n", &entry->addr.sin_addr);
	else
		seq_printf(m, "address: %pI6\n", &entry->addr.sin_addr);
	seq_printf(m, "nfs vers: %u\n", entry->addr.nfs_vers);
	seq_printf(m, "hash: 0x%X\n", __client_addr_hash(&entry->addr));
	put_client(entry);

	return 0;
}

static int client_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, client_info_show, inode);
}

static int client_io_stat_show(struct seq_file *m, void *v)
{
	int i;
	int cpu;
	struct inode *inode = m->private;
	struct syno_nfsd_client_entry *entry;
	struct syno_nfsd_io_stat io_stat;
	memset(&io_stat, 0, sizeof(io_stat));

	entry = get_client_from_inode(inode);
	if (!entry)
		return -ENXIO;

	for_each_possible_cpu(cpu) {
		struct syno_nfsd_io_stat *ptr = per_cpu_ptr(entry->vfs_io, cpu);

		for (i = SYNO_NFSD_IO_READ; i < SYNO_NFSD_IO_TYPE_END; ++i) {
			io_stat.io_count[i] += ptr->io_count[i];
			io_stat.io_bytes[i] += ptr->io_bytes[i];
			io_stat.io_latency[i] += ptr->io_latency[i];
			io_stat.max_io_latency[i] =
				max(io_stat.max_io_latency[i],
				    ptr->max_io_latency[i]);
		}
	}

	for (i = SYNO_NFSD_IO_READ; i < SYNO_NFSD_IO_TYPE_END; ++i)
		seq_printf(m, "%llu %llu %llu %llu\n",
			   io_stat.io_count[i], io_stat.io_bytes[i],
			   io_stat.io_latency[i],
			   io_stat.max_io_latency[i]);
	put_client(entry);

	return 0;
}

static int client_io_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, client_io_stat_show, inode);
}

#ifdef MY_ABC_HERE

#define MAX_OS_ENTRIES 		(100)

atomic_t g_curr_os_entries;
atomic_t g_max_connection;
atomic_t g_connection;
static DEFINE_SPINLOCK(g_connection_lock);

struct syno_nfsd_connection_os_entry {
	struct xdr_netobj os_name;
	struct list_head list;
	struct rb_root clients;
};

struct syno_nfsd_connection_client_entry {
	struct rb_node node;
	struct syno_nfsd_client_addr addr;
};

struct list_head g_os_list_head;

void syno_nfsd_connection_init(void)
{
	atomic_set(&g_connection, 0);
	atomic_set(&g_curr_os_entries, 0);
	syno_nfsd_max_connection_init();
	INIT_LIST_HEAD(&g_os_list_head);
}

void syno_nfsd_connection_destroy(void)
{
	struct syno_nfsd_connection_os_entry *os, *os_tmp;
	struct syno_nfsd_connection_client_entry *client, *client_tmp;
	spin_lock(&g_connection_lock);
	list_for_each_entry_safe(os, os_tmp, &g_os_list_head, list) {
		rbtree_postorder_for_each_entry_safe(client, client_tmp, &os->clients, node)
			kfree(client);
		list_del(&os->list);
		kfree(os->os_name.data);
		kfree(os);
	}
	spin_unlock(&g_connection_lock);
	atomic_set(&g_curr_os_entries, 0);
}

static void inc_connection(void)
{
	atomic_inc(&g_connection);
	atomic_set(&g_max_connection, max(atomic_read(&g_connection), atomic_read(&g_connection)));
}

static void __nfsd_connection_reg(const struct xdr_netobj *os_name, struct syno_nfsd_client_addr addr)
{
	bool exist = false;
	int cmp;
	struct rb_node **p = NULL;
	struct rb_node *parent_node = NULL;
	struct syno_nfsd_connection_os_entry *os;
	struct syno_nfsd_connection_os_entry *new_os = NULL;
	struct syno_nfsd_connection_client_entry *client;
	struct syno_nfsd_connection_client_entry *new_client = NULL;
	static const struct xdr_netobj unknown = {.len = 6, .data = "Unkown"};

	if (!os_name || !os_name->data || !os_name->len)
		os_name = &unknown;

	spin_lock(&g_connection_lock);
	list_for_each_entry(os, &g_os_list_head, list) {
		if (os->os_name.data && os_name->len == os->os_name.len &&
		    0 == memcmp(os_name->data, os->os_name.data, os_name->len))
			break;
	}
	if (list_entry_is_head(os, &g_os_list_head, list)) {
		if (atomic_read(&g_curr_os_entries) > MAX_OS_ENTRIES)
			goto out;
		/*
		 * Since it may hold some spinlock when register connection
		 * we use ATOMIC flag to make sure memory allocation won't sleep
		 */
		new_os = kzalloc(sizeof(*new_os), GFP_ATOMIC);
		if (!new_os)
			goto out;

		xdr_netobj_dup(&new_os->os_name, (struct xdr_netobj *) os_name, GFP_ATOMIC);
		if (!new_os->os_name.data)
			goto out;
		new_os->clients = RB_ROOT;
		list_add_tail(&new_os->list, &g_os_list_head);
		os = new_os;
		new_os = NULL;
		atomic_inc(&g_curr_os_entries);
	}
	p = &os->clients.rb_node;
	while (*p) {
		parent_node = *p;
		client = rb_entry(parent_node, struct syno_nfsd_connection_client_entry, node);
		cmp = cmp_client_addr(client->addr, addr);
		if (cmp == 0) {
			exist = true;
			break;
		} else if (cmp > 0) {
			p = &(*p)->rb_left;
		} else {
			p = &(*p)->rb_right;
		}
	}
	if (!exist) {
		new_client = kzalloc(sizeof(*new_client), GFP_ATOMIC);
		if (!new_client)
			goto out;
		new_client->addr = addr;
		rb_link_node(&new_client->node, parent_node, p);
		rb_insert_color(&new_client->node, &os->clients);
		new_client = NULL;
	}
out:
	spin_unlock(&g_connection_lock);
	if (new_os)
		kfree(new_os->os_name.data);
	kfree(new_os);
	kfree(new_client);
}

static void nfsd_connection_reg(struct xdr_netobj *os_name, struct sockaddr *s_addr,
				enum syno_nfsd_version nfs_vers)
{
	int ret;
	struct syno_nfsd_client_addr addr;

	if (!s_addr)
		return;

	ret = sockaddr_to_client_addr(s_addr, nfs_vers, &addr);
	if (ret)
		return;
	__nfsd_connection_reg(os_name, addr);
}

static void dec_connection(void)
{
	atomic_dec(&g_connection);
}

void syno_nfsd_max_connection_init(void)
{
	atomic_set(&g_max_connection, atomic_read(&g_connection));
}

int syno_nfsd_max_connection(void)
{
	return atomic_read(&g_max_connection);
}

static int total_connection_stat_show(struct seq_file *m, void *v)
{
	int i;
	int vers[SYNO_NFSD_VERSION_END] = {0};
	struct syno_nfsd_connection_os_entry *os;
	struct syno_nfsd_connection_client_entry *client, *client_tmp;

	spin_lock(&g_connection_lock);
	list_for_each_entry(os, &g_os_list_head, list) {
		if (!os->os_name.data)
			continue;
		memset(vers, 0, sizeof(vers));
		rbtree_postorder_for_each_entry_safe(client, client_tmp, &os->clients, node)
			vers[client->addr.nfs_vers]++;
		seq_printf(m, "\"");
		seq_escape_mem_ascii(m, os->os_name.data, os->os_name.len);
		seq_printf(m, "\":");
		for (i = SYNO_NFSD_VERSION_2; i < SYNO_NFSD_VERSION_END; ++i)
			seq_printf(m, " %d", vers[i]);
		seq_printf(m, "\n");

	}
	spin_unlock(&g_connection_lock);

	return 0;
}

int syno_nfsd_total_connection_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, total_connection_stat_show, NULL);
}

void syno_nfsd_total_connection_reset(void)
{
	int i;
	struct nfsd_net *nn = syno_nfsd_net_get();
	struct nfs4_client *clp, *clp_tmp;
	struct hlist_bl_head *b;
	struct hlist_bl_node *node;
	struct hlist_bl_node *tmp;
	struct syno_nfsd_client_entry *entry;

	syno_nfsd_connection_destroy();
	spin_lock(&nn->client_lock);
	rbtree_postorder_for_each_entry_safe(clp, clp_tmp, &nn->conf_name_tree, cl_namenode) {
		nfsd_connection_reg(&clp->cl_nii_name, (struct sockaddr *) &clp->cl_addr, SYNO_NFSD_VERSION_4);
	}
	spin_unlock(&nn->client_lock);

	mutex_lock(&g_syno_delete_client_mutex);
	for (i = 0; i < ARRAY_SIZE(g_syno_nfsd_client_hosts); ++i) {
		b = &g_syno_nfsd_client_hosts[i];
		hlist_bl_for_each_entry_safe(entry, node, tmp, b, client_hash) {
			if (entry->addr.nfs_vers >= SYNO_NFSD_VERSION_4)
				continue;
			__nfsd_connection_reg(NULL, entry->addr);
		}
	}
	mutex_unlock(&g_syno_delete_client_mutex);
}

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
enum syno_nfsd_latency_stage {
	SYNO_NFSD_LATENCY_STAGE_TOTAL_RPC = 0,
	SYNO_NFSD_LATENCY_STAGE_TOTAL_VFS,
	SYNO_NFSD_LATENCY_STAGE_ONLY_RPC,
	SYNO_NFSD_LATENCY_STAGE_END,
};

#define SYNO_NFSD_LATENCY_BUCKET_NR		(9u)

static DEFINE_SPINLOCK(g_latency_histogram_lock);
u64 g_latency_histogram[SYNO_NFSD_VERSION_END][SYNO_NFSD_LATENCY_STAGE_END][SYNO_NFSD_IO_TYPE_END][SYNO_NFSD_LATENCY_BUCKET_NR];

static DEFINE_SPINLOCK(g_errtbl_lock);

static struct {
	const int nfserr;
	unsigned int counts[SYNO_NFSD_VERSION_END];
} g_errtbl[] = {
	{NFSERR_PERM, },
	{NFSERR_NOENT, },
	{NFSERR_IO, },
	{NFSERR_NXIO, },
	{NFSERR_EAGAIN, },
	{NFSERR_ACCES, },
	{NFSERR_EXIST, },
	{NFSERR_XDEV, },
	{NFSERR_NODEV, },
	{NFSERR_NOTDIR, },
	{NFSERR_ISDIR, },
	{NFSERR_INVAL, },
	{NFSERR_FBIG, },
	{NFSERR_NOSPC, },
	{NFSERR_ROFS, },
	{NFSERR_MLINK, },
	{NFSERR_OPNOTSUPP, },
	{NFSERR_NAMETOOLONG, },
	{NFSERR_NOTEMPTY, },
	{NFSERR_DQUOT, },
	{NFSERR_STALE, },
	{NFSERR_REMOTE, },
	{NFSERR_WFLUSH, },
	{NFSERR_BADHANDLE, },
	{NFSERR_NOT_SYNC, },
	{NFSERR_BAD_COOKIE, },
	{NFSERR_NOTSUPP, },
	{NFSERR_TOOSMALL, },
	{NFSERR_SERVERFAULT, },
	{NFSERR_BADTYPE, },
	{NFSERR_JUKEBOX, },
	{NFSERR_SAME, },
	{NFSERR_DENIED, },
	{NFSERR_EXPIRED, },
	{NFSERR_LOCKED, },
	{NFSERR_GRACE, },
	{NFSERR_FHEXPIRED, },
	{NFSERR_SHARE_DENIED, },
	{NFSERR_WRONGSEC, },
	{NFSERR_CLID_INUSE, },
	{NFSERR_RESOURCE, },
	{NFSERR_MOVED, },
	{NFSERR_NOFILEHANDLE, },
	{NFSERR_MINOR_VERS_MISMATCH, },
	{NFSERR_STALE_CLIENTID, },
	{NFSERR_STALE_STATEID, },
	{NFSERR_OLD_STATEID, },
	{NFSERR_BAD_STATEID, },
	{NFSERR_BAD_SEQID, },
	{NFSERR_NOT_SAME, },
	{NFSERR_LOCK_RANGE, },
	{NFSERR_SYMLINK, },
	{NFSERR_RESTOREFH, },
	{NFSERR_LEASE_MOVED, },
	{NFSERR_ATTRNOTSUPP, },
	{NFSERR_NO_GRACE, },
	{NFSERR_RECLAIM_BAD, },
	{NFSERR_RECLAIM_CONFLICT, },
	{NFSERR_BAD_XDR, },
	{NFSERR_LOCKS_HELD, },
	{NFSERR_OPENMODE, },
	{NFSERR_BADOWNER, },
	{NFSERR_BADCHAR, },
	{NFSERR_BADNAME, },
	{NFSERR_BAD_RANGE, },
	{NFSERR_LOCK_NOTSUPP, },
	{NFSERR_OP_ILLEGAL, },
	{NFSERR_DEADLOCK, },
	{NFSERR_FILE_OPEN, },
	{NFSERR_ADMIN_REVOKED, },
	{NFSERR_CB_PATH_DOWN, }
};

void syno_nfsd_udc_stat_init(void)
{
	int i;
	memset(g_latency_histogram, 0, sizeof(g_latency_histogram));
	for (i = 0; i < ARRAY_SIZE(g_errtbl); i++) {
		memset(g_errtbl[i].counts, 0, sizeof(g_errtbl[i].counts));
	}
}

static inline void store_latency(unsigned int lat_s,
			  enum syno_nfsd_version nfs_vers,
			  enum syno_nfsd_io_stat_type op,
			  enum syno_nfsd_latency_stage stage)
{
	unsigned int msb = min(SYNO_NFSD_LATENCY_BUCKET_NR - 1, (unsigned int) fls(lat_s));
	g_latency_histogram[nfs_vers][stage][op][msb]++;
}

void syno_nfsd_store_latency_into_histogram(unsigned int rpc_lat_s, unsigned int vfs_lat_s,
					 enum syno_nfsd_version nfs_vers,
					 enum syno_nfsd_io_stat_type op)
{
	unsigned int only_rpc_lat_s = max(0, (int) rpc_lat_s - (int) vfs_lat_s);
	spin_lock(&g_latency_histogram_lock);
	store_latency(rpc_lat_s, nfs_vers, op, SYNO_NFSD_LATENCY_STAGE_TOTAL_RPC);
	store_latency(vfs_lat_s, nfs_vers, op, SYNO_NFSD_LATENCY_STAGE_TOTAL_VFS);
	store_latency(only_rpc_lat_s, nfs_vers, op, SYNO_NFSD_LATENCY_STAGE_ONLY_RPC);
	spin_unlock(&g_latency_histogram_lock);
}

static int latency_histogram_show(struct seq_file *m, void *v)
{
	int i, j, k, l;
	for (l = 0; l < SYNO_NFSD_LATENCY_BUCKET_NR; ++l) {
		for (i = SYNO_NFSD_VERSION_2; i < SYNO_NFSD_VERSION_END; ++i)
			for (j = SYNO_NFSD_LATENCY_STAGE_TOTAL_RPC; j < SYNO_NFSD_LATENCY_STAGE_END; ++j)
				for (k = SYNO_NFSD_IO_READ; k < SYNO_NFSD_IO_TYPE_END; ++k)
					seq_printf(m, "%llu ", g_latency_histogram[i][j][k][l]);
		seq_printf(m, "\n");
	}

	return 0;
}

int syno_nfsd_latency_histogram_open(struct inode *inode, struct file *file)
{
	return single_open(file, latency_histogram_show, NULL);
}

static int total_error_show(struct seq_file *m, void *v)
{

	int i, j;

	for (i = 0; i < ARRAY_SIZE(g_errtbl); i++) {
		seq_printf(m, "err %d:", g_errtbl[i].nfserr);
		for (j = SYNO_NFSD_VERSION_2; j < SYNO_NFSD_VERSION_END; ++j)
			seq_printf(m, " %u", g_errtbl[i].counts[j]);
		seq_printf(m, "\n");
	}
	return 0;
}

int syno_nfsd_total_error_open(struct inode *inode, struct file *file)
{
	return single_open(file, total_error_show, NULL);
}

void syno_nfsd_store_error(int errno, enum syno_nfsd_version nfs_vers)
{
	int i;
	if (!errno)
		return;

	for (i = 0; i < ARRAY_SIZE(g_errtbl); i++) {
		if (g_errtbl[i].nfserr == errno) {
			spin_lock(&g_errtbl_lock);
			g_errtbl[i].counts[nfs_vers]++;
			spin_unlock(&g_errtbl_lock);
			break;
		}
	}
}

#endif /* MY_ABC_HERE */

