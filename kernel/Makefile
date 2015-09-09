ifneq ($(KERNELRELEASE),)
obj-m := sfs.o
sfs-objs := super.o inode.o namei.o dir.o file.o bitmap.o itree.o
CFLAGS_super.o := -DDEBUG
CFLAGS_inode.o := -DDEBUG
CFLAGS_namei.o := -DDEBUG
CFLAGS_dir.o := -DDEBUG
CFLAGS_file.o := -DDEBUG
CFLAGS_bitmap.o := -DDEBUG
CFLAGS_itree.o := -DDEBUG
else
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
endif
