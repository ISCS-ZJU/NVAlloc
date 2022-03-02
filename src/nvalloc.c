
#define NVALLOC_C_
#include "nvalloc/internal/nvalloc_internal.h"

/******************************************************************************/
/* Data. */

int32_t *logs;
char *path = "/mnt/pmem/nvalloc_files/";
struct stat sb;

size_t opt_narenas = 0;

unsigned ncpus;

pthread_mutex_t arenas_lock;
arena_t **arenas;
unsigned narenas_total;
unsigned narenas_auto;

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

/******************************************************************************/
/* Inline tool function */

/******************************************************************************/
arena_t *
arenas_extend(unsigned ind)
{
    arena_t *ret;

    ret = (arena_t *)_malloc(sizeof(arena_t));
    if (ret != NULL && arena_new(ret, ind) == false)
    {
        arenas[ind] = ret;
        return (ret);
    }

    return (arenas[0]);
}

static unsigned
malloc_ncpus(void)
{
    long result;

    result = sysconf(_SC_NPROCESSORS_ONLN);

    return ((result == -1) ? 1 : (unsigned)result);
}

void arenas_cleanup(void *arg)
{
    arena_t *arena = *(arena_t **)arg;

    pthread_mutex_lock(&arenas_lock);
    arena->nthreads--;
    pthread_mutex_unlock(&arenas_lock);
}

static inline bool is_small_size(size_t size, size_t *idx)
{
    if (size > MAX_SZ)
    {
        return false;
    }
    else
    {
        *idx = get_sizeclass_id_by_size(size);

        if (unlikely(*idx == 32 || *idx == 36 || *idx == 38))
        {

            return false;
        }
    }
    return true;
}

void *nvalloc_malloc_to(size_t size, void **ptr)
{
    size = ALIGNMENT_CEILING(size, CACHE_LINE_SIZE);
    tcache_t *tcache = tcache_get();
    size_t sz_idx;
    if (is_small_size(size, &sz_idx))
    {
        cache_t *cache = &tcache->caches[sz_idx];
        void *ret;

#ifdef SLAB_MORPHING
        while (true)
        {
#endif
            if (unlikely(cache->bm == 0))
            {
                arena_t *arena = choose_arena(NULL);
                bin_t *bin = &arena->bins[sz_idx];
                assert(&bin->bin_lock);
                pthread_mutex_lock(&bin->bin_lock);
                tcache_fill_cache(arena, tcache, cache, bin, sz_idx);
                pthread_mutex_unlock(&bin->bin_lock);
            }

            int now = cache_bitmap_findnxt(cache->bm, cache->now);

#ifdef SLAB_MORPHING
            while (cache->ncached[now] >= 0 && cache->avail[now][cache->ncached[now]].is_sb)
            {
                cache->ncached[now]--;
            }

            if (cache->ncached[now] == -1)
            {
                cache->ncached[now] = 0;
                cache->bm = cache_bitmap_flip(cache->bm, now);
                continue;
            }
            else
            {
#endif // SLAB_MORPHING
                cache->ncached[now]--;
                add_minilog(minilog, &global_index, ptr);

                meta_set(cache->avail[now][cache->ncached[now]].metas, cache->avail[now][cache->ncached[now]].index);
#ifdef SLAB_MORPHING
                dmeta_set_state(cache->avail[now][cache->ncached[now]].dmeta, META_ALLOCED);
#endif
                _mm_clwb(cache->avail[now][cache->ncached[now]].metas);
                _mm_sfence();

                if (cache->ncached[now] == 0)
                {
                    cache->bm = cache_bitmap_flip(cache->bm, now);
                }
                cache->now = now;
                ret = cache->avail[now][cache->ncached[now]].ret;
#ifdef SLAB_MORPHING
                break;
            }
        } 
#endif    // SLAB_MORPHING

        *ptr = ret;
        _mm_clwb(ptr);
        _mm_sfence();
        return ret;
    }
    else
    {

        size_t npages = size % PAGE_SIZE == 0 ? size / PAGE_SIZE : size / PAGE_SIZE + 1;

        return tcache_alloc_large(tcache, npages, ptr);
    }

    return NULL;
}

void nvalloc_free_from(void **pptr)

{

    void *ptr = *pptr;
    tcache_t *tcache = tcache_get();
    szind_t szind = MAX_PSZ_IDX;
    bool slab = false;
    rtree_szind_slab_read(&extents_rtree, &tcache->extents_rtree_ctx,
                          (uintptr_t)ptr, true, &szind, &slab);

    assert(szind != MAX_PSZ_IDX);
    if (slab)
    {
        assert(szind == SLAB_SCIND);
        vslab_t *vslab = rtree_vslab_read(tcache, &extents_rtree, (uintptr_t)ptr);
        arena_t *arena = NULL;
        arena = choose_arena(arena);
        assert(arena != NULL);

        slab_free_small(arena, tcache, vslab, ptr, pptr);
    }
    else
    {
        //We use the same write-ahead log(WAL) strategy as nvm_malloc. It doesn't use WAL in deallocating process. 
        // add_minilog(minilog, &global_index, pptr);

        *pptr = NULL;
        _mm_clwb(pptr);
        _mm_sfence();

        assert(szind < MAX_PSZ_IDX);

        large_cache_t *cache = &tcache->large_caches[szind];

        if (cache->ncached == NUM_MAX_CACHE)
        {
            arena_dalloc(tcache, ptr, szind, slab);
        }
        else
        {

            extent_t *extent = rtree_extent_read(&extents_rtree, &tcache->extents_rtree_ctx, (uintptr_t)ptr, true);
            arena_t *arena = extent->arena;

            pthread_mutex_lock(&arena->log_lock);
            add_log(arena, &arena->log, FREE_LOG, (uint64_t)extent->log, (uint64_t)NULL, false);
            pthread_mutex_unlock(&arena->log_lock);
            cache->avail[cache->ncached] = extent;
            cache->ncached++;
        }
        return;
    }
}

arena_t *
choose_arena_hard(void)
{
    arena_t *ret;

    if (narenas_auto > 1)
    {
        unsigned i, choose, first_null;

        choose = 0;
        first_null = narenas_auto;
        pthread_mutex_lock(&arenas_lock);
        assert(arenas[0] != NULL);
        for (i = 1; i < narenas_auto; i++)
        {
            if (arenas[i] != NULL)
            {

                if (arenas[i]->nthreads <
                    arenas[choose]->nthreads)
                    choose = i;
            }
            else if (first_null == narenas_auto)
            {

                first_null = i;
            }
        }

        if (arenas[choose]->nthreads == 0 || first_null == narenas_auto)
        {
            ret = arenas[choose];
        }
        else
        {
            ret = arenas_extend(first_null);
        }
        ret->nthreads++;
        pthread_mutex_unlock(&arenas_lock);
    }
    else
    {
        ret = arenas[0];
        pthread_mutex_lock(&arenas_lock);
        ret->nthreads++;
        pthread_mutex_unlock(&arenas_lock);
    }

    arenas_tsd_set(&ret);

    return (ret);
}

int nvalloc_init()
{
    ncpus = malloc_ncpus();
    if (opt_narenas == 0)
    {
        if (ncpus > 1)
            opt_narenas = ncpus << 2;
        else
            opt_narenas = 1;
    }

    minilog = minilog_create();
    global_index = 0;

    tcache_boot(opt_narenas);
    sizeclass_boot();

    if (extent_boot())
    {
        return true;
    }

    if (rtree_new(&extents_rtree, true))
    {
        exit(1);
    }

    arena_t *init_arenas[1];
    arena_boot();

    narenas_total = narenas_auto = 1;
    arenas = init_arenas;
    memset(arenas, 0, sizeof(arena_t *) * narenas_auto);
    arenas_extend(0);

    narenas_auto = opt_narenas;
    narenas_total = narenas_auto;

    arenas = (arena_t **)_malloc(sizeof(arena_t *) * narenas_total);
    if (arenas == NULL)
    {
        assert(false);
    }

    memset(arenas, 0, sizeof(arena_t *) * narenas_total);
    arenas[0] = init_arenas[0];

    return 1;
}

uint64_t nvget_memory_usage()
{
    uint64_t usage = 0;
    struct stat statbuf;
    char fname[100];
    for (int k = 0; k < narenas_total; k++)
    {
        arena_t *arena = arenas[k];
        if (arena == NULL)
            break;
        for (int i = 0; i < arena->file_id; i++)
        {
            get_filepath(arena->ind, fname, i, PMEMPATH);
            if (stat(fname, &statbuf) == -1)
            {
                printf("%s stat error! errno = %d\n\n", fname, errno);
                exit(1);
            }
            usage += (uint64_t)statbuf.st_blocks * 512;
        }
    }
    return usage;
}
