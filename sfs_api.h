#ifndef SFS_API_H
#define SFS_API_H

#include <sys/stat.h>

#define SFS_API_FILENAME    "/home/jeremiep/myfs.sfs"
#define SFS_API_BLOCK_SIZE  1024
#define SFS_API_NUM_BLOCKS  2048
#define SFS_MAGIC_NUMBER    0xACBD0005
#define SFS_INODE_TABLE_SIZE    20
#define SFS_NUM_DIRECT_PTR  12
#define SFS_MAX_FILENAME    13
#define SFS_MAX_FDENTRIES   1024

void mksfs(int fresh);  // creates the file system

typedef struct {
    int magic;
    int block_size;
    int fs_size;
    int inode_table_len;
    int root_inode_no;
} superblock;

typedef struct {
    mode_t mode;
    int size;
    int allocated_ptr;
    int ind_block_ptr;
    int ptrs[SFS_NUM_DIRECT_PTR];
} inode;

typedef struct {
    int size;
    int allocated_cnt;
    char* free_inodes;
    inode* inodes;
} inode_table;

typedef struct {
    int count;
    int* ptrs;
} indirection_block;

typedef struct {
    int inode_index;
    char filename[SFS_MAX_FILENAME];
    char extension[3];
} directory_entry;

typedef struct {
    int count;
    directory_entry* entries;
} directory;

typedef struct {
    int in_use;
    int inode_index;
    int rw_ptr;
} file_descriptor_entry;

typedef struct {
    int size;
    file_descriptor_entry entries[1024];
} file_descriptor_table;



#endif /* SFS_API_H */

