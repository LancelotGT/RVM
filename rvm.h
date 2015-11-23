#ifndef __LIBRVM__
#define __LIBRVM__

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
 
/* Initialze the library with the specified directory as backing store */
rvm_t rvm_init(const char *directory);

/* map a segment from disk into memory. If the segment does not already exist,
 * then create it and give it size size_to_create. If the segment exits but is
 * shorter than size_to_create, then extend it until it is long enough. It is an
 * error to try map the same segment twice */
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);

/* unmap a segment from memory */
void rvm_unmap(rvm_t rvm, void *segbase);

/* destroy a segment completely, erasing its backing store. This function should
 * not be called on a segment that is currently mapped */
void rvm_destroy(rvm_t rvm, const char *segname);

/* begin a transaction that will modify the segments listed in segbases. If any
 * of the specified is already being modified by a transaction, then the call
 * should fail and return (trans_t) - 1.*/
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);

/* declare that the library is about to modify a specified range of memory in
 * the specified segment. The segment must be one of the segment specified in
 * rvm_begin_trans. */
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);

/* commit all changes that have been made within the specified transaction. When
 * the call returns, then enough information should have been saved to disk so
 * that, even if the program crashes, the changes will be seen by the program
 * when it restarts. */
void rvm_commit_trans(trans_t tid);

/* undo all changes have happened within the specified transaction */
void rvm_abort_trans(trans_t tid);

/* play through any committed or aborted items in the log files and shrink the
 * log file as much as possible */
void rvm_truncate_log(rvm_t rvm);

#endif
