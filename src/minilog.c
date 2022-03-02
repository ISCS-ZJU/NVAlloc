#include "nvalloc/internal/nvalloc_internal.h"

static inline void *minilog_file_create(size_t align)
{
    void *addr, *tmp;

    size_t mapped_len;
    char str[100];
    int is_pmem;
    sprintf(str, "/mnt/pmem/nvalloc_files/minilog_file");

    if ((tmp = pmem_map_file(str, 8 * 1024, PMEM_FILE_CREATE | PMEM_FILE_SPARSE, 0666, &mapped_len, &is_pmem)) == NULL)
    {
        printf("map file fail! %d\n", errno);
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

minilog_t *minilog_create()
{

    minilog_t *log = (minilog_t *)((uint64_t)minilog_file_create(8 * 1024) + 4 * 1024 - MINILOG_SIZE / 2);
    memset(log, 0, MINILOG_SIZE);
    persist(log, MINILOG_SIZE);
    return log;
}

static inline int minilog_count_to_index(int bid)
{
    int alt = bid & 1;
    int bid2 = bid >> 1;
    int n = CACHE_LINE_SIZE / MINILOG_ITEM_SIZE;
    int nbanks = MINILOG_NUM / 2 / n;
    return ((bid2 % nbanks) * n + bid2 / nbanks) + alt * MINILOG_NUM / 2;
}

static inline uint64_t get_slot_in_minilog(minilog_t *log, uint64_t *global_index)
{

    int index1 = __sync_fetch_and_add(global_index, 1) % MINILOG_NUM;
    int index2 = minilog_count_to_index(index1); // interleave
    return index2;
}

void add_minilog(minilog_t *log, uint64_t *global_index, uint64_t ptr)
{
    uint64_t index = get_slot_in_minilog(log, global_index);
    log->log_item[index].ptr = ptr;
    persist(&log->log_item[index], MINILOG_ITEM_SIZE);
}
