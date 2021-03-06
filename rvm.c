/*
 * Implementation for RVM
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include "rvm.h"
#include "rvm_internal.h"

/* private syscall wrappers */
static int Open(const char* path, int oflag);
static void Close(int fd);
static void* Mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
static void Munmap(void* start, size_t length);
static void* Malloc(size_t size);
static void Free(void* ptr);
static DIR *Opendir(const char *name); 
static int Closedir(DIR *dirp);

/* definition for a simple list used by RVM */
typedef struct node_t {
    struct node_t* next; 
    void* value;
} node_t; 

typedef struct {
    node_t* front;
    node_t* back;
    int N;
} list_t;

void list_init(list_t* l);
void list_enqueue(list_t* l, void* value);
void list_push(list_t* l, void* value);
void* list_front(list_t* l);
void* list_back(list_t* l);
int list_empty(list_t* l);
void* list_pop_front(list_t* l);
void list_destroy(list_t* l);

/* definition for a symbol table used by RVM */
typedef struct item_t {
    void* key;
    void* value;
    struct item_t* next;
} item_t; 

typedef struct {
    item_t* head;
    item_t* tail;
    int N;
} ST_t;

int ST_init(ST_t* st);
int ST_put(ST_t* st, void* key, void* value);
void* ST_get(ST_t* st, void* key);
int ST_erase(ST_t* st, void* key);
int ST_empty(ST_t* st);
int ST_destroy(ST_t* st); 

/* private helper functions */
static void get_logpath(char* logpath, char* path);
static void apply_log(char* logpath, char* segpath);
static void check_segment(char* filename, int size_to_create);
static int check_addr(trans_t tid, void* segbase);
static void* recover_data(char* path);

/* global variable */
static int rvm_id = 0;
static ST_t segment_table[MAXDIR];

rvm_t rvm_init(const char *directory)
{   /* if the dir does not exist, create one */
    rvm_t rvm;
    rvm.rid = rvm_id++;

    struct stat st;
    if (stat(directory, &st) == -1)
        mkdir(directory, 0777); /* create directory if it does not exist */

    strcpy(rvm.directory, directory); /* copy the directory name */
    ST_init(&segment_table[rvm.rid]); /* init the segment lookup table */
    return rvm;
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create)
{   /* use a symbol table to store segname and addr mapping */
    char path[MAXLINE];
    strcpy(path, rvm.directory);
    strcat(path, "/");
    strcat(path, segname);

    check_segment(path, size_to_create);
    rvm_truncate_log(rvm);

    /* allocate memory for segment and recover data from backing store */
    void* addr = recover_data(path);

    /* create the in memory segment data structure and insert the 
     * addr->segment pair in segment table */ 
    segment_t* seg = (segment_t*) Malloc(sizeof(segment_t));
    strcpy(seg->path, path);
    seg->length = size_to_create;
    seg->modified = 0;
    seg->undo_log = Malloc(sizeof(list_t));
    list_init(seg->undo_log);
    ST_put(&segment_table[rvm.rid], addr, seg);
    return addr; 
}

void rvm_unmap(rvm_t rvm, void *segbase)
{
    segment_t* seg = (segment_t*) ST_get(&segment_table[rvm.rid], segbase);
    if (!seg) {
        fprintf(stderr, "segment address does not exist\n");
        return;
    }

    Free(segbase); /* free the actual log segment in memory */
    Free(seg->undo_log); /* free undo log stack */
    Free(seg); /* free the segment struct */
    ST_erase(&segment_table[rvm.rid], segbase);
}

void rvm_destroy(rvm_t rvm, const char *segname)
{
    char path[MAXLINE], logpath[MAXLINE];
    strcpy(path, rvm.directory);
    strcat(path, segname); 
    get_logpath(logpath, path);

    /* check if the file exists */
    struct stat sb;
    if (stat(path, &sb) == -1) {
        return;
    }

    if (remove(path) != 0)
        fprintf(stderr, "remove error\n");
    if (remove(logpath) != 0)
        fprintf(stderr, "remove error\n"); 
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases)
{
    trans_t curr = (trans_t) Malloc(sizeof(trans));

    /* check if segments have already been modified */
    int i;
    for(i = 0; i < numsegs; i++) {
        segment_t* seg = (segment_t*) ST_get(&segment_table[rvm.rid], segbases[i]);
        if (!seg) {
            fprintf(stderr, "Cannot find segbase [%lu]\n", (unsigned long) segbases[i]);
            return (trans_t) -1; 
        }
        
        if (seg->modified != 0) {
            fprintf(stderr, "Segment already modified\n");
            return (trans_t) -1;
        }
        seg->modified = 1;
    }
    curr->rid = rvm.rid;
    curr->segbases = segbases;
    curr->numsegs = numsegs;
    return curr;
}

void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)
{
    /* check if segbase is initialized by rvm_begin_trans */
    if (!check_addr(tid, segbase))
        return;
    
    /* create and push the in memory undo log into undo logs list*/
    segment_t* seg = (segment_t*) ST_get(&segment_table[tid->rid], segbase);
    log_t* log = (log_t*) Malloc(sizeof(log_t));
    log->size = size;
    log->offset = offset;
    log->data = (char*) Malloc(size);
    memcpy(log->data, (char*) segbase + offset, size);
    list_push(seg->undo_log, log);
}

void rvm_commit_trans(trans_t tid)
{   /* apply changes in current transactions one by one */ 
    ST_t* st = &segment_table[tid->rid];

    int i;
    for (i = 0; i < tid->numsegs; i++) {   /* for each data segment */
        segment_t* seg = (segment_t*) ST_get(st, tid->segbases[i]); 
        char logpath[MAXLINE];
        get_logpath(logpath, seg->path);

        /* open the log segment and write changes */
        int fd = Open(logpath, O_RDWR | O_APPEND); ;
        /* write modified segments according to the offset and size
         * in undo logs. Then clear undo logs */
        while (!list_empty(seg->undo_log)) {
            log_t* log = (log_t*) list_pop_front(seg->undo_log);
            write(fd, &log->size, sizeof(int)); /* write size into log file */
            write(fd, &log->offset, sizeof(int)); /* write offset into log file */
            /* write memory segment into log segment */
            write(fd, (char*) tid->segbases[i] + log->offset, log->size); 
            Free(log->data);
            Free(log);
        }
        seg->modified = 0; /* reset modified bit for next transaction */
        Close(fd); 
    }

    /* clear the entire transaction */
    Free(tid);
}

void rvm_abort_trans(trans_t tid)
{   
    ST_t* st = &segment_table[tid->rid];

    /* apply undo logs in a LIFO manner */ 
    int i;
    for (i = 0; i < tid->numsegs; i++) {
        segment_t* seg = (segment_t*) ST_get(st, tid->segbases[i]); 

        while (!list_empty(seg->undo_log)) {
            log_t* log = (log_t*) list_pop_front(seg->undo_log);
            /* copy undo log data back to segment base address + offset */
            memcpy((char*) tid->segbases[i] + log->offset, log->data, log->size);
            Free(log->data);
        }
        seg->modified = 0;
    }

    /* clear the entire transaction */
    Free(tid); 
}

void rvm_truncate_log(rvm_t rvm)
{   /* currently only iterate the log file and write changes to data file.
       this should not depend on the global address->segment mapping table 
       since that data structure is in memory. But this function is required 
       to work after crash */
       struct dirent* pDirent;
       DIR* pDir;
       pDir = Opendir(rvm.directory);

       while ((pDirent = readdir(pDir)) != NULL) {
           char* filename = pDirent->d_name;
           if (strstr(filename, ".log")) { /* check filename ends with .log */
               char logpath[MAXLINE], segpath[MAXLINE];
               strcpy(logpath, rvm.directory);
               strcat(logpath, "/");
               strcat(logpath, filename);

               strcpy(segpath, rvm.directory);
               strcat(segpath, "/"); 
               strncat(segpath, filename, strlen(filename) - 4);

               fflush(stdout);
               apply_log(logpath, segpath);
           }
       }
       Closedir (pDir);
}


/*
 * private helper functions
 */

void get_logpath(char* logpath, char* path)
{
    strcpy(logpath, path);
    strcat(logpath, ".log");
}

/* check whether a segment exists. If it does not exist, it will 
 * create the directory. If it exist but size is shorter than 
 * size_to_create, it will elongate the segment size to size_to_create */
void check_segment(char* filename, int size_to_create)
{
    char logpath[MAXLINE];
    strcpy(logpath, filename);
    strcat(logpath, ".log"); 
    struct stat st;

    if (stat(filename, &st) == -1) { /* log segment does not exist */
        int data_fd = creat(filename, S_IRWXU); /* create data segment */
        creat(logpath, S_IRWXU); /* create log segment */
        write(data_fd, &size_to_create, sizeof(size_to_create));

        char a = '0';
        int i;
        for (i = 0; i < size_to_create; i++)
            write(data_fd, &a, 1); /* fill the data segment with 0 */ 
    } else {
        int current_size;
        int fd = Open(filename, O_RDWR);
        read(fd, &current_size, sizeof(int));

        if (current_size < size_to_create) {
            /* elongate the data segment if necessary */
            lseek(fd, 0, SEEK_SET);
            write(fd, &size_to_create, sizeof(size_to_create));
            lseek(fd, current_size + sizeof(size_to_create), SEEK_SET);
            char a = '0';
            int i;
            for (i = 0; i < size_to_create - current_size; i++)
                write(fd, &a, 1); /* fill the rest file with 0 */
        }
        Close(fd); 
    }
}


/* check whether a segment address is associated with a transaction */
int check_addr(trans_t tid, void* segbase)
{
    int i;
    for (i = 0; i < tid->numsegs; i++)
        if (tid->segbases[i] == segbase)
            return 1;
    fprintf(stderr, "segment address not associated with transaction\n");
    return 0;
}

/* recover the data from data segment to memory address */

void* recover_data(char* path)
{
    int size;
    int fd = Open(path, O_RDONLY);
    read(fd, &size, sizeof(int));
    void* segbase = Malloc(size);
    read(fd, segbase, size);
    Close(fd);
    return segbase;
}

/* apply the log segments to the data segments given their names */ 
void apply_log(char* logpath, char* segpath)
{
    /* read the log segments and apply them */
    int fd = Open(logpath, O_RDONLY);
    int data_fd = Open(segpath, O_RDWR); 

    struct stat st1, st2;
    fstat(fd, &st1);
    int log_len = st1.st_size;
    if (!log_len)
        return; /* if length of log is zero, skip it */

    char* logfile = (char*) Mmap(NULL, log_len, PROT_READ, MAP_SHARED, fd, 0);

    fstat(data_fd, &st2);
    int data_len = st2.st_size; 
    char* datafile = (char*) Mmap(NULL, data_len, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0); 

    Close(fd);
    Close(data_fd); 

    int pos = 0;
    while (pos < log_len) {
        int size = *(int*) (logfile + pos);
        pos += 4;
        int offset = *(int*) (logfile + pos); 
        pos += 4;

        /* skip header and transfer data */
        memcpy(datafile + offset + sizeof(int), logfile + pos, size);
        pos += size;
    }

    Munmap(logfile, log_len);
    Munmap(datafile, data_len);

    /* clear the content of log segment */
    remove(logpath);
    creat(logpath, S_IRWXU);
}

/*
 *  Wrappers for linux system calls
 */
void *Malloc(size_t size) 
{
    void *p;
    if ((p  = malloc(size)) == NULL)
        fprintf(stderr, "Malloc error\n");
    return p;
}

void Free(void *ptr) 
{
    free(ptr);
}

void *Mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) 
{
    void *ptr;

    if ((ptr = mmap(addr, len, prot, flags, fd, offset)) == ((void *) -1))
    fprintf(stderr, "Mmap error\n");
    return(ptr);
}

void Munmap(void *start, size_t length) 
{
    if (munmap(start, length) < 0)
    fprintf(stderr, "Munmap error\n");
}

int Open(const char *pathname, int flags) 
{
    int rc;
    if ((rc = open(pathname, flags))  < 0)
    fprintf(stderr, "Open error\n");
    return rc;
}

void Close(int fd) 
{
    int rc;
    if ((rc = close(fd)) < 0)
    fprintf(stderr, "Close error\n");
}

DIR *Opendir(const char *name) 
{
    DIR *dirp = opendir(name); 

    if (!dirp)
        fprintf(stderr, "opendir error\n");
    return dirp;
}

int Closedir(DIR *dirp) 
{
    int rc;
    if ((rc = closedir(dirp)) < 0)
        fprintf(stderr, "closedir error\n");
    return rc;
}

/* implementation internal data structures */
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

    if(l->front == NULL) {
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

    if (st->head == NULL) {
        st->head = st->tail = new_item;
    }
    else {
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

    while (current != st->tail) {
        if (current->key == key)
            return current->value;
        current = current->next;
    }
    if (current->key == key)
        return current->value;
    return NULL;
}

int ST_erase(ST_t* st, void* key)
{
    if (!st) return -1;
    item_t* current = st->head;

    if (current->key == key) { /* item to remove is head */
        item_t* erased = current;
        st->head = current->next;
        if (erased == st->tail)
            st->tail = NULL;
        free(erased); 
        st->N--;
        return 0;
    }
    else {
        while (current->next != st->tail) {
            if (current->next->key == key) {
                item_t* erased = current->next;
                current->next = erased->next;
                free(erased);
                st->N--; 
                return 0;
            }
            current = current->next;
        } 
        
        if (current->next->key == key) { /* item to remove is tail */ 
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
