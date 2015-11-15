/*
 * Implementation for RVM
 */

#include "rvm.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>

static int rvm_id = 0;
static ST_t segname_table[MAXDIR];

rvm_t rvm_init(const char *directory)
{
    rvm_t rvm;
    rvm.tid = rvm_id++;
    int i;
    for (i = 0; directory[i] != '\0'; i++)
        rvm.directory[i] = directory[i];
    rvm.directory[i] = '\0';

    ST_init(&segname_table[rvm.tid]);
    return rvm;
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create)
{   /* use a symbol table to store segname and addr mapping */
    void* addr = malloc(size_to_create);
    char* fullpath = strcat(rvm.directory, segname);
    int fd = open(fullpath, O_APPEND);
    mmap(addr, size_to_create, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ST_put(&segname_table[rvm.tid], (void*) segname, addr);
    return addr; 
}

void rvm_unmap(rvm_t rvm, void *segbase);

void rvm_destroy(rvm_t rvm, const char *segname);

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);

void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);

void rvm_commit_trans(trans_t tid);

void rvm_abort_trans(trans_t tid);

void rvm_truncate_log(rvm_t rvm);
