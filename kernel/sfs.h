#ifndef __SFS_H__
#define __SFS_H__

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#else	/* __KERNEL__ */
#include <linux/types.h>

#define SFS_BLOCK_SIZE          4096
#define BITS_PER_BLOCK          (8*SFS_BLOCK_SIZE)
#endif	/* __KERNEL__ */

#define SFS_MAX_NAME_LEN		60	

static const unsigned long SFS_MAGIC = 0x20150825;

#define SUPER_BLOCK_NO			0
#define SFS_BAD_INO			0
#define SFS_ROOT_INO			1
#define SFS_LINK_MAX			32000

struct sfs_super_block {
	__le32	s_magic;
	__le32	s_blocksize;
	__le32	s_bam_blocks;
	__le32	s_iam_blocks;
	__le32	s_inode_blocks;
	__le32	s_nblocks;
	__le32	s_ninodes;
};

struct sfs_inode {
	__le16 i_mode;
	__le16 i_nlink;
	__le32 i_uid;
	__le32 i_gid;
	__le32 i_size;
	__le32 i_atime;
	__le32 i_mtime;
	__le32 i_ctime;
	__le32 i_blkaddr[9];	//	6+1+1+1
};

struct sfs_dir_entry {
	char de_name[SFS_MAX_NAME_LEN];
	__le32 de_inode;
};

#ifdef __KERNEL__
struct sfs_sb_info {
	__u32	s_magic;
	__u32	s_blocksize;
	__u32	s_bam_blocks;
	__u32	s_iam_blocks;
	__u32	s_inode_blocks;
	__u32	s_nblocks;
	__u32	s_ninodes;

	/* some additional info	*/
	__u32	s_inodes_per_block;
	__u32	s_bits_per_block;
	__u32	s_dir_entries_per_block;
	struct buffer_head **s_bam_bh;
	struct buffer_head **s_iam_bh;
	__u32	s_bam_last;
	__u32	s_iam_last;
	__u32	s_inode_list_start;
	__u32	s_data_block_start;
};

static inline struct sfs_sb_info *SFS_SB(struct super_block *sb)
{
	return (struct sfs_sb_info *)sb->s_fs_info;
}

struct sfs_inode_info {
	__le32			blkaddr[9];	
	struct inode	vfs_inode;
};

static inline struct sfs_inode_info *SFS_INODE(struct inode *inode)
{
	return container_of(inode, struct sfs_inode_info, vfs_inode);
}

extern const struct address_space_operations sfs_aops;
extern const struct inode_operations sfs_file_inode_ops;
extern const struct inode_operations sfs_dir_inode_ops;
extern const struct inode_operations sfs_symlink_inode_ops;
extern const struct file_operations sfs_file_ops;
extern const struct file_operations sfs_dir_ops;
int sfs_get_block(struct inode *inode, sector_t block,
            struct buffer_head *bh, int create);

int sfs_add_link(struct dentry *dentry, struct inode *inode);
ino_t sfs_inode_by_name(struct inode *dir, struct qstr *child);
int sfs_make_empty(struct inode *inode, struct inode *dir);
struct sfs_dir_entry *sfs_dotdot (struct inode *dir, struct page **p);
struct sfs_dir_entry *
sfs_find_entry(struct dentry *dentry, struct page **res_page);
int sfs_empty_dir(struct inode * inode);
void sfs_set_link(struct sfs_dir_entry *de, struct page *page,
	struct inode *inode);
int sfs_delete_entry(struct sfs_dir_entry *de, struct page *page);

unsigned sfs_blocks(loff_t size, struct super_block *sb);

unsigned long sfs_new_block(struct inode *inode, int *err);
struct inode *sfs_new_inode(struct inode *dir, umode_t mode, int *err);
void sfs_free_block(struct inode *inode, unsigned long block);

struct sfs_inode *sfs_get_inode(struct super_block *sb, ino_t ino,
	struct buffer_head **p);

void sfs_set_inode(struct inode *inode, dev_t rdev);
struct inode *sfs_iget(struct super_block *sb, unsigned long no);
int sfs_write_inode(struct inode *inode, struct writeback_control *wbc);
void sfs_truncate_inode(struct inode *inode);
void sfs_evict_inode(struct inode *inode);
void sfs_free_inode(struct inode *inode);

unsigned long sfs_count_free_blocks(struct super_block *sb);
unsigned long sfs_count_free_inodes(struct super_block *sb);
#endif	/* __KERNEL__ */

#endif /*__SFS_H__*/
