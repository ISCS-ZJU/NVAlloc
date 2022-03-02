#define NVALLOC_TCACHE_C_
#include "nvalloc/internal/nvalloc_internal.h"

/******************************************************************************/
/* Data. */
malloc_tsd_data(, tcache, tcache_t *, NULL);

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

/******************************************************************************/
/* Inline tool function */

/******************************************************************************/


void tcache_thread_cleanup(void *arg)
{
    return;
}

void tcache_boot()
{
    tcache_tsd_boot();
}

tcache_t *tcache_create()
{

    tcache_t *tcache = _malloc(sizeof(tcache_t) + opt_narenas * sizeof(ticker_t));
    for (int i = 0; i < MAX_SZ_IDX; i++)
    {
        for (int j = 0; j < NBANKS; j++)
        {
            tcache->caches[i].ncached[j] = 0;
        }
        tcache->caches[i].bm = 0;
        tcache->caches[i].now = NBANKS - 1; 

        for (int i = 0; i < opt_narenas; i++)
        {
            ticker_init(&tcache->arena_tickers[i], 1000);
        }
    }

    for (int i = 0; i < MAX_PSZ_IDX; i++)
    {
        tcache->large_caches[i].ncached = 0;
    }


    rtree_ctx_data_init(&tcache->extents_rtree_ctx);

    tcache_tsd_set(&tcache);
    return tcache;
}

tcache_t *tcache_get()
{
    tcache_t *tcache = *tcache_tsd_get();
    if (tcache == 0)
    {
        tcache = tcache_create();
    }

    return tcache;
}




void tcache_fill_cache(arena_t *arena, tcache_t *tcache, cache_t *cache, bin_t *bin, size_t sc_idx)
{
#ifdef SLAB_MORPHING
    __sync_fetch_and_add(&arena->timestamp, 1);
#endif


    while (1) 
    {
        dlnode_t *head = bin->vslabs->head;

        if (bin->now_vslab == head)
        {
            bin->now_vslab = head->nxt;
        }
        dlnode_t *dln = bin->now_vslab;

        if (dln == head)
            dln = dln->nxt;
        if (dln != head)
        {
            while (true)
            {
                vslab_t *vslab = dln->data;
                assert(vslab);
                assert(&vslab->slab_lock);
                pthread_mutex_lock(&vslab->slab_lock);

                int ret = slab_pop_one_cache(arena, cache, dln->data, bin, sc_idx);
                while (ret == 1) 
                {
                    ret = slab_pop_one_cache(arena, cache, dln->data, bin, sc_idx);
                }

                pthread_mutex_unlock(&vslab->slab_lock);

                if (ret == 0)
                {
                    bin->now_vslab = dln;
                    goto end_;
                }

                dln = dln->nxt;

                if (dln == head)
                    dln = dln->nxt;
                if (dln == bin->now_vslab)
                    break;
            }
        }
        vslab_t *vs = slab_get_one(tcache, arena, bin, sc_idx);
        bin->now_vslab = vs->dlnode;
    }
end_:;
}


int cache_bitmap_findnxt(uint64_t bm, int now)
{
    uint64_t tmp = bm >> (now + 1);
    if (tmp != 0) 
    {
        return __builtin_ffsl(tmp) + now; 
    }
    else 
    {
        return __builtin_ctzl(bm);
    }
}


uint32_t cache_bitmap_flip(uint32_t bm, int pos)
{
    return bm ^ (1U << pos);
}




