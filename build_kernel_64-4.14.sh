#!/bin/bash
TOPDIR=`pwd`
TOOLCHAIN=gcc-linaro-7.2.1-2017.11-x86_64_aarch64-linux-gnu
export PATH=$TOPDIR/toolchains/$TOOLCHAIN/bin:$PATH
cd linux-4.14
./build_64.sh
