SFS (simple file system for Linux)
=========

The file system "sfs" is helpful to understand Linux VFS and file system basics.<br> 
The source code of sfs was written by referring to  
aufs (https://github.com/krinkinmu/aufs), ext2 and minix file systems.

This code was written and tested for Linux kernel 3.15 or before.

# On-disk file system layout

- Super Block
- Block Allocation Bitmap
- Inode Allocation Bitmap
- Inode List
- Data Blocks (including root directory

All on-disk metadata numbers are in little-endian order.

# Current features

 - Basic file and directory operations
 - Max. length of filename = 60 bytes
 - The maximum file system size = 16TB
 - No extended attribute support

# How to build kernel module 

$ cd kernel<br>
$ make<br>

# How to build mkfs.sfs 

$ cd tools<br>
$ make<br>

# How to test
$ cd test<br>
$ ./prepare_vdisk.sh<br>
$ ./load_and_mount.sh<br>
$ ./umount_and_unload.sh<br>

