#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "betterassert.h"

#ifndef min
    #define min(a,b) (a<b?a:b)
#endif

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

tfs_params params;

int tfs_init(tfs_params const *params_ptr) {
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {

    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && strlen(name) + 1 <= MAX_FILE_NAME && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    // TODO: assert that root_inode is the root directory
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");
    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t* inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        if(inode->i_node_type == T_SYMLINK){
            SCOPED_RWLOCK_W(inode->rwlock);
            // Make sure that during the wait the inode hasnt become invalid
            if(!is_inum_taken(inum)) return -1;

            void *block = data_block_get(inode->i_data_block);
            char path[MAX_FILE_NAME];
            memset(path,0,MAX_FILE_NAME);
            memcpy(path, block, inode->i_size);

            return tfs_open(path, mode);
        }
        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                data_block_free(inode->i_data_block);
                inode->i_size = 0;
            }
        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            return -1; // no space in directory
        }

        offset = 0;
    } else {
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

/**
 * Creates symbolic links
 *
 * Input:
 *   - target: path of the file to link 
 *   - link_name: path of the link
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - Unvalid link path name
 *   - Target file was not found in the directory
 *   - Inode became invalid during the wait
 *   - Target path corresponds to a symbolic link
 *   - No space to allocate a new block
 *   - No space for a new entry in the directory
 */
int tfs_sym_link(char const *target, char const *link_name) {

    if(!valid_pathname(link_name)){
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    int inum_target = tfs_lookup(target, root_dir_inode);
    if(inum_target == -1){
        return -1;
    }

    int inum_sym = inode_create(T_SYMLINK);
    ALWAYS_ASSERT(inum_sym!=-1, "no space");

    inode_t *sym_inode = inode_get(inum_sym);

    SCOPED_RWLOCK_W(sym_inode->rwlock);
    // Make sure that during the wait the inode hasnt become invalid
    if(!is_inum_taken(inum_sym)) return -1;

    // Determine how many bytes to write
    size_t block_size = strlen(link_name)+1;
    
    int bnum = data_block_alloc();
    if (bnum == -1) {
        inode_delete(inum_sym);
        return -1; // no space
    }

    sym_inode->i_data_block = bnum;

    void *block = data_block_get(sym_inode->i_data_block);

    memcpy(block, target, block_size);

    sym_inode->i_size = block_size;

    int dir_entry = add_dir_entry(root_dir_inode,link_name+1,inum_sym);

    ALWAYS_ASSERT(dir_entry!=-1, "add_dir_entry");

    return 0;
}

/**
 * Creates hard links
 *
 * Input:
 *   - target: path of the file to link 
 *   - link_name: path of the link
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - Unvalid link path name
 *   - Target file was not found in the directory
 *   - Inode became invalid during the wait
 *   - Target path corresponds to a symbolic link
 *   - No space for a new entry in the directory
 */
int tfs_link(char const *target, char const *link_name) {

    if(!valid_pathname(link_name)){
        return -1;
    }
    
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    int inum = tfs_lookup(target, root_dir_inode);
    if(inum == -1) return -1;
    inode_t *target_inode = inode_get(inum);

    SCOPED_RWLOCK_W(target_inode->rwlock);
    // Make sure that during the wait the inode hasnt become invalid
    if(!is_inum_taken(inum)) return -1;

    if(target_inode->i_node_type==T_SYMLINK){
        return -1;
    }
    target_inode->hard_links ++;
    int dir_entry = add_dir_entry(root_dir_inode,link_name+1,inum);

    ALWAYS_ASSERT(dir_entry!=-1, "add_dir_entry");

    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    SCOPED_LOCK(file->mtx);
    if (file == NULL) {
        return -1; // invalid fd
    }

    file->of_inumber = -1;
    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");
    SCOPED_RWLOCK_W(inode->rwlock);
    // Make sure that during the wait the inode hasnt become invalid
    if(!is_inum_taken(file->of_inumber)) return -1;

    // Determine how many bytes to write
    size_t block_size = state_block_size();
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                return -1; // no space
            }

            inode->i_data_block = bnum;
        }

        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);

        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    // From the open file table entry, we get the inode
    inode_t* inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    SCOPED_RWLOCK_R(inode->rwlock);

    size_t offset, to_read;
    {
        SCOPED_LOCK(file->mtx);
        // Make sure that during the wait the inode hasnt become invalid
        if(!is_inum_taken(file->of_inumber)) return -1;
        // Determine how many bytes to read
        offset = file->of_offset;
        to_read = min(inode->i_size - file->of_offset, len);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + offset, to_read);
    }

    return (ssize_t)to_read;
}

/**
 * Erases files and links
 *
 * Input:
 *   - target: file or link path to erase
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - File or link was not found in the directory
 *   - The file is opened
 *   - Inode became invalid during the wait
 */
int tfs_unlink(char const *target) {
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    int inum = tfs_lookup(target, root_dir_inode);
    if(inum == -1){
        return -1;
    }
    if(is_file_open(inum)==true){
        return -1;
    }
    inode_t *inode = inode_get(inum);

    SCOPED_RWLOCK_W(inode->rwlock);
    // Make sure that during the wait the inode hasnt become invalid
    if(!is_inum_taken(inum)) return -1;
    
    inode->hard_links--;

    if(inode->hard_links == 0){
        inode_delete(inum);
    }

    int clear_dir = clear_dir_entry(root_dir_inode, target+1);
    ALWAYS_ASSERT(clear_dir!=-1, "clear_dir_entry");
    return 0;
}

/**
 * Imports the contents of an external file to a file inside of TecnicoFS
 *
 * Input:
 *   - source_path: source file path
 *   - dest_path: destination file path
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - Unvalid destination path name
 *   - Error opening or creating both files
 *   - Error writting the information
 */
int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    
    char buffer[BUFFER_SIZE];
    memset(buffer,0,BUFFER_SIZE);
    FILE *source_file;
    size_t bytes;
    ssize_t write;

    if(!valid_pathname(dest_path)){
        return -1;
    }

    source_file = fopen (source_path, "r");
    if(source_file == NULL){
        return -1;
    }

    int fhandle = tfs_open(dest_path, TFS_O_CREAT | TFS_O_TRUNC);
    if(fhandle == -1){
        ALWAYS_ASSERT(fclose(source_file) == 0, "fclose");
        return -1;
    }

    bytes = fread(buffer, 1, BUFFER_SIZE, source_file);
    while(bytes > 0){
        write = tfs_write(fhandle, buffer, bytes);
        if(write != bytes){
                ALWAYS_ASSERT(fclose(source_file) == 0, "fclose");
                ALWAYS_ASSERT(tfs_close(fhandle) == 0, "tfs_close"); 
            return -1;
        }

        bytes = fread(buffer, 1, BUFFER_SIZE, source_file);
    }

    ALWAYS_ASSERT(fclose(source_file) == 0, "fclose");
    ALWAYS_ASSERT(tfs_close(fhandle) == 0, "tfs_close"); 
    return 0;
}