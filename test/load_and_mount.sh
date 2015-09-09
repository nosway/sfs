#!/bin/sh

insmod ../kernel/sfs.ko
mount -o loop -t sfs vdisk /mnt
ls -al /mnt

