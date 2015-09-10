#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vfs.h>

#include "sfs.h"

static void sfs_put_super(struct super_block *sb)
{
	struct sfs_sb_info *sbi = SFS_SB(sb);

	if (sbi) {
		int i;
		for (i = 0; i < sbi->s_bam_blocks; i++)
			brelse(sbi->s_bam_bh[i]);
		for (i = 0; i < sbi->s_iam_blocks; i++)
			brelse(sbi->s_iam_bh[i]);
		kfree(sbi->s_bam_bh);
		kfree(sbi);
	}
	sb->s_fs_info = NULL;
	pr_debug("sfs super block destroyed\n");
}

static inline void sfs_super_block_fill(struct sfs_sb_info *sbi,
			struct sfs_super_block const *dsb)
{
	sbi->s_magic = le32_to_cpu(dsb->s_magic);
	sbi->s_blocksize = le32_to_cpu(dsb->s_blocksize);
	sbi->s_bam_blocks = le32_to_cpu(dsb->s_bam_blocks);
	sbi->s_iam_blocks = le32_to_cpu(dsb->s_iam_blocks);
	sbi->s_inode_blocks = le32_to_cpu(dsb->s_inode_blocks);
	sbi->s_nblocks = le32_to_cpu(dsb->s_nblocks);
	sbi->s_ninodes = le32_to_cpu(dsb->s_ninodes);
	sbi->s_inodes_per_block = sbi->s_blocksize / sizeof(struct sfs_inode); 
	sbi->s_bits_per_block = 8*sbi->s_blocksize;
	sbi->s_dir_entries_per_block =
			sbi->s_blocksize / sizeof(struct sfs_dir_entry);
	sbi->s_inode_list_start = sbi->s_bam_blocks + sbi->s_iam_blocks + 1; 
	sbi->s_data_block_start = sbi->s_inode_list_start + sbi->s_inode_blocks;
}

static struct sfs_sb_info *sfs_super_block_read(struct super_block *sb)
{
	struct sfs_sb_info *sbi = (struct sfs_sb_info *)
			kzalloc(sizeof(struct sfs_sb_info), GFP_NOFS);
	struct sfs_super_block *dsb;
	struct buffer_head *bh;

	if (!sbi) {
		pr_err("sfs cannot allocate super block\n");
		return NULL;
	}

	bh = sb_bread(sb, SUPER_BLOCK_NO);
	if (!bh) {
		pr_err("cannot read super block\n");
		goto free_memory;
	}

	dsb = (struct sfs_super_block *)bh->b_data;
	sfs_super_block_fill(sbi, dsb);
	brelse(bh);

	if (sbi->s_magic != SFS_MAGIC) {
		pr_err("wrong magic number %lu\n",
			(unsigned long)sbi->s_magic);
		goto free_memory;
	}

	return sbi;

free_memory:
	kfree(sbi);
	return NULL;
}

static int sfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct sfs_sb_info *sbi = SFS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->s_nblocks - sbi->s_data_block_start;
	buf->f_bfree = sfs_count_free_blocks(sb);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_ninodes;
	buf->f_ffree = sfs_count_free_inodes(sb);
	buf->f_namelen = SFS_MAX_NAME_LEN;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static struct kmem_cache *sfs_inode_cache;

static struct inode *sfs_alloc_inode(struct super_block *sb)
{
	struct sfs_inode_info *si = (struct sfs_inode_info *)
				kmem_cache_alloc(sfs_inode_cache, GFP_KERNEL);

	if (!si)
		return NULL;

	return &si->vfs_inode;
}

static void sfs_destroy_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);

	pr_debug("destroying inode %lu\n", (unsigned long)inode->i_ino);
	kmem_cache_free(sfs_inode_cache, SFS_INODE(inode));
}

static void sfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, sfs_destroy_callback);
}

static void sfs_inode_init_once(void *p)
{
	struct sfs_inode_info *si = (struct sfs_inode_info *)p;

	inode_init_once(&si->vfs_inode);
}

static int sfs_inode_cache_create(void)
{
	sfs_inode_cache = kmem_cache_create("sfs_inode",
		sizeof(struct sfs_inode_info), 0,
		(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD), sfs_inode_init_once);

	if (sfs_inode_cache == NULL)
		return -ENOMEM;
	return 0;
}

static void sfs_inode_cache_destroy(void)
{
	rcu_barrier();
	kmem_cache_destroy(sfs_inode_cache);
	sfs_inode_cache = NULL;
}

static struct super_operations const sfs_super_ops = {
	.alloc_inode		= sfs_alloc_inode,
	.destroy_inode		= sfs_destroy_inode,
	.write_inode		= sfs_write_inode,
	.evict_inode		= sfs_evict_inode,
	.put_super		= sfs_put_super,
	.statfs			= sfs_statfs,
};

static int sfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct sfs_sb_info *sbi = sfs_super_block_read(sb);
	struct buffer_head **map;
	struct inode *root;
	unsigned long i, block;

	if (!sbi)
		return -EINVAL;

	sb->s_magic = sbi->s_magic;
	sb->s_fs_info = sbi;
	sb->s_op = &sfs_super_ops;
	sb->s_max_links = SFS_LINK_MAX;

	if (sb_set_blocksize(sb, sbi->s_blocksize) == 0) {
		pr_err("device does not support block size %lu\n",
			(unsigned long)sbi->s_blocksize);
		return -EINVAL;
	}

	if (!sbi->s_bam_blocks || !sbi->s_iam_blocks || !sbi->s_inode_blocks) {
		pr_err("Invalid meta: BAM(%ld), IAM(%ld), Inode list(%ld)\n",
			(long)sbi->s_bam_blocks, 
			(long)sbi->s_iam_blocks, 
			(long)sbi->s_inode_blocks);
		return -EINVAL;
	}	 

	map = kzalloc(sizeof(struct buffer_head *) * 
			(sbi->s_bam_blocks + sbi->s_iam_blocks), GFP_KERNEL);
	sbi->s_bam_bh = &map[0]; 
	sbi->s_iam_bh = &map[sbi->s_bam_blocks];

	block = 1;
	for (i = 0; i < sbi->s_bam_blocks; i++) {
		sbi->s_bam_bh[i] = sb_bread(sb, block);
		if (!sbi->s_bam_bh[i])
			goto error;
		block++;
	}  
	for (i = 0; i < sbi->s_iam_blocks; i++) {
		sbi->s_iam_bh[i] = sb_bread(sb, block);
		if (!sbi->s_iam_bh[i])
			goto error;
		block++;
	}  
	sbi->s_bam_last = 0;
	sbi->s_iam_last = 0;

	root = sfs_iget(sb, SFS_ROOT_INO);
	if (IS_ERR(root))
		return PTR_ERR(root);

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_err("sfs cannot create root\n");
		return -ENOMEM;
	}
	return 0;

error:
	for (i = 0; i < sbi->s_bam_blocks; i++)
		brelse(sbi->s_bam_bh[i]);
	for (i = 0; i < sbi->s_iam_blocks; i++)
		brelse(sbi->s_iam_bh[i]);
	kfree(map);
	return -EIO;	
}

static struct dentry *sfs_mount(struct file_system_type *type, int flags,
			char const *dev, void *data)
{
	struct dentry *entry = 
			mount_bdev(type, flags, dev, data, sfs_fill_super);

	if (IS_ERR(entry))
		pr_err("sfs mounting failed\n");
	else
		pr_debug("sfs mounted\n");
	return entry;
}

static struct file_system_type sfs_type = {
	.owner			= THIS_MODULE,
	.name			= "sfs",
	.mount			= sfs_mount,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV
};

static int __init init_sfs_fs(void)
{
	int ret = sfs_inode_cache_create();

	if (ret != 0) {
		pr_err("cannot create inode cache\n");
		return ret;
	}

	ret = register_filesystem(&sfs_type);
	if (ret != 0) {
		sfs_inode_cache_destroy();
		pr_err("cannot register filesystem\n");
		return ret;
	}

	pr_debug("sfs module loaded\n");

	return 0;
}

static void __exit exit_sfs_fs(void)
{
	int ret = unregister_filesystem(&sfs_type);

	if (ret != 0)
		pr_err("cannot unregister filesystem\n");

	sfs_inode_cache_destroy();

	pr_debug("sfs module unloaded\n");
}

module_init(init_sfs_fs);
module_exit(exit_sfs_fs);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("simple file system");
MODULE_AUTHOR("Taewoong Kim <taewoong.kim@gmail.com>");
