/******************************************************************************/
#ifdef NVALLOC_H_TYPES

typedef struct arena_s arena_t;
typedef struct arena_decay_s arena_decay_t;
typedef struct file_s file_t;

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

struct file_s
{
	int fd;
	int id;
	void *addr;
	void *log;
	rb_node(file_t) file_link;
};

struct arena_decay_s
{

	pthread_mutex_t mtx;
	bool purging;
	atomic_zd_t time_ms;
	nstime_t interval;
	nstime_t epoch;
	uint64_t jitter_state;
	nstime_t deadline;
	size_t nunpurged;
	size_t backlog[SMOOTHSTEP_NSTEPS];
};

typedef rb_tree(file_t) file_tree_t;
struct arena_s
{
	unsigned ind;
	unsigned nthreads;
	pthread_mutex_t lock;

	void *memory_new;
	size_t npage_new;

	extents_t extents_dirty;
	extents_t extents_muzzy;

	arena_decay_t decay_dirty; 
	arena_decay_t decay_muzzy; 

	file_tree_t file_tree;
	size_t file_id;
	pthread_mutex_t log_lock;
	vlog_t *log;

	bin_t bins[MAX_SZ_IDX]; 

#ifdef SLAB_MORPHING
	dl_t *LRU_vslabs;		  
	uint64_t timestamp;		  
	pthread_mutex_t LRU_lock; 
	uint64_t LRU_len; 	
	uint64_t morphing_len; 
#endif
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

void arena_boot(void);

bool arena_new(arena_t *arena, unsigned ind);

void *arena_large_alloc(tcache_t *tcache, arena_t *arena, size_t npages, void **ptr);

extent_t *arena_extent_alloc_large(tcache_t *tcache, arena_t *arena, size_t npages);

void arena_dalloc(tcache_t *tcache, void *ptr, szind_t szind, bool slab);

void arena_decay(tcache_t *tcache, arena_t *arena);
void *arena_activate_new_memory(arena_t *arena, tcache_t *tcache, size_t npages);
void arena_file_unmap(extent_t *extent, arena_t *arena, const char *path);

extent_t *arena_slab_alloc(tcache_t *tcache, arena_t *arena, size_t size, vslab_t *vslab);
void arena_slab_dalloc(tcache_t *tcache, arena_t *arena, extent_t *slab);

rb_proto(, file_tree_, file_tree_t, file_t)

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

	malloc_tsd_protos(, arenas, arena_t *)

#if defined(NVALLOC_ARENA_C_)
		malloc_tsd_externs(arenas, arena_t *)
			malloc_tsd_funcs(inline, arenas, arena_t *, NULL, NULL)
#endif

				static inline void arena_decay_ticks(tcache_t *tcache, arena_t *arena, unsigned nticks)
{

	ticker_t *decay_ticker;

	decay_ticker = &tcache->arena_tickers[arena->ind];
	if (unlikely(decay_ticker == NULL))
	{
		return;
	}
	if (unlikely(ticker_ticks(decay_ticker, nticks)))
	{
		arena_decay(tcache, arena);
	}
}

static inline void get_filepath(unsigned int arena_ind, char *str, int file_id, const char *path)
{
	sprintf(str, "%s%u_%d", path, arena_ind, file_id);
}

#endif /* NVALLOC_H_INLINES */
	   /******************************************************************************/