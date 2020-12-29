#!/bin/bash
# Simple brutal sh-scripts chain.
# Decided to read this one first? - that's right. Everything will be installed
# to /usr/local/ps2/mipsr5900el-unknown-linux-gnu . No VARIABLES=""
# prunings. BRUTAL. SCRIPTS. CHAIN.
# Also, no 'sudo' . And, obviously, you have to have libgmp,libmpfr,libmpc
# devels installed in your system.
# Chains are good to continue from the next place where the building stucks.
# In the end, the root of the system will be in /usr/local/ps2/mipsr5900el-unknown-linux-gnu
# so in the system we will just place a symlink to / in there.
# If there will be an issues while compiling - read the script with that package issue,
# maybe I have a comments how to solve them there.
echo -en "\033[36;1mFrom now on, you can go smoke / drink tea / etc., because this is not for a short period of time. But when everyting will be downloaded, go to the downloaded kernel source:\033[0m\n"
echo -en "\033[31;1mcd ../building/linux-ps2-v5.4\033[0m\n"
echo -en "\033[36;1mand install the headers of it by typing this command:\033[0m\n"
echo -en "\033[31;1mmake ARCH=mips headers_install INSTALL_HDR_PATH=/usr/local/ps2/mipsr5900el-unknown-linux-gnu\033[0m\n"
echo -en "\033[36;1mThen, after the headers will be installed, you may proceed with building a cross-compiler by typing these commands:\033[0m\n"
echo -en "\033[31;1mcd ../../scripts
sh 1_binutils.sh\033[0m\n"

cd ../building
git clone -b ps2-v5.4 --single-branch https://github.com/frno7/linux.git
mv linux linux-ps2-v5.4
ln -s linux-ps2-v5.4 linux

mkdir /usr/local/ps2
mkdir /usr/local/ps2/mipsr5900el-unknown-linux-gnu

#cd linux-ps2-v5.4
#make ARCH=mips headers_install INSTALL_HDR_PATH=/usr/local/ps2/mipsr5900el-unknown-linux-gnu

#cd ../../scripts
#sh 1_binutils.sh
