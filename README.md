![compilation workflow](https://github.com/frno7/linux/actions/workflows/compilation.yml/badge.svg)

# PlayStation 2 Linux kernel

This Linux kernel branch implements the [o32 ABI](https://www.linux-mips.org/wiki/MIPS_ABI_History) for the Sony [PlayStation 2](https://en.wikipedia.org/wiki/PlayStation_2).

```
# uname -mrs
Linux 5.4.221+ mips
# cat /proc/cpuinfo
system type		: Sony PlayStation 2
machine			: SCPH-37000 L
processor		: 0
cpu model		: R5900 V3.1
BogoMIPS		: 291.58
wait instruction	: no
microsecond timers	: yes
tlb_entries		: 48
extra interrupt vector	: yes
hardware watchpoint	: no
isa			: mips1 mips3
ASEs implemented	: toshiba-mmi
shadow register sets	: 1
kscratch registers	: 0
package			: 0
core			: 0
VCED exceptions		: not available
VCEI exceptions		: not available
```

## Frequently asked questions

The [wiki](https://github.com/frno7/linux/wiki) has [frequently asked questions about PlayStation 2 Linux](https://github.com/frno7/linux/wiki/Frequently-asked-questions-about-PlayStation-2-Linux).

## Building and installing

This kernel can be started directly from a USB flash drive, using for example uLaunchELF for the PlayStation 2. A special kernel loader is unnecessary.

The wiki has a guide on [building and installing PlayStation 2 Linux](https://github.com/frno7/linux/wiki/Installing-and-booting-PlayStation-2-Linux).

## PlayStation 2 Linux distributions

There is a [Gentoo live USB for the PlayStation 2](https://github.com/frno7/gentoo-mipsr5900el/wiki/Gentoo-live-USB-for-the-PlayStation-2).

## PlayStation 2 Linux emulation

[R5900 QEMU](https://github.com/frno7/qemu) can be used to emulate programs compiled for PlayStation 2 Linux.

## General README

Review the general [README](README) for further information on the Linux kernel.
