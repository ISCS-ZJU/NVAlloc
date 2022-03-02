#define NVALLOC_ARENA_C_
#include "nvalloc/internal/nvalloc_internal.h"

/******************************************************************************/
/* Data. */

const char PMEMPATH[50] = "/mnt/pmem/nvalloc_files/nvalloc_files_";

#define DIRTY_DECAY_MS_DEFAULT ZD(10 * 1000)
#define MUZZY_DECAY_MS_DEFAULT ZD(100 * 1000)

ssize_t opt_dirty_decay_ms = DIRTY_DECAY_MS_DEFAULT;
ssize_t opt_muzzy_decay_ms = MUZZY_DECAY_MS_DEFAULT;

malloc_tsd_data(, arenas, arena_t *, NULL);
static atomic_zd_t dirty_decay_ms_default;
static atomic_zd_t muzzy_decay_ms_default;

const uint64_t h_steps[SMOOTHSTEP_NSTEPS] = {
#define STEP(step, h, x, y) \
	h,
	SMOOTHSTEP
#undef STEP
};
/******************************************************************************/
/* Function prototypes for non-inline static functions. */

static inline int file_cmp(void *a, void *b)
{
	int ret;

	intptr_t az = (intptr_t)((file_t *)a)->addr;
	intptr_t bz = (intptr_t)((file_t *)b)->addr;
	ret = (az < bz) - (az > bz);

	return ret;

} 

rb_gen(, file_tree_, file_tree_t, file_t, file_link, file_cmp);

/******************************************************************************/
/* Inline tool function */

/******************************************************************************/

bool arena_dirty_decay_ms_default_set(ssize_t decay_ms)
{

	atomic_store_zd(&dirty_decay_ms_default, decay_ms, ATOMIC_RELAXED);
	return false;
}
ssize_t
arena_muzzy_decay_ms_default_get(void)
{
	return atomic_load_zd(&muzzy_decay_ms_default, ATOMIC_RELAXED);
}

bool arena_muzzy_decay_ms_default_set(ssize_t decay_ms)
{

	atomic_store_zd(&muzzy_decay_ms_default, decay_ms, ATOMIC_RELAXED);
	return false;
}

void arena_boot(void)
{
	arena_dirty_decay_ms_default_set(opt_dirty_decay_ms);
	arena_muzzy_decay_ms_default_set(opt_muzzy_decay_ms);
}

static void
arena_decay_ms_write(arena_decay_t *decay, ssize_t decay_ms)
{
	atomic_store_zd(&decay->time_ms, decay_ms, ATOMIC_RELAXED);
}
static ssize_t
arena_decay_ms_read(arena_decay_t *decay)
{
	return atomic_load_zd(&decay->time_ms, ATOMIC_RELAXED);
}

static void
arena_decay_deadline_init(arena_decay_t *decay)
{

	nstime_copy(&decay->deadline, &decay->epoch);
	nstime_add(&decay->deadline, &decay->interval);
	if (arena_decay_ms_read(decay) > 0)
	{
		nstime_t jitter;

		nstime_init(&jitter, prng_range_u64(&decay->jitter_state,
											nstime_ns(&decay->interval)));
		nstime_add(&decay->deadline, &jitter);
	}
}

static void
arena_decay_reinit(arena_decay_t *decay, ssize_t decay_ms)
{
	arena_decay_ms_write(decay, decay_ms);
	if (decay_ms > 0)
	{
		nstime_init(&decay->interval, (uint64_t)decay_ms *
										  KQU(1000000));
		nstime_idivide(&decay->interval, SMOOTHSTEP_NSTEPS);
	}

	nstime_init(&decay->epoch, 0);
	nstime_update(&decay->epoch);
	decay->jitter_state = (uint64_t)(uintptr_t)decay;
	arena_decay_deadline_init(decay);
	decay->nunpurged = 0;
	memset(decay->backlog, 0, SMOOTHSTEP_NSTEPS * sizeof(size_t));
}

static bool
arena_decay_init(arena_decay_t *decay, ssize_t decay_ms)
{

	if (pthread_mutex_init(&decay->mtx, 0))
	{
		return true;
	}
	decay->purging = false;
	arena_decay_reinit(decay, decay_ms);

	return false;
}

static int arena_evict_new_memory_locked(arena_t *arena, tcache_t *tcache)
{
	assert(arena->memory_new != NULL);

	if (arena->npage_new == 0 || arena->npage_new != get_psizeclass_by_idx(get_psizeclass(arena->npage_new))->npages)
	{
		arena->npage_new = 0;
		arena->memory_new = NULL;
		return 0;
	}

	extent_t *extent = extent_alloc_with_old_addr(tcache, arena, arena->memory_new, arena->npage_new, false, get_psizeclass(arena->npage_new), false);

	extent_deactivate(tcache, arena, &arena->extents_dirty, extent);

	arena->memory_new = NULL;
	arena->npage_new = 0;

	return 0;
}

int arena_evict_new_memory(arena_t *arena, tcache_t *tcache)
{
	pthread_mutex_lock(&arena->lock);
	int err = arena_evict_new_memory_locked(arena, tcache);
	pthread_mutex_unlock(&arena->lock);
	return err;
}

void arena_file_unmap_locked(extent_t *extent, arena_t *arena, const char *path)
{
	assert(((uintptr_t)extent->e_addr) % CHUNK_SIZE == 0);
	assert(extent->size % CHUNK_SIZE == 0 && extent->size != 0);
	assert(extent->state == extent_state_active);
	assert(extent->szind == MAX_PSZ_IDX);

	int nunmap = extent->size / CHUNK_SIZE;
	for (int i = 0; i < nunmap; i++)
	{
		uintptr_t addr = ((uintptr_t)extent->e_addr) + i * CHUNK_SIZE;
		file_t key;
		key.addr = (void *)addr;

		file_t *file = file_tree_search(&arena->file_tree, &key);
		assert(file != NULL);
		assert((uintptr_t)file->addr == addr);
		int fd = file->fd;
		file_tree_remove(&arena->file_tree, file);
		munmap((void *)addr, CHUNK_SIZE);
		close(fd);
		char str[100];

		get_filepath(arena->ind, str, file->id, path);
		remove(str);
		pthread_mutex_lock(&arena->log_lock);
		add_log(arena, &arena->log, FREE_LOG, (uint64_t)file->log, (uint64_t)NULL, false);
		pthread_mutex_unlock(&arena->log_lock);
		_free(file);
	}
}
void arena_file_unmap(extent_t *extent, arena_t *arena, const char *path)
{
	pthread_mutex_lock(&arena->lock);
	arena_file_unmap_locked(extent, arena, path);
	pthread_mutex_unlock(&arena->lock);
}

static void *arena_file_map_locked(arena_t *arena, size_t size, const char *path, size_t align)
{

	char str[100];

	get_filepath(arena->ind, str, arena->file_id, path);

	int fd = open(str, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd == -1)
	{
		printf("open file error %s\n", str);
		exit(1);
	}

	int err = posix_fallocate(fd, 0, size);

	if (err == -1)
	{
		printf("ftruncate file error\n");
		exit(1);
	}

	void *addr = mmap(0, size, PROT_READ | PROT_WRITE, 0x80003, fd, 0);

	if (pmem_is_pmem(addr, size)) 
	{
		printf("not pmem\n");
		exit(1);
	}
	if (addr == -1)
	{
		printf("errno = %d\n",errno);
		printf("mmap new memory fail\n");
		abort();
	}

	assert((uintptr_t)addr == roundUp((uintptr_t)addr, align));
	if ((uintptr_t)addr != roundUp((uintptr_t)addr, align)) 
	{
		munmap(addr, size);
		int err = ftruncate(fd, size + align);
		if (err == -1)
		{
			printf("ftruncate file error\n");
			exit(1);
		}

		addr = mmap(0, size + align, PROT_READ | PROT_WRITE, 0x80003, fd, 0);

		if (pmem_is_pmem(addr, size)) 
		{
			printf("not pmem\n");
			exit(1);
		}
		void *tmp = addr;
		addr = (void *)roundUp((uintptr_t)addr, align);
		munmap(addr, ((intptr_t)addr - (intptr_t)tmp));
		if ((intptr_t)tmp + align - (intptr_t)addr != 0)
			munmap((void *)((intptr_t)addr + size), ((intptr_t)tmp + align - (intptr_t)addr));
	}

	file_t *file = _malloc(sizeof(file_t));
	file->fd = fd;
	file->addr = addr;
	file->id = arena->file_id;
	assert(file->fd != -1 && (intptr_t)file->addr != -1 && (uintptr_t)file->addr == roundUp((uintptr_t)file->addr, align));
	pthread_mutex_lock(&arena->log_lock);
	file->log = add_log(arena, &arena->log, FILE_LOG, (uint64_t)file->addr, (uint64_t)file->id, false);
	pthread_mutex_unlock(&arena->log_lock);
	file_tree_insert(&arena->file_tree, file);
	arena->file_id++;
	return addr;
}

int arena_get_new_memory_locked(arena_t *arena, size_t npages)
{
	if (!(arena->memory_new == NULL && arena->npage_new == 0))
	{
		printf("memory_new=%p npage_new=%ld\n", arena->memory_new, arena->npage_new);
	}
	assert(arena->memory_new == NULL && arena->npage_new == 0);
	arena->memory_new = arena_file_map_locked(arena, CHUNK_SIZE, PMEMPATH, CHUNK_SIZE);
	if (arena->memory_new == NULL)
	{
		return 1;
	}
	arena->npage_new = CHUNK_SIZE / PAGE_SIZE;
	return 0;
}

int arena_get_new_memory(arena_t *arena, size_t npages)
{
	pthread_mutex_lock(&arena->lock);
	int err = arena_get_new_memory_locked(arena, npages);
	pthread_mutex_unlock(&arena->lock);
	return err;
}

ssize_t
arena_dirty_decay_ms_default_get(void)
{
	return atomic_load_zd(&dirty_decay_ms_default, ATOMIC_RELAXED);
}

void *arena_activate_new_memory(arena_t *arena, tcache_t *tcache, size_t npages)
{
	void *ret;
	pthread_mutex_lock(&arena->lock);
	assert(npages > 0);
	if (npages > arena->npage_new)
	{
		if (arena_evict_new_memory_locked(arena, tcache))
			return NULL;
		if (arena_get_new_memory_locked(arena, npages))
			return NULL;
	}
	assert(arena->memory_new != NULL);
	ret = arena->memory_new;
	arena->memory_new = (void *)(((uintptr_t)arena->memory_new) + npages * PAGE_SIZE);
	arena->npage_new -= npages;
	assert(arena->npage_new >= 0);

	pthread_mutex_unlock(&arena->lock);
	return ret;
}

bool arena_new(arena_t *arena, unsigned ind)
{

	arena->ind = ind;
	arena->nthreads = 0;

	pthread_mutex_init(&arena->lock, 0);
	pthread_mutex_init(&arena->log_lock, 0);

	arena->log = log_create(arena->ind);

	rb_new(file_t, file_link, &arena->file_tree);

	arena->file_id = 0;
	if (extents_init(&arena->extents_dirty, extent_state_dirty,
					 true))
	{
		return true;
	}

	arena->memory_new = NULL;
	arena->npage_new = 0;
	if (arena_get_new_memory(arena, CHUNK_SIZE / PAGE_SIZE))
	{
		return true;
	}

	if (extents_init(&arena->extents_muzzy, extent_state_muzzy,
					 false))
	{
		return true;
	}

	if (arena_decay_init(&arena->decay_dirty, arena_dirty_decay_ms_default_get()))
	{
		return true;
	}

	if (arena_decay_init(&arena->decay_muzzy, arena_muzzy_decay_ms_default_get()))
	{
		return true;
	}

	for (int i = 0; i < MAX_SZ_IDX; i++)
	{
		arena->bins[i].vslabs = dl_new();
		arena->bins[i].now_vslab = arena->bins[i].vslabs->head;
		pthread_mutex_init(&arena->bins[i].bin_lock, 0);
	}

#ifdef SLAB_MORPHING

	arena->timestamp = 0;
	arena->LRU_vslabs = dl_new(); 
	pthread_mutex_init(&arena->LRU_lock, 0);
	arena->LRU_len = 0;		
	arena->morphing_len = 0; 
#endif

	return false;
}

extent_t *
arena_extent_alloc_large(tcache_t *tcache, arena_t *arena, size_t npages)
{
	szind_t szind = get_psizeclass(npages);
	assert(npages == get_psizeclass_by_idx(szind)->npages);


	bool commit = true;
	extent_t *extent = extent_recycle(tcache, arena,
									  &arena->extents_dirty, NULL, npages, false,
									  szind, &commit, false);
	if (extent == NULL)
	{
		extent = extent_recycle(tcache, arena,
								&arena->extents_muzzy, NULL, npages,
								false, szind, &commit, false);
	}
	if (extent == NULL)
	{
		extent = extent_alloc_wrapper(tcache, arena, NULL,
									  npages, false, szind, &commit);
	}
	arena_decay_ticks(tcache, arena, 1);

	return extent;
}

void *arena_large_alloc(tcache_t *tcache, arena_t *arena, size_t npages, void **ptr)

{
	extent_t *extent;

	if (unlikely(arena == NULL) || (extent = arena_extent_alloc_large(tcache,
																	  arena, npages)) == NULL)
	{
		return NULL;
	}

	add_minilog(minilog, &global_index, ptr);
 	*ptr = extent_addr_get(extent);


	pthread_mutex_lock(&arena->log_lock);
	extent->log = add_log(arena, &arena->log, NORMAL_LOG, (uint64_t)extent_addr_get(extent), (uint64_t)extent_size_get(extent), false);

	pthread_mutex_unlock(&arena->log_lock);
	return extent_addr_get(extent);
}

void arena_extents_dirty_dalloc(tcache_t *tcache, arena_t *arena, extent_t *extent)
{

	extents_dalloc(tcache, arena, &arena->extents_dirty, extent);
	arena_decay_ticks(tcache, arena, 1);
}

void arena_dalloc(tcache_t *tcache, void *ptr,
				  szind_t szind, bool slab)
{

	assert(ptr != NULL);

	extent_t *extent = rtree_extent_read(&extents_rtree, &tcache->extents_rtree_ctx, (uintptr_t)ptr, true);
	arena_t *arena = extent->arena;
	arena_extents_dirty_dalloc(tcache, extent->arena, extent);
	pthread_mutex_lock(&arena->log_lock);
	add_log(arena, &arena->log, FREE_LOG, (uint64_t)extent->log, (uint64_t)NULL, false);
	pthread_mutex_unlock(&arena->log_lock);
}

static size_t
arena_stash_decayed(tcache_t *tcache, arena_t *arena, extents_t *extents, size_t npages_limit,
					size_t npages_decay_max, extent_list_t *decay_extents)
{


	size_t nstashed = 0;
	extent_t *extent;
	while (nstashed < npages_decay_max &&
		   (extent = extents_evict(tcache, arena, extents, npages_limit)) != NULL)
	{
		extent_list_append(decay_extents, extent);
		nstashed += extent_size_get(extent) >> LG_PAGE;
	}
	return nstashed;
}

static size_t
arena_decay_stashed(tcache_t *tcache, arena_t *arena,
					arena_decay_t *decay, extents_t *extents,
					extent_list_t *decay_extents)
{


	size_t npurged;

	npurged = 0;
	for (extent_t *extent = extent_list_first(decay_extents); extent !=
															  NULL;
		 extent = extent_list_first(decay_extents))
	{

		size_t npages = extent_size_get(extent) >> LG_PAGE;
		npurged += npages;
		extent_list_remove(decay_extents, extent);
		switch (extents_state_get(extents))
		{
		case extent_state_active:
			not_reached();
		case extent_state_dirty:
			extents_dalloc(tcache, arena,
						   &arena->extents_muzzy, extent);
			break;

		case extent_state_muzzy:
			printf("do muzzy purge!\n");
			extent_dalloc_wrapper(tcache, arena, extent);

		default:
			not_reached();
		}
	}


	return npurged;
}


static void
arena_decay_to_limit(tcache_t *tcache, arena_t *arena, arena_decay_t *decay,
					 extents_t *extents, size_t npages_limit, size_t npages_decay_max)
{

	if (decay->purging)
	{
		return;
	}
	decay->purging = true;
	pthread_mutex_unlock(&decay->mtx);

	extent_list_t decay_extents;
	extent_list_init(&decay_extents);

	size_t npurge = arena_stash_decayed(tcache, arena, extents,
										npages_limit, npages_decay_max, &decay_extents);
	if (npurge != 0)
	{
		size_t npurged = arena_decay_stashed(tcache, arena,
											 decay, extents, &decay_extents);
		assert(npurged == npurge);
	}

	pthread_mutex_lock(&decay->mtx);
	decay->purging = false;
}

size_t
extents_npages_get(extents_t *extents)
{
	return atomic_load_zu(&extents->npages, ATOMIC_RELAXED);
}
static bool
arena_decay_deadline_reached(const arena_decay_t *decay, const nstime_t *time)
{
	return (nstime_compare(&decay->deadline, time) <= 0);
}

static void
arena_decay_backlog_update_last(arena_decay_t *decay, size_t current_npages)
{
	size_t npages_delta = (current_npages > decay->nunpurged) ? current_npages - decay->nunpurged : 0;
	decay->backlog[SMOOTHSTEP_NSTEPS - 1] = npages_delta;
}

static void
arena_decay_backlog_update(arena_decay_t *decay, uint64_t nadvance_u64,
						   size_t current_npages)
{
	if (nadvance_u64 >= SMOOTHSTEP_NSTEPS)
	{
		memset(decay->backlog, 0, (SMOOTHSTEP_NSTEPS - 1) * sizeof(size_t));
	}
	else
	{
		size_t nadvance_z = (size_t)nadvance_u64;

		assert((uint64_t)nadvance_z == nadvance_u64);

		memmove(decay->backlog, &decay->backlog[nadvance_z],
				(SMOOTHSTEP_NSTEPS - nadvance_z) * sizeof(size_t));
		if (nadvance_z > 1)
		{
			memset(&decay->backlog[SMOOTHSTEP_NSTEPS -
								   nadvance_z],
				   0, (nadvance_z - 1) * sizeof(size_t));
		}
	}

	arena_decay_backlog_update_last(decay, current_npages);
}
static void
arena_decay_epoch_advance_helper(arena_decay_t *decay, const nstime_t *time,
								 size_t current_npages)
{
	assert(arena_decay_deadline_reached(decay, time));

	nstime_t delta;
	nstime_copy(&delta, time);
	nstime_subtract(&delta, &decay->epoch);

	uint64_t nadvance_u64 = nstime_divide(&delta, &decay->interval);
	assert(nadvance_u64 > 0);


	nstime_copy(&delta, &decay->interval);
	nstime_imultiply(&delta, nadvance_u64);
	nstime_add(&decay->epoch, &delta);


	arena_decay_deadline_init(decay);


	arena_decay_backlog_update(decay, nadvance_u64, current_npages);
}

static size_t
arena_decay_backlog_npages_limit(const arena_decay_t *decay)
{
	uint64_t sum;
	size_t npages_limit_backlog;
	unsigned i;


	sum = 0;
	for (i = 0; i < SMOOTHSTEP_NSTEPS; i++)
	{
		sum += decay->backlog[i] * h_steps[i];
	}
	npages_limit_backlog = (size_t)(sum >> SMOOTHSTEP_BFP);

	return npages_limit_backlog;
}

static void
arena_decay_try_purge(tcache_t *tcache, arena_t *arena, arena_decay_t *decay,
					  extents_t *extents, size_t current_npages, size_t npages_limit)
{
	if (current_npages > npages_limit)
	{
		arena_decay_to_limit(tcache, arena, decay, extents,
							 npages_limit, current_npages - npages_limit);
	}
}

static void
arena_decay_epoch_advance(tcache_t *tcache, arena_t *arena, arena_decay_t *decay,
						  extents_t *extents, const nstime_t *time)
{
	size_t current_npages = extents_npages_get(extents);
	arena_decay_epoch_advance_helper(decay, time, current_npages);

	size_t npages_limit = arena_decay_backlog_npages_limit(decay);

	decay->nunpurged = (npages_limit > current_npages) ? npages_limit : current_npages;

	arena_decay_try_purge(tcache, arena, decay, extents,
						  current_npages, npages_limit);
}
static bool
arena_maybe_decay(tcache_t *tcache, arena_t *arena, arena_decay_t *decay,
				  extents_t *extents)
{

	ssize_t decay_ms = arena_decay_ms_read(decay);
	if (decay_ms <= 0)
	{
		if (decay_ms == 0)
		{
			arena_decay_to_limit(tcache, arena, decay, extents,
								 0, extents_npages_get(extents));
		}
		return false;
	}

	nstime_t time;
	nstime_init(&time, 0);
	nstime_update(&time);
	if (unlikely(!nstime_monotonic() && nstime_compare(&decay->epoch, &time) > 0))
	{

		nstime_copy(&decay->epoch, &time);
		arena_decay_deadline_init(decay);
	}
	else
	{

		assert(nstime_compare(&decay->epoch, &time) <= 0);
	}


	bool advance_epoch = arena_decay_deadline_reached(decay, &time);
	if (advance_epoch)
	{
		arena_decay_epoch_advance(tcache, arena, decay, extents, &time);
	}

	return advance_epoch;
}

static bool
arena_decay_impl(tcache_t *tcache, arena_t *arena, arena_decay_t *decay,
				 extents_t *extents)
{
	if (pthread_mutex_trylock(&decay->mtx))
	{
		return true;
	}

	arena_maybe_decay(tcache, arena, decay, extents);

	pthread_mutex_unlock(&decay->mtx);

	return false;
}

static bool
arena_decay_dirty(tcache_t *tcache, arena_t *arena)
{
	return arena_decay_impl(tcache, arena, &arena->decay_dirty,
							&arena->extents_dirty);
}

static bool
arena_decay_muzzy(tcache_t *tcache, arena_t *arena)
{
	return arena_decay_impl(tcache, arena, &arena->decay_muzzy,
							&arena->extents_muzzy);
}

void arena_decay(tcache_t *tcache, arena_t *arena)
{
	if (arena_decay_dirty(tcache, arena))
	{
		return;
	}
	arena_decay_muzzy(tcache, arena);
}

static extent_t *
arena_slab_alloc_hard(tcache_t *tcache, size_t size, arena_t *arena,
						 szind_t szind, vslab_t *vslab)
{
	extent_t *slab;
	bool commit;

	commit = true;
	slab = extent_alloc_wrapper(tcache, arena, vslab,
								size / PAGE_SIZE, true, szind, &commit);

	return slab;
}

extent_t *
arena_slab_alloc(tcache_t *tcache, arena_t *arena, size_t size, vslab_t *vslab)
{

	bool commit;
	assert(size % PAGE_SIZE == 0);
	assert(get_psizeclass_by_idx(get_psizeclass(size / PAGE_SIZE))->npages == size / PAGE_SIZE);
	extent_t *slab = extents_alloc(tcache, arena,
								   &arena->extents_dirty, vslab, size / PAGE_SIZE, true,
								   SLAB_SCIND, &commit);
	if (slab == NULL)
	{
		slab = extents_alloc(tcache, arena,
							 &arena->extents_muzzy, vslab, size / PAGE_SIZE, true,
							 SLAB_SCIND, &commit);
	}
	if (slab == NULL)
	{
		slab = arena_slab_alloc_hard(tcache, size, arena, SLAB_SCIND, vslab);
		if (slab == NULL)
		{
			return NULL;
		}
	}
	assert(extent_slab_get(slab));
	assert(extent_szind_get(slab) == SLAB_SCIND);
	assert(extent_size_get(slab) == size);
	assert(extent_addr_get(slab) != NULL);
	pthread_mutex_lock(&arena->log_lock);
	slab->log = add_log(arena,&arena->log, NORMAL_LOG, (uint64_t)extent_addr_get(slab), (uint64_t)extent_size_get(slab), true);

	pthread_mutex_unlock(&arena->log_lock);
	return slab;
}

void arena_slab_dalloc(tcache_t *tcache, arena_t *arena, extent_t *slab)
{
	assert(slab->state == extent_state_active);
	arena_extents_dirty_dalloc(tcache, arena, slab);
	pthread_mutex_lock(&arena->log_lock);
	add_log(arena, &arena->log, FREE_LOG, (uint64_t)slab->log, (uint64_t)NULL, true);
	pthread_mutex_unlock(&arena->log_lock);
}
