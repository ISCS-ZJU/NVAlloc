#ifndef NVALLOC_H_
#define NVALLOC_H_
#ifdef __cplusplus
extern "C"
{
#endif

#include "stdint.h"



void *nvalloc_malloc_to(size_t size, void **ptr);

void nvalloc_free_from(void **pptr);

int nvalloc_init();

uint64_t nvget_memory_usage();



#ifdef __cplusplus
};
#endif
#endif /* NVALLOC_H_ */