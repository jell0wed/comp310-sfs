#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sfs_api.h"
#include "disk_emu.h"

const int free_block_list_req_blocks = (int)ceil((float)SFS_API_NUM_BLOCKS / (float)SFS_API_BLOCK_SIZE);
const int max_inodes = floor((float)(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE) / (float)sizeof(inode));
    
superblock* sblock;
inode_table* itbl;
char free_block_list[SFS_API_NUM_BLOCKS];

void initialize_inode_table() {
    itbl = malloc(sizeof(inode_table) - 1 + max_inodes * sizeof(inode));
    
    itbl->inodes = malloc(max_inodes * sizeof(inode));
    itbl->allocated_cnt = 0;
    itbl->size = max_inodes;
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
    for(int i = 0; i < itbl->allocated_cnt; i++) {
        memcpy(inode_table_buff + sizeof(int) * 2 + i * sizeof(inode), (inode*)&(itbl->inodes[i]), sizeof(inode));
    }
    
    write_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
    free(inode_table_buff);
}

void mksfs(int fresh) {
    if(fresh) {
        init_fresh_disk(SFS_API_FILENAME, SFS_API_BLOCK_SIZE, SFS_API_NUM_BLOCKS);
        
        // create the super block
        char* superblock_buff = malloc(SFS_API_BLOCK_SIZE);
        sblock = malloc(sizeof(superblock));
        sblock->magic = SFS_MAGIC_NUMBER;
        sblock->block_size = SFS_API_BLOCK_SIZE;
        sblock->fs_size = SFS_API_NUM_BLOCKS;
        sblock->inode_table_len = SFS_INODE_TABLE_SIZE;
        sblock->root_inode_no = 0;
        
        memcpy(superblock_buff, (void*)sblock, sizeof(superblock));    // magic
        write_blocks(0, 1, superblock_buff);
        free(superblock_buff);
        
        int offset = 0;
        free_block_list[offset] = 1; // space for superblock
        offset++;
        
        // allocate block for inode table
        for(int i = 0; i < SFS_INODE_TABLE_SIZE; i++) {
            free_block_list[offset + i] = 1;
            offset++;
        }
        
        // allocate n block to free space management (bitvector)
        int free_block_list_req_blocks = (int)ceil((float)SFS_API_NUM_BLOCKS / (float)SFS_API_BLOCK_SIZE);
        for(int i = 0; i < free_block_list_req_blocks; i++) {
            free_block_list[(offset) + i] = 1;
            offset++;
        }
        
        initialize_inode_table();
        write_free_block_list();
        
        inode root_inode;
        root_inode.mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        root_inode.size = 1;
        
        itbl->inodes[0] = root_inode;
        itbl->allocated_cnt++;
        write_inode_table();
    } else {
        init_disk(SFS_API_FILENAME, SFS_API_BLOCK_SIZE, SFS_API_NUM_BLOCKS);
        
        // read superblock
        char* superblock_buff = malloc(SFS_API_BLOCK_SIZE);
        read_blocks(0, 1, superblock_buff);
        sblock = superblock_buff;
        
        initialize_inode_table();
        char* inode_table_buff = malloc(SFS_INODE_TABLE_SIZE * SFS_API_BLOCK_SIZE);
        read_blocks(1, SFS_INODE_TABLE_SIZE, inode_table_buff);
        itbl->size = *((int*)inode_table_buff);
        itbl->allocated_cnt = *((int*)(inode_table_buff + sizeof(int)));
        memcpy(itbl->inodes, (void*)(inode_table_buff + 2*sizeof(int)), itbl->size * sizeof(inode));
    }
    return;
}

int main() {
    mksfs(0);
}
