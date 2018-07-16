#!/bin/bash
TOPDIR=`pwd`
TOOLCHAIN=gcc-linaro-7.2.1-2017.11-x86_64_arm-linux-gnueabihf
export PATH=$TOPDIR/toolchains/$TOOLCHAIN/bin:$PATH
cd linux-4.14
./build.sh
