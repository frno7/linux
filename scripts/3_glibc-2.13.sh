#!/bin/bash
# Simple brutal sh-scripts chain.
if [ "$(echo $PATH | grep -o /usr/local/ps2/bin)" != "/usr/local/ps2/bin" ]
then
 echo -en "\033[36;1m There is no "/usr/local/ps2/bin" was added to \$PATH . Adding... \033[0m\n"
 export PATH=/usr/local/ps2/bin:$PATH
fi

cd ../sources
if [ ! -e glibc-2.13.tar.xz ]
then
wget https://ftp.gnu.org/gnu/glibc/glibc-2.13.tar.xz || exit -1
fi
if [ ! -e glibc-ports-2.13.tar.gz ]
then
wget https://ftp.gnu.org/gnu/glibc/glibc-ports-2.13.tar.gz || exit -1
fi
cd ../building
tar -Jxvf ../sources/glibc-2.13.tar.xz
cd glibc-2.13
tar -zxvf ../../sources/glibc-ports-2.13.tar.gz
patch -p1 < ../../patches/3_glibc-2.13-cross_hacks-3.patch
mkdir build
cd build
echo "libc_cv_forced_unwind=yes" > config.cache
echo "libc_cv_c_cleanup=yes" >> config.cache
# To build the newest glibc, it is needed to have the compiler with libatomic,
# libstdc++. Those gcc libs are based on glibc, so building glibc-2.13
# first as a compromise.
# FPU is not optimized for nextgen PS2Linux yet. So using --without-fp
../configure mipsel-linux --prefix=/usr/local/ps2/mipsr5900el-unknown-linux-gnu --build=i686-pc-linux-gnu --host=mipsr5900el-unknown-linux-gnu --enable-shared --with-headers=/usr/local/ps2/mipsr5900el-unknown-linux-gnu/include --enable-add-ons --with-tls --with-__thread --without-fp --cache-file=config.cache || exit -1

make -j 4 || exit -1
make install || exit -1

sleep 2

cd ../../../scripts
sh 4_gcc_st2_on_glibc-2.13.sh
