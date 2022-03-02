#define NVALLOC_XX_C_
#include "nvalloc/internal/nvalloc_internal.h"
#include <linux/falloc.h>
/******************************************************************************/
/* Data. */

rtree_t extents_rtree;
mutex_pool_t extent_mutex_pool;

ph_gen(, extent_heap_, extent_heap_t, extent_t, ph_link, extent_ad_comp)

	static extent_t *extent_try_coalesce_impl(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
											  extent_t *extent, bool *coalesced, bool inactive_only);
static void
extents_insert_locked(tcache_t *tcache, extents_t *extents, extent_t *extent);

static void
extent_interior_deregister(tcache_t *tcache, rtree_ctx_t *rtree_ctx,
						   extent_t *extent);
static extent_t *
extent_split_impl(tcache_t *tcache, arena_t *arena,
				  extent_t *extent, size_t size_a,
				  szind_t szind_a, bool slab_a, size_t size_b, szind_t szind_b, bool slab_b,
				  bool growing_retained, vslab_t *vslab_a, vslab_t *vslab_b);
/******************************************************************************/
/* Inline tool function */
static inline pszind_t get_psizeclass_floor(size_t npages)
{
	pszind_t pind = get_psizeclass(npages);
	pszind_t ppind = get_psizeclass(npages + 1);
	return pind == ppind ? pind - 1 : pind;
}
/******************************************************************************/

static void
extents_stats_add(extents_t *extent, pszind_t pind, size_t sz)
{
	size_t cur = atomic_load_zu(&extent->nextents[pind], ATOMIC_RELAXED);
	atomic_store_zu(&extent->nextents[pind], cur + 1, ATOMIC_RELAXED);
	cur = atomic_load_zu(&extent->nbytes[pind], ATOMIC_RELAXED);
	atomic_store_zu(&extent->nbytes[pind], cur + sz, ATOMIC_RELAXED);
}

static void
extents_stats_sub(extents_t *extent, pszind_t pind, size_t sz)
{
	size_t cur = atomic_load_zu(&extent->nextents[pind], ATOMIC_RELAXED);
	atomic_store_zu(&extent->nextents[pind], cur - 1, ATOMIC_RELAXED);
	cur = atomic_load_zu(&extent->nbytes[pind], ATOMIC_RELAXED);
	atomic_store_zu(&extent->nbytes[pind], cur - sz, ATOMIC_RELAXED);
}
size_t
extents_nextents_get(extents_t *extents, pszind_t pind)
{
	return atomic_load_zu(&extents->nextents[pind], ATOMIC_RELAXED);
}
size_t
extents_nbytes_get(extents_t *extents, pszind_t pind)
{
	return atomic_load_zu(&extents->nbytes[pind], ATOMIC_RELAXED);
}

extent_state_t
extents_state_get(const extents_t *extents)
{
	return extents->state;
}

bool extent_boot(void)
{
	if (rtree_new(&extents_rtree, true))
	{
		return true;
	}

	if (mutex_pool_init(&extent_mutex_pool))
	{
		return true;
	}

	return false;
}

static bool
extent_rtree_leaf_elms_lookup(tcache_t *tcache, rtree_ctx_t *rtree_ctx,
							  const extent_t *extent, bool dependent, bool init_missing,
							  rtree_leaf_elm_t **r_elm_a, rtree_leaf_elm_t **r_elm_b)
{
	*r_elm_a = rtree_leaf_elm_lookup(&extents_rtree, rtree_ctx,
									 (uintptr_t)extent_addr_get(extent), dependent, init_missing);
	if (!dependent && *r_elm_a == NULL)
	{
		return true;
	}
	assert(*r_elm_a != NULL);

	*r_elm_b = rtree_leaf_elm_lookup(&extents_rtree, rtree_ctx,
									 (uintptr_t)extent_last_get(extent), dependent, init_missing);
	if (!dependent && *r_elm_b == NULL)
	{
		return true;
	}
	assert(*r_elm_b != NULL);

	return false;
}

static void
extent_rtree_write_acquired(rtree_leaf_elm_t *elm_a,
							rtree_leaf_elm_t *elm_b, extent_t *extent, szind_t szind, bool slab)
{

	rtree_leaf_elm_write(&extents_rtree, elm_a, extent, szind, slab);

	if (elm_b != NULL)
	{

		rtree_leaf_elm_write(&extents_rtree, elm_b, extent, szind, slab);
	}
}

bool extents_init(extents_t *extents, extent_state_t state,
				  bool delay_coalesce)
{

	pthread_mutex_init(&extents->mtx, 0);
	for (unsigned i = 0; i < MAX_PSZ_IDX + 1; i++)
	{
		extent_heap_new(&extents->heaps[i]);
	}

	memset(extents->bitmap, 0x00, sizeof(extents->bitmap));
	bitmap_unset_bits(extents->bitmap, MAX_PSZ_IDX);

	extent_list_init(&extents->lru);
	atomic_store_zu(&extents->npages, 0, ATOMIC_RELAXED);
	extents->state = state;
	extents->delay_coalesce = delay_coalesce;
	return false;
}

extent_t *
extent_alloc(tcache_t *tcache, arena_t *arena)
{
	extent_t *extent = NULL;
	posix_memalign((void **)&extent, CACHELINE_SIZE, sizeof(extent_t));

	return extent;
}

void extent_dalloc(arena_t *arena, extent_t *extent)
{
	_free(extent);
}


static void
extent_deregister_impl(tcache_t *tcache, extent_t *extent)
{

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *elm_a, *elm_b;
	extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, extent, true, false,
								  &elm_a, &elm_b);

	extent_lock(extent);

	extent_rtree_write_acquired(elm_a, elm_b, NULL, MAX_PSZ_IDX, false);
	if (extent_slab_get(extent))
	{
		extent_interior_deregister(tcache, rtree_ctx, extent);
		extent_slab_set(extent, false);
	}

	extent_unlock(extent);
}

static void
extent_deregister(tcache_t *tcache, extent_t *extent)
{
	extent_deregister_impl(tcache, extent);
}

void extent_dalloc_wrapper(tcache_t *tcache, arena_t *arena,
						   extent_t *extent)
{

	extent_deregister(tcache, extent);

	arena_file_unmap(extent, arena, PMEMPATH);

	extent_dalloc(arena, extent);

	return;
}

static inline void
extent_init(extent_t *extent, arena_t *arena, void *addr, size_t size,
			bool slab, szind_t szind, extent_state_t state)
{

	extent_arena_set(extent, arena);
	extent_addr_set(extent, addr);
	extent_size_set(extent, size);
	extent_slab_set(extent, slab);
	extent_szind_set(extent, szind);
	extent_state_set(extent, state);

	ql_elm_new(extent, ql_link);
}


static extent_t *
extents_first_fit_locked(tcache_t *tcache, arena_t *arena, extents_t *extents,
						 size_t npages)
{
	extent_t *ret = NULL;

	pszind_t pind = get_psizeclass(npages);

	for (pszind_t i = (pszind_t)bitmap_ffu(extents->bitmap, (size_t)pind);
		 i < MAX_PSZ_IDX;
		 i = (pszind_t)bitmap_ffu(extents->bitmap, (size_t)i + 1))
	{

		assert(!extent_heap_empty(&extents->heaps[i]));
		extent_t *extent = extent_heap_first(&extents->heaps[i]);

		if (extents->delay_coalesce && get_psizeclass_by_idx(i)->npages > npages * 32)
		{
			break;
		}
		if (ret == NULL || extent_ad_comp(extent, ret) < 0)
		{
			ret = extent;
		}
		if (i == MAX_PSZ_IDX - 1)
		{
			break;
		}
	}

	return ret;
}

#ifdef NVALLOC_BESTFIT

__attribute__((unused)) static extent_t *
extents_best_fit_locked(tcache_t *tcache, arena_t *arena, extents_t *extents,
						size_t npages)
{
	extent_t *ret = NULL;

	pszind_t pind = get_psizeclass(npages);

	for (pszind_t i = (pszind_t)bitmap_ffu(extents->bitmap, (size_t)pind);
		 i < MAX_PSZ_IDX;
		 i = (pszind_t)bitmap_ffu(extents->bitmap, (size_t)i + 1))
	{

		assert(!extent_heap_empty(&extents->heaps[i]));
		extent_t *extent = extent_heap_first(&extents->heaps[i]);


		if (extents->delay_coalesce && get_psizeclass_by_idx(i)->npages > npages * 32)
		{
			break;
		}
		if (ret == NULL)
		{
			ret = extent;
			break;
		}
		if (i == MAX_PSZ_IDX - 1)
		{
			break;
		}
	}

	return ret;
}
#endif


static extent_t *
extents_fit_locked(tcache_t *tcache, arena_t *arena, extents_t *extents,
				   size_t npages)
{
#ifdef NVALLOC_BESTFIT
	extent_t *extent = extents_best_fit_locked(tcache, arena, extents, npages);
#else
	extent_t *extent = extents_first_fit_locked(tcache, arena, extents, npages);
#endif

	return extent;
}

static void
extents_remove_locked(tcache_t *tcache, extents_t *extents, extent_t *extent)
{

	assert(extent_state_get(extent) == extents->state);

	size_t size = extent_size_get(extent);
	pszind_t pind = get_psizeclass_floor(size / PAGE_SIZE);
	extent_heap_remove(&extents->heaps[pind], extent);

	if (extent_heap_empty(&extents->heaps[pind]))
	{
		bitmap_set_bits(extents->bitmap, pind);
	}
	extent_list_remove(&extents->lru, extent);



	/*
	 * As in extents_insert_locked, we hold extents->mtx and so don't need
	 * atomic operations for updating extents->npages.
	 */
	size_t cur_extents_npages =
		atomic_load_zu(&extents->npages, ATOMIC_RELAXED);

	atomic_store_zu(&extents->npages,
					cur_extents_npages - (size >> LG_PAGE), ATOMIC_RELAXED);
}
static extent_t *
extent_try_coalesce(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
					extent_t *extent, bool *coalesced)
{
	return extent_try_coalesce_impl(tcache, arena, rtree_ctx,
									extents, extent, coalesced, false);
}

static bool
extent_try_delayed_coalesce(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
							extent_t *extent)
{
	extent_state_set(extent, extent_state_active);
	bool coalesced;
	extent = extent_try_coalesce(tcache, arena, rtree_ctx,
								 extents, extent, &coalesced);
	extent_state_set(extent, extents_state_get(extents));

	if (!coalesced)
	{
		return true;
	}
	extents_insert_locked(tcache, extents, extent);
	return false;
}

static inline int n_muzzy_extent_can_evict(extent_t *extent, void **start)
{
	assert(extent->state == extent_state_muzzy);
	if (extent->size < CHUNK_SIZE)
		return 0;
	uintptr_t minaddr = (uintptr_t)extent->e_addr;
	uintptr_t maxaddr = ((uintptr_t)extent->e_addr) + extent->size;
	uintptr_t tempaddr = roundUp(minaddr, CHUNK_SIZE);
	int ret = 0;
	while (true)
	{
		tempaddr = tempaddr + CHUNK_SIZE;
		if (tempaddr <= maxaddr)
		{
			if (ret == 0)
			{
				*start = (void *)tempaddr;
			}
			ret++;
		}
		else
		{
			return ret;
		}
	}
}

extent_t *
extents_evict(tcache_t *tcache, arena_t *arena,
			  extents_t *extents, size_t npages_min)
{

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;

	pthread_mutex_lock(&extents->mtx);


	extent_t *extent;
	while (true)
	{
		extent = extent_list_first(&extents->lru);
		if (extent == NULL)
		{
			goto label_return;
		}
		size_t extents_npages = atomic_load_zu(&extents->npages,
											   ATOMIC_RELAXED);
		if (extents_npages <= npages_min)
		{
			extent = NULL;
			goto label_return;
		}
		if (!extents->delay_coalesce)
		{
			if (extents_npages < CHUNK_SIZE)
			{
				extent = NULL;
				goto label_return;
			}

			void *start = NULL;
			int nevict;
			for (; extent != NULL; extent = extent_list_next(&extents->lru, extent))
			{
				nevict = n_muzzy_extent_can_evict(extent, &start);

				if (nevict != 0) 
				{
					extents_remove_locked(tcache, extents, extent);

					if ((uintptr_t)start != (uintptr_t)extent->e_addr)
					{
						assert(extent->szind == MAX_PSZ_IDX);
						extent_t *extent_b = extent_split_impl(tcache, arena, extent, (uintptr_t)start - (uintptr_t)extent->e_addr, extent->szind, extent->slab, extent->size - ((uintptr_t)start - (uintptr_t)extent->e_addr), extent->szind, extent->slab, false, NULL, NULL);
						if (extent_b == NULL)
							abort();
						extents_insert_locked(tcache, extents, extent);
						assert(extent_b->szind == MAX_PSZ_IDX);
						extent = extent_b;
					}


					if (extent->size % CHUNK_SIZE != 0)
					{
						extent_t *extent_c = extent_split_impl(tcache, arena, extent, CHUNK_SIZE * nevict, extent->szind, extent->slab, extent->size % CHUNK_SIZE, extent->szind, extent->slab, false, NULL, NULL);
						if (extent_c == NULL)
							abort();
						assert(extent_c->szind == MAX_PSZ_IDX);
						extents_insert_locked(tcache, extents, extent_c);
					}


					assert(extent->size == CHUNK_SIZE * nevict);
					assert((uintptr_t)extent->e_addr == roundUp((uintptr_t)extent->e_addr, CHUNK_SIZE));
					assert(extent->state == extent_state_muzzy);

					goto label_break;
				}
			}
			extent = NULL;
			goto label_return;
		}

		extents_remove_locked(tcache, extents, extent);


		if (extent_try_delayed_coalesce(tcache, arena,
										rtree_ctx, extents, extent))
		{
			break;
		}

	}
label_break:

	switch (extents_state_get(extents))
	{
	case extent_state_active:
		not_reached();
	case extent_state_dirty:
	case extent_state_muzzy:
		extent_state_set(extent, extent_state_active);
		break;
	default:
		not_reached();
	}

label_return:
	pthread_mutex_unlock(&extents->mtx);
	return extent;
}

static void
extent_activate_locked(tcache_t *tcache, arena_t *arena, extents_t *extents,
					   extent_t *extent)
{
	assert(extent_arena_get(extent) == arena);
	assert(extent_state_get(extent) == extents_state_get(extents));

	extents_remove_locked(tcache, extents, extent);
	extent_state_set(extent, extent_state_active);
}


static extent_t *
extent_recycle_extract(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
					   vslab_t *vslab, size_t npages, bool slab,
					   bool growing_retained)
{

	pthread_mutex_lock(&extents->mtx);

	extent_t *extent;

	extent = extents_fit_locked(tcache, arena, extents, npages);

	if (extent == NULL)
	{
		pthread_mutex_unlock(&extents->mtx);
		return NULL;
	}

	extent_activate_locked(tcache, arena, extents, extent);
	pthread_mutex_unlock(&extents->mtx);

	return extent;
}


typedef enum
{

	extent_split_interior_ok,

	extent_split_interior_cant_alloc,

	extent_split_interior_error
} extent_split_interior_result_t;


static extent_t *
extent_split_impl(tcache_t *tcache, arena_t *arena,
				  extent_t *extent, size_t size_a,
				  szind_t szind_a, bool slab_a, size_t size_b, szind_t szind_b, bool slab_b,
				  bool growing_retained, vslab_t *vslab_a, vslab_t *vslab_b)
{
	assert(extent_size_get(extent) == size_a + size_b);
	assert(((slab_a == false) && (vslab_a == NULL)) || ((slab_a == true) && (vslab_a != NULL)));
	assert(((slab_b == false) && (vslab_b == NULL)) || ((slab_b == true) && (vslab_b != NULL)));

	extent_t *trail = extent_alloc(tcache, arena);

	if (trail == NULL)
	{
		goto label_error_a;
	}

	extent_init(trail, arena, (void *)((uintptr_t)extent_addr_get(extent) + size_a), size_b, slab_b, szind_b, extent_state_get(extent));

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *lead_elm_a, *lead_elm_b;
	{
		extent_t lead;

		extent_init(&lead, arena, extent_addr_get(extent), size_a,
					slab_a, szind_a, extent_state_get(extent));

		extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, &lead, false,
									  true, &lead_elm_a, &lead_elm_b);
	}
	rtree_leaf_elm_t *trail_elm_a, *trail_elm_b;
	extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, trail, false, true,
								  &trail_elm_a, &trail_elm_b);

	if (lead_elm_a == NULL || lead_elm_b == NULL || trail_elm_a == NULL || trail_elm_b == NULL)
	{
		goto label_error_b;
	}

	extent_lock2(extent, trail);

	extent_size_set(extent, size_a);
	extent_szind_set(extent, szind_a);
	if (slab_a)
	{
		extent_rtree_write_acquired(lead_elm_a, lead_elm_b, (extent_t *)vslab_a,
									szind_a, slab_a);
	}
	else
	{
		extent_rtree_write_acquired(lead_elm_a, lead_elm_b, extent,
									szind_a, slab_a);
	}
	if (slab_b)
	{
		extent_rtree_write_acquired(trail_elm_a, trail_elm_b, (extent_t *)vslab_b,
									szind_b, slab_b);
	}
	else
	{

		extent_rtree_write_acquired(trail_elm_a, trail_elm_b, trail,
									szind_b, slab_b);
	}

	extent_unlock2(extent, trail);

	return trail;
label_error_b:
	extent_dalloc(arena, trail);
label_error_a:
	return NULL;
}

static extent_split_interior_result_t
extent_split_interior(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx,
					  extent_t **extent, extent_t **lead, extent_t **trail,
					  extent_t **to_leak, extent_t **to_salvage,
					  vslab_t *vslab, size_t npages, bool slab,
					  szind_t szind, bool growing_retained)
{

	size_t psize = npages * PAGE_SIZE;

	size_t leadsize = (uintptr_t)extent_addr_get(*extent) - (uintptr_t)extent_addr_get(*extent);
	assert(leadsize == 0);


	size_t trailsize = extent_size_get(*extent) - leadsize - psize;

	*lead = NULL;
	*trail = NULL;
	*to_leak = NULL;
	*to_salvage = NULL;

	if (leadsize != 0)
	{
		*lead = *extent;
		*extent = extent_split_impl(tcache, arena,
									*lead, leadsize, MAX_PSZ_IDX, false, psize + trailsize, szind,
									slab, growing_retained, NULL, vslab);
		if (*extent == NULL)
		{
			*to_leak = *lead;
			*lead = NULL;
			return extent_split_interior_error;
		}
	}

	if (trailsize != 0)
	{
		*trail = extent_split_impl(tcache, arena, *extent,
								   psize, szind, slab, trailsize, MAX_PSZ_IDX, false,
								   growing_retained, vslab, NULL);
		if (*trail == NULL)
		{
			*to_leak = *extent;
			*to_salvage = *lead;
			*lead = NULL;
			*extent = NULL;
			return extent_split_interior_error;
		}
	}

	if (leadsize == 0 && trailsize == 0)
	{

		extent_szind_set(*extent, szind);
		if (szind != MAX_PSZ_IDX)
		{
			assert((*extent)->state == extent_state_active);
			rtree_szind_slab_update(&extents_rtree, rtree_ctx,
									(uintptr_t)extent_addr_get(*extent), szind, slab);

			if (slab && extent_size_get(*extent) > PAGE_SIZE)
			{
				rtree_szind_slab_update(&extents_rtree,
										rtree_ctx,
										(uintptr_t)extent_past_get(*extent) -
											(uintptr_t)PAGE_SIZE,
										szind, slab);
			}
		}
	}

	return extent_split_interior_ok;
}

static void
extents_insert_locked(tcache_t *tcache, extents_t *extents, extent_t *extent)
{

	assert(extent_state_get(extent) == extents->state);
	assert(extent_slab_get(extent) == false);

	size_t size = extent_size_get(extent);
	assert(extent_size_get(extent) != 0);
	assert(extent_size_get(extent) % PAGE_SIZE == 0);
	pszind_t pind = get_psizeclass_floor(size / PAGE_SIZE);

	if (extent_heap_empty(&extents->heaps[pind]))
	{
		bitmap_unset_bits(extents->bitmap, pind);
	}

	extent_heap_insert(&extents->heaps[pind], extent);


	extent_list_append(&extents->lru, extent);
	size_t npages = size / PAGE_SIZE;

	size_t cur_extents_npages =
		atomic_load_zu(&extents->npages, ATOMIC_RELAXED);
	atomic_store_zu(&extents->npages, cur_extents_npages + npages,
					ATOMIC_RELAXED);
}

static void
extent_deactivate_locked(tcache_t *tcache, arena_t *arena, extents_t *extents,
						 extent_t *extent)
{
	assert(extent_arena_get(extent) == arena);
	assert(extent_state_get(extent) == extent_state_active);

	extent_state_set(extent, extents_state_get(extents));
	extents_insert_locked(tcache, extents, extent);
}

void extent_deactivate(tcache_t *tcache, arena_t *arena, extents_t *extents,
					   extent_t *extent)
{
	pthread_mutex_lock(&extents->mtx);
	extent_deactivate_locked(tcache, arena, extents, extent);
	pthread_mutex_unlock(&extents->mtx);
}


static extent_t *
extent_recycle_split(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
					 vslab_t *vslab, size_t npages, bool slab,
					 szind_t szind, extent_t *extent, bool growing_retained)
{
	extent_t *lead;
	extent_t *trail;
	extent_t *to_leak;
	extent_t *to_salvage;

	extent_split_interior_result_t result = extent_split_interior(
		tcache, arena, rtree_ctx, &extent, &lead, &trail,
		&to_leak, &to_salvage, vslab, npages, slab, szind,
		growing_retained);

	if (result == extent_split_interior_ok)
	{
		if (lead != NULL)
		{
			extent_deactivate(tcache, arena, extents, lead);
		}
		if (trail != NULL)
		{
			extent_deactivate(tcache, arena, extents, trail);
		}
		return extent;
	}
	else
	{

		assert(0);
	}
	not_reached();
}
static void extent_replace_to_vslab(tcache_t *tcache, extent_t *extent, vslab_t *vslab)
{
	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *elm_a = NULL, *elm_b = NULL;


	if (extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, extent, false, false,
									  &elm_a, &elm_b))
	{
		assert(0);
	}
	szind_t szind = extent_szind_get(extent);
	bool slab = extent_slab_get(extent);
	assert(slab == true);

	extent_rtree_write_acquired(elm_a, elm_b, (extent_t *)vslab, szind, slab);
}

static void
extent_interior_register(tcache_t *tcache, rtree_ctx_t *rtree_ctx, extent_t *extent,
						 szind_t szind, vslab_t *vslab)
{
	assert(extent_slab_get(extent));

	for (size_t i = 1; i < (extent_size_get(extent) >> LG_PAGE) - 1; i++)
	{
		rtree_write(&extents_rtree, rtree_ctx,
					(uintptr_t)extent_addr_get(extent) + (uintptr_t)(i << LG_PAGE), (extent_t *)vslab, szind, true);
	}
	extent_replace_to_vslab(tcache, extent, vslab);
}


extent_t *
extent_recycle(tcache_t *tcache, arena_t *arena, extents_t *extents, vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit,
			   bool growing_retained)
{

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;

	extent_t *extent = extent_recycle_extract(tcache, arena,
											  rtree_ctx, extents, vslab, npages, slab,
											  growing_retained);
	if (extent == NULL)
	{
		return NULL;
	}

	extent = extent_recycle_split(tcache, arena, rtree_ctx,
								  extents, vslab, npages, slab, szind, extent,
								  growing_retained);
	if (extent == NULL)
	{
		return NULL;
	}

	assert(extent_state_get(extent) == extent_state_active);
	if (slab)
	{
		extent_slab_set(extent, slab);
		extent_interior_register(tcache, rtree_ctx, extent, szind, vslab);
	}

	return extent;
}



static void extent_replace_vslab(tcache_t *tcache, extent_t *extent)
{
	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *elm_a = NULL, *elm_b = NULL;



	if (extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, extent, false, false,
									  &elm_a, &elm_b))
	{
		assert(0);
	}
	szind_t szind = extent_szind_get(extent);
	bool slab = extent_slab_get(extent);
	assert(slab == true);

	extent_rtree_write_acquired(elm_a, elm_b, extent, szind, slab);
}


static bool
extent_register_impl(tcache_t *tcache, extent_t *extent, bool gdump_add, vslab_t *vslab)
{

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *elm_a, *elm_b;


	extent_lock(extent);

	if (extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, extent, false, true,
									  &elm_a, &elm_b))
	{
		extent_unlock(extent);
		return true;
	}

	szind_t szind = extent_szind_get(extent);
	bool slab = extent_slab_get(extent);
	if (slab)
	{
		assert(extent->state == extent_state_active);
		extent_rtree_write_acquired(elm_a, elm_b, (extent_t *)vslab, szind, slab);
	}
	else
	{
		extent_rtree_write_acquired(elm_a, elm_b, extent, szind, slab);
	}

	if (slab)
	{
		assert(extent->state == extent_state_active);
		extent_interior_register(tcache, rtree_ctx, extent, szind, vslab);
	}

	extent_unlock(extent);

	return false;
}

static bool
extent_register(tcache_t *tcache, extent_t *extent, vslab_t *vslab)
{
	assert(((extent->slab == false) && (vslab == NULL)) || ((extent->slab == true) && (vslab != NULL)));
	return extent_register_impl(tcache, extent, true, vslab);
}

extent_t *
extent_alloc_with_old_addr(tcache_t *tcache, arena_t *arena,
						   void *new_addr, size_t npages, bool slab, szind_t szind, bool *commit)
{
	size_t esize = npages * PAGE_SIZE;
	extent_t *extent = extent_alloc(tcache, arena);

	assert(new_addr != NULL);
	if (extent == NULL)
	{
		return NULL;
	}

	extent_init(extent, arena, new_addr, esize, slab, szind, extent_state_active);

	if (extent_register(tcache, extent, NULL))
	{
		extent_dalloc(arena, extent);
		return NULL;
	}

	return extent;
}

static extent_t *
extent_alloc_wrapper_hard(tcache_t *tcache, arena_t *arena,
						  vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit)
{
	size_t esize = npages * PAGE_SIZE;
	extent_t *extent = extent_alloc(tcache, arena);
	if (extent == NULL)
	{
		return NULL;
	}
	void *addr;

	addr = arena_activate_new_memory(arena, tcache, npages);

	if (addr == NULL)
	{
		extent_dalloc(arena, extent);
		return NULL;
	}
	extent_init(extent, arena, addr, esize, slab, szind, extent_state_active);

	if (extent_register(tcache, extent, (vslab_t *)vslab))
	{
		extent_dalloc(arena, extent);
		return NULL;
	}

	return extent;
}

extent_t *
extent_alloc_wrapper(tcache_t *tcache, arena_t *arena,
					 vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit)
{

	extent_t *extent = extent_alloc_wrapper_hard(tcache, arena,
												 vslab, npages, slab, szind, commit);

	return extent;
}


static void
extent_interior_deregister(tcache_t *tcache, rtree_ctx_t *rtree_ctx,
						   extent_t *extent)
{
	size_t i;

	assert(extent_slab_get(extent));

	for (i = 1; i < (extent_size_get(extent) >> LG_PAGE) - 1; i++)
	{
		rtree_clear(&extents_rtree, rtree_ctx,
					(uintptr_t)extent_addr_get(extent) + (uintptr_t)(i << LG_PAGE));
	}
	extent_replace_vslab(tcache, extent);
}

typedef enum
{
	lock_result_success,
	lock_result_failure,
	lock_result_no_extent
} lock_result_t;

static lock_result_t
extent_rtree_leaf_elm_try_lock(tcache_t *tsdn, rtree_leaf_elm_t *elm,
							   extent_t **result, bool inactive_only)
{
	extent_t *extent1 = rtree_leaf_elm_extent_read(&extents_rtree,
												   elm, true);


	if (extent1 == NULL || (inactive_only && rtree_leaf_elm_slab_read(
												 &extents_rtree, elm, true)))
	{
		return lock_result_no_extent;
	}

	extent_lock(extent1);
	extent_t *extent2 = rtree_leaf_elm_extent_read(
		&extents_rtree, elm, true);

	if (extent1 == extent2)
	{
		*result = extent1;
		return lock_result_success;
	}
	else
	{
		extent_unlock(extent1);
		return lock_result_failure;
	}
}


static extent_t *
extent_lock_from_addr(tcache_t *tcache, rtree_ctx_t *rtree_ctx, void *addr,
					  bool inactive_only)
{
	extent_t *ret = NULL;
	rtree_leaf_elm_t *elm = rtree_leaf_elm_lookup(&extents_rtree,
												  rtree_ctx, (uintptr_t)addr, false, false);
	if (elm == NULL)
	{
		return NULL;
	}
	lock_result_t lock_result;
	do
	{
		lock_result = extent_rtree_leaf_elm_try_lock(tcache, elm, &ret,
													 inactive_only);
	} while (lock_result == lock_result_failure);
	return ret;
}

static bool
extent_can_coalesce(arena_t *arena, extents_t *extents, const extent_t *inner,
					const extent_t *outer)
{
	assert(extent_arena_get(inner) == arena);
	if (extent_arena_get(outer) != arena)
	{
		return false;
	}

	assert(extent_state_get(inner) == extent_state_active);
	if (extent_state_get(outer) != extents->state)
	{
		return false;
	}

	return true;
}

static bool
extent_merge_impl(tcache_t *tcache, arena_t *arena, extent_t *a, extent_t *b)
{

	assert(extent_addr_get(a) < extent_addr_get(b));


	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;
	rtree_leaf_elm_t *a_elm_a, *a_elm_b, *b_elm_a, *b_elm_b;
	extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, a, true, false, &a_elm_a,
								  &a_elm_b);
	extent_rtree_leaf_elms_lookup(tcache, rtree_ctx, b, true, false, &b_elm_a,
								  &b_elm_b);

	extent_lock2(a, b);

	if (a_elm_b != NULL)
	{
		rtree_leaf_elm_write(&extents_rtree, a_elm_b, NULL,
							 MAX_PSZ_IDX, false);
	}
	if (b_elm_b != NULL)
	{
		rtree_leaf_elm_write(&extents_rtree, b_elm_a, NULL,
							 MAX_PSZ_IDX, false);
	}
	else
	{
		b_elm_b = b_elm_a;
	}

	extent_size_set(a, extent_size_get(a) + extent_size_get(b));
	extent_szind_set(a, MAX_PSZ_IDX);

	extent_rtree_write_acquired(a_elm_a, b_elm_b, a, MAX_PSZ_IDX,
								false);

	extent_unlock2(a, b);

	extent_dalloc(extent_arena_get(b), b);

	return false;
}

static bool
extent_coalesce(tcache_t *tcache, arena_t *arena,
				extents_t *extents, extent_t *inner, extent_t *outer, bool forward)
{
	assert(extent_can_coalesce(arena, extents, inner, outer));

	extent_activate_locked(tcache, arena, extents, outer);

	pthread_mutex_unlock(&extents->mtx);
	bool err = extent_merge_impl(tcache, arena,
								 forward ? inner : outer, forward ? outer : inner);
	pthread_mutex_lock(&extents->mtx);

	if (err)
	{
		extent_deactivate_locked(tcache, arena, extents, outer);
	}

	return err;
}

static extent_t *
extent_try_coalesce_impl(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
						 extent_t *extent, bool *coalesced, bool inactive_only)
{

	bool again;
	do
	{
		again = false;

		extent_t *next = extent_lock_from_addr(tcache, rtree_ctx,
											   extent_past_get(extent), inactive_only);
		if (next != NULL)
		{

			bool can_coalesce = extent_can_coalesce(arena, extents,
													extent, next);

			extent_unlock(next);

			if (can_coalesce && !extent_coalesce(tcache, arena,
												 extents, extent, next, true))
			{
				if (extents->delay_coalesce)
				{

					*coalesced = true;
					return extent;
				}
				again = true;
			}
		}


		extent_t *prev = extent_lock_from_addr(tcache, rtree_ctx,
											   extent_before_get(extent), inactive_only);
		if (prev != NULL)
		{
			bool can_coalesce = extent_can_coalesce(arena, extents,
													extent, prev);
			extent_unlock(prev);

			if (can_coalesce && !extent_coalesce(tcache, arena, extents, extent, prev, false))
			{
				extent = prev;
				if (extents->delay_coalesce)
				{

					*coalesced = true;
					return extent;
				}
				again = true;
			}
		}
	} while (again);

	if (extents->delay_coalesce)
	{
		*coalesced = false;
	}
	return extent;
}

static extent_t *
extent_try_coalesce_large(tcache_t *tcache, arena_t *arena, rtree_ctx_t *rtree_ctx, extents_t *extents,
						  extent_t *extent, bool *coalesced)
{
	return extent_try_coalesce_impl(tcache, arena, rtree_ctx,
									extents, extent, coalesced, true);
}


static void
extent_record(tcache_t *tcache, arena_t *arena,
			  extents_t *extents, extent_t *extent)
{

	rtree_ctx_t *rtree_ctx = &tcache->extents_rtree_ctx;

	pthread_mutex_lock(&extents->mtx);

	extent_szind_set(extent, MAX_PSZ_IDX);
	if (extent_slab_get(extent))
	{
		assert(extents->state = extent_state_dirty);
		extent_interior_deregister(tcache, rtree_ctx, extent);
		extent_slab_set(extent, false);
	}

	assert(rtree_extent_read(&extents_rtree, rtree_ctx,
							 (uintptr_t)extent_addr_get(extent), true) == extent);

	if (!extents->delay_coalesce)
	{
		extent = extent_try_coalesce(tcache, arena,
									 rtree_ctx, extents, extent, NULL);
	}
	else 
	{
		assert(extents == &arena->extents_dirty);

		bool coalesced;
		do
		{
			assert(extent_state_get(extent) == extent_state_active);
			extent = extent_try_coalesce_large(tcache, arena, rtree_ctx, extents, extent,
											   &coalesced);
		} while (coalesced);

	}
	extent_deactivate_locked(tcache, arena, extents, extent);

	pthread_mutex_unlock(&extents->mtx);
}

void extents_dalloc(tcache_t *tcache, arena_t *arena,
					extents_t *extents, extent_t *extent)
{
	assert(extent_addr_get(extent) != NULL);
	assert(extent_size_get(extent) != 0);

	if (extents->state == extent_state_muzzy)
	{
		assert(extents == &arena->extents_muzzy);
		assert(extent->slab == false);

		void *caddr = ALIGNMENT_ADDR2BASE(extent->e_addr, CHUNK_SIZE);
		size_t coff = ALIGNMENT_ADDR2OFFSET(extent->e_addr, CHUNK_SIZE);
		file_t key;
		key.addr = caddr;

		file_t *file = file_tree_search(&arena->file_tree, &key);
		assert(file != NULL);

		fallocate(file->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, coff, extent->size);
	}

	extent_record(tcache, arena, extents, extent);
}

extent_t *
extents_alloc(tcache_t *tcache, arena_t *arena,
			  extents_t *extents, vslab_t *vslab, size_t npages, bool slab, szind_t szind, bool *commit)
{
	assert(npages != 0);

	extent_t *extent = extent_recycle(tcache, arena, extents,
									  vslab, npages, slab, szind, commit, false);

	return extent;
}
