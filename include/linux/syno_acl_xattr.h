/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */
#ifndef _LINUX_SYNO_ACL_XATTR_H
#define _LINUX_SYNO_ACL_XATTR_H

#define SYNO_ACL_VERSION  0x0002

typedef struct {
	__le16          e_tag;
	__le16          e_inherit;
	__le32          e_perm;
	__le32          e_id;
} __attribute__ ((__packed__)) syno_acl_entry_t;

typedef struct {
	__le16          a_version;
} __attribute__ ((__packed__)) syno_acl_header_t;

#endif	/* _LINUX_SYNO_ACL_XATTR_H */
