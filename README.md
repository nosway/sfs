sfs (simple file system for Linux)
=========

The source code of sfs is written by referring to  
aufs (https://github.com/krinkinmu/aufs), minix, and ext2 file systems.

On-disk file system layout

+-------------------------+
|       Super Block       |
+-------------------------+
| Block Allocation Bitmap |  
+-------------------------+
| Inode Allocation Bitmap | 
+-------------------------+
|       Inode List        | 
+-------------------------+
|       Data Blocks       | 
|  (including root dir.)  |
+-------------------------+

All on-disk metadata numbers are in little-endian order.

Current feaures
 - Basic file and directory operations
 - Max. length of filename = 60 bytes
 - The maximum file system size = 16TB
 - No extended attribute support


