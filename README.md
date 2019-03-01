# PlayStation 2 Linux kernel

This Linux kernel branch implements the [o32 ABI](https://www.linux-mips.org/wiki/MIPS_ABI_History) for the Sony [PlayStation 2](https://en.wikipedia.org/wiki/PlayStation_2).

```
# uname -mrs
Linux 5.0.0+ mips
# cat /proc/cpuinfo
system type		: Sony PlayStation 2
machine			: Unknown
processor		: 0
cpu model		: R5900 V3.1
BogoMIPS		: 583.68
wait instruction	: no
microsecond timers	: yes
tlb_entries		: 48
extra interrupt vector	: yes
hardware watchpoint	: no
isa			: mips1 mips3
ASEs implemented	:
shadow register sets	: 1
kscratch registers	: 0
package			: 0
core			: 0
VCED exceptions		: not available
VCEI exceptions		: not available
```

## Building

A `mipsr5900el-unknown-linux-gnu` target GCC cross-compiler is recommended, with for example the command `make ARCH=mips CROSS_COMPILE=mipsr5900el-unknown-linux-gnu- vmlinux`. A provisional configuration is available in [arch/mips/configs/ps2_defconfig](arch/mips/configs/ps2_defconfig).

## Input/Output Processor (IOP) modules

The [Input/Output Processor (IOP)](https://en.wikipedia.org/wiki/PlayStation_2_technical_specifications#I/O_processor) is a MIPS R3000, or in later PlayStation 2 models a PowerPC 405GP emulating a MIPS R3000. This processor provides a number of kernel device driver services, for example handling of USB OHCI interrupts. These are implemented in IOP modules that the kernel loads as firmware.

This kernel requires the IOP modules `ps2dev9.irx` and `intrelay-direct.irx` to be installed in the `firmware/ps2` directory of the kernel source tree. The `CONFIG_EXTRA_FIRMWARE` configuration value contains a list of modules linked into the kernel.

## Installing

This kernel can be started directly from a USB flash drive, using for example uLaunchELF for the PlayStation 2. A special kernel loader is unnecessary.

## Limitations

This PlayStation 2 Linux kernel is work in progress, and only a minimum of devices for a useful system have been ported to the latest kernel version. A much older version [2.6.35.14 kernel](https://github.com/frno7/linux/tree/ps2-v2.6.35.14) from 2010 supports more devices etc.

## Graphics

The [Graphics Synthesizer](https://en.wikipedia.org/wiki/PlayStation_2_technical_specifications#Graphics_processing_unit) device driver supports 1920x1080p (noninterlace), with either an HDMI adapter or component video, as well as common video resolutions such as 720x576p, 720x480p, 640x512i, 640x480i, etc. Console text is accelerated with hardware texturing.

## USB devices

USB devices such as keyboards, flash drives, wifi, etc. are supported. However, due to memory limitations in the PlayStation 2 I/O Processor (IOP), some USB device drivers may need to be adjusted to work properly.

## Dependencies when compiling PlayStation 2 Linux programs

GCC 9.1 or later is recommended. Older GCC versions can be used if commit [d728eb9085d8](https://gcc.gnu.org/git/?p=gcc.git;a=commit;h=d728eb9085d8) (MIPS: Default to --with-llsc for the R5900 Linux target as well) is applied.

Glibc 2.29 or later is recommended. Older Glibc versions can be used if commit [8e3c00db16fc](https://sourceware.org/git/?p=glibc.git;a=commit;h=8e3c00db16fc) (MIPS: Use `.set mips2' to emulate LL/SC for the R5900 too) is applied.

GAS 2.32 or later is optional. It has the `-mfix-r5900` option to compile generic MIPS II and MIPS III code with the appropriate workaround for the R5900 short-loop hardware bug.

## PlayStation 2 Linux distributions

A modern [Gentoo Linux](https://gentoo.org/) can be compiled for the R5900 and the PlayStation 2. The Gentoo [sys-devel/crossdev](https://wiki.gentoo.org/wiki/Crossdev) package page and the [cross build environment guide](https://wiki.gentoo.org/wiki/Cross_build_environment) explain the details involving configuring for example a Gentoo profile and a corresponding overlay. Once those simple steps have been taken the command `# crossdev -s4 -t mipsr5900el-unknown-linux-gnu` can be used to obtain an R5900 cross toolchain as well as the basis of an R5900 Gentoo root filesystem in `/usr/mipsr5900el-unknown-linux-gnu`. As the guide explains, the R5900 base system can be built from scratch using the command `# mipsr5900el-unknown-linux-gnu-emerge -uva --keep-going @system`. The root filesystem can then be used with R5900 QEMU user mode emulation for further compilations and testing, or directly in Linux on PlayStation 2 hardware.

## PlayStation 2 Linux emulation

[R5900 QEMU](https://github.com/frno7/qemu) can be used to emulate programs compiled for PlayStation 2 Linux.

## General README

Read the general [README](README) for further information on the Linux kernel.
