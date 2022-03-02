#define NVALLOC_MUTEX_POOL_C_


#include "nvalloc/internal/nvalloc_internal.h"



bool
mutex_pool_init(mutex_pool_t *pool) {
	for (int i = 0; i < MUTEX_POOL_SIZE; ++i) {
		if (pthread_mutex_init(&pool->mutexes[i],0)) {
			return true;
		}
	}
	return false;
}
