#!/bin/bash
nproc=$(cat /proc/cpuinfo | grep processor | wc -l )
echo nproc=$nproc
export ARCH=arm64
#export CROSS_COMPILE=arm-linux-gnueabihf-
export CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_OUTPUT=output/bpi-64
mkdir -p $KBUILD_OUTPUT
make bpi_64_defconfig
make -j$nproc dtbs
LOADADDR=0x40008000 make -j$nproc Image Image.gz
make -j$nproc modules
make -j$nproc INSTALL_MOD_PATH=out modules_install

#make headers_install
