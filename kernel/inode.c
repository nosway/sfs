#include <linux/aio.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/slab.h>

#include "sfs.h"

static void sfs_inode_fill(struct sfs_inode_info *si,
			struct sfs_inode const *di)
{
	int i;

	si->vfs_inode.i_mode = le16_to_cpu(di->i_mode);
	si->vfs_inode.i_size = le32_to_cpu(di->i_size);
	si->vfs_inode.i_ctime.tv_sec = le32_to_cpu(di->i_ctime);
	si->vfs_inode.i_atime.tv_sec = le32_to_cpu(di->i_atime);
	si->vfs_inode.i_mtime.tv_sec = le32_to_cpu(di->i_mtime);
	si->vfs_inode.i_mtime.tv_nsec = si->vfs_inode.i_atime.tv_nsec =
				si->vfs_inode.i_ctime.tv_nsec = 0;
	i_uid_write(&si->vfs_inode, (uid_t)le32_to_cpu(di->i_uid));
	i_gid_write(&si->vfs_inode, (gid_t)le32_to_cpu(di->i_gid));
	set_nlink(&si->vfs_inode, le16_to_cpu(di->i_nlink));
	for (i = 0; i < 9; i++) 
		si->blkaddr[i] = di->i_blkaddr[i];
}

static inline sector_t sfs_inode_block(struct sfs_sb_info const *sbi,
			ino_t ino)
{
	return (sector_t)(sbi->s_inode_list_start + ino / sbi->s_inodes_per_block);
}

static size_t sfs_inode_offset(struct sfs_sb_info const *sbi,
			ino_t ino)
{
	return sizeof(struct sfs_inode) * (ino % sbi->s_inodes_per_block);
}

/*
 * The function that is called for file truncation.
 */
void sfs_truncate(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;
	sfs_truncate_inode(inode);
}

void sfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	if (!inode->i_nlink) {
		inode->i_size = 0;
		sfs_truncate(inode);
	}
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	if (!inode->i_nlink)
		sfs_free_inode(inode);
}

void sfs_set_inode(struct inode *inode, dev_t rdev)
{
	inode->i_mapping->a_ops = &sfs_aops;
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &sfs_file_inode_ops;
		inode->i_fop = &sfs_file_ops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &sfs_dir_inode_ops;
		inode->i_fop = &sfs_dir_ops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &sfs_symlink_inode_ops;
	} else { 
		inode->i_mapping->a_ops = NULL;
		init_special_inode(inode, inode->i_mode, rdev);
	}
}

struct inode *sfs_iget(struct super_block *sb, ino_t ino)
{
	struct sfs_sb_info *sbi = SFS_SB(sb);
	struct buffer_head *bh;
	struct sfs_inode *di;
	struct sfs_inode_info *si;
	struct inode *inode;
	size_t block, offset;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	si = SFS_INODE(inode);
	block = sfs_inode_block(sbi, ino);
	offset = sfs_inode_offset(sbi, ino);

	pr_debug("sfs reads inode %lu from %lu block with offset %lu\n",
		(unsigned long)ino, (unsigned long)block, (unsigned long)offset);

	bh = sb_bread(sb, block);
	if (!bh) {
		pr_err("cannot read block %lu\n", (unsigned long)block);
		goto read_error;
	}

	di = (struct sfs_inode *)(bh->b_data + offset);
	sfs_inode_fill(si, di);
	brelse(bh);

	sfs_set_inode(inode, new_decode_dev(le32_to_cpu(si->blkaddr[0]))); 

	unlock_new_inode(inode);

	return inode;

read_error:
	pr_err("sfs cannot read inode %lu\n", (unsigned long)ino);
	iget_failed(inode);

	return ERR_PTR(-EIO);
}

struct sfs_inode *sfs_get_inode(struct super_block *sb, ino_t ino,
	struct buffer_head **p)
{
    struct sfs_sb_info *sbi = SFS_SB(sb);
    size_t block, offset;

	block = sfs_inode_block(sbi, ino);
	offset = sfs_inode_offset(sbi, ino);
	*p = sb_bread(sb, block);
	if (!*p) {
		pr_debug("Unable to read inode block\n");
		return NULL;
	}
	return (struct sfs_inode *)((*p)->b_data + offset);
}

static struct buffer_head *sfs_update_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct sfs_inode_info *si;
	struct sfs_inode *di;
	int i;
	
	si = SFS_INODE(inode);
	di = sfs_get_inode(inode->i_sb, inode->i_ino, &bh);
	if (!di)
		return NULL;

	di->i_size = cpu_to_le32(inode->i_size);
	di->i_mode = cpu_to_le16(inode->i_mode);
	di->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	di->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	di->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	di->i_uid = cpu_to_le32(i_uid_read(inode));
	di->i_gid = cpu_to_le32(i_gid_read(inode));
	di->i_nlink = cpu_to_le16(inode->i_nlink); 
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		di->i_blkaddr[0] = cpu_to_le32(new_encode_dev(inode->i_rdev));
		for (i = 1; i < 9; i++)
			di->i_blkaddr[i] = cpu_to_le32(0);
	} else for (i = 0; i < 9; i++)
			di->i_blkaddr[i] = si->blkaddr[i];

	mark_buffer_dirty(bh);
	return bh;
}

int sfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err = 0;
	struct buffer_head *bh;

	pr_debug("Enter: sfs_write_inode (ino = %ld)\n", inode->i_ino);
	bh = sfs_update_inode(inode);
	if (!bh)
		return -EIO;
	
	if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			pr_debug("IO error syncing sfs inode 0x%lx\n", inode->i_ino);
			err = -EIO;
		}
	}
	pr_debug("Leave: sfs_write_inode (ino = %ld)\n", inode->i_ino);
	brelse(bh);
	return err;
}
	
static int 
sfs_writepage(struct page *page, struct writeback_control *wbc)
{
	pr_debug("sfs_writepage called\n");
	return block_write_full_page(page, sfs_get_block, wbc);
}

static int 
sfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	pr_debug("sfs_writepages called\n");
	return mpage_writepages(mapping, wbc, sfs_get_block);
}

static int sfs_readpage(struct file *file, struct page *page)
{
	pr_debug("sfs_readpage called\n");
	return mpage_readpage(page, sfs_get_block);
}

static int 
sfs_readpages(struct file *file, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	pr_debug("sfs_readpages called\n");
	return mpage_readpages(mapping, pages, nr_pages, sfs_get_block);
}

#if 0   // original
static ssize_t sfs_direct_io(int rw, struct kiocb *iocb,
			struct iov_iter *iter, loff_t off)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	return blockdev_direct_IO(rw, iocb, inode, iter, off, sfs_get_block);
}
#else
static ssize_t sfs_direct_io(int rw, struct kiocb *iocb,
			const struct iovec *iov, loff_t off, unsigned long nr_segs)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	pr_debug("sfs_direct_io called\n");
	return blockdev_direct_IO(rw, iocb, inode, iov, off, nr_segs, sfs_get_block);
}
#endif

static void sfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	pr_debug("sfs_write_failed called.\n");
	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		sfs_truncate(inode);
	}	
}

static int
sfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret;

	pr_debug("sfs_write_begin called\n");
	ret = block_write_begin(mapping, pos, len, flags, pagep, sfs_get_block);
    if (ret < 0)
		sfs_write_failed(mapping, pos + len);
	return ret;
}

static int sfs_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied,
		struct page *page, void *fsdata)
{
	int ret;

	pr_debug("sfs_write_end called\n");
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	
	mark_inode_dirty(mapping->host);

	if (ret < len)
		sfs_write_failed(mapping, pos + len);
	return ret;
}

static sector_t sfs_bmap(struct address_space *mapping, sector_t block)
{
	pr_debug("sfs_bmap called\n");
	return generic_block_bmap(mapping, block, sfs_get_block);
}

const struct address_space_operations sfs_aops = {
	.readpage = sfs_readpage,
	.readpages = sfs_readpages,
	.writepage = sfs_writepage,
	.writepages = sfs_writepages,
	.write_begin = sfs_write_begin,
	.write_end = sfs_write_end,
	.bmap = sfs_bmap, 
	.direct_IO = sfs_direct_io
};
