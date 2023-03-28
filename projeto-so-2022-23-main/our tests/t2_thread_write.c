#include "operations.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#ifndef min
    #define min(a,b) ((a<b)?a:b)
#endif


// Config params
#define NUM_THREADS 10
#define WRITES_PER_THREAD 20
#define WRITE_WIDTH 500

#define BLOCK_SIZE (NUM_THREADS*WRITES_PER_THREAD*WRITE_WIDTH)

_Static_assert(WRITE_WIDTH>1, 
    "The test doesnt make sense for the macro WRITE_WIDTH <= 1!");

tfs_params param = {
    .max_inode_count = 64,
    .max_block_count = 1024,
    .max_open_files_count = 16,
    .block_size = BLOCK_SIZE,
};

int msleep(long msec);
pthread_t threads[NUM_THREADS];
int file_handle=-1;




void* task(void* input){
    int index = *((int*)input);

    char to_write[WRITE_WIDTH];
    memset(to_write, (char)('A'+(char)index), WRITE_WIDTH);

    for(int i=0;i<WRITES_PER_THREAD;i++){
        assert(tfs_write(file_handle, to_write, WRITE_WIDTH)==WRITE_WIDTH);
        msleep(rand()%2);
    }
    return NULL;
}

/*
 * Verifies that the same characters 
 * have been written without anything else inbetween
*/
void verify(){
    char read[BLOCK_SIZE];
    assert(tfs_read(file_handle, read, BLOCK_SIZE) == BLOCK_SIZE);
    for(int i=0;i<BLOCK_SIZE;i+=WRITE_WIDTH){
        for(int x=0;x<WRITE_WIDTH;x++){
            assert(read[i]==read[i+x]);
        }
    }
}

int main() {
    srand((unsigned int)time(NULL));

    assert(tfs_init(&param) != -1);

    file_handle = tfs_open("/example", TFS_O_CREAT);
    assert(file_handle!=-1);

    int vals[NUM_THREADS];
    for(int i=0;i<NUM_THREADS;i++) vals[i] = i;

    for(int i=0;i<NUM_THREADS;i++){
        pthread_create(threads+i, NULL, task, (void*)&vals[i]);
    }

    for(int i=0;i<NUM_THREADS;i++){
        pthread_join(threads[i], NULL);
    }

    assert(tfs_close(file_handle)==0);

    file_handle = tfs_open("/example", 0);
    assert(file_handle!=-1);

    verify();

    assert(tfs_close(file_handle)==0);

    assert(tfs_destroy() == 0);

    printf("Successful test.\n");

    return 0;
}

int msleep(long msec){
    struct timespec ts;
    int res;
    if (msec < 0){
        errno = EINVAL;
        return -1;
    }
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}