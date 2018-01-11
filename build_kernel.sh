#!/bin/bash
TOPDIR=`pwd`
export PATH=$TOPDIR/toolchains/gcc-linaro-7.1.1-2017.08-x86_64_arm-linux-gnueabihf/bin:$PATH
cd linux-4.14
./build.sh
