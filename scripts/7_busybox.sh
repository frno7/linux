#!/bin/bash
# Simple brutal sh-scripts chain.
if [ "$(echo $PATH | grep -o /usr/local/ps2/bin)" != "/usr/local/ps2/bin" ]
then
 echo -en "\033[36;1m There is no "/usr/local/ps2/bin" was added to \$PATH . Adding... \033[0m\n"
 export PATH=/usr/local/ps2/bin:$PATH
fi

cd ../sources
if [ ! -e busybox-1.28.0.tar.bz2 ]
then
wget https://busybox.net/downloads/busybox-1.28.0.tar.bz2 || exit -1
fi
cd ../building
tar -jxvf ../sources/busybox-1.28.0.tar.bz2
cd busybox-1.28.0
cp ../../config-files/config-busybox.txt .config || exit -1
make oldconfig || exit -1
make -j 4 || exit -1
# According to the config-busybox-default.txt, everything is going to be installed
# to the _install folder here.
make install || exit -1

cd _install
mkdir {dev,lib,proc,sys,mnt,newroot}
mkdir lib/firmware
mkdir lib/firmware/ps2
cp ../../../config-files/"init-for-busybox.sh" init || exit -1
cp ../../../config-files/"actual_init-for-busybox.sh" sbin/init || exit -1

sleep 2

cd ../../
ln -s busybox-1.28.0/_install initramfs-ps2
cd ../scripts
sh 8_iopmod.sh
