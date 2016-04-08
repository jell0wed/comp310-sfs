#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"

const int free_block_list_req_blocks = (int)ceil((float)SFS_API_NUM_BLOCKS / (float)SFS_API_BLOCK_SIZE);
const int max_inodes = floor((float)(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE) / (float)sizeof(inode));
    
superblock* sblock;
inode_table* itbl;
directory* root_dir;
file_descriptor_table* fdtbl;
char free_block_list[SFS_API_NUM_BLOCKS];

void initialize_inode_table() {
    itbl = malloc(sizeof(inode_table) - 1 + max_inodes * sizeof(inode));
    
    itbl->free_inodes = malloc(max_inodes * sizeof(char));
    itbl->inodes = malloc(max_inodes * sizeof(inode));
    itbl->allocated_cnt = 0;
    itbl->size = max_inodes;
    
    for(int i = 0; i < max_inodes; i++) {
        itbl->free_inodes[i] = 0;
    }
}

void initialize_file_descriptor_table() {
    fdtbl = malloc(sizeof(file_descriptor_table));
    
    fdtbl->size = 1024;
    for(int i = 0; i < fdtbl->size; i++) {
        (fdtbl->entries[i]).in_use = 0;
    }
}

void write_free_block_list() {
    char* free_block_buff = malloc(free_block_list_req_blocks * SFS_API_BLOCK_SIZE);
    
    memcpy(free_block_buff, free_block_list, SFS_API_NUM_BLOCKS);
    write_blocks(1 + SFS_INODE_TABLE_SIZE - 1, free_block_list_req_blocks, free_block_buff);
    free(free_block_buff);
}

void write_inode_table() {
    char* inode_table_buff = malloc(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE);
    memcpy(inode_table_buff, (int*)&(itbl->size), sizeof(int));
    memcpy(inode_table_buff + sizeof(int), (int*)&(itbl->allocated_cnt), sizeof(int));
    memcpy(inode_table_buff + 2*sizeof(int), (char*)(itbl->free_inodes), max_inodes * sizeof(char));
    for(int i = 0; i < itbl->allocated_cnt; i++) {
        memcpy(inode_table_buff + sizeof(int) * 2 + max_inodes * sizeof(char) + i * sizeof(inode), (inode*)&(itbl->inodes[i]), sizeof(inode));
    }
    
    write_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
    free(inode_table_buff);
}

void read_inode_table() {
    char* inode_table_buff = malloc(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE);
    
    read_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
    itbl->size = *((int*)inode_table_buff);
    itbl->allocated_cnt = *((int*)(inode_table_buff + sizeof(int)));
    memcpy(itbl->free_inodes, (void*)(inode_table_buff + 2*sizeof(int)), max_inodes * sizeof(char));
    memcpy(itbl->inodes, (void*)(inode_table_buff + 2*sizeof(int) + max_inodes * sizeof(char)), itbl->size * sizeof(inode));
    
    free(inode_table_buff);
}

void allocate_block(int start_block, int nblocks, char* buff) {
    write_blocks(start_block, nblocks, buff);
    
    for(int i = start_block; i < (start_block + nblocks); i++) {
        free_block_list[i] = 1;
    }
    
    write_free_block_list();
}

void deallocate_block(int start_block, int nblock) {
    for(int i = start_block; i < (start_block + nblock); i++) {
        free_block_list[i] = 0;
    }
    
    write_free_block_list();
}

void save_inode(inode* inode, int index) {
    itbl->inodes[index] = *inode;
    itbl->allocated_cnt++;
    itbl->free_inodes[index] = 1;
    
    write_inode_table();
}

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
                return 0;
            }
        }
    }
    
    return -1;
}

int find_next_available_inode_index(int* inode_index) {
    for(int i = 0; i < max_inodes; i++) {
        if(itbl->free_inodes[i] == 0) { *inode_index = i; return 1; }
    }
    
    return 0;
}

int find_next_avail_fd_entry(int* fd_index) {
    for(int i = 0; i < fdtbl->size; i++) {
        if((fdtbl->entries[i]).in_use == 0) { *fd_index = i; return 1; }
    }
    
    return 0;
}


void read_root_dir() {
    if(root_dir != 0) { free(root_dir); }
    read_inode_table();
    inode* root_inode = &itbl->inodes[sblock->root_inode_no];
    
    char* root_dir_buff = malloc(root_inode->allocated_ptr * SFS_API_BLOCK_SIZE);
    read_blocks(root_inode->ptrs[0], root_inode->allocated_ptr, (char*)root_dir_buff);
    
    int* countbuff = malloc(sizeof(int));
    memcpy(countbuff, root_dir_buff, sizeof(int));
    
    root_dir = malloc(sizeof(directory));
    root_dir->count = *countbuff;
    root_dir->entries = (directory_entry*)calloc(root_dir->count, sizeof(directory_entry));
    for(int i = 0; i < root_dir->count; i++) {
        memcpy(&((root_dir->entries[i]).inode_index), root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i, sizeof(int));
        memcpy((root_dir->entries[i]).filename, root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int), 16);
        memcpy((root_dir->entries[i]).extension, root_dir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int) + 16, 3);
    }
    
    free(countbuff);
    free(root_dir_buff);
}

void insert_root_dir(directory_entry entry) {
    int total_dir_size = sizeof(directory) - 1 + root_dir->count * sizeof(directory_entry);
    int total_dir_cap = (&itbl->inodes[sblock->root_inode_no])->allocated_ptr * SFS_API_BLOCK_SIZE;
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
    
    read_root_dir();
    
    directory* new_root = malloc(sizeof(directory));
    new_root->count = root_dir->count + 1;
    new_root->entries = (directory_entry*)calloc(new_root->count, sizeof(directory_entry));
    for(int i = 0; i < root_dir->count; i++) {
        memset((new_root->entries[i]).filename, '\0', sizeof((new_root->entries[i]).filename));
        memset((new_root->entries[i]).extension, '\0', sizeof((new_root->entries[i]).extension));
        
        strcpy((new_root->entries[i]).filename, (root_dir->entries[i]).filename);
        strcpy((new_root->entries[i]).extension, (root_dir->entries[i]).extension);
        (new_root->entries[i]).inode_index = (root_dir->entries[i]).inode_index;
    }
    
    strcpy((new_root->entries[root_dir->count]).filename, entry.filename);
    strcpy((new_root->entries[root_dir->count]).extension, entry.extension);
    (new_root->entries[root_dir->count]).inode_index = entry.inode_index;
    
    char* rootdir_buff = malloc(total_dir_cap);
    memcpy(rootdir_buff, new_root, sizeof(int));
    for(int i = 0; i < new_root->count; i++) {
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i, &((new_root->entries[i]).inode_index), sizeof(int));
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int), (new_root->entries[i]).filename, 16);
        memcpy(rootdir_buff + sizeof(int) + (sizeof(int) + 16 + 3) * i + sizeof(int) + 16, (new_root->entries[i]).extension, 3);
    }
    //memcpy(rootdir_buff, new_root, sizeof(int) + new_root->count * sizeof(directory_entry));
    write_blocks((itbl->inodes[sblock->root_inode_no]).ptrs[0], (itbl->inodes[sblock->root_inode_no]).allocated_ptr, rootdir_buff);
    free(rootdir_buff);
    
    read_root_dir();
}

void mksfs(int fresh) {
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
        read_inode_table();
        read_root_dir();
    }
    
    initialize_file_descriptor_table();
    return;
}

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

directory_entry* create_file(char* filename, char* ext) {
    inode file_inode;
    file_inode.mode = S_IRWXU | S_IRWXG | S_IRWXO;
    file_inode.size = 0;
    file_inode.allocated_ptr = 0;
    
    int inode_index;
    find_next_available_inode_index(&inode_index);
    
    save_inode(&file_inode, inode_index);
    
    directory_entry entry;
    entry.inode_index = inode_index;
    strcpy(entry.filename, filename);
    strcpy(entry.extension, ext);
    
    read_root_dir();
    insert_root_dir(entry);
    
    return get_file(filename);
}


int next_pos = 0;
int sfs_getnextfilename(char* fname) { // get the name of the next file in directory
    read_root_dir();
    
    if(next_pos >= root_dir->count) { return 0; }
    
    char buff[1024];
    sprintf(buff, "%s", (root_dir->entries[next_pos]).filename);
    strcpy(fname, buff);
    next_pos++;
}

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

int sfs_fopen(char* name) {
    directory_entry* file = get_file(name);
    if(!file) {
        file = create_file(name, "");
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

void sfs_fclose(int fdId) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    (fdtbl->entries[fdId]).in_use = 0;
    return;
}

void sfs_fwrite(int fdId, char* buf, int len) {
    if(fdId >= fdtbl->size) { return -1; }
    if((fdtbl->entries[fdId]).in_use == 0) { return -1; }
    
    file_descriptor_entry* entry = &(fdtbl->entries[fdId]);
    
    if((itbl->inodes[entry->inode_index]).allocated_ptr > 0) {
        int start_block = (itbl->inodes[entry->inode_index]).ptrs[(int)floor((float)(fdtbl->entries[fdId]).rw_ptr / (float)SFS_API_BLOCK_SIZE)];
        int last_index = (fdtbl->entries[fdId]).rw_ptr % SFS_API_BLOCK_SIZE;
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
        
        (itbl->inodes[entry->inode_index]).size += fill_len;
    }
    
    int block_start, block_len;
    if(!find_free_space(len, &block_start, &block_len)) {
        // TODO : handle error
    }
    
    if(len > 0) {
        char* new_blocks_buff = malloc(block_len * SFS_API_BLOCK_SIZE);
        memcpy(new_blocks_buff, buf, len);
        allocate_block(block_start, block_len, new_blocks_buff);
        for(int i = 0; i < block_len; i++) {
            (itbl->inodes[entry->inode_index]).ptrs[(itbl->inodes[entry->inode_index]).allocated_ptr + i] = block_start + i;
            (itbl->inodes[entry->inode_index]).allocated_ptr++;
        }

        (itbl->inodes[entry->inode_index]).size += len;
        entry->rw_ptr += len;
    }
    
    write_inode_table();
}

int main() {
    mksfs(0);
    
    int fd = sfs_fopen("test");
    sfs_fwrite(fd, "hello", 6);
    
    char test[1024];
    while(sfs_getnextfilename(&test) != 0) {
        int size = sfs_getfilesize(test);
        printf("%s : size = %d\n", test, size);
    }
    
    return 0;
}
