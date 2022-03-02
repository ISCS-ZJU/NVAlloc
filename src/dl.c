#define	DL_C_
#include "nvalloc/internal/nvalloc_internal.h"

dl_t * dl_new()
{
    dl_t * dl = (dl_t *)_malloc(sizeof(dl_t));
    dlnode_t * head = (dlnode_t *)_malloc(sizeof(dlnode_t));
    head->pre = head;
    head->nxt = head;
    dl->head = head;
    return dl;
}


dlnode_t * dl_insert(dl_t *dl, void *ptr)
{
    dlnode_t * node = (dlnode_t *)_malloc(sizeof(dlnode_t));
    node->data = ptr;
    node->nxt = dl->head->nxt;
    node->pre = dl->head;
    dl->head->nxt->pre = node;
    dl->head->nxt = node;
    return node;
}

dlnode_t * dl_insert_tail(dl_t *dl, void *ptr)
{
    dlnode_t * node = (dlnode_t *)_malloc(sizeof(dlnode_t));
    node->data = ptr;
    node->nxt = dl->head;
    node->pre = dl->head->pre;
    dl->head->pre->nxt = node;
    dl->head->pre = node;
    return node;    
}

dlnode_t * dl_search(dl_t * dl, void *ptr)
{
    dlnode_t * node = dl->head->nxt;
    while (node != dl->head)
    {
        if (node->data == ptr) break;
        node = node->nxt;
    }
    return node;
}

void dl_delete( dlnode_t *dlnode)
{
    dlnode->pre->nxt = dlnode->nxt;
    dlnode->nxt->pre = dlnode->pre;
    _free(dlnode);
}


void dl_move_to_tail(dl_t * dl, dlnode_t * dlnode)
{ 
    dlnode->pre->nxt = dlnode->nxt;
    dlnode->nxt->pre = dlnode->pre;
    dlnode->nxt = dl->head;
    dlnode->pre = dl->head->pre;
    dl->head->pre->nxt = dlnode;
    dl->head->pre = dlnode;
}   