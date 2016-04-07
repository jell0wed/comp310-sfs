#ifndef SFS_API_H
#define SFS_API_H

#include <sys/stat.h>

#define SFS_API_FILENAME    "/home/jeremiep/myfs.sfs"
#define SFS_API_BLOCK_SIZE  1024
#define SFS_API_NUM_BLOCKS  1024
#define SFS_MAGIC_NUMBER    0xACBD0005
#define SFS_INODE_TABLE_SIZE    3

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
    int* ptrs[12];
} inode;

typedef struct {
    int size;
    int allocated_cnt;
    inode* inodes;
} inode_table;

typedef struct {
    char* filename;
    char* extension;
    int inode_index;
} directory_entry;

typedef struct {
    int count;
    directory_entry* entries;
} directory;



#endif /* SFS_API_H */

