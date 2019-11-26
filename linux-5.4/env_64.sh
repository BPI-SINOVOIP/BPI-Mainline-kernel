#!/bin/bash
export ARCH=arm64
#export CROSS_COMPILE=arm-linux-gnueabihf-
export CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_OUTPUT=output/bpi-64
mkdir -p $KBUILD_OUTPUT
#make bpi_64_defconfig
#make -j8 dtbs
#LOADADDR=0x40008000 make -j8 Image
#make -j8 modules
#make -j8 INSTALL_MOD_PATH=out modules_install

