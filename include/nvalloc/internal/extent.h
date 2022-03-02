/******************************************************************************/
#ifdef NVALLOC_H_TYPES

typedef struct extent_s extent_t;
typedef struct extents_s extents_t;

typedef enum
{
	extent_state_active = 0,
	extent_state_dirty = 1,
	extent_state_muzzy = 2,

} extent_state_t;

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

typedef ql_head(extent_t) extent_list_t;
typedef ph(extent_t) extent_heap_t;

struct extent_s
{
	void *e_addr;
	size_t size;
	szind_t szind;
	extent_state_t state;
	arena_t *arena;
	bool slab;
	void *log;
	ql_elm(extent_t) ql_link;
	phn(extent_t) ph_link;
};

struct extents_s
{
	pthread_mutex_t mtx;

	extent_heap_t heaps[MAX_PSZ_IDX + 1];
	atomic_zu_t nextents[MAX_PSZ_IDX + 1];
	atomic_zu_t nbytes[MAX_PSZ_IDX + 1];

	bitmap_t bitmap[(MAX_PSZ_IDX / 64) + 1];

	extent_list_t lru;

	atomic_zu_t npages;

	extent_state_t state;

	bool delay_coalesce;
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS
#include "nvalloc/internal/rtree.h"
extern mutex_pool_t extent_mutex_pool;
	extern rtree_t extents_rtree;

ph_proto(, extent_heap_, extent_heap_t, extent_t)

void extents_dalloc(tcache_t *tcache, arena_t *arena,extents_t *extents, extent_t *extent);
extent_t *extent_recycle(tcache_t *tcache, arena_t *arena, extents_t *extents, vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit, bool growing_retained);
extent_t *extent_alloc_wrapper(tcache_t *tcache, arena_t *arena, vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit);
bool extents_init(extents_t *extents, extent_state_t state, bool delay_coalesce);
bool extent_boot(void);
extent_t *extents_evict(tcache_t *tcache, arena_t *arena, extents_t *extents, size_t npages_min);
extent_t *extent_alloc_with_old_addr(tcache_t *tcache, arena_t *arena,void *new_addr, size_t npages, bool slab, szind_t szind, bool *commit);
void extent_deactivate(tcache_t *tcache, arena_t *arena, extents_t *extents,extent_t *extent);
void extent_dalloc_wrapper(tcache_t *tcache, arena_t *arena,extent_t *extent);
size_t extents_nextents_get(extents_t *extents, pszind_t pind);
size_t extents_nbytes_get(extents_t *extents, pszind_t pind);

extent_state_t extents_state_get(const extents_t *extents);
extent_t * extents_alloc(tcache_t *tcache, arena_t *arena, extents_t *extents, vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit);
#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

static inline void *extent_addr_get(const extent_t *extent)
{
	return extent->e_addr;
}
static inline void extent_addr_set(extent_t *extent, void *addr)
{
	extent->e_addr = addr;
}

static inline size_t
extent_size_get(const extent_t *extent)
{
	return extent->size;
}

static inline void *
extent_last_get(const extent_t *extent)
{
	return (void *)((uintptr_t)extent_addr_get(extent) +
					extent_size_get(extent) - PAGE_SIZE);
}
static inline void *
extent_before_get(const extent_t *extent)
{
	return (void *)((uintptr_t)extent_addr_get(extent) - PAGE_SIZE);
}
static inline void *
extent_past_get(const extent_t *extent)
{
	return (void *)((uintptr_t)extent_addr_get(extent) +
					extent_size_get(extent));
}
static inline void
extent_size_set(extent_t *extent, size_t size)
{
	extent->size = size;
	return;
}

static inline void
extent_szind_set(extent_t *extent, szind_t szind)
{
	extent->szind = szind;
}

static inline szind_t
extent_szind_get(const extent_t *extent)
{

	return extent->szind;
}

static inline extent_state_t
extent_state_get(const extent_t *extent)
{
	return extent->state;
}
static inline bool
extent_slab_get(const extent_t *extent)
{
	return extent->slab;
}

static inline void
extent_slab_set(extent_t *extent, bool slab)
{
	extent->slab = slab;
}

static inline void
extent_state_set(extent_t *extent, extent_state_t state)
{
	extent->state = state;
}

static inline arena_t *
extent_arena_get(const extent_t *extent)
{

	return extent->arena;
}

static inline void
extent_arena_set(extent_t *extent, arena_t *arena)
{
	extent->arena = arena;
}

static inline void
extent_lock(extent_t *extent)
{
	assert(extent != NULL);
	mutex_pool_lock(&extent_mutex_pool, (uintptr_t)extent);
}

static inline void
extent_unlock(extent_t *extent)
{
	assert(extent != NULL);
	mutex_pool_unlock(&extent_mutex_pool, (uintptr_t)extent);
}

static inline void
extent_lock2(extent_t *extent1, extent_t *extent2)
{
	assert(extent1 != NULL && extent2 != NULL);
	mutex_pool_lock2(&extent_mutex_pool, (uintptr_t)extent1,
					 (uintptr_t)extent2);
}

static inline void
extent_unlock2(extent_t *extent1, extent_t *extent2)
{
	assert(extent1 != NULL && extent2 != NULL);
	mutex_pool_unlock2(&extent_mutex_pool, (uintptr_t)extent1,
					   (uintptr_t)extent2);
}

static inline void
extent_list_append(extent_list_t *list, extent_t *extent)
{
	ql_tail_insert(list, extent, ql_link);
}

static inline void
extent_list_remove(extent_list_t *list, extent_t *extent)
{
	ql_remove(list, extent, ql_link);
}
static inline extent_t *
extent_list_first(const extent_list_t *list)
{
	return ql_first(list);
}
static inline extent_t *
extent_list_next(const extent_list_t *list, const extent_t *extent)
{
	return ql_next(list, extent, ql_link);
}
static inline void
extent_list_init(extent_list_t *list)
{
	ql_new(list);
}

static inline int
extent_ad_comp(const extent_t *a, const extent_t *b)
{
	uintptr_t a_addr = (uintptr_t)extent_addr_get(a);
	uintptr_t b_addr = (uintptr_t)extent_addr_get(b);

	return (a_addr > b_addr) - (a_addr < b_addr);
}

#endif /* NVALLOC_H_INLINES */
	   /******************************************************************************/