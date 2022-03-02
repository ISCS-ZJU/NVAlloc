
typedef struct mini_item_s mini_item_t;
typedef struct minilog_s minilog_t;
#define MINILOG_ITEM_SIZE (sizeof(mini_item_t))
#define MINILOG_SIZE (sizeof(minilog_t))
#define MINILOG_NUM (NBANKS * 2 * (CACHE_LINE_SIZE / MINILOG_ITEM_SIZE)) // 2 because we have 2 DIMMs, we want the WAL to use all DIMMs

#define MINILOG_TYPE_SMALL_ALLOC 1
#define MINILOG_TYPE_SMALL_FREE 2
#define MINILOG_TYPE_LARGE_ALLOC 3
#define MINILOG_TYPE_LARGE_FREE 4

minilog_t *minilog;
uint64_t global_index;

struct mini_item_s
{
   uint64_t ptr;
};

struct minilog_s
{
   mini_item_t log_item[MINILOG_NUM];
};

minilog_t *minilog_create();
void add_minilog(minilog_t *log, uint64_t *global_index, uint64_t ptr);