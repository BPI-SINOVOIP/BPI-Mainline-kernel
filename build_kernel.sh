#!/bin/bash
TOPDIR=`pwd`
TOOLCHAIN=gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf
export PATH=$TOPDIR/toolchains/$TOOLCHAIN/bin:$PATH
cd linux-4.19
./build.sh
