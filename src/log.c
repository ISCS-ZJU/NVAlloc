#include "nvalloc/internal/nvalloc_internal.h"

static inline int vlchunk_cmp(void *a, void *b)
{
    int ret;

    intptr_t az = (intptr_t)((vlchunk_t *)a)->lchunk;
    intptr_t bz = (intptr_t)((vlchunk_t *)b)->lchunk;
    ret = (az < bz) - (az > bz);

    return ret;
}

rb_gen(, vlchunk_tree_, vlchunk_tree_t, vlchunk_t, vlchunk_link, vlchunk_cmp);

static inline void set_bitmap(vlchunk_t *vlchunk, int index, bool set)
{
    assert(index >= 0 && index < N_ITEMS_PER_LCHUNK);
    int a = index / 64;
    int b = index % 64;
    if (set)
        vlchunk->bitmap[a] = (vlchunk->bitmap[a] | (1ULL << b));
    else
        vlchunk->bitmap[a] = (vlchunk->bitmap[a] & (~(1ULL << b)));
}

static inline bool bitmap_empty(vlchunk_t *vlchunk)
{
    for (int i = 0; i < BITMAP_ARRAY_SIZE; i++)
    {
        if (vlchunk->bitmap[i] != 0)
        {
            return false;
        }
    }
    return true;
}

static inline vlchunk_t *vlchunk_alloc(vlog_t *vlog)
{
    vlchunk_t *new_vlchunk = (vlchunk_t *)simple_list_get_first(vlog->global_free_list);
    lchunk_t *new_lchunk = NULL;

    if (new_vlchunk == NULL)
    {
        new_vlchunk = (vlchunk_t *)_malloc(sizeof(vlchunk_t));

        new_lchunk = (lchunk_t *)((uint64_t)vlog->log_file_addr + vlog->log_file_used);
        vlog->log_file_used += LCHUNK_SIZE;
        if (vlog->log_file_used >= LOG_FILE_SIZE)
        {
            printf("LOG FILE IS FULL!\n");
            exit(1);
        }
    }
    else
    {
        new_lchunk = new_vlchunk->lchunk;
    }
    memset(new_vlchunk, 0, sizeof(vlchunk_t));
    memset(new_lchunk, 0, LCHUNK_SIZE);
    new_lchunk->id = vlog->global_lchunk_id;
    new_vlchunk->id = vlog->global_lchunk_id;
    vlog->global_lchunk_id++;

    persist(new_lchunk, LCHUNK_SIZE);
    new_vlchunk->lchunk = new_lchunk;

    vlog->num_of_chunks++;

    return new_vlchunk;
}

void *log_file_create(size_t align, uint64_t arena_id)
{
    void *addr, *tmp;

    size_t mapped_len;
    char str[100];
    int is_pmem;
    sprintf(str, "%slog_%ld", "/mnt/pmem/nvalloc_files/nvalloc_files_", arena_id);

    if ((tmp = pmem_map_file(str, LOG_FILE_SIZE, PMEM_FILE_CREATE | PMEM_FILE_SPARSE, 0666, &mapped_len, &is_pmem)) == NULL)
    {
        printf("map file fail!log\n %d\n", errno);
        exit(1);
    }
    if (!is_pmem)
    {
        printf("is not nvm!\n");
        exit(1);
    }

    assert((uintptr_t)tmp == roundUp((uintptr_t)tmp, align));

    return tmp;
}

vlog_t *log_create(uint64_t areana_id)
{
    vlog_t *vlog = (vlog_t *)_malloc(sizeof(vlog_t));
    vlog->gc_timer = 0;

    vlog->log_file_addr = log_file_create(LCHUNK_SIZE, areana_id);
    vlog->log_file_used = LCHUNK_SIZE;
    log_file_head_t *log_file_head = (log_file_head_t *)vlog->log_file_addr;
    log_file_head->alt = 0;
    persist(&log_file_head->alt, sizeof(log_file_head->alt));
    log_t *log = &(log_file_head->log[0]);

    vlog->global_lchunk_id = 1;

    vlog->global_free_list = simple_list_create();

    vlog->num_of_chunks = 0;
    vlog->num_of_items = 0;

    vlchunk_t *root_vlchunk = vlchunk_alloc(vlog);
    vlchunk_t *first_vlchunk = vlchunk_alloc(vlog);
    first_vlchunk->pre_lchunk = root_vlchunk->lchunk;
    root_vlchunk->lchunk->next = first_vlchunk->lchunk;
    persist(root_vlchunk->lchunk, sizeof(lchunk_t));

    vlchunk_tree_new(&vlog->vlchunks);
    vlchunk_tree_insert(&vlog->vlchunks, first_vlchunk);
    vlog->tail_vlchunk = first_vlchunk;
    vlog->log = log;

    log->root_lchunk = root_vlchunk->lchunk;
    log->areana_id = areana_id;
    persist(log, sizeof(log_t));

    return vlog;
}

static inline void log_destroy(vlog_t *vlog)
{
    vlchunk_t *cursor_vlchunk = vlchunk_tree_first(&vlog->vlchunks);
    while (cursor_vlchunk)
    {
        simple_list_insert(vlog->global_free_list, cursor_vlchunk);
        cursor_vlchunk = vlchunk_tree_next(&vlog->vlchunks, cursor_vlchunk);
    }
    cursor_vlchunk = vlchunk_tree_first(&vlog->vlchunks);
    while (cursor_vlchunk)
    {
        vlchunk_tree_remove(&vlog->vlchunks, cursor_vlchunk);
        cursor_vlchunk = vlchunk_tree_first(&vlog->vlchunks);
    }
    vlchunk_t *temp_vlchunk = (vlchunk_t *)_malloc(sizeof(vlchunk_t));
    temp_vlchunk->lchunk = vlog->log->root_lchunk;
    simple_list_insert(vlog->global_free_list, temp_vlchunk);
    _free(vlog);
}

static inline int count_to_index(int bid)
{
    int n = CACHE_LINE_SIZE / sizeof(ITEM_SIZE);
    int nn = n * NBANKS;
    int i = bid % nn;
    return (bid / nn) * nn + (bid % NBANKS) * n + i / NBANKS;
}

void *add_log(arena_t *arena, vlog_t **vlog_ptr, uint8_t type, uint64_t ptr, uint64_t size_version_id, bool slab)
{
    vlog_t *vlog = *vlog_ptr;
    vlchunk_t *vlchunk = vlog->tail_vlchunk;
    int count = vlchunk->count;

    int index = count_to_index(count);
    if (index >= N_ITEMS_PER_LCHUNK)
    {

        vlchunk_t *new_vlchunk = vlchunk_alloc(vlog);
        new_vlchunk->pre_lchunk = vlchunk->lchunk;

        vlchunk->lchunk->next = new_vlchunk->lchunk;
        persist(&vlchunk->lchunk->next, sizeof(vlchunk->lchunk->next));

        vlchunk_tree_insert(&vlog->vlchunks, new_vlchunk);
        vlog->tail_vlchunk = new_vlchunk;

        vlchunk = new_vlchunk;
        index = count_to_index(new_vlchunk->count);
    }

    lchunk_t *lchunk = vlchunk->lchunk;
    log_item_t *item = &lchunk->items[index];
    item->type = type;
    item->ptr = ptr;
    item->size_version_id = slab ? size_version_id + 1 : size_version_id;

    if (item->type == FREE_LOG)
    {
        lchunk_t *lchunk = ptr2lchunk(item->ptr);
        int index = ptr2lindex(item->ptr, lchunk);

        vlchunk_t *temp_vlchunk = (vlchunk_t *)_malloc(sizeof(vlchunk_t));
        temp_vlchunk->lchunk = lchunk;

        vlchunk_t *vlchunk = vlchunk_tree_search(&vlog->vlchunks, temp_vlchunk);
        if (!vlchunk)
        {
            printf("&lchunk = %p,vlchunk= %p\n", lchunk, vlchunk);
            assert(vlchunk);
            exit(0);
        }

        set_bitmap(vlchunk, index, false);

        item->size_version_id = vlchunk->id;
    }
    else
    {
        vlog->num_of_items++;
    }

    persist(item, ITEM_SIZE);
    vlchunk->count++;
    set_bitmap(vlchunk, index, true);

    if (item->type == FREE_LOG)
    {
        vlog->gc_timer++;
        if (vlog->gc_timer == 10000)

        {
            vlog->gc_timer = 0;

            if (vlog->num_of_chunks * N_ITEMS_PER_LCHUNK / vlog->num_of_items >= 10)
            {
                slow_GC(vlog_ptr, arena);
            }
            else if (vlog->num_of_chunks * N_ITEMS_PER_LCHUNK / vlog->num_of_items >= 2)
            {
                fast_GC(vlog);
            }
        }
    }

    return item;
}

int get_heap_usage()
{
    char fname[100];
    struct stat statbuf;
    arena_t *arena = NULL;
    arena = choose_arena(arena);
    get_filepath(arena->ind, fname, arena->file_id, PMEMPATH);
    if (stat(fname, &statbuf) == -1)
    {
        printf("%s stat error! errno = %d\n\n", fname, errno);
        exit(1);
    }
    return (uint64_t)statbuf.st_blocks * 512;
}

static inline void *add_log_for_GC(vlog_t *vlog, log_item_t *item)
{

    vlchunk_t *vlchunk = vlog->tail_vlchunk;
    int count = vlchunk->count;

    int index = count_to_index(count);
    if (index >= N_ITEMS_PER_LCHUNK)
    {

        vlchunk_t *new_vlchunk = vlchunk_alloc(vlog);
        new_vlchunk->pre_lchunk = vlchunk->lchunk;

        vlchunk->lchunk->next = new_vlchunk->lchunk;
        persist(vlchunk->lchunk, LCHUNK_SIZE);

        vlchunk_tree_insert(&vlog->vlchunks, new_vlchunk);
        vlog->tail_vlchunk = new_vlchunk;

        vlchunk = new_vlchunk;
        index = count_to_index(new_vlchunk->count);
    }

    lchunk_t *lchunk = vlchunk->lchunk;
    log_item_t *item_ptr = &lchunk->items[index];
    memcpy(item_ptr, item, ITEM_SIZE);

    vlchunk->count++;
    set_bitmap(vlchunk, index, true);

    vlog->num_of_items++;
    return item_ptr;
}

void fast_GC(vlog_t *vlog)
{

    vlchunk_t *cursor_vlchunk = vlchunk_tree_first(&vlog->vlchunks);
    simple_list_t *free_vlchunk_list = simple_list_create();
    while (cursor_vlchunk)
    {
        if (bitmap_empty(cursor_vlchunk))
        {
            cursor_vlchunk->id = 0;
            cursor_vlchunk->lchunk->id = 0;
            persist(&cursor_vlchunk->lchunk->id, sizeof(cursor_vlchunk->lchunk->id));
            if (cursor_vlchunk->pre_lchunk)
            {
                cursor_vlchunk->pre_lchunk->next = cursor_vlchunk->lchunk->next;
                persist(&cursor_vlchunk->pre_lchunk->next, sizeof(cursor_vlchunk->pre_lchunk->next));
            }

            simple_list_insert(free_vlchunk_list, cursor_vlchunk);
        }
        cursor_vlchunk = vlchunk_tree_next(&vlog->vlchunks, cursor_vlchunk);
    }

    node_t *node = (node_t *)free_vlchunk_list->head->next;
    while (node)
    {
        vlog->num_of_chunks--;
        vlog->num_of_items -= N_ITEMS_PER_LCHUNK;
        vlchunk_tree_remove(&vlog->vlchunks, (vlchunk_t *)node->data);
        simple_list_insert(vlog->global_free_list, node->data);
        node = (node_t *)node->next;
    }
    simple_list_destroy(free_vlchunk_list);
}

static inline vlog_t *log_create_for_GC(vlog_t *old_vlog)
{
    vlog_t *vlog = (vlog_t *)_malloc(sizeof(vlog_t));
    log_file_head_t *log_file_head = (log_file_head_t *)old_vlog->log_file_addr;
    log_t *log = &log_file_head->log[1 - log_file_head->alt];

    vlog->global_lchunk_id = 0;

    vlog->global_free_list = old_vlog->global_free_list;
    vlog->log_file_addr = old_vlog->log_file_addr;
    vlog->log_file_used = old_vlog->log_file_used;

    vlog->num_of_chunks = 0;
    vlog->num_of_items = 0;

    vlchunk_t *root_vlchunk = vlchunk_alloc(vlog);
    vlchunk_t *first_vlchunk = vlchunk_alloc(vlog);
    first_vlchunk->pre_lchunk = root_vlchunk->lchunk;
    root_vlchunk->lchunk->next = first_vlchunk->lchunk;
    persist(root_vlchunk->lchunk, sizeof(lchunk_t));

    vlchunk_tree_new(&vlog->vlchunks);
    vlchunk_tree_insert(&vlog->vlchunks, first_vlchunk);
    vlog->tail_vlchunk = first_vlchunk;
    vlog->log = log;

    log->root_lchunk = root_vlchunk->lchunk;
    log->areana_id = old_vlog->log->areana_id;
    persist(log, sizeof(log_t));

    return vlog;
}

void slow_GC(vlog_t **vlog_ptr, arena_t *arena)
{

    tcache_t *tcache = tcache_get();

    vlog_t *vlog = *vlog_ptr;
    vlog_t *new_vlog = log_create_for_GC(vlog);

    vlchunk_t *temp_vlchunk = (vlchunk_t *)_malloc(sizeof(vlchunk_t));
    vlchunk_t *cursor_vlchunk = NULL;
    lchunk_t *cursor_lchunk = vlog->log->root_lchunk->next;
    log_item_t *cursor_item = NULL;
    while (cursor_lchunk)
    {
        temp_vlchunk->lchunk = cursor_lchunk;
        cursor_vlchunk = vlchunk_tree_search(&vlog->vlchunks, temp_vlchunk);
        assert(cursor_vlchunk);
        for (int i = 0; i < BITMAP_ARRAY_SIZE; i++)
        {
            for (int j = 0; j < 64; j++)
            {
                if (cursor_vlchunk->bitmap[i] & (1ULL << j))
                {
                    cursor_item = &cursor_lchunk->items[i * 64 + j];
                    if (cursor_item->type == NORMAL_LOG)
                    {
                        extent_t *extent;
                        if (cursor_item->size_version_id & 1ULL)
                        {
                            vslab_t *vslab;
                        RETRY:
                            vslab = rtree_vslab_read(tcache, &extents_rtree, cursor_item->ptr);
                            extent = vslab->eslab;
                            if (extent == 0)
                            {
                                goto RETRY; 
                            }
                        }
                        else
                        {
                            extent = rtree_extent_read(&extents_rtree, &tcache->extents_rtree_ctx, cursor_item->ptr, false);
                        }
                        if (extent->log != cursor_item)
                        {
                            printf("extent->log = %p, cursor_item = %p\n", extent->log, cursor_item);
                            assert(extent->log == cursor_item);
                            exit(0);
                        }
                        extent->log = add_log_for_GC(new_vlog, cursor_item);
                    }
                    else if (cursor_item->type == FILE_LOG)
                    {
                        file_t key;
                        key.addr = (void *)cursor_item->ptr;
                        file_t *file = file_tree_search(&arena->file_tree, &key);
                        if (file->log != cursor_item)
                        {
                            printf("file->log = %p, cursor_item = %p\n", file->log, cursor_item);
                            assert(file->log == cursor_item);
                            exit(0);
                        }
                        file->log = add_log_for_GC(new_vlog, cursor_item);
                    }
                }
            }
        }
        cursor_lchunk = cursor_lchunk->next;
    }
    persist(new_vlog->tail_vlchunk->lchunk, LCHUNK_SIZE);

    log_file_head_t *log_file_head = (log_file_head_t *)new_vlog->log_file_addr;
    log_file_head->alt = 1 - log_file_head->alt;
    persist(&log_file_head->alt, sizeof(log_file_head->alt));

    *vlog_ptr = new_vlog;
    log_destroy(vlog);
}