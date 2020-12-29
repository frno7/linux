#!/bin/sh

PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH

mount -t sysfs none /sys
mount -t tmpfs none /var
mount -t tmpfs none /tmp
mount -t proc none /proc
mkdir /dev/pts
mount -t devpts none /dev/pts
mount -t debugfs none /sys/kernel/debug

modprobe ps2fb mode_option=1920x1080p@50 mode_margin=+13+0

modprobe sif
modprobe iop-memory
modprobe iop-module
modprobe iop-irq
modprobe iop-fio
modprobe iop
modprobe iop-dev9

modprobe sd_mod

modprobe ohci-ps2
modprobe ums-usbat
modprobe usbhid
modprobe hid-generic

uname -a

echo "Welcome!"
sh

while :
do
	sleep 1
done
