#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "operations.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>

/**
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY, T_SYMLINK } inode_type;

/**
 * Inode
 */
typedef struct {
    inode_type i_node_type;
    pthread_rwlock_t rwlock;
    size_t i_size;
    int i_data_block;
    int hard_links;
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

/**
 * Open file entry (in open file table)
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
    pthread_mutex_t mtx;
} open_file_entry_t;

void rwlock_unlock(pthread_rwlock_t** lk);
void mutex_unlock(pthread_mutex_t** mt);

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b

#define INTERNAL_SCOPED_RWLOCK_R(rwlock, c)\
    pthread_rwlock_t* CONCAT(lock, c) __attribute__((cleanup(rwlock_unlock)))=&rwlock;\
    pthread_rwlock_rdlock(CONCAT(lock, c))

#define INTERNAL_SCOPED_RWLOCK_W(rwlock, c)\
    pthread_rwlock_t* CONCAT(lock, c) __attribute__((cleanup(rwlock_unlock)))=&rwlock;\
    pthread_rwlock_wrlock(CONCAT(lock, c))

#define INTERNAL_SCOPED_LOCK(mutex, c)\
    pthread_mutex_t* CONCAT(lock, c) __attribute__((cleanup(mutex_unlock)))=&mutex;\
    pthread_mutex_lock(CONCAT(lock, c))

/*
 * Helper macro to avoid more than one pthread_rwlock_t* with the same
 * name as defined in the macro above so just concat a counter 
 * variable so the names become: lock0, lock1, lock2, lock3, lock4
 * which avoids name collisions if multiple locks are defined in the same scope
 */
#define SCOPED_RWLOCK_R(rwlock) INTERNAL_SCOPED_RWLOCK_R(rwlock, __COUNTER__)
#define SCOPED_RWLOCK_W(rwlock) INTERNAL_SCOPED_RWLOCK_W(rwlock, __COUNTER__)

#define SCOPED_LOCK(mutex) INTERNAL_SCOPED_LOCK(mutex, __COUNTER__)

int state_init(tfs_params);
int state_destroy(void);

size_t state_block_size(void);

int inode_create(inode_type n_type);
void inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(inode_t *inode, char const *sub_name);
int add_dir_entry(inode_t *inode, char const *sub_name, int sub_inumber);
int find_in_dir(inode_t const *inode, char const *sub_name);

int data_block_alloc(void);
void data_block_free(int block_number);
void* data_block_get(int block_number);

int add_to_open_file_table(int inumber, size_t offset);
void remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);
bool is_file_open(int inumber);

int is_inum_taken(int inum);

#endif // STATE_H
