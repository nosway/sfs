sfs (simple file system for Linux)
=========

The source code of sfs is written by referring to  
aufs (https://github.com/krinkinmu/aufs), minix, and ext2 file systems.

On-disk file system layout

+-------------------------+<br>
|       Super Block       |<br>
+-------------------------+<br>
| Block Allocation Bitmap |<br>  
+-------------------------+<br>
| Inode Allocation Bitmap |<br> 
+-------------------------+<br>
|       Inode List        |<br> 
+-------------------------+<br>
|       Data Blocks       |<br> 
|  (including root dir.)  |<br>
+-------------------------+<br>

All on-disk metadata numbers are in little-endian order.

Current feaures
 - Basic file and directory operations
 - Max. length of filename = 60 bytes
 - The maximum file system size = 16TB
 - No extended attribute support


