/*
 *  Internal data types and structures used by RVM
 */

#ifndef __LIBRVM_INTERNAL__
#define __LIBRVM_INTERNAL__ 

#include <stdlib.h>
#include <stdio.h>
#define MAXLINE 512 
#define MAXDIR 100

typedef struct rvm_t {
    char directory[MAXLINE];
    int tid;
} rvm_t;

typedef struct trans_t {
    rvm_t* rvm;
    int numsegs;
    void** segbases;
} trans_t; 

typedef struct queue_t {
    void* head;
    void* tail;
    int N;
} queue_t;

typedef struct item_t {
    void* key;
    void* value;
    item_t* prev;
    item_t* next;
} item_t; 

typedef struct ST_t {
    item_t* head;
    item_t* tail;
    int N;
} ST_t;

/* Implementation for queue used by RVM */
void queue_init(queue_t* q);
void queue_push(queue_t* q, void* item);
void* queue_front(queue_t* q);
void queue_pop(queue_t* q);

/* Implementation for a symbol table used by RVM */
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
        new_item->prev = NULL;
        st->head = st->tail = new_item;
    }
    else
    {
        new_item->prev = st->tail; 
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
        if (current->key == key)
            return current->value;
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
#endif
