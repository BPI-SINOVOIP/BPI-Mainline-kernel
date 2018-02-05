#!/bin/bash
TOPDIR=`pwd`
export PATH=$TOPDIR/toolchains/gcc-linaro-7.2.1-2017.11-x86_64_aarch64-linux-gnu/bin:$PATH
cd linux-4.14
./build_64.sh
