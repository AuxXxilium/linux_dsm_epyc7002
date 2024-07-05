#ifndef _LINUX_ION_RTK_CARVEOUT_HEAP_DEBUGFS_H
#define _LINUX_ION_RTK_CARVEOUT_HEAP_DEBUGFS_H

#include "ion.h"

#if defined(CONFIG_DEBUG_FS)
void debugfs_add_heap(struct ion_heap *heap);
#else
void debugfs_add_heap(struct ion_heap *heap)
{
}
#endif

#endif /* _LINUX_ION_RTK_CARVEOUT_HEAP_DEBUGFS_H */
