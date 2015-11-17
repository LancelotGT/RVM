/*
 *  Internal data types and structures used by RVM
 */

#ifndef __LIBRVM_INTERNAL__
#define __LIBRVM_INTERNAL__ 

#include <stdlib.h>
#include <stdio.h>
#define MAXLINE 512 
#define MAXDIR 100

/* Implementation for queue used by RVM */ 
typedef struct node_t {
    node_t* next; 
    void* value;
} node_t; 

typedef struct {
    node_t* front;
    node_t* back;
    int N;
} list_t;

void list_init(list_t* l)
{
    l->front = NULL;
    l->back = NULL;
    l->N = 0;
}

void list_enqueue(list_t* l, void* value)
{
    node_t* node;
    node = (node_t*) malloc(sizeof(node_t));
    node->value = value;
    node->next = NULL;

    if(l->back == NULL)
        l->front = node;
    else
        l->back->next = node;

    l->back = node;
    l->N++;
}

void list_push(list_t* l, void* value){
    node_t* node;
    node = (node_t*) malloc(sizeof(node_t));
    node->value = value;
    node->next = l->front;

    if(l->back == NULL)
        l->back = node;

    l->front = node;
    l->N++;
}

void* list_front(list_t* l)
{
    return l->front;
}

void* list_back(list_t* l) 
{
    return l->back;
}

int list_empty(list_t* l)
{
    return l->N == 0;
}

void* list_pop_front(list_t* l)
{
    void* value;
    node_t* node;

    if(l->front == NULL){
        fprintf(stderr, "Error: underflow in steque_pop.\n");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    node = l->front;
    value = node->value;

    l->front = l->front->next;
    if (l->front == NULL) l->back = NULL;
    free(node);
    l->N--;

    return value;
}

void list_destroy(list_t* l)
{
    while (!list_empty(l))
        list_pop_front(l);
}

/* Implementation for a symbol table used by RVM */
typedef struct item_t {
    void* key;
    void* value;
    item_t* next;
} item_t; 

typedef struct {
    item_t* head;
    item_t* tail;
    int N;
} ST_t;

int ST_init(ST_t* st) 
{
    if (!st) return -1;
    st->head = NULL;
    st->tail = NULL;
    st->N = 0;
    return 0;
}

int ST_put(ST_t* st, void* key, void* value)
{
    if (!st) return -1;
    item_t* new_item = (item_t*) malloc(sizeof(item_t));
    new_item->key = key;
    new_item->value = value;
    new_item->next = NULL; 

    if (st->head == NULL)
    {
        st->head = st->tail = new_item;
    }
    else
    {
        st->tail->next = new_item;
        st->tail = new_item;
    }
    st->N++;
    return 0;
}

void* ST_get(ST_t* st, void* key)
{
    if (!st) return NULL;

    item_t* current = st->head;
    while (current != st->tail)
    {
        if (current->key == key)
            return current->value;
        current = current->next;
    }
    return NULL;
}

int ST_erase(ST_t* st, void* key)
{
    if (!st) return -1;
    item_t* current = st->head;

    if (current->key == key) /* item to remove is head */
    {
        item_t* erased = current;
        st->head = current->next;
        if (erased == st->tail)
            st->tail = NULL;
        free(erased); 
        st->N--;
        return 0;
    }
    else
    {
        while (current->next != st->tail)
        {
            if (current->next->key == key) 
            {
                item_t* erased = current->next;
                current->next = erased->next;
                free(erased);
                st->N--; 
                return 0;
            }
            current = current->next;
        } 
        
        if (current->next->key == key) /* item to remove is tail */ 
        {
            free(current->next);
            current->next = NULL;
            st->tail = current;
            st->N--; 
            return 0;
        }
    }

    return -1; /* no item found */
} 

int ST_empty(ST_t* st)
{
    return st->N == 0;
}
int ST_destroy(ST_t* st)
{
    if (!st) return -1;
    while (!ST_empty(st))
        ST_erase(st, st->head->key);
    return 0;
}

typedef struct {
    char directory[MAXLINE];
    int rid;
} rvm_t;

typedef struct {
    int rid; /* rvm id associated with the transaction */
    int numsegs;
    void** segbases;
} trans;

typedef trans* trans_t;

typedef struct {
    size_t size;
    size_t offset;
    char* data;
} log_t;

typedef struct {
    char name[MAXLINE];
    size_t length;
    int modified;
    int fd;
    list_t* undo_log;
    list_t* redo_log;
} segment_t; 

#endif
