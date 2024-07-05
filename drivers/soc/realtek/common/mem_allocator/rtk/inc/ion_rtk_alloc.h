#ifndef _LINUX_ION_RTK_ALLOC_H
#define _LINUX_ION_RTK_ALLOC_H

#include <linux/types.h>
#include <linux/fdtable.h>

int ext_rtk_ion_alloc(size_t len, unsigned int heap_type_mask,
		      unsigned int flags);
int ext_rtk_ion_close_fd(struct files_struct *files, unsigned fd);

#endif /* _LINUX_ION_RTK_ALLOC_H */
