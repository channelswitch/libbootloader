#!/bin/sh
ARCH=i386
as linux_trampoline_$ARCH.S -o linux_trampoline.o
if [ $? != 0 ]; then exit 1; fi
if [ $? != 0 ]; then exit 1; fi

cc -g *.c linux_trampoline_$ARCH.S `pkg-config --cflags --libs libudev blkid` -lpthread -laio

