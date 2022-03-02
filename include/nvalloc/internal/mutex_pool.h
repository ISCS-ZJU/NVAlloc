#ifndef NVALLOC_INTERNAL_MUTEX_POOL_H
#define NVALLOC_INTERNAL_MUTEX_POOL_H




/* We do mod reductions by this value, so it should be kept a power of 2. */
#define MUTEX_POOL_SIZE 256

typedef struct mutex_pool_s mutex_pool_t;
struct mutex_pool_s {
	pthread_mutex_t mutexes[MUTEX_POOL_SIZE];
};

bool mutex_pool_init(mutex_pool_t *pool);

/* Internal helper - not meant to be called outside this module. */
static inline pthread_mutex_t *
mutex_pool_mutex(mutex_pool_t *pool, uintptr_t key) {
	size_t hash_result[2];
	hash(&key, sizeof(key), 0xd50dcc1b, hash_result);
	return &pool->mutexes[hash_result[0] % MUTEX_POOL_SIZE];
}


/*
 * Note that a mutex pool doesn't work exactly the way an embdedded mutex would.
 * You're not allowed to acquire mutexes in the pool one at a time.  You have to
 * acquire all the mutexes you'll need in a single function call, and then
 * release them all in a single function call.
 */

static inline void
mutex_pool_lock(mutex_pool_t *pool, uintptr_t key) {


	pthread_mutex_t *mutex = mutex_pool_mutex(pool, key);
	pthread_mutex_lock(mutex);
}

static inline void
mutex_pool_unlock(mutex_pool_t *pool, uintptr_t key) {
	pthread_mutex_t *mutex = mutex_pool_mutex(pool, key);
	pthread_mutex_unlock(mutex);

}

static inline void
mutex_pool_lock2(mutex_pool_t *pool, uintptr_t key1,
    uintptr_t key2) {


	pthread_mutex_t *mutex1 = mutex_pool_mutex(pool, key1);
	pthread_mutex_t *mutex2 = mutex_pool_mutex(pool, key2);
	if ((uintptr_t)mutex1 < (uintptr_t)mutex2) {
		pthread_mutex_lock(mutex1);
		pthread_mutex_lock(mutex2);
	} else if ((uintptr_t)mutex1 == (uintptr_t)mutex2) {
		pthread_mutex_lock(mutex1);
	} else {
		pthread_mutex_lock(mutex2);
		pthread_mutex_lock(mutex1);
	}
}

static inline void
mutex_pool_unlock2(mutex_pool_t *pool, uintptr_t key1,
    uintptr_t key2) {
	pthread_mutex_t *mutex1 = mutex_pool_mutex(pool, key1);
	pthread_mutex_t *mutex2 = mutex_pool_mutex(pool, key2);
	if (mutex1 == mutex2) {
		pthread_mutex_unlock(mutex1);
	} else {
		pthread_mutex_unlock(mutex1);
		pthread_mutex_unlock(mutex2);
	}


}



#endif /* NVALLOC_INTERNAL_MUTEX_POOL_H */
