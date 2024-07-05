#ifndef SYNO_SHUTDOWN_HOOK_H
#define SYNO_SHUTDOWN_HOOK_H

#include <linux/mutex.h>
typedef struct __tag_SYNO_SHUTDOWN_HOOK {
	struct list_head node;
	void (*shutdown)(void);
	void (*restart)(void);
} SYNO_SHUTDOWN_HOOK;

typedef struct _tag_SYNO_SHUTDOWN_HOOK_LIST {
	struct list_head hookList;
	struct mutex hookLock;
} SYNO_SHUTDOWN_HOOK_LIST;

#endif /* SYNO_SHUTDOWN_HOOK_H */
