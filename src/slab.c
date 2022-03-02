#define NVALLOC_SLAB_C_
#include "nvalloc/internal/nvalloc_internal.h"

/******************************************************************************/
/* Data. */
uint32_t meta_size = 0;

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

/******************************************************************************/
/* Inline tool function */



/******************************************************************************/

vslab_t *slab_create(tcache_t *tcache, arena_t *arena, bin_t *bin, dl_t *dl, size_t sc_idx)
{
    vslab_t *vslab = (vslab_t *)_malloc(sizeof(vslab_t));
    extent_t *extent = arena_slab_alloc(tcache, arena, SLAB_SIZE + 16 * 1024, vslab);
    slab_t *slab = (slab_t *)(((uintptr_t)extent_addr_get(extent)) + (rand() % 2) * PAGE_SIZE); //Because we use two DIMMs, we want to distribute the headers equally to them by random. Only visual memory space is wasted.
    vslab->eslab = extent;                                                                      
    vslab->slab = slab;                                                                         
    slab_init(slab, sc_idx);
    vslab_init(vslab, sc_idx);
#ifdef SLAB_MORPHING
    pthread_mutex_lock(&arena->LRU_lock);
    vslab->LRU_dlnode = dl_insert_tail(arena->LRU_vslabs, vslab);
    arena->LRU_len++;
    arena->morphing_len++;

    assert(vslab->LRU_dlnode != NULL);
    pthread_mutex_unlock(&arena->LRU_lock);
#endif
    vslab->dlnode = dl_insert(dl, vslab); 

    return vslab;
}

void slab_init(slab_t *slab, size_t sc_idx)
{
    sizeclass_t sc = get_sizeclass_by_idx(sc_idx);
    memset((void *)((intptr_t)slab + sc.moffset), 0, sc.roffset - sc.moffset);
    slab->sc = sc;

#ifdef SLAB_MORPHING
    slab->is_mixed = 0; 
#endif
}

void vslab_init(vslab_t *vslab, size_t sc_idx)
{
    sizeclass_t sc = get_sizeclass_by_idx(sc_idx);
    vslab->bitmap_header.cur = 0; 
    vslab->bitmap_header.nbits = 0;
    vslab->bitmap = (bitmap_t *)_malloc(sc.n_bitmaps * sizeof(bitmap_t));
    memset(vslab->bitmap, 0xffU, sc.n_bitmaps * sizeof(bitmap_t));

    vslab->sc = sc;
    pthread_mutex_init(&vslab->slab_lock, 0);

#ifdef SLAB_MORPHING
    vslab->dmeta = (dmeta_t *)_malloc((sc.nbits + 1) * sizeof(dmeta_t));
    memset(vslab->dmeta, 0, (sc.nbits + 1) * sizeof(dmeta_t));
    vslab->timestamp = 0;
    vslab->num_b = 0;
#endif
}

vslab_t *slab_get_one(tcache_t *tcache, arena_t *arena, bin_t *bin, size_t sc_idx) 
{
    dl_t *vslabs = bin->vslabs;

#ifdef SLAB_MORPHING
    if (arena->morphing_len > 1.0 * arena->LRU_len * MORHPING_LIST_SEARCH_THRESHOLD)
    {
        dlnode_t *head = arena->LRU_vslabs->head;
        uint64_t timestamp = arena->timestamp;
        size_t bitsize = get_sizeclass_by_idx(sc_idx).bitsize;
        pthread_mutex_lock(&arena->LRU_lock);
        dlnode_t *node = head->nxt;
        while (node != head)
        {
            vslab_t *vslab = (vslab_t *)node->data;
            if ((timestamp - vslab->timestamp) > TIMESTAMP_MORPH_THRESHOLD) 
            {
                if (vslab->bitmap_header.nbits <= 1.0 * MEMORY_MORPH_THRESHOLOD * vslab->sc.nbits)
                {
                    if (vslab->sc.bitsize != bitsize)
                    {
                        pthread_mutex_lock(&vslab->slab_lock);
                        if (slab_morphing(arena, tcache, vslab, sc_idx)) 
                        {
                            pthread_mutex_unlock(&vslab->slab_lock);
                            pthread_mutex_unlock(&arena->LRU_lock);
                            return vslab;
                        }
                        else
                        {
                            pthread_mutex_unlock(&vslab->slab_lock);
                        }
                    }
                }
            }
            else
                break;
            node = node->nxt;
        }
        pthread_mutex_unlock(&arena->LRU_lock);
    }
#endif

    return slab_create(tcache, arena, bin, vslabs, sc_idx);
}

void dl_delete_vslab(arena_t *arena, vslab_t *vslab, int sc_idx)
{
    if (arena->bins[sc_idx].now_vslab == vslab->dlnode)
    {
        arena->bins[sc_idx].now_vslab = vslab->dlnode->nxt;
    }
    dl_delete(vslab->dlnode);
}

#ifdef SLAB_MORPHING
static inline bool
slab_unset_bm_tcache(tcache_t *tcache, int bid, slab_t *slab,
                     vslab_t *vslab, sizeclass_t sc, bitmap_t *bitmap, bool checked[], size_t sc_idx)
{
    int cid = bid % NBANKS;
    if (checked[cid])
    {
        return false; 
    }
    if (unlikely(dmeta_get_state(&vslab->dmeta[bid]) == META_FREE)) 
    {
        uintptr_t slab_begin = (uintptr_t)slab;
        uintptr_t slab_end = slab_begin + SLAB_SIZE;
        cache_t *cache = &tcache->caches[sc_idx];
        int ncached = cache->ncached[cid];
        for (int i = 0; i < ncached; i++)
        {
            cache_entry_t *entry = &(cache->avail[cid][i]);
            uintptr_t addr = (uintptr_t)entry->ret;
            if (addr >= slab_begin && addr < slab_end && !entry->is_sb)
            {
                entry->is_sb = true;
                int id = (addr - (slab_begin + sc.roffset)) / sc.bitsize;
                bitmap_unset_bits(bitmap, id);
                vslab->bitmap_header.nbits--;
            }
        }
        checked[cid] = true;
        return true; 
    }
    return false; 
}

static inline int get_sb_table_size(int nbits)
{
    return roundUp((sizeof(sb_table_t) * nbits), CACHELINE_SIZE);
}


bool slab_morphing(arena_t *arena, tcache_t *tcache, vslab_t *vslab, size_t sc_idx) 
{
    sizeclass_t sc_b = vslab->sc;
    size_t sc_idx_b = get_sizeclass_id_by_size(sc_b.bitsize);

    //******************step1******************/
    slab_t *slab = vslab->slab;
    int nbm_b = sc_b.n_bitmaps;     
    bool checked[NBANKS] = {false}; 

    for (int i = 0; i < nbm_b; i++)
    {
        bitmap_t x = ~vslab->bitmap[i];
        if (x == 0)
            continue;
        while (x != 0)
        {
            int bid = i * 64 + __builtin_ffsl(x) - 1;
            assert(bid < sc_b.nbits);

            if (slab_unset_bm_tcache(tcache, bid, slab, vslab, sc_b, vslab->bitmap, checked, sc_idx) && vslab->bitmap_header.nbits == 0)
            {
                break;
            }
            x = x ^ (x & -x);
        }
    }

    /*******************step2*****************/
    if (vslab->bitmap_header.nbits == 0)
    {
        pthread_mutex_lock(&arena->bins[sc_idx_b].bin_lock); 
        dl_delete_vslab(arena, vslab, sc_idx_b);
        pthread_mutex_unlock(&arena->bins[sc_idx_b].bin_lock);

        slab_init(slab, sc_idx);
        vslab_init(vslab, sc_idx);

        vslab->dlnode = dl_insert(arena->bins[sc_idx].vslabs, vslab);

        return true;
    }

    //******************step3********************/
    sizeclass_t sc_tmp = get_sizeclass_by_idx(sc_idx);
    sizeclass_reset(&sc_tmp, get_sb_table_size(vslab->bitmap_header.nbits)); 

    int bid_b = -1;
    for (int i = 0; i < nbm_b; i++)
    {
        bitmap_t x = ~vslab->bitmap[i];
        if (x == 0)
            continue;
        bid_b = i * 64 + __builtin_ffsl(x) - 1;
        break;
    }
    assert(bid_b >= 0 && bid_b < sc_b.nbits);
    if (sc_tmp.roffset > sc_b.roffset + bid_b * sc_b.bitsize) 
    {
        return false;
    }

    //********************step4*********************/
    __sync_fetch_and_add(&slab->is_mixed, 1); 
    persist(&slab->is_mixed, sizeof(slab->is_mixed));

    slab->sc_b = sc_b;
    __sync_fetch_and_add(&slab->is_mixed, 1); 
    persist(&slab->sc_b, sizeof(slab->sc_b));
    persist(&slab->is_mixed, sizeof(slab->is_mixed));

    sizeclass_t sc_a = sc_tmp;
    vslab->sc = sc_a;
    vslab->sc_b = sc_b;
    vslab->num_b = vslab->bitmap_header.nbits; 
    vslab->bitmap_header.nbits = 0;
    slab->sc = sc_a;

    pthread_mutex_lock(&arena->bins[sc_idx_b].bin_lock); 
    dl_delete_vslab(arena, vslab, sc_idx_b);
    pthread_mutex_unlock(&arena->bins[sc_idx_b].bin_lock);

    vslab->dlnode = dl_insert(arena->bins[sc_idx].vslabs, vslab);

    dl_delete(vslab->LRU_dlnode); 
    vslab->LRU_dlnode = NULL;
    arena->LRU_len--;
    arena->morphing_len--;

    bitmap_t *bm_b = (bitmap_t *)_malloc(nbm_b * sizeof(bitmap_t)); 
    memcpy(bm_b, vslab->bitmap, nbm_b * sizeof(bitmap_t));
    int nbm_a = sc_a.n_bitmaps;
    _free(vslab->bitmap);
    vslab->bitmap = (bitmap_t *)_malloc(nbm_a * sizeof(bitmap_t));
    memset(vslab->bitmap, 0xffU, nbm_a * sizeof(bitmap_t));

    //**************step4.1******************/
    sb_table_t *sb_table = (sb_table_t *)((uintptr_t)slab + sc_a.toffset);
    int sb_table_num = 0;
    for (int i = 0; i < nbm_b; i++)
    {
        bitmap_t x = ~bm_b[i];
        while (x != 0)
        {
            int pos_b = __builtin_ffsl(x) - 1;
            int bid_b = i * 64 + pos_b;
            assert(bid_b < sc_b.nbits);

            sb_table[sb_table_num].is_freed = 0;
            sb_table[sb_table_num].bid_b = bid_b;
            sb_table_num++;

            x = x ^ (x & -x);
        }
    }

    assert(sb_table_num == vslab->num_b);

    if (sc_a.toffset + sizeof(sb_table_t) * (sb_table_num + 1) <= sc_a.roffset)
    {
        sb_table[sb_table_num].bid_b = 0;
    }

    __sync_fetch_and_add(&slab->is_mixed, 1); 
    persist(&slab->is_mixed, sizeof(slab->is_mixed));
    persist((void *)((uintptr_t)slab + sc_a.toffset), sc_a.roffset - sc_a.toffset);

    //***************step4.2*******************/
    memset((void *)((intptr_t)slab + sc_a.moffset), 0, sc_a.toffset - sc_a.moffset);
    _free(vslab->dmeta);
    vslab->dmeta = (dmeta_t *)_malloc((sc_a.nbits + 1) * sizeof(dmeta_t));
    memset(vslab->dmeta, 0, (sc_a.nbits + 1) * sizeof(dmeta_t));

    for (int i = 0; i < nbm_b; i++)
    {
        bitmap_t x = ~bm_b[i];
        while (x != 0)
        {
            int pos_b = __builtin_ffsl(x) - 1;
            int bid_b = i * 64 + pos_b;
            assert(bid_b < sc_b.nbits);

            uintptr_t addr = (uintptr_t)slab + sc_b.roffset + bid_b * sc_b.bitsize;
            int bid = (addr - ((uintptr_t)slab + sc_a.roffset)) / sc_a.bitsize; 

            while ((addr + sc_b.bitsize > (uintptr_t)slab + sc_a.roffset + bid * sc_a.bitsize))
            {
                assert(bid <= sc_a.nbits); 
                int mid = bid_to_mid(bid);
                int inmeta_index = mid % 8;
                metamap_t *meta = (metamap_t *)((uintptr_t)slab + sc_a.moffset + mid / 8);
                dmeta_t *dmeta = &(vslab->dmeta[bid]);
                assert((uintptr_t)meta + sizeof(metamap_t) <= (uintptr_t)slab + sc_a.roffset);
                if (dmeta->num_b == 0)
                {
                    bitmap_set_bits(vslab->bitmap, bid);
                    meta_set(meta, inmeta_index);
                    dmeta_set_state(dmeta, META_ALLOCED);
                    vslab->bitmap_header.nbits++;
                }
                dmeta->num_b += 1;
                bid++;
            }

            x = x ^ (x & -x);
        }
    }

    persist((void *)((uintptr_t)slab + sc_a.moffset), sc_a.toffset - sc_a.moffset);

    //*******************step5*********************/
    assert(vslab->bitmap_header.nbits <= vslab->sc.nbits + 1);
    _free(bm_b);

    __sync_fetch_and_add(&slab->is_mixed, 1);
    persist(&slab->is_mixed, sizeof(slab->is_mixed));

    return true;
}
#endif //SLAB_MORPHING

int slab_pop_one_cache(arena_t *arena, cache_t *cache, vslab_t *vslab, bin_t *bin, size_t sc_idx)
{
    sizeclass_t sc = vslab->sc;

#ifdef SLAB_MORPHING
    if (get_sizeclass_id_by_size(sc.bitsize) != sc_idx) 
    {
        return -1;
    }

    if (vslab->bitmap_header.nbits == sc.nbits + 1) 
    {
        return -1;
    }
#endif

    assert(vslab->bitmap_header.nbits <= sc.nbits);
    if (vslab->bitmap_header.nbits == sc.nbits) 
    {
        return -1;
    }

    int index = bitmap_sfu(vslab->sc.n_bitmaps, vslab->bitmap_header.nbits, vslab->bitmap, true); 
    assert(index < sc.nbits);
    slab_t *slab = vslab->slab;
    vslab->bitmap_header.nbits++;
#ifdef SLAB_MORPHING
    if (vslab->num_b == 0)
    {
        update_arena_LRU_morphing_slab(arena,
                                       vslab->bitmap_header.nbits - 1, vslab->bitmap_header.nbits, sc.nbits, vslab);
    }
#endif

    int mid = bid_to_mid(index);
    int cid = index % NBANKS; 

    int ncached = cache->ncached[cid];
    if (ncached == 0)
    {
        cache->bm = cache_bitmap_flip(cache->bm, cid);
    }

    cache_entry_t *entry = &(cache->avail[cid][ncached]);
    entry->index = mid % 8;
    entry->metas = (bitmap_t *)((intptr_t)slab + sc.moffset + mid / 8);
    entry->ret = (void *)((intptr_t)slab + sc.roffset + index * sc.bitsize);

#ifdef SLAB_MORPHING
    entry->dmeta = &vslab->dmeta[index];
    entry->is_sb = false;
#endif

    cache->ncached[cid] = ++ncached;

    if (ncached == NUM_MAX_CACHE) 
        return 0;

#ifdef SLAB_MORPHING
    if (vslab->num_b == 0)
    {
        dl_t *LRU = arena->LRU_vslabs;
        pthread_mutex_lock(&arena->LRU_lock);
        if (LRU->head->pre != vslab->LRU_dlnode)
        {
            assert(vslab->LRU_dlnode != NULL);
            dl_move_to_tail(LRU, vslab->LRU_dlnode);
        }
        pthread_mutex_unlock(&arena->LRU_lock);
        vslab->timestamp = arena->timestamp;
    }
#endif

    return 1;
}

void slab_free_small(arena_t *free_arena, tcache_t *free_tcache, vslab_t *vslab, void *ptr, void **pptr)

{
    bool is_cross_thread;
    arena_t *arena;
    tcache_t *tcache = free_tcache;
    extent_t *extent = vslab->eslab;
    if (extent->arena != free_arena)
    {
        is_cross_thread = true;
        arena = extent->arena;
    }
    else
    {
        is_cross_thread = false;
        arena = free_arena;
    }

    sizeclass_t sc = vslab->sc;
    int sc_idx = get_sizeclass_id_by_size(sc.bitsize);
#ifdef SLAB_MORPHING
    sizeclass_t sc_b = vslab->sc_b;
#endif
    slab_t *slab = vslab->slab;
    int index = (((uintptr_t)ptr - sc.roffset - (uintptr_t)slab)) / sc.bitsize; 
#ifdef SLAB_MORPHING
    assert(index <= vslab->sc.nbits);
#else
    assert(index < vslab->sc.nbits);
#endif
    metamap_t *meta_start = (metamap_t *)((uintptr_t)slab + sc.moffset);

    int mid = bid_to_mid(index);
    unsigned mindex = mid % 8;
    metamap_t *meta = (metamap_t *)((intptr_t)meta_start + mid / 8);

#ifdef SLAB_MORPHING
    dmeta_t *dmeta = &vslab->dmeta[index];
    if (vslab->num_b != 0 && dmeta->num_b != 0) 
    {
        pthread_mutex_lock(&vslab->slab_lock);

        vslab->num_b--;
        int bid_b = ((uintptr_t)ptr - ((uintptr_t)slab + sc_b.roffset)) / sc_b.bitsize;
        sb_table_t *table = (sb_table_t *)((uintptr_t)slab + sc.toffset);
        while (true)
        {
            if (!((uintptr_t)table < (uintptr_t)slab + sc.roffset))
                assert((uintptr_t)table < (uintptr_t)slab + sc.roffset);

            if (table->bid_b == bid_b)
            {
                table->is_freed += 1; 
                _mm_clwb(table);
                _mm_sfence();
                break;
            }
            table = (sb_table_t *)((uintptr_t)table + sizeof(sb_table_t));
        }

        int bid = index;
        while (((uintptr_t)ptr + sc_b.bitsize > (uintptr_t)slab + sc.roffset + bid * sc.bitsize))
        {
            int mid = bid_to_mid(bid);

            int inmeta_index = mid % 8;
            metamap_t *meta = (metamap_t *)((uintptr_t)slab + sc.moffset + mid / 8);

            dmeta_t *dmeta = &vslab->dmeta[bid];
            dmeta->num_b--;
            if (dmeta->num_b == 0) 
            {
                bitmap_unset_bits(vslab->bitmap, bid);
                meta_unset(meta, inmeta_index);
                _mm_clwb(meta);
                _mm_sfence();
                dmeta_set_state(dmeta, META_FREE);
                vslab->bitmap_header.nbits--;
            }
            bid++;
        }

        table->is_freed += 1; 
        _mm_clwb(table);
        _mm_sfence();

        if (vslab->bitmap_header.nbits == 0) 
        {
            pthread_mutex_unlock(&vslab->slab_lock);

            pthread_mutex_lock(&arena->bins[sc_idx].bin_lock);
            if (vslab->bitmap_header.nbits == 0)
                dl_delete_vslab(arena, vslab, sc_idx);
            pthread_mutex_unlock(&arena->bins[sc_idx].bin_lock);

            if (vslab->bitmap_header.nbits == 0)
            {
                arena_slab_dalloc(tcache, arena, vslab->eslab);
                _free(vslab->bitmap);
                _free(vslab->dmeta);
                _free(vslab);
            }
        }
        else if (vslab->num_b == 0) 
        {
            pthread_mutex_unlock(&vslab->slab_lock);

            vslab->timestamp = arena->timestamp; 

            pthread_mutex_lock(&arena->LRU_lock);
            vslab->LRU_dlnode = dl_insert_tail(arena->LRU_vslabs, vslab); 
            assert(vslab->LRU_dlnode != NULL);
            arena->LRU_len++;
            if (vslab->bitmap_header.nbits <= 1.0 * LOW_SLAB_THRESHOLOD * vslab->sc.nbits + 0.0001)
            {
                arena->morphing_len++;
            }
            pthread_mutex_unlock(&arena->LRU_lock);
        }
        else
        {
            pthread_mutex_unlock(&vslab->slab_lock);
        }
    }
    else 
#endif  
    {

        meta_unset(meta, mindex);
#ifdef SLAB_MORPHING
        dmeta_set_state(dmeta, META_FREE);
#endif
        _mm_clwb(meta);
        _mm_sfence();

        cache_t *cache = &tcache->caches[sc_idx];
        int cid = index % NBANKS;                                   
        if (!is_cross_thread && cache->ncached[cid] < NUM_MAX_CACHE) 
        {
            int ncached = cache->ncached[cid];
            if (ncached == 0)
            {
                cache->bm = cache_bitmap_flip(cache->bm, cid);
            }

            cache_entry_t *entry = &(cache->avail[cid][ncached]);
            cache->ncached[cid] = ++ncached;
            entry->index = mindex;
            entry->metas = meta;
            entry->ret = ptr;
#ifdef SLAB_MORPHING
            entry->dmeta = dmeta;
            entry->is_sb = false;
#endif //SLAB_MORPHING
        }
        else 
        {
            pthread_mutex_lock(&vslab->slab_lock);
            bitmap_unset_bits(vslab->bitmap, index);
            vslab->bitmap_header.nbits--;
#ifdef SLAB_MORPHING
            if (vslab->num_b == 0)
                update_arena_LRU_morphing_slab(arena, vslab->bitmap_header.nbits + 1, vslab->bitmap_header.nbits, sc.nbits, vslab);
#endif
            if (vslab->bitmap_header.nbits == 0) 
            {
                pthread_mutex_unlock(&vslab->slab_lock); 

                pthread_mutex_lock(&arena->bins[sc_idx].bin_lock);
                if (vslab->bitmap_header.nbits == 0)
                {
                    dl_delete_vslab(arena, vslab, sc_idx);
                }
                pthread_mutex_unlock(&arena->bins[sc_idx].bin_lock);

                if (vslab->bitmap_header.nbits == 0)
                {
#ifdef SLAB_MORPHING
                    pthread_mutex_lock(&arena->LRU_lock);
                    dl_delete(vslab->LRU_dlnode);
                    arena->LRU_len--;
                    arena->morphing_len--;

                    pthread_mutex_unlock(&arena->LRU_lock);
#endif //SLAB_MORPHING
                    arena_slab_dalloc(tcache, arena, vslab->eslab);
                    _free(vslab->bitmap);
                    _free(vslab);
                }
            }
            else
            {
                pthread_mutex_unlock(&vslab->slab_lock);
            }
        }
    }
    return;
}
