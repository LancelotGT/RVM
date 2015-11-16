/*
 * Implementation for RVM
 */

#include "rvm.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static int rvm_id = 0;
static ST_t segment_table[MAXDIR];
static ST_t trans_table;

rvm_t rvm_init(const char *directory)
{
    rvm_t rvm;
    rvm.rid = rvm_id++;
    strcpy(rvm.directory, directory); /* copy the directory name */
    ST_init(&segment_table[rvm.rid]); /* init the segment lookup table */
    return rvm;
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create)
{   /* use a symbol table to store segname and addr mapping */
    char fullpath[MAXLINE];
    strcpy(fullpath, rvm.directory);
    strcat(fullpath, segname);
    int fd;
    if ((fd = open(fullpath, O_APPEND)) < 0)
    {
        printf("Open segment log failed.\n");
        return NULL;
    }

    void* addr;
    if ((addr = malloc(sizeof(size_to_create))) == NULL)
    {
        printf("Memory allocation error\n");
        return NULL;
    }

    segment_t* seg = (segment_t*) malloc(sizeof(segment_t));
    strcpy(seg->name, fullpath);
    seg->length = size_to_create;
    seg->modified = 0;
    seg->fd = fd;
    seg->undo_log = (list_t*) malloc(sizeof(list_t)); 
    ST_put(&segment_table[rvm.rid], addr, seg);
    return addr; 
}

void rvm_unmap(rvm_t rvm, void *segbase)
{
    segment_t* seg = (segment_t*) ST_get(&segment_table[rvm.rid], segbase);

    if (close(((segment_t*) seg)->fd) != 0)
        printf("Close file error\n");

    free(segbase); /* free the actual log segment in memory */
    free(seg->undo_log); /* free undo log stack */
    free(seg); /* free the segment struct */
    ST_erase(&segment_table[rvm.rid], segbase);
}

void rvm_destroy(rvm_t rvm, const char *segname)
{
    char fullpath[MAXLINE];
    strcpy(fullpath, rvm.directory);
    strcat(fullpath, segname); 

    /* check if the file exists */
    struct stat sb;
    if (stat(fullpath, &sb) == -1) {
        printf("stat log segment error");
        return;
    }

    if (remove(fullpath) != 0)
        printf("remove log segment error");
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases)
{
    trans_t curr = (trans_t) malloc(sizeof(trans));

    /* check if segments have already been modified */
    int i;
    for(i = 0; i < numsegs; i++) 
    {
        segment_t* seg = (segment_t*) ST_get(&segment_table[rvm.rid], segbases[i]);
        if (seg->modified != 0)
        {
            printf("Segment already modified by a transaction\n");
            return (trans_t) -1;
        }
    }
    curr->rid = rvm.rid;
    curr->segbases = segbases;
    curr->numsegs = numsegs;
    return curr;
}

void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)
{
    /* check if segbase is initialized by rvm_begin_trans */
    int is_initialized;
    int i;
    for (i = 0; tid->numsegs; i++)
        if (tid->segbases[i] == segbase) 
            is_initialized = true;
    if (!is_initialized)
    {
        printf("segment address not associated with transaction\n");
        return;
    }
    
    /* create and save the in memory undo log */
    segment_t* seg = (segment_t*) ST_get(&segment_table[tid->rid], segbase);
    log_t* log = (log_t*) malloc(sizeof(log_t));
    log->size = size;
    log->offset = offset;
    log->data = (char*) malloc(sizeof(size));
    memcpy(log->data, (char*) segbase + offset, size);
    list_push(seg->undo_log, log);
}

void rvm_commit_trans(trans_t tid)
{
    ST_t* st = &segment_table[tid->rid];
    /* apply changes in current transactions one by one */
    int i;
    for (i = 0; i < tid->numsegs; i++)
    {
        segment_t* seg = (segment_t*) ST_get(st, tid->segbases[i]); 
        char* path = seg->name;
        size_t length = seg->length;

        int fd;
        if ((fd = open(path, O_APPEND)) == -1)
            fprintf(stderr, "open file error\n");

        /* write segment header, footer and content */
        char buffer[50];
        int n = sprintf(buffer, "segment start\n");
        write(fd, buffer, n);
        n = sprintf(buffer, "%lu\n", length); 
        write(fd, buffer, n);
        write(fd, tid->segbases[i], length);
        n = sprintf(buffer, "segment end");  
        write(fd, buffer, n);
        close(fd);

        /* clear undo logs */
        while (!list_empty(seg->undo_log))
        {
            log_t* log = (log_t*) list_pop_front(seg->undo_log);
            free(log->data);
        }
    }

    /* clear the entire transaction */
    free(tid);
}

void rvm_abort_trans(trans_t tid)
{   
    ST_t* st = &segment_table[tid->rid];

    /* apply undo logs in a LIFO manner */ 
    int i;
    for (i = 0; i < tid->numsegs; i++)
    {
        segment_t* seg = (segment_t*) ST_get(st, tid->segbases[i]); 

        while (!list_empty(seg->undo_log))
        {
            log_t* log = (log_t*) list_pop_front(seg->undo_log);
            /* copy undo log data back to segment base address + offset */
            memcpy((char*) tid->segbases[i] + log->offset, log->data, log->size);
            free(log->data);
        }
    }

    /* clear the entire transaction */
    free(tid); 
}

void rvm_truncate_log(rvm_t rvm)
{

}
