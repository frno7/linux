#!/bin/bash
# Simple brutal sh-scripts chain.
if [ "$(echo $PATH | grep -o /usr/local/ps2/bin)" != "/usr/local/ps2/bin" ]
then
 echo -en "\033[36;1m There is no "/usr/local/ps2/bin" was added to \$PATH . Adding... \033[0m\n"
 export PATH=/usr/local/ps2/bin:$PATH
fi

cd ../building
if [ ! -e iopmod ]
then
git clone https://github.com/frno7/iopmod.git || exit -1
fi
cd iopmod

make CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- -j 4 || exit -1
# modules.irx supposed to be in the 'module' folder from now. Further, let's
# copy them to the appropriate kernel source initrd folder.
cp -rdf module/*.irx ../initramfs-ps2/lib/firmware/ps2/

sleep 2

cd ../../scripts
sh 9_vmlinux.sh
