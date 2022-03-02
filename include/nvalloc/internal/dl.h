//a simple double list implementaion, to be replaced

typedef struct dlnode_s dlnode_t;
typedef struct dl_s dl_t;

struct dlnode_s
{
    void *data;
    dlnode_t *pre, *nxt;
};

struct dl_s
{
    dlnode_t *head;
};

dl_t *dl_new();
dlnode_t *dl_insert(dl_t *dl, void *ptr);
dlnode_t * dl_insert_tail(dl_t *dl, void *ptr);
dlnode_t *dl_search(dl_t *dl, void *ptr);
void dl_delete( dlnode_t *dlnode);
void dl_move_to_tail(dl_t * dl, dlnode_t * dlnode);