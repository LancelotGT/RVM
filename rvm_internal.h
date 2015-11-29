/*
 *  Internal data structures used by RVM
 */

#ifndef __LIBRVM_INTERNAL__
#define __LIBRVM_INTERNAL__ 

#define MAXLINE 512 
#define MAXDIR 100 

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
    int size;
    int offset;
    char* data;
} log_t;

typedef struct {
    char path[MAXLINE];
    int length;
    int modified;
    void* undo_log;
} segment_t;   

#endif
