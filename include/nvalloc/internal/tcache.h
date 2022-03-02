
#ifdef NVALLOC_H_TYPES

typedef struct tcache_s tcache_t;

typedef struct cache_s cache_t;

typedef struct bin_s bin_t;

typedef struct cache_entry_s cache_entry_t;

typedef struct large_cache_s large_cache_t;

#define NUM_MAX_CACHE 32

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

struct cache_entry_s
{
    void *ret;
    metamap_t *metas;
    unsigned index;
#ifdef SLAB_MORPHING
    dmeta_t *dmeta;
    bool is_sb;
#endif
};

struct cache_s
{
    int ncached[NBANKS];
    cache_entry_t avail[NBANKS][NUM_MAX_CACHE];
    uint64_t bm;
    int now;
};

struct large_cache_s
{
    int ncached;
    extent_t *avail[NUM_MAX_CACHE];
};

struct bin_s
{
    dl_t *vslabs;
    dlnode_t *now_vslab;
    pthread_mutex_t bin_lock;
};
struct tcache_s
{
    cache_t caches[MAX_SZ_IDX];
    rtree_ctx_t extents_rtree_ctx;
    large_cache_t large_caches[MAX_PSZ_IDX];

    ticker_t arena_tickers[0];
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

void tcache_boot();

tcache_t *tcache_get();

tcache_t *tcache_create();

void tcache_fill_cache(arena_t *arena, tcache_t *tcache, cache_t *cache, bin_t *bin, size_t sc_idx);

void *tcache_alloc_slab(tcache_t *tcache);

void tcache_thread_cleanup(void *arg);

int cache_bitmap_findnxt(uint64_t bm, int now);

uint32_t cache_bitmap_flip(uint32_t bm, int pos);

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

malloc_tsd_protos(, tcache, tcache_t *)

#if defined(NVALLOC_TCACHE_C_)
    malloc_tsd_externs(tcache, tcache_t *)
        malloc_tsd_funcs(inline, tcache, tcache_t *, NULL, tcache_thread_cleanup)
#endif

            static inline void *tcache_alloc_large(tcache_t *tcache, size_t npages, void **ptr)

{
    void *ret;
    arena_t *arena = NULL;
    bool tcache_success = false;

    szind_t szind = get_psizeclass(npages);

    large_cache_t *cache = &tcache->large_caches[szind];

    if (cache->ncached == 0)
    {
        arena = choose_arena(arena);
        size_t np = get_psizeclass_by_idx(szind)->npages;
        int i = 0;
        while (i < NUM_MAX_CACHE)
        {
            cache->avail[i] = arena_extent_alloc_large(tcache, arena, np);
            i++;
        }
        cache->ncached = NUM_MAX_CACHE;
    }
    cache->ncached--;
    extent_t *extent = cache->avail[cache->ncached];
    tcache_success = true;
    ret = extent_addr_get(extent);

    add_minilog(minilog, &global_index, (uint64_t)ptr);

    *ptr = ret;
    _mm_clwb(ptr);
    _mm_mfence();

    arena = extent->arena;
    pthread_mutex_lock(&arena->log_lock);
    extent->log = add_log(arena,&arena->log, NORMAL_LOG, (uint64_t)ret, (uint64_t)extent_size_get(extent), false);
    pthread_mutex_unlock(&arena->log_lock);

    if (unlikely(!tcache_success))
    {
        assert(0);

        arena = choose_arena(arena);
        if (unlikely(arena == NULL))
        {
            return NULL;
        }

        ret = arena_large_alloc(tcache, arena, npages, ptr);

        if (ret == NULL)
        {
            return NULL;
        }
    }
    else
    {
    }

    return ret;
}

#endif /* NVALLOC_H_INLINES */
       /******************************************************************************/