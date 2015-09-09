sfs (simple file system for Linux)
=========

The file system "sfs" is helpful to understand Linux VFS and file system basics. 
The source code of sfs was written by referring to  
aufs (https://github.com/krinkinmu/aufs), minix, and ext2 file systems.

On-disk file system layout

- Super Block
- Block Allocation Bitmap
- Inode Allocation Bitmap
- Inode List
- Data Blocks (including root dir.)

All on-disk metadata numbers are in little-endian order.

Current features
 - Basic file and directory operations
 - Max. length of filename = 60 bytes
 - The maximum file system size = 16TB
 - No extended attribute support


