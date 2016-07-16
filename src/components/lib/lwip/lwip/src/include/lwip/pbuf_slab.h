#ifndef LWIP_PBUF_SLAB_H
#define LWIP_PBUF_SLAB_H
#include <ps_slab.h>

#define BYTES    4
#define MAX_SLAB 30

typedef void *(*slab_alloc_t)(void);
typedef void (*slab_free_t)(void *buf);
struct slab_func {
	slab_alloc_t alloc;
	slab_free_t free;
};
struct slab_func slabs[MAX_SLAB] = {{.alloc = NULL, .free = NULL}};

#define INIT_SLAB(i) \
slabs[i].alloc = ps_slab_alloc_##i;  \
slabs[i].free  = ps_slab_free_##i;

PS_SLAB_CREATE_DEF(7, 7*BYTES)
PS_SLAB_CREATE_DEF(18, 18*BYTES)
PS_SLAB_CREATE_DEF(21, 21*BYTES)
PS_SLAB_CREATE_DEF(22, 22*BYTES)

inline void pbuf_slab_init(void)
{
	INIT_SLAB(7)
	INIT_SLAB(18)
	INIT_SLAB(21)
	INIT_SLAB(22)
}

#endif /* LWIP_PBUF_SLAB_H */
