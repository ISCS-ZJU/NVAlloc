/******************************************************************************/
#ifdef NVALLOC_H_TYPES

typedef struct lchunk_s lchunk_t;
typedef struct vlchunk_s vlchunk_t;
typedef struct log_item_s log_item_t;
typedef struct log_s log_t;
typedef struct vlog_s vlog_t;
typedef struct log_file_head_s log_file_head_t;

#define LOG_FILE_SIZE 10737418240ULL
#define LCHUNK_SIZE 1024
#define ITEM_SIZE (sizeof(log_item_t))
#define N_ITEMS_PER_LCHUNK ((LCHUNK_SIZE - sizeof(lchunk_t)) / ITEM_SIZE)
#define BITMAP_ARRAY_SIZE (((N_ITEMS_PER_LCHUNK - 1) / 64) + 1)
#define ptr2lchunk(ptr) (lchunk_t *)(((intptr_t)ptr) & ~(LCHUNK_SIZE - 1))
#define ptr2lindex(ptr, lchunk) (((intptr_t)ptr - (intptr_t)lchunk - sizeof(lchunk_t)) / ITEM_SIZE)
#define NORMAL_LOG 0
#define FREE_LOG 1
#define FILE_LOG 2

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

struct log_item_s
{
   uint64_t type : 2;             // 0:normal_log, 1:free_log, 2:file_log
   uint64_t ptr : 48;             // 0:assigned address, 1:log address, 2:file address
   uint64_t size_version_id : 14; // 0:allocated size, 1:log chunk version, 2:file identify
};

struct lchunk_s
{
   lchunk_t *next;
   uint64_t id;
   log_item_t items[0];
};

struct vlchunk_s
{
   int count;
   rb_node(vlchunk_t) vlchunk_link;
   uint64_t bitmap[BITMAP_ARRAY_SIZE];
   uint64_t id;
   lchunk_t *lchunk;
   lchunk_t *pre_lchunk;
};

typedef rb_tree(vlchunk_t) vlchunk_tree_t;

struct log_s
{
   lchunk_t *root_lchunk;
   uint64_t areana_id;
};

struct vlog_s
{
   log_t *log;
   uint64_t gc_timer;
   vlchunk_tree_t vlchunks;
   vlchunk_t *tail_vlchunk;
   uint64_t global_lchunk_id; // 0 means this lchunk is free.

   simple_list_t *global_free_list;

   void *log_file_addr;
   uint64_t log_file_used;

   uint64_t num_of_chunks;
   uint64_t num_of_items;

};

struct log_file_head_s
{
   uint8_t alt;
   log_t log[2];
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

vlog_t *log_create(uint64_t areana_id);

void *add_log(arena_t *arena, vlog_t **vlog_ptr, uint8_t type, uint64_t ptr, uint64_t size_version_id, bool slab);
void fast_GC(vlog_t *vlog);
void slow_GC(vlog_t **vlog_ptr,arena_t *arena);

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

#endif /* NVALLOC_H_INLINES */
       /******************************************************************************/