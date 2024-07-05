/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FS_CEPH_HASH_H
#define FS_CEPH_HASH_H

#ifdef CONFIG_SYNO_CEPH_CASELESS_STAT
#include <linux/fs.h>
#endif /* CONFIG_SYNO_CEPH_CASELESS_STAT */

#define CEPH_STR_HASH_LINUX      0x1  /* linux dcache hash */
#define CEPH_STR_HASH_RJENKINS   0x2  /* robert jenkins' */

extern unsigned ceph_str_hash_linux(const char *s, unsigned len);
extern unsigned ceph_str_hash_rjenkins(const char *s, unsigned len);

extern unsigned ceph_str_hash(int type, const char *s, unsigned len);
#ifdef CONFIG_SYNO_CEPH_CASELESS_STAT
extern unsigned int ceph_str_upper_hash(int type, const char *s, unsigned int len);
#endif /* CONFIG_SYNO_CEPH_CASELESS_STAT */
extern const char *ceph_str_hash_name(int type);

#endif
