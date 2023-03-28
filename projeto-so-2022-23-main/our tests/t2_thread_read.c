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
#define READS_PER_THREAD 20
#define READ_WIDTH 500

#define BLOCK_SIZE (NUM_THREADS*READS_PER_THREAD*READ_WIDTH)

_Static_assert(READ_WIDTH>1, 
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




void* task(){
    char read_buff[READ_WIDTH];

    for(int i=0;i<READS_PER_THREAD;i++){
        assert(tfs_read(file_handle, read_buff, READ_WIDTH)==READ_WIDTH);
        for(int c=0;c<READ_WIDTH;c++){
            assert(read_buff[0]==read_buff[c]);
        }
        msleep(rand()%2);
    }

    return NULL;
}

int main() {
    srand((unsigned int)time(NULL));

    assert(tfs_init(&param) != -1);

    file_handle = tfs_open("/example", TFS_O_CREAT);
    assert(file_handle!=-1);

    char buffer[BLOCK_SIZE];

    for(int i=0;i<BLOCK_SIZE;i+=READ_WIDTH){
        memset(buffer+i, (char)(i/READ_WIDTH), READ_WIDTH);
    }

    assert(tfs_write(file_handle, buffer, BLOCK_SIZE) == BLOCK_SIZE);

    assert(tfs_close(file_handle)==0);

    file_handle = tfs_open("/example", 0);
    assert(file_handle!=-1);

    for(int i=0;i<NUM_THREADS;i++){
        pthread_create(threads+i, NULL, task, NULL);
    }

    for(int i=0;i<NUM_THREADS;i++){
        pthread_join(threads[i], NULL);
    }

    char temp[BLOCK_SIZE];

    assert(tfs_read(file_handle, temp, BLOCK_SIZE) == 0);

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