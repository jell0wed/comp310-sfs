#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"

// the number of disk block required to store the free block list
const int free_block_list_req_blocks = (int)ceil((float)SFS_API_NUM_BLOCKS / (float)SFS_API_BLOCK_SIZE);

// the maximum number of inodes 
const int max_inodes = (int)floor((float)(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE) / (float)sizeof(inode));

// the number of indirection datablock pointer per data black
const int indirection_datablock_count = (int)floor((float)(SFS_API_BLOCK_SIZE - sizeof(int)) / (float)sizeof(int));

superblock* sblock;
inode_table* itbl;
directory* root_dir;
file_descriptor_table* fdtbl;
char* free_block_list;

/*
 * Initializes the inode table data structure (in mem)
 */
void initialize_inode_table() {
    itbl = malloc(sizeof(inode_table));
    
    itbl->free_inodes = (char*)calloc(max_inodes, sizeof(char));
    itbl->inodes = (inode*)calloc(max_inodes, sizeof(inode));
    itbl->allocated_cnt = 0;
    itbl->size = max_inodes;
    
    for(int i = 0; i < max_inodes; i++) {
        itbl->free_inodes[i] = 0;
    }
}

/**
 * Initializes the file descriptor table data structure (in mem)
 */
void initialize_file_descriptor_table() {
    fdtbl = malloc(sizeof(file_descriptor_table));
    
    fdtbl->size = SFS_MAX_FDENTRIES;
    for(int i = 0; i < fdtbl->size; i++) {
        (fdtbl->entries[i]).in_use = 0;
    }
}

/**
 * Persists the free block list data structure on the disk
 * 
 * Basic algorithm:
 *   Free block list is stored as a (char) array of length = number of blocks
 */
void write_free_block_list() {
    char* free_block_buff = strdup(free_block_list);
    //memcpy(free_block_buff, free_block_list, SFS_API_BLOCK_SIZE);
    write_blocks(1 + SFS_INODE_TABLE_SIZE, free_block_list_req_blocks, &(free_block_list[0]));
    free(free_block_buff);
}

/**
 * Reads the free block list data structure from the disk to main memory
 * 
 * Basic algorithm:
 *   Free block list is stored as a (char) array of length = number of blocks
 *   Read as a whole block (no iteration)
 */
void read_free_block_list() {
    char* free_block_buff = malloc(free_block_list_req_blocks * SFS_API_BLOCK_SIZE);
    read_blocks(1 + SFS_INODE_TABLE_SIZE, free_block_list_req_blocks, free_block_buff);
    memcpy(free_block_list, free_block_buff, SFS_API_BLOCK_SIZE);
    
    free(free_block_buff);
}

/**
 * Persists the inode table data structure on the disk
 * 
 * Basic algorithm: 
 *   The inode table is stored on the disk with its inodes entries 
 *      Iterate over the free_inode table, for each used inode store it on disk
 *      at its corresponding position
 */
void write_inode_table() {
    char* inode_table_buff = malloc(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE);
    
    memcpy(inode_table_buff, (int*)&(itbl->size), sizeof(int));
    memcpy(inode_table_buff + sizeof(int), (int*)&(itbl->allocated_cnt), sizeof(int));
    memcpy(inode_table_buff + 2*sizeof(int), (char*)(itbl->free_inodes), max_inodes * sizeof(char));
    
    /*for(int i = 0; i < itbl->allocated_cnt; i++) {
        memcpy(inode_table_buff + sizeof(int) * 2 + max_inodes * sizeof(char) + i * sizeof(inode), (inode*)&(itbl->inodes[i]), sizeof(inode));
    }*/
    for(int i = 0; i < itbl->size; i++) {
        if(itbl->free_inodes[i] == 1) { // inode at index i is used, persist on disk
            memcpy(inode_table_buff + sizeof(int) * 2 + max_inodes * sizeof(char) + i * sizeof(inode), (inode*)&(itbl->inodes[i]), sizeof(inode));
        }
    }
    
    write_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
    free(inode_table_buff);
}

/**
 * Reads the inode table from the disk to main memory
 */
void read_inode_table() {
    if(itbl != 0) { free(itbl->inodes); free(itbl->free_inodes); free(itbl); }
    
    char* inode_table_buff = malloc(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE);
    read_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
    
    itbl = malloc(sizeof(inode_table));
    itbl->size = *((int*)inode_table_buff);
    itbl->allocated_cnt = *((int*)(inode_table_buff + sizeof(int)));
    
    itbl->free_inodes = (char*)calloc(itbl->size, sizeof(char));
    itbl->inodes = (inode*)calloc(itbl->size, sizeof(inode));
    
    memcpy(itbl->free_inodes, (void*)(inode_table_buff + 2*sizeof(int)), max_inodes * sizeof(char));
    memcpy(itbl->inodes, (void*)(inode_table_buff + 2*sizeof(int) + max_inodes * sizeof(char)), itbl->size * sizeof(inode));
    
    free(inode_table_buff);
}

/**
 * Main method to allocate block of data on the disk. This method makes sure to
 * flag block used by the allocation as used in the free block list as well as
 * persisting data from buffer in main memory.
 * @param start_block the disk block to start from
 * @param nblocks the number of block to write 
 * @param buff the actual buffer containing data
 */
void allocate_block(int start_block, int nblocks, char* buff) {
    write_blocks(start_block, nblocks, buff);
    
    for(int i = start_block; i < (start_block + nblocks); i++) {
        free_block_list[i] = 1;
    }
    
    write_free_block_list();
}

/**
 * Deallocate disk block while maintaining the free_block_list updated.
 * @param start_block start block to be deallocated
 * @param nblock the number of block to be deallocated
 */
void deallocate_block(int start_block, int nblock) {
    for(int i = start_block; i < (start_block + nblock); i++) {
        free_block_list[i] = 0;
    }
    
    write_free_block_list();
}

/**
 * Save the inode at a given index, maintain the free_inodes char array list
 * up to date
 * @param inode the inode to be stored
 * @param index the index
 */
void save_inode(inode* inode, int index) {
    itbl->inodes[index] = *inode;
    itbl->allocated_cnt++;
    itbl->free_inodes[index] = 1;
    
    write_inode_table();
}

/**
 * Finds contiguous blocks to be allocated for a desired length (in bytes).
 * 
 * Basic greedy algorithm: 
 *  - Starting from first block
 *    - check if there is n blocks available (from free block list)
 *    - if not: loop and start at start_index + len
 *    - return start_index with n as length
 * 
 * @param desired_len The desired buffer size to store (in bytes)
 * @param start_block Ptrs to the variable start_block return variable
 * @param len Ptrs to the variable len return variable
 * @return 1 if space found, -1 if no space found
 */
int find_free_space(int desired_len, int* start_block, int* len) {
    int num_blocks = (int)ceil((float)desired_len / (float)SFS_API_BLOCK_SIZE);
    for(int i = 0; i < SFS_API_NUM_BLOCKS; i++) {
        char i_isfree = free_block_list[i];
        if(i_isfree == 0) {
            char contiguous = 1;
            int j = i;
            while(contiguous && j < (i + num_blocks)) {
                char j_isfree = free_block_list[j];
                contiguous = j_isfree == 0 ? 1 : 0;
                j++;
            }
            
            if(contiguous) {
                *start_block = i;
                *len = num_blocks;
                return 1;
            }
        }
    }
    
    *start_block = -1;
    *len = -1;
    return -1;
}

/**
 * Finds the next available inode index
 * Returns the first free available inode index
 * @param inode_index Ptr to return variable
 * @return -1 if no space found , 1 if found
 */
int find_next_available_inode_index(int* inode_index) {
    for(int i = 0; i < max_inodes; i++) {
        if(itbl->free_inodes[i] == 0) { *inode_index = i; return 1; }
    }
    
    return -1;
}

/**
 * Finds the next available file descriptor index
 * Returns the first free available file descriptor index
 * @param fd_index Ptr to return variable
 * @return -1 if no fd entry avail, 1 if found
 */
int find_next_avail_fd_entry(int* fd_index) {
    for(int i = 0; i < fdtbl->size; i++) {
        if((fdtbl->entries[i]).in_use == 0) { *fd_index = i; return 1; }
    }
    
    return -11;
}

/**
 * Reads the root directory entry from the disk to the main memory
 * - Finds the root inode
 * - Reads the datablock contents from 0 to allocated_ptr (root directory could
 *   span over multiple blocks)
 * - For each root entry, read name & extension
 */
void read_root_dir() {
    if(root_dir != 0) { free(root_dir->entries); free(root_dir); }
    read_inode_table();
    inode* root_inode = &itbl->inodes[sblock->root_inode_no];
    
    // read the whole root directory block(s) in the buffer
    char* root_dir_buff = malloc(root_inode->allocated_ptr * SFS_API_BLOCK_SIZE);
    read_blocks(root_inode->ptrs[0], root_inode->allocated_ptr, (char*)root_dir_buff);
    
    root_dir = malloc(sizeof(directory));
    root_dir->count = *((int*)root_dir_buff);
    root_dir->entries = (directory_entry*)calloc(root_dir->count, sizeof(directory_entry));
    if(root_dir->entries == 0) { 
        return -1;
    }
    
    // for each entry, retrieve the filename, inode index and extension
    for(int i = 0; i < root_dir->count; i++) {
        memcpy(&((root_dir->entries[i]).inode_index), root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i, sizeof(int));
        memcpy((root_dir->entries[i]).filename, root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int), 16);
        memcpy((root_dir->entries[i]).extension, root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int) + 16, 3);
    }
    
    // free buffer
    free(root_dir_buff);
}

/**
 * Insert a directory_entry in the root directory, that is, update the root_directory
 * data structure and persists changes on the disk.
 * 
 * Basic Algorithm:
 *  - if directory is NOT big enough:
 *     - find free space with sufficent size
 *     - move the whole root dir to those blocks
 *     - update root inode
 *     - persists inode table
 *  - reallocate the entries array to contain one more element
 *  - append entry at the end of the entries
 *  - update entries count
 * 
 * @param entry the entry to insert in the root directory 
 */
void insert_root_dir(directory_entry entry) {
    int total_dir_size = sizeof(directory) - 1 + root_dir->count * sizeof(directory_entry);
    int total_dir_cap = (&itbl->inodes[sblock->root_inode_no])->allocated_ptr * SFS_API_BLOCK_SIZE;
    // check if the directory is big enough to insert the item
    if((total_dir_size + sizeof(directory_entry)) > total_dir_cap) {
        // reallocate a new set of block for the root directory and update inode
        int start_index, block_len;
        find_free_space(total_dir_size + sizeof(directory_entry), &start_index, &block_len);
        
        allocate_block(start_index, block_len, root_dir);
        (itbl->inodes[sblock->root_inode_no]).allocated_ptr = block_len;
        for(int i = 0; i < block_len; i++) {
            (itbl->inodes[sblock->root_inode_no]).ptrs[i] = start_index + i;
        }
        deallocate_block((itbl->inodes[sblock->root_inode_no]).ptrs[0], (itbl->inodes[sblock->root_inode_no]).allocated_ptr);
        write_inode_table();
    }
    
    read_root_dir(); // make sure we are up to date
    
    directory* new_root = malloc(sizeof(directory));
    new_root->count = root_dir->count + 1;
    // reallocate entries array
    new_root->entries = (directory_entry*)calloc(new_root->count, sizeof(directory_entry));
    for(int i = 0; i < root_dir->count; i++) { // copy old stuff
        memset((new_root->entries[i]).filename, '\0', sizeof((new_root->entries[i]).filename));
        memset((new_root->entries[i]).extension, '\0', sizeof((new_root->entries[i]).extension));
        
        strcpy((new_root->entries[i]).filename, (root_dir->entries[i]).filename);
        strcpy((new_root->entries[i]).extension, (root_dir->entries[i]).extension);
        (new_root->entries[i]).inode_index = (root_dir->entries[i]).inode_index;
    }
    
    // insert new entry at the end
    strcpy((new_root->entries[root_dir->count]).filename, entry.filename);
    strcpy((new_root->entries[root_dir->count]).extension, entry.extension);
    (new_root->entries[root_dir->count]).inode_index = entry.inode_index;
    
    root_dir = new_root;
    write_root_dir();
    
    read_root_dir();
}

void write_root_dir() {
    char* rootdir_buff = malloc((&itbl->inodes[sblock->root_inode_no])->allocated_ptr * SFS_API_BLOCK_SIZE);
    memcpy(rootdir_buff, root_dir, sizeof(int));
    for(int i = 0; i < root_dir->count; i++) {
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i, &((root_dir->entries[i]).inode_index), sizeof(int));
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int), (root_dir->entries[i]).filename, 16);
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int) + 16, (root_dir->entries[i]).extension, 3);
    
    }
    // persist root directory
    //memcpy(rootdir_buff, new_root, sizeof(int) + new_root->count * sizeof(directory_entry));
    write_blocks((itbl->inodes[sblock->root_inode_no]).ptrs[0], (itbl->inodes[sblock->root_inode_no]).allocated_ptr, rootdir_buff);
    free(rootdir_buff);
}

/**
 * Initialize the file system
 * Initializes the basic in-memory data structures as well as on-disk data structures.
 * - initialize inode_table
 * - initialize & load superblock
 * - initialize free block list
 * - initialize/read root directory
 * - initialize file descriptor table
 * @param fresh Should we start from scratch or not?
 */
void mksfs(int fresh) {
    free_block_list = calloc(SFS_API_BLOCK_SIZE, sizeof(char));
    if(fresh) {
        init_fresh_disk(SFS_API_FILENAME, SFS_API_BLOCK_SIZE, SFS_API_NUM_BLOCKS);
        
        initialize_inode_table();
        
        int root_inode_index;
        find_next_available_inode_index(&root_inode_index);
        
        // create the super block
        char* superblock_buff = malloc(SFS_API_BLOCK_SIZE);
        sblock = malloc(sizeof(superblock));
        sblock->magic = SFS_MAGIC_NUMBER;
        sblock->block_size = SFS_API_BLOCK_SIZE;
        sblock->fs_size = SFS_API_NUM_BLOCKS;
        sblock->inode_table_len = SFS_INODE_TABLE_SIZE;
        sblock->root_inode_no = root_inode_index;
        
        memcpy(superblock_buff, (void*)sblock, sizeof(superblock));    // magic
        write_blocks(0, 1, superblock_buff);
        free(superblock_buff);
        
        int offset = 0;
        free_block_list[offset] = (char)1; // space for superblock
        offset++;
        
        // allocate block for inode table
        for(int i = 0; i < SFS_INODE_TABLE_SIZE; i++) {
            free_block_list[offset + i] = (char)1;
        }
        offset += SFS_INODE_TABLE_SIZE;
        
        // allocate n block to free space management (bitvector)
        int free_block_list_req_blocks = (int)ceil((float)SFS_API_NUM_BLOCKS / (float)SFS_API_BLOCK_SIZE);
        for(int i = 0; i < free_block_list_req_blocks; i++) {
            free_block_list[(offset) + i] = (char)1;
        }
        offset += free_block_list_req_blocks;
        
        write_free_block_list();
        
        int start_block, nblocks;
        find_free_space(sizeof(directory), &start_block, &nblocks);
        
        directory* rootdir_buff = malloc(sizeof(nblocks * SFS_API_BLOCK_SIZE));
        rootdir_buff->count = 0;
        allocate_block(start_block, nblocks, (char*)rootdir_buff);
        free(rootdir_buff);
        
        inode root_inode;
        root_inode.mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        root_inode.size = 1;
        root_inode.ptrs[0] = start_block;
        root_inode.allocated_ptr = 1;
        
        save_inode(&root_inode, root_inode_index);
        
        read_root_dir();
    } else {
        init_disk(SFS_API_FILENAME, SFS_API_BLOCK_SIZE, SFS_API_NUM_BLOCKS);
        
        // read superblock
        char* superblock_buff = malloc(SFS_API_BLOCK_SIZE);
        read_blocks(0, 1, superblock_buff);
        sblock = malloc(sizeof(superblock));
        memcpy(sblock, superblock_buff, sizeof(superblock));
        free(superblock_buff);
        
        initialize_inode_table();
        read_free_block_list();
        read_inode_table();
        read_root_dir();
    }
    
    initialize_file_descriptor_table();
    return;
}

/**
 * Gets a pointer to a root directory entry for a given file name
 * @param filename The file name of the file
 * @return directory_entry* pointer to the file having this file name, 0 (null ptr) if not found
 */
directory_entry* get_file(char* filename) {
    read_root_dir();
    
    directory_entry* entry = 0;
    int directory_index = 0;
    while(entry == 0 && directory_index < root_dir->count) {
        char* namebuff = strdup((root_dir->entries[directory_index]).filename);
        int cmp = strcmp(filename, namebuff);
        if(cmp == 0) {
            entry = &root_dir->entries[directory_index];
        }
        
        free(namebuff);
        directory_index++;
    }
    
    return entry;
}

/**
 * Extracts the file extension from a given file name
 * @param filename Filename with extension
 * @return Extension of the file name
 */
const int extract_filename_ext(char* filename, char* ext_filename, char* ext_ext) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return -1;
    
    strncpy(ext_filename, filename, dot - filename);
    strncpy(ext_ext, dot + 1, 3);
    
    return 0;
}

/**
 * Create a file and insert it into the root directory
 * @param filename The file name to create
 * @return A pointer to the newly created file entry
 */
directory_entry* create_file(char* filename) {
    inode file_inode;
    file_inode.mode = S_IRWXU | S_IRWXG | S_IRWXO;
    file_inode.size = 0;
    file_inode.allocated_ptr = 0;
    file_inode.ind_block_ptr = -1;
    
    int inode_index;
    find_next_available_inode_index(&inode_index);
    
    save_inode(&file_inode, inode_index);
    
    directory_entry entry;
    entry.inode_index = inode_index;
    //extract_filename_ext(filename, entry.filename, entry.extension);
    strcpy(entry.filename, filename);
    //strcpy(entry.extension, ext);
    
    read_root_dir();
    insert_root_dir(entry);
    
    return get_file(filename);
}

int next_pos = 0;
/**
 * Gets the name of the next file in the root directory
 * This maintains a global variable that is incremented on each function call
 * @param fname The return buffer variable
 * @return 1 if file found, 0 if no more file in the directory
 */
int sfs_getnextfilename(char* fname) { // get the name of the next file in directory
    read_root_dir();
    
    if(next_pos >= root_dir->count) { 
        return 0;
    }
    
    char buff[1024];
    sprintf(buff, "%s", (root_dir->entries[next_pos]).filename);
    strcpy(fname, buff);
    next_pos++;
    return 1;
}

/**
 * Gets the file size of a given file name
 * @param path The filename of the file
 * @return File size in Bytes, -1 if file not found
 */
int sfs_getfilesize(const char* path) { // get the size of a given file
    read_root_dir();
    
    directory_entry* entry = 0;
    int directory_index = 0;
    while(entry == 0 && directory_index < root_dir->count) {
        char* namebuff = strdup((root_dir->entries[directory_index]).filename);
        int cmp = strcmp(path, namebuff);
        if(cmp == 0) {
            entry = &root_dir->entries[directory_index];
        }
        
        free(namebuff);
        directory_index++;
    }
    
    if(entry == 0) { return -1; }
    
    return (itbl->inodes[entry->inode_index]).size;
}

/**
 * Opens a file from the root directory and a file descriptor entry.
 * The file is created if it doesnot exists.
 * Will fail if the file is already opened (if a file descriptor entry exists)
 * 
 * Basic Algorithm:
 * - if file does NOT exist:
 *    - create file
 * - if file is already opened:
 *    - return -1
 * - find next avail. fd entry
 * - set it in use and initialize rw_ptr at the end of the file
 * - return fd entry
 * @param name Name of the file to open/create from root directory
 * @return A file descriptor entry index
 */
int sfs_fopen(char* name) {
    if(strlen(name) > SFS_MAX_FILENAME) { return -1; }
    
    directory_entry* file = get_file(name);
    if(!file) {
        file = create_file(name);
    }
    
    if(!file) { return -1; }
   
    // check if the file is already opened
    for(int i = 0; i < fdtbl->size; i++) {
        if((fdtbl->entries[i]).in_use == 1 && (fdtbl->entries[i]).inode_index == file->inode_index) {
            return -1;
        }
    }
    
    // create file descriptor entry
    int fd_index;
    if(!find_next_avail_fd_entry(&fd_index)) {
        // TODO : handle error -- no more file descriptor
    }
    
    (fdtbl->entries[fd_index]).in_use = 1;
    (fdtbl->entries[fd_index]).inode_index = file->inode_index;
    (fdtbl->entries[fd_index]).rw_ptr = (itbl->inodes[file->inode_index]).size;
    
    return fd_index;
}

/**
 * Given a file descriptor, closes the opened file.
 * 
 * Basic Algorithm:
 * - if fd does NOT exists:
 *   return -1
 * - close it
 * @param fdId the file descriptor index to close
 * @return 0 if closed, -1 if unable to close
 */
int sfs_fclose(int fdId) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    (fdtbl->entries[fdId]).in_use = 0;
    return 0;
}

/**
 * Write data to an opened file through a file descriptor index
 * 
 * Basic algorithm:
 * - If fd does not exists : return -1
 * - If fd is not opened : return -1
 * 
 * - If file is not empty: 
 *   - get data block according to rw_ptr (either direct data block or indirect)
 *   - position start writing from rw_ptr % block size at this block
 *   - write (block_size - rw_ptr % block size) len in order to fill up the first block
 *   - update the write len -= (block-size - rw_ptr % block_size)
 * 
 *  - if new block needed for subsequent block, allocate them and update the 
 *    inode (direct ptrs or indirect pointers)
 *  - copy data from buffer to disk datablock
 *  - update the inode table
 *  - return the total length written to disk (in bytes) 
 * @param fdId File descriptor to write to
 * @param buf The data buffer
 * @param len Length of data to write on disk
 * @return number of bytes written to disk
 */
int sfs_fwrite(int fdId, char* buf, int len) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    file_descriptor_entry* entry = &(fdtbl->entries[fdId]);
    indirection_block* indirection_block = 0;
    int total_written = 0;
    
    if((itbl->inodes[entry->inode_index]).allocated_ptr > 0) {
        int start_inode_ptr_idx = (int)floor((float)((fdtbl->entries[fdId]).rw_ptr) / (float)SFS_API_BLOCK_SIZE);
        int last_index = (fdtbl->entries[fdId]).rw_ptr % SFS_API_BLOCK_SIZE;
        if(start_inode_ptr_idx >= SFS_NUM_DIRECT_PTR) { // ie. has an indirection datablock
            // load indirection block
            indirection_block = malloc(SFS_API_BLOCK_SIZE);
            indirection_block->count = 0;
            indirection_block->ptrs = (int*)calloc(indirection_datablock_count, sizeof(int));
            
            char* indirection_block_buff = malloc(SFS_API_BLOCK_SIZE);
            read_blocks((itbl->inodes[entry->inode_index]).ind_block_ptr, 1, indirection_block_buff);
            
            memcpy(indirection_block, indirection_block_buff, sizeof(int));
            memcpy(indirection_block->ptrs, indirection_block_buff + sizeof(int), indirection_datablock_count * sizeof(int));
            
            free(indirection_block_buff);
        }
        
        if(last_index > 0) {
            int start_block = 0;
        
            if(start_inode_ptr_idx >= SFS_NUM_DIRECT_PTR) {
                start_block = indirection_block->ptrs[start_inode_ptr_idx - SFS_NUM_DIRECT_PTR];
            } else {
                start_block = (itbl->inodes[entry->inode_index]).ptrs[start_inode_ptr_idx/* - 1*/];
            }
            
            int fill_len = last_index + len > SFS_API_BLOCK_SIZE ? (SFS_API_BLOCK_SIZE - last_index) : len;

            // fill in last block
            char* block_buff = malloc(SFS_API_BLOCK_SIZE);
            read_blocks(start_block, 1, block_buff);
            memcpy(block_buff + last_index, buf, fill_len);
            // write last block
            write_blocks(start_block, 1, block_buff);
            free(block_buff);

            buf += fill_len;
            len -= fill_len;
            entry->rw_ptr += fill_len;
            total_written += fill_len;
        }
    }
    
    // allocate space for subsequent blocks
    int block_start, block_len;
    if(!find_free_space(len, &block_start, &block_len)) {
        printf("No more space left on device");
        return -1;
    }
    
    if(len > 0) {
        char* new_blocks_buff = malloc(block_len * SFS_API_BLOCK_SIZE);
        memset(new_blocks_buff, 0, block_len * SFS_API_BLOCK_SIZE);
        memcpy(new_blocks_buff, buf, len);
        allocate_block(block_start, block_len, new_blocks_buff);
        free(new_blocks_buff);
        
        // for each subsequent block, assign ptr index in the file inode
        // (creates the indirection data block if necessary)
        for(int i = 0; i < block_len; i++) {
            if((itbl->inodes[entry->inode_index]).allocated_ptr >= SFS_NUM_DIRECT_PTR) {
                // we now need an indirection block, create one and update inode
                if(indirection_block == 0) {
                    int ind_block_start, block_len;
                    if(!find_free_space(len, &ind_block_start, &block_len)) {
                        printf("No more space left on device (indirection data block)");
                        return -1;
                    }

                    indirection_block = malloc(SFS_API_BLOCK_SIZE);
                    indirection_block->count = 0;
                    indirection_block->ptrs = (int*)calloc(indirection_datablock_count, sizeof(int));

                    char* block_buff = malloc(SFS_API_BLOCK_SIZE);
                    memcpy(block_buff, indirection_block, sizeof(int));
                    memcpy(block_buff + sizeof(int), indirection_block->ptrs, indirection_datablock_count * sizeof(int));
                    allocate_block(ind_block_start, block_len, block_buff);
                    free(block_buff);
                    
                    (itbl->inodes[entry->inode_index]).ind_block_ptr = ind_block_start;
                }
                
                if(indirection_block->count >= indirection_datablock_count) {
                    return -1;
                }
                
                indirection_block->ptrs[indirection_block->count] = block_start + i;
                indirection_block->count++;
            } else {
                (itbl->inodes[entry->inode_index]).ptrs[(itbl->inodes[entry->inode_index]).allocated_ptr] = block_start + i;
                (itbl->inodes[entry->inode_index]).allocated_ptr++;
            }
        }

        entry->rw_ptr += len;
        total_written += len;
    }
    
    if(indirection_block != 0){
        char* block_buff = malloc(SFS_API_BLOCK_SIZE);
        memcpy(block_buff, indirection_block, sizeof(int));
        memcpy(block_buff + sizeof(int), indirection_block->ptrs, indirection_datablock_count * sizeof(int));
        write_blocks((itbl->inodes[entry->inode_index]).ind_block_ptr, block_len, block_buff);
        free(block_buff);
        
        free(indirection_block->ptrs);
        free(indirection_block);
    }
    
    (itbl->inodes[entry->inode_index]).size += total_written; // update file total file size
    
    write_inode_table(); // update the inode table
    return total_written;
}

/**
 * Given an opened file descriptor, update the rw_ptr to a given position
 * @param fdId The opened file descriptor index
 * @param loc The location to locate the rw_ptr
 * @return -1 fd does not exist/not in use, 0 if ok
 */
int sfs_fseek(int fdId, int loc) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    file_descriptor_entry* entry = &(fdtbl->entries[fdId]);
    
    entry->rw_ptr = loc;
    return 0;
}

/**
 * Reads an opened file descriptor and copy the data in the buffer passed in as
 * parameter
 * 
 * Basic Algorithm : 
 * - while there is still something to be read
 *    - find the relative data block index (rw_ptr % block_size)
 *    - if this index resides within an indirection block, use the ind table
 *    - read the data and place it in the buffer
 *    - increase the rw_ptr 
 *    - update the relative data block index 
 * 
 * @param fdId The opened file descriptor to the file
 * @param buf The buffer to return the data
 * @param len The length of data to read from the file
 * @return -1 if error, the length of data readed
 */
int sfs_fread(int fdId, char* buf, int len) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    file_descriptor_entry* entry = &(fdtbl->entries[fdId]);
    
    int read_len = len > (itbl->inodes[entry->inode_index]).size ? (itbl->inodes[entry->inode_index]).size : len;
    if(read_len < 0) {
        printf("filesize limit reached");
        return;
    }
    
    int before = entry->rw_ptr;
    int rel_start_block_index = (int)floor((float)entry->rw_ptr / (float)SFS_API_BLOCK_SIZE);
    int start_index = entry->rw_ptr % SFS_API_BLOCK_SIZE;
    int read = 0;
    
    indirection_block* ind_block = 0;
    while(read_len > 0) {
        int block_read_index = 0;
        
        if(rel_start_block_index >= SFS_NUM_DIRECT_PTR) {
            // rel read index lies within the indirection block
            if(ind_block == 0) {
                char* ind_block_buff = malloc(SFS_API_BLOCK_SIZE);
                read_blocks((itbl->inodes[entry->inode_index]).ind_block_ptr, 1, ind_block_buff);
                
                ind_block = malloc(SFS_API_BLOCK_SIZE);
                memcpy(&(ind_block->count), ind_block_buff, sizeof(int));
                
                ind_block->ptrs = calloc(ind_block->count, sizeof(int));
                memcpy(ind_block->ptrs, ind_block_buff + sizeof(int), ind_block->count * sizeof(int));
                free(ind_block_buff);
            }
            
            block_read_index = ind_block->ptrs[rel_start_block_index - SFS_NUM_DIRECT_PTR];
        } else {
            block_read_index = (itbl->inodes[entry->inode_index]).ptrs[rel_start_block_index];
        }
        
        //int to_read_len = start_index + read_len > SFS_API_BLOCK_SIZE ? SFS_API_BLOCK_SIZE - start_index : read_len;
        
        int to_read_len = start_index + read_len > SFS_API_BLOCK_SIZE ? SFS_API_BLOCK_SIZE - start_index : read_len;
        
        char* block_buff = malloc(SFS_API_BLOCK_SIZE);
        read_blocks(block_read_index, 1, block_buff);
        memcpy(buf + read, block_buff + start_index, to_read_len);
        free(block_buff);
        
        read += to_read_len;
        entry->rw_ptr += to_read_len;
        read_len -= to_read_len;
        start_index = 0;
        rel_start_block_index = (int)floor((float)entry->rw_ptr / (float)SFS_API_BLOCK_SIZE);
    }
    
    if(ind_block != 0) {
        free(ind_block);
    }
    //entry->rw_ptr += before;
    return read;
}

/**
 * Removes a file from the root directory
 * 
 * - check if the file eixsts
 * - return -1 if file does not exist
 * @param name the file name to be removed
 * @return 1 if file successfully removed, -1 if file not found
 */
int sfs_remove(char* name) {
    directory_entry* file = get_file(name);
    if(!file) {
        printf("File not found.");
        return -1;
    }
    
    for(int i = 0; i < (itbl->inodes[file->inode_index]).allocated_ptr; i++) {
        deallocate_block((itbl->inodes[file->inode_index]).ptrs[i], 1);
    }
    
    for(int i = 0 ; i < root_dir->count - 1; i++) {
        if(&root_dir->entries[i] >= file) {
            root_dir->entries[i] = root_dir->entries[i + 1];
        }
    }
    
    root_dir->count--;
    itbl->free_inodes[file->inode_index] = 0;
    write_inode_table();
    write_free_block_list();
    write_root_dir();
    read_root_dir();
    return 1;
}

/*int main() {
    mksfs(1);
    
    int fds[20];
    for(int i = 0; i < 20; i++) {
        char name[1024];
        sprintf(name, "file_%d", i);
        fds[i] = sfs_fopen(name);
        
        if(fds[i] > 0) {
            printf("Opened %s\n", name);
        }
        
    }
    
    mksfs(0);
    printf("Reopened sfs...");
    
    char namebuff[1024];
    while(sfs_getnextfilename(namebuff))
    {
        int size = sfs_getfilesize(namebuff);
        printf("file %s as file size = %d\n", namebuff, size);
    }
    
    
    
    
    return 0;
}*/