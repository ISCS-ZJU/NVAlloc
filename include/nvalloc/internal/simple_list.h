//a simple single list implementaion, to be replaced

typedef struct node_s node_t;
typedef struct simple_list_s simple_list_t;

struct node_s
{
    void *data;
    void *next;
};

struct simple_list_s
{
    node_t *head;
    node_t *tail;
};

static inline node_t *node_create()
{
    node_t *node = (node_t *)_malloc(sizeof(node_t));
    node->data = NULL;
    node->next = NULL;
    return node;
}

static inline simple_list_t *simple_list_create()
{
    simple_list_t *list = (simple_list_t *)_malloc(sizeof(simple_list_t));
    list->head = node_create(); 
    list->tail = list->head;
    return list;
}

static inline void simple_list_destroy(simple_list_t *list)
{
    node_t *cursor_node = list->head;
    while (cursor_node)
    {
        node_t *next_node = (node_t *)cursor_node->next;
        _free(cursor_node);
        cursor_node = next_node;
    }
    _free(list);
}

static inline void simple_list_insert(simple_list_t *list, void *data)
{
    node_t *new_node = node_create();
    new_node->data = data;
    list->tail->next = new_node;
    list->tail = new_node;
}

static inline node_t *simple_list_search(simple_list_t *list, void *data)
{
    node_t *cursor_node = list->head;
    while (cursor_node)
    {
        if (cursor_node->data == data) 
            return cursor_node;
        cursor_node = (node_t *)cursor_node->next;
    }
    return NULL;
}


static inline void *simple_list_get_first(simple_list_t *list)
{
    node_t *first_node = (node_t *)list->head->next;
    if (first_node)
    {
        list->head->next = first_node->next;
        if (first_node == list->tail) 
            list->tail = list->head;
        return first_node->data;
    }
    return NULL;
}