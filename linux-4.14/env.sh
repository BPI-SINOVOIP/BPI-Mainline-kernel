#!/bin/bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export KBUILD_OUTPUT=output/bpi-all
mkdir -p $KBUILD_OUTPUT
#make bpi_defconfig
#make -j8 dtbs
#LOADADDR=0x40008000 make -j8 uImage
#make -j8 modules
#make -j8 INSTALL_MOD_PATH=out modules_install

