#ifndef _RTK_RPC_MEM_H
#define _RTK_RPC_MEM_H

#include <linux/types.h>
#include <linux/dma-buf.h>

typedef struct r_program_entry {
	unsigned long phys_addr;
	unsigned long size;
	struct dma_buf *rpc_dmabuf;
	struct r_program_entry *next;
} r_program_entry_t;

void r_program_add(r_program_entry_t * entry);
r_program_entry_t *r_program_remove(unsigned long phys_addr);

#endif /* _RTK_RPC_MEM_H */
