#include "fs/operations.h"
#include "fs/state.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#define NUM_FILES (param.block_size / sizeof(dir_entry_t))
#define NUM_THREADS (NUM_FILES)

char const path[] = "/f1";

tfs_params param = {
    .max_inode_count = 64,
    .max_block_count = 1024,
    .max_open_files_count = 16,
    .block_size = 1024,
};

void* create_file(void* dataptr) {
    char* filename = (char*)dataptr;
    int f = tfs_open(filename, TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f)==0);
    return NULL;
}



int main() {
    assert(tfs_init(&param) != -1);
    pthread_t* threads = malloc(sizeof(pthread_t)*NUM_THREADS);

    char* data[param.max_inode_count];

    // setup unique file names
    for(int i=0;i<param.max_inode_count;i++){
        data[i] = malloc(3);
        assert(data[i]!=NULL);
        char temp[] = "/_";
        temp[1] = (char)(i+'A');
        memcpy(data[i], temp, sizeof(temp));
    }

    for(int i=0; i<NUM_THREADS;i++){
        pthread_create(threads+i, NULL, create_file, data[i]);
    }

    for(int i=0;i<NUM_THREADS;i++){
        pthread_join(threads[i], NULL);
    }

    // No more space for new files in inode table
    int fd = tfs_open("/nomorespace", TFS_O_CREAT);
    assert(fd==-1);


    free(threads);
    // Cleanup
    for(int i=0;i<param.max_inode_count;i++){
        free(data[i]);
    }

    assert(tfs_destroy()==0);
    printf("Successful test.\n");

    return 0;
}