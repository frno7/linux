#!/bin/bash
# Simple brutal sh-scripts chain.
cd ../sources
if [ ! -e binutils-2.34.tar.xz ]
then
wget https://ftp.gnu.org/gnu/binutils/binutils-2.34.tar.xz || exit -1
fi
cd ../building
tar -Jxvf ../sources/binutils-2.34.tar.xz
cd binutils-2.34
mkdir build
cd build
../configure --prefix=/usr/local/ps2 --target=mipsr5900el-unknown-linux-gnu --enable-shared --enable-plugins --disable-werror || exit -1

make -j 4 || exit -1
make install || exit -1

sleep 2

cd ../../../scripts
sh 2_gcc_st1_for_glibc-2.13.sh
