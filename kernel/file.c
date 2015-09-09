#include <linux/fs.h>

const struct file_operations sfs_file_ops = {
	.llseek = generic_file_llseek,
#if 0   // original
	.read = new_sync_read,
	.read_iter = generic_file_read_iter,
#else
    .read = do_sync_read,
    .aio_read = generic_file_aio_read,
#endif
	.write = do_sync_write,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write
};
