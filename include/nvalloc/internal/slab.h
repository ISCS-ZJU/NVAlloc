#ifdef NVALLOC_H_TYPES
typedef struct slab_s slab_t;
typedef struct vslab_s vslab_t;
typedef struct dmeta_s dmeta_t;
typedef struct sb_table_s sb_table_t;

#define SLAB_SIZE_LG 16
#define SLAB_SIZE (1ULL << SLAB_SIZE_LG)

#define META_FREE 0
#define META_ALLOCED 1

#ifdef SLAB_MORPHING

#define MEMORY_MORPH_THRESHOLOD 0.2
#define TIMESTAMP_MORPH_THRESHOLD 1000

#define LOW_SLAB_THRESHOLOD MEMORY_MORPH_THRESHOLOD

#define MORHPING_LIST_SEARCH_THRESHOLD 0.1
#endif

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

#ifdef SLAB_MORPHING

struct dmeta_s
{
    uint16_t state : 1;
    uint16_t num_b : 15;
};

struct sb_table_s
{
    uint16_t bid_b : 14;
    uint16_t is_freed : 2; 
};
#endif

struct slab_s
{
    sizeclass_t sc;
#ifdef SLAB_MORPHING
    sizeclass_t sc_b;
    uint8_t is_mixed;
#endif
};

struct vslab_s
{
    extent_t *eslab;
    slab_t *slab;
    bitmap_header_t bitmap_header;
    bitmap_t *bitmap;
    dlnode_t *dlnode;
    sizeclass_t sc;
    pthread_mutex_t slab_lock;

#ifdef SLAB_MORPHING
    dmeta_t *dmeta;
    uint64_t timestamp;
    dlnode_t *LRU_dlnode;
    uint8_t num_b;
    sizeclass_t sc_b;
#endif
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

vslab_t *slab_create(tcache_t *tcache, arena_t *arena, bin_t *bin, dl_t *dl, size_t sc_idx);

void slab_init(slab_t *slab, size_t sc_idx);

void vslab_init(vslab_t *vslab, size_t sc_idx);

int slab_pop_one_cache(arena_t *arena, cache_t *cache, vslab_t *vslab, bin_t *bin, size_t sc_idx);

void slab_free_small(arena_t *free_arena, tcache_t *free_tcache, vslab_t *vslab, void *ptr, void **pptr);

#ifdef SLAB_MORPHING
bool slab_morphing(arena_t *arena, tcache_t *tcache, vslab_t *vslab, size_t sc_idx);
#endif

vslab_t *slab_get_one(tcache_t *tcache, arena_t *arena, bin_t *bin, size_t sc_idx);

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

static inline void meta_set(metamap_t *meta, unsigned index)
{

    __sync_fetch_and_or(meta, (metamap_t)(1 << index));
    return;
}

static inline void meta_unset(metamap_t *meta, unsigned index)
{

    metamap_t tmp = ~(1ULL << index);
    __sync_fetch_and_and(meta, tmp);
    return;
}

#ifdef SLAB_MORPHING
static inline void dmeta_set_state(dmeta_t *dmeta, int state)
{
    dmeta->state = state;
    return;
}

static inline int dmeta_get_state(dmeta_t *dmeta)
{
    return dmeta->state;
}
#endif

//interleaved mapping
static inline int bid_to_mid(int bid)
{

    int n = 512;
    int nn = n * NBANKS;
    int i = bid % nn;
    return (bid / nn) * nn + (bid % NBANKS) * n + i / NBANKS;
}

#ifdef SLAB_MORPHING
static inline void update_arena_LRU_morphing_slab(arena_t *arena, size_t num_a, size_t num_b, size_t num, vslab_t *vslab)
{
    double threshold = 1.0 * num * LOW_SLAB_THRESHOLOD + 0.00001;
    if (num_a <= threshold && num_b > threshold)
    {

        arena->morphing_len--;
    }
    else if (num_a > threshold && num_b <= threshold)
    {

        arena->morphing_len++;
    }
    assert(arena->morphing_len <= arena->LRU_len);
}
#endif

#endif /* NVALLOC_H_INLINES */
/******************************************************************************/
