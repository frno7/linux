#!/bin/bash
# Simple brutal sh-scripts chain.
if [ "$(echo $PATH | grep -o /usr/local/ps2/bin)" != "/usr/local/ps2/bin" ]
then
 echo -en "\033[36;1m There is no "/usr/local/ps2/bin" was added to \$PATH . Adding... \033[0m\n"
 export PATH=/usr/local/ps2/bin:$PATH
fi

# To be able to control init, edit files 'init' and 'sbin/init' in the initramfs-ps2
# folder. If you have the target-system, better keep only the some modules inside
# the kernel for just to be able to launch that target-system, but all modules in
# the appropriate folder of target-system.
cd ../building/linux-ps2-v5.4
# ps2_defconfig is a default config-file to make oldconfig with. Unless you
# have your own.
cp arch/mips/configs/ps2_defconfig .config

make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- oldconfig || exit -1
make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- -j 4 modules
cd ../initramfs-ps2
INITRAMFS_DIR=$(pwd)
cd ../linux-ps2-v5.4
make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- INSTALL_MOD_PATH=$INITRAMFS_DIR INSTALL_MOD_STRIP=1 modules_install
make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- -j 4 vmlinuz || exit -1
mv vmlinuz PS2Linux5.4.ELF
echo -en "\033[31;1mIf you will be editing init files, you have to clean the kernel build and rebuild it by manually typing these commands:
make clean
make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- -j 4 vmlinuz
mv vmlinuz PS2Linux5.4.ELF\033[0m\n"

sleep 2

echo -en "\033[36;1mCongratulations! You have successfully built PS2Linux kernel v5.4 in it's test variant. Now, you can proudly move PS2Linux5.4.ELF file to the USB flash stick, connect that stick to your PS2 and launch that file using uLaunchELF. Prepare your USB keyboard...\033[0m\n"

cd ../../scripts
#sh Till_the_next_time.sh
