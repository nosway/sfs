#!/bin/sh

dd if=/dev/zero of=vdisk bs=1M count=2
../tools/mkfs.sfs vdisk
