#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "sfs.h"
#include "bitmap.h"

struct fs_config {
    int			fs_fd;
	uint64_t	fs_blocksize;
	uint64_t	fs_iam_blocks;
	uint64_t	fs_inode_blocks;
	uint64_t	fs_bam_blocks;
    uint64_t	fs_nblocks;
	uint64_t	fs_ninodes;
	uint64_t	fs_data_start;
};

struct fs_config cfg;

int read_block(int blk_no, void *block);
int write_block(int blk_no, void *block);
void *bc_read(int blk_no);
void bc_write(int blk_no, int sync);

#define BAM_BLOCK_START		1
#define IAM_BLOCK_START		(BAM_BLOCK_START+cfg.fs_bam_blocks)
#define INODE_LIST_START	(IAM_BLOCK_START+cfg.fs_iam_blocks)
#define DATA_BLOCK_START	(INODE_LIST_START+cfg.fs_inode_blocks)
#define INODES_PER_BLOCK	(SFS_BLOCK_SIZE/sizeof(struct sfs_inode))

int init_super_block()
{
	char buffer[SFS_BLOCK_SIZE];
	struct sfs_super_block *sb = (struct sfs_super_block *)buffer; 

	memset(buffer, 0, SFS_BLOCK_SIZE);

	sb->s_magic = SFS_MAGIC;
	sb->s_blocksize = cfg.fs_blocksize;
	sb->s_bam_blocks = cfg.fs_bam_blocks;
	sb->s_iam_blocks = cfg.fs_iam_blocks;
	sb->s_inode_blocks = cfg.fs_inode_blocks;
	sb->s_nblocks = cfg.fs_nblocks;
	sb->s_ninodes = cfg.fs_ninodes;
	
	write_block(SUPER_BLOCK_NO, buffer);

	return 0;
}

int min(int x, int y)
{
	return (x < y)? x : y;
}

int init_block_alloc_map()
{
	char buffer[SFS_BLOCK_SIZE];
	uint64_t *map = (uint64_t *) buffer;
	int i, block;
	int preallocated = cfg.fs_data_start; 

	block = BAM_BLOCK_START;
	for (i = 1; i <= cfg.fs_bam_blocks; i++) {
		if (preallocated > BITS_PER_BLOCK) { 
			memset(buffer, 0xff, SFS_BLOCK_SIZE);
			preallocated -= BITS_PER_BLOCK;
		} else {
			memset(buffer, 0, SFS_BLOCK_SIZE);
			if (preallocated) {
				bitmap_set(map, 0, preallocated);
				preallocated = 0;
			}
		}

		if (i == cfg.fs_bam_blocks) {	// last BAM
			if (cfg.fs_nblocks != cfg.fs_bam_blocks * BITS_PER_BLOCK) {
				int bits = (int) (cfg.fs_bam_blocks * BITS_PER_BLOCK - cfg.fs_nblocks);
				
				bitmap_set(map, BITS_PER_BLOCK-bits, bits);
			}	

		}
		write_block(block, buffer);
		block++;
	}

	return 0;
}

int init_inode_alloc_map()
{
	char buffer[SFS_BLOCK_SIZE];
	uint64_t *map = (uint64_t *) buffer;
	int i, block;
	int preallocated = 1; 

	block = IAM_BLOCK_START; 
	for (i = 1; i <= cfg.fs_iam_blocks; i++) {
		if (preallocated > BITS_PER_BLOCK) { 
			memset(buffer, 0xff, SFS_BLOCK_SIZE);
			preallocated -= BITS_PER_BLOCK;
		} else {
			memset(buffer, 0, SFS_BLOCK_SIZE);
			if (preallocated) {
				bitmap_set(map, 0, preallocated);
				preallocated = 0;
			}
		}

		if (i == cfg.fs_iam_blocks) {	// last IAM
			if (cfg.fs_ninodes != cfg.fs_iam_blocks * BITS_PER_BLOCK) {
				int bits = (int) (cfg.fs_iam_blocks * BITS_PER_BLOCK - cfg.fs_ninodes);
				
				bitmap_set(map, BITS_PER_BLOCK-bits, bits);
			}	

		}
		write_block(block, buffer);
		block++;
	}

	return 0;
}

int init_inode_list()
{
	char buffer[SFS_BLOCK_SIZE];
	int i, block;

	block = INODE_LIST_START; 

	memset(buffer, 0, SFS_BLOCK_SIZE);
	for (i = 1; i <= cfg.fs_inode_blocks; i++) {
		write_block(block, buffer);
		block++;
	}
	return 0;
}	
		
int read_block(int blk_no, void *block)
{
	lseek(cfg.fs_fd, blk_no * SFS_BLOCK_SIZE, SEEK_SET);
	return read(cfg.fs_fd, block, SFS_BLOCK_SIZE);
}

int write_block(int blk_no, void *block)
{
	lseek(cfg.fs_fd, blk_no * SFS_BLOCK_SIZE, SEEK_SET);
	return write(cfg.fs_fd, block, SFS_BLOCK_SIZE);
}

struct blk_cache {
	int		dirty;
	int		blk_no;
	char	block[SFS_BLOCK_SIZE];
	struct blk_cache *next; 
};

struct blk_cache *bc_head = NULL; 

void bc_insert(struct blk_cache *new)
{
	new->next = bc_head;
	bc_head = new;	
}

void bc_sync()
{
	struct blk_cache *p = bc_head;
	struct blk_cache *next;
	
	while (p) {
		next = p->next;
		if (p->dirty) {
			write_block(p->blk_no, p->block);
		}
		free(p);
		p = next;
	} 
	bc_head = NULL;
}

struct blk_cache *bc_find(int blk_no)
{
	struct blk_cache *p = bc_head;

	while (p) {
		if (p->blk_no == blk_no)
			break;
		p = p->next;	
	}
	return p;
}

void *bc_read(int blk_no)
{
	struct blk_cache *p;

	p = bc_find(blk_no);
	if (p) {
		return p->block;
	}

	p = malloc(sizeof(struct blk_cache));
	p->dirty = 0;
	p->blk_no = blk_no;
	p->next = NULL;
	read_block(blk_no, p->block);
	bc_insert(p);

	return p->block;
}	

void bc_write(int blk_no, int sync)
{
	struct blk_cache *p;

	p = bc_find(blk_no);
	if (p) {
		if (sync) {
			write_block(p->blk_no, p->block);
			p->dirty = 0;
		}
		else {
			p->dirty = 1;
		}
	}
}
	
uint32_t allocate_blk(int blocks)
{
	uint64_t *map; 
	uint64_t n;
	int i, block = BAM_BLOCK_START;

	for (i = 0; i < cfg.fs_bam_blocks; i++) {
		map = (uint64_t *) bc_read(block);
		n = bitmap_alloc_region(map, BITS_PER_BLOCK, 0, blocks);
		if (n == INVALID_NO) {
			block++;
		} else {
			n += i * BITS_PER_BLOCK;
			bc_write(block, 0);
			break;
		}
	}

	return n;
}

uint32_t allocate_inode() 
{
	uint64_t *map = (uint64_t *) bc_read(IAM_BLOCK_START);
	uint64_t n;

	n = bitmap_alloc_region(map, BITS_PER_BLOCK, SFS_ROOT_INO, 1);
	if (n == INVALID_NO) {
		printf("Error: cannot allocate inode\n");
	} else {
		bc_write(IAM_BLOCK_START, 0);
	}

	return n;
}

void free_inode(uint32_t ino) 
{
	uint64_t *map = (uint64_t *) bc_read(IAM_BLOCK_START);

	if (ino >= INODES_PER_BLOCK) return; 
	bitmap_free_region(map, ino, 1); 
	bc_write(IAM_BLOCK_START, 0);
}

struct sfs_inode *get_inode(uint32_t ino)
{
	struct sfs_inode *ino_list = (struct sfs_inode *) bc_read(INODE_LIST_START);
	if (ino >= INODES_PER_BLOCK) 
		return NULL;
	return &ino_list[ino];
}		

uint32_t new_inode(mode_t mode, int byte_size)
{
	uint32_t ino;
	struct sfs_inode *ip;
	int nblocks;

	nblocks = (byte_size + SFS_BLOCK_SIZE -1) / SFS_BLOCK_SIZE;  

	ino = allocate_inode();	
	if (ino == INVALID_NO) 
		return ino;

	ip = get_inode(ino);
	if (ip == NULL) {
		printf("Cannot read inode\n");
		exit(1);
	}
	
	ip->i_blkaddr[0] = allocate_blk(nblocks);
	if (ip->i_blkaddr[0] == INVALID_NO) {
		free_inode(ino);
		return INVALID_NO;
	}

	ip->i_size = 0;
	if (S_ISDIR(mode))
		ip->i_nlink = 2;
	else
		ip->i_nlink = 1;
	ip->i_uid = getuid();
	ip->i_gid = getgid();
	ip->i_mode = mode; 
	ip->i_ctime = time(NULL);
	ip->i_atime = ip->i_ctime;
	ip->i_mtime = ip->i_ctime;
	bc_write(INODE_LIST_START, 0);	 
	return ino;
}	
	
uint32_t ll_mkdir(int entries)
{
	if (!entries)
		entries = 64;
	return new_inode(S_IFDIR | 0755, entries * sizeof(struct sfs_dir_entry));
}	

int ll_write(uint32_t ino, char *data, int size);
int ll_read(uint32_t ino, char *data, int size);

void dump_inode(struct sfs_inode *ip);

void sfs_add_dir_entry(struct sfs_inode *ip, char *name, uint32_t new_ino)
{
	uint32_t left = SFS_BLOCK_SIZE - ip->i_size;	
	uint32_t blk_no;
	uint32_t offset;
	struct sfs_dir_entry *dp;

	if (!left) {
		printf("Error: no enough space for creating a directory entry\n");
		printf("name = %s, new_ino = %d\n", name, new_ino);
		dump_inode(ip);
		bc_sync();
		exit(1);
	}

	blk_no = ip->i_blkaddr[0] + (ip->i_size / SFS_BLOCK_SIZE); 
	offset = ip->i_size % SFS_BLOCK_SIZE; 
		
	dp = (struct sfs_dir_entry *) ((char *)bc_read(blk_no) + offset);	
	strncpy(dp->de_name, name, SFS_MAX_NAME_LEN - 1);
	dp->de_name[SFS_MAX_NAME_LEN - 1] = '\0';
	dp->de_inode = new_ino;	

	ip->i_size += sizeof(struct sfs_dir_entry);	
	bc_write(blk_no, 0);
}

void dump_inode(struct sfs_inode *ip)
{
	printf("ip->i_blkaddr[0] = %d\n", ip->i_blkaddr[0]);
	printf("ip->i_size = %d\n", ip->i_size);
	printf("ip->i_mode = 0x%x\n", ip->i_mode);
}

void make_rootdir()
{
	uint32_t ino;
	struct sfs_inode *ip;

	ino = ll_mkdir(0);
	if (ino == INVALID_NO) {
		printf("Create root dir error\n");
		bc_sync();
		exit(1);
	}
	ip = get_inode(ino);
	sfs_add_dir_entry(ip, ".", SFS_ROOT_INO);
	sfs_add_dir_entry(ip, "..", SFS_ROOT_INO);
	//sfs_add_dir_entry(ip, ".trash", ll_mkdir(0));
}

int main(int ac, char *av[])
{
    struct stat st;
	char *block;
	off_t size;

    if (ac < 2) {
        printf("error\n");
        exit(1);
    }
    cfg.fs_fd = open(av[1], O_RDWR);
    if (cfg.fs_fd < 0) {
        printf("file open error\n");
        exit(2);
    }

	size = lseek(cfg.fs_fd, 0, SEEK_END);

	// Initialize cfg
	cfg.fs_blocksize = SFS_BLOCK_SIZE;
    cfg.fs_nblocks = size / SFS_BLOCK_SIZE;
	cfg.fs_bam_blocks = (cfg.fs_nblocks+BITS_PER_BLOCK-1)/BITS_PER_BLOCK;
	cfg.fs_inode_blocks = (cfg.fs_nblocks/4)/INODES_PER_BLOCK;
	cfg.fs_ninodes = cfg.fs_inode_blocks * INODES_PER_BLOCK;
	cfg.fs_iam_blocks = (cfg.fs_ninodes+BITS_PER_BLOCK-1)/BITS_PER_BLOCK;
	cfg.fs_data_start = 1 + cfg.fs_bam_blocks + 
			cfg.fs_iam_blocks + cfg.fs_inode_blocks;

    printf("Device size = %Ld\n", (long long) size);
    printf("No. of blocks = %Ld\n", (long long) cfg.fs_nblocks);
    printf("BAM blocks = %Ld\n", (long long) cfg.fs_bam_blocks);
    printf("IAM blocks = %Ld\n", (long long) cfg.fs_iam_blocks);
    printf("inode blocks = %Ld\n", (long long) cfg.fs_inode_blocks);

	printf("Number of inodes = %Ld\n", (long long) cfg.fs_ninodes);

	printf("Data block starts at %Ld block\n", (long long) cfg.fs_data_start); 

	init_super_block(); 
	init_block_alloc_map();
	init_inode_alloc_map();
	init_inode_list();
	make_rootdir();
	
	bc_sync();
	printf("Device write complete\n");
    close(cfg.fs_fd);
}    

