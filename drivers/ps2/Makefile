ps2smap-objs			:= smap.o smap_mdio.o
ps2smaprpc-objs				:= smaprpc.o smaprpc_mdio.o
ps2pad-objs			:= pad.o
ps2cdvd-objs		:= cdvd.o cdvdaltdev.o cdvddev.o cdvdutil.o
ps2mc-objs			:= mc.o mcdir.o mcfile.o mcdev.o
ps2sd-objs			:= sd.o sdmixer.o sdcnv.o iopmem.o
ps2js-objs			:= joystick.o
mcfs-objs			:= mcfs/ps2mcfs.o

obj-$(CONFIG_PS2_ETHER_SMAP)	+= ps2smap.o
obj-$(CONFIG_PS2_ETHER_SMAPRPC)	+= ps2smaprpc.o
obj-$(CONFIG_SONY_PS2)			+= sysproc.o
obj-$(CONFIG_PS2_PAD)			+= ps2pad.o
obj-$(CONFIG_PS2_CDVD)			+= ps2cdvd.o
obj-$(CONFIG_PS2_SD)			+= ps2sd.o
obj-$(CONFIG_PS2_MC)			+= ps2mc.o
obj-$(CONFIG_PS2_PAD_JS)		+= ps2js.o
obj-$(CONFIG_PS2_AUDIO)			+= ps2audio.o

# Load module ps2mcfs after ps2mc. Don't change order in Makefile!
obj-y							+= mcfs/
