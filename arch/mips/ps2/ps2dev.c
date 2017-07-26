/*
 *  PlayStation 2 integrated device driver
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/tlbmisc.h>
#include <asm/cop2.h>

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/gsfunc.h>

#include "ps2dev.h"

#define MINOR_UNIT(x)	((MINOR(x) - ps2dev_minor) & 0x0f)
#define FN2MINOR(x) (x << 4)

#define PS2DEV_NR_OFDEVS 6
#define PS2DEV_COUNT    (PS2DEV_NR_OFDEVS * 16)
#define PS2MEM_FUNC	0
#define PS2EVENT_FUNC	1
#define PS2GS_FUNC	2
#define PS2VPU_FUNC	3
#define PS2IPU_FUNC	4
#define PS2SPR_FUNC	5

static struct class *ps2dev_class;
static int ps2dev_major = PS2DEV_MAJOR;
static int ps2dev_minor = 0;
void *ps2spr_vaddr;		/* scratchpad RAM virtual address */
static struct cdev ps2dev_cdev[PS2DEV_NR_OFDEVS];

/*
 *  common DMA device functions
 */

static int ps2dev_send_ioctl(struct dma_device *dev,
			     unsigned int cmd, unsigned long arg)
{
    struct ps2_packet pkt;
    struct ps2_packet *pkts;
    struct ps2_plist plist;
    struct ps2_pstop pstop;
    struct ps2_pchain pchain;
    int result;
    u32 ff, oldff;
    int val;
    wait_queue_t wait;

    switch (cmd) {
    case PS2IOC_SEND:
    case PS2IOC_SENDA:
	if (copy_from_user(&pkt, (void *)arg, sizeof(pkt)))
	    return -EFAULT;
	return ps2dma_send(dev, &pkt, cmd == PS2IOC_SENDA);
    case PS2IOC_SENDL:
	if (copy_from_user(&plist, (void *)arg, sizeof(plist)))
	    return -EFAULT;
	if (plist.num < 0)
	    return -EINVAL;
	if ((pkts = kmalloc(sizeof(struct ps2_packet) * plist.num, GFP_KERNEL) ) == NULL)
	    return -ENOMEM;
	if (copy_from_user(pkts, plist.packet, sizeof(struct ps2_packet) * plist.num)) {
	    kfree(pkts);
	    return -EFAULT;
	}
	result = ps2dma_send_list(dev, plist.num, pkts);
	kfree(pkts);
	return result;
    case PS2IOC_SENDQCT:
	return ps2dma_get_qct(dev, DMA_SENDCH, arg);
    case PS2IOC_SENDSTOP:
	ps2dma_stop(dev, DMA_SENDCH, &pstop);
	return copy_to_user((void *)arg, &pstop, sizeof(pstop)) ? -EFAULT : 0;
    case PS2IOC_SENDLIMIT:
	return ps2dma_set_qlimit(dev, DMA_SENDCH, arg);

    case PS2IOC_SENDC:
	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
	    return -EPERM;
	if (copy_from_user(&pchain, (void *)arg, sizeof(pchain)))
	    return -EFAULT;
	return ps2dma_send_chain(dev, &pchain);

    case PS2IOC_ENABLEEVENT:
	oldff = dev->intr_mask;
	if ((int)arg >= 0) {
	    spin_lock_irq(&dev->lock);
	    ff = dev->intr_mask ^ arg;
	    dev->intr_flag &= ~ff;
	    dev->intr_mask = arg;
	    spin_unlock_irq(&dev->lock);
	}
	return oldff;
    case PS2IOC_GETEVENT:
	spin_lock_irq(&dev->lock);
	oldff = dev->intr_flag;
	if ((int)arg > 0)
	    dev->intr_flag &= ~arg;
	spin_unlock_irq(&dev->lock);
	return oldff;
    case PS2IOC_WAITEVENT:
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&dev->empty, &wait);
	while (1) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    if (dev->intr_flag & arg) {
		spin_lock_irq(&dev->lock);
		oldff = dev->intr_flag;
		dev->intr_flag &= ~arg;
		spin_unlock_irq(&dev->lock);
		break;
	    }
	    schedule();
	    if (signal_pending(current)) {
		oldff = -ERESTARTSYS;	/* signal arrived */
		break;
	    }
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dev->empty, &wait);
	return oldff;
    case PS2IOC_SETSIGNAL:
	val = dev->sig;
	if ((int)arg >= 0)
	    dev->sig = arg;
	return val;
    }
    return -ENOIOCTLCMD;
}

static int ps2dev_recv_ioctl(struct dma_device *dev,
			     unsigned int cmd, unsigned long arg)
{
    struct ps2_packet pkt;
    struct ps2_packet *pkts;
    struct ps2_plist plist;
    struct ps2_pstop pstop;
    int result;

    switch (cmd) {
    case PS2IOC_RECV:
    case PS2IOC_RECVA:
	if (copy_from_user(&pkt, (void *)arg, sizeof(pkt)))
	    return -EFAULT;
	return ps2dma_recv(dev, &pkt, cmd == PS2IOC_RECVA);
    case PS2IOC_RECVL:
	if (copy_from_user(&plist, (void *)arg, sizeof(plist)))
	    return -EFAULT;
	if (plist.num < 0)
	    return -EINVAL;
	if ((pkts = kmalloc(sizeof(struct ps2_packet) * plist.num, GFP_KERNEL) ) == NULL)
	    return -ENOMEM;
	if (copy_from_user(pkts, plist.packet, sizeof(struct ps2_packet) * plist.num)) {
	    kfree(pkts);
	    return -EFAULT;
	}
	result = ps2dma_recv_list(dev, plist.num, pkts);
	kfree(pkts);
	return result;
    case PS2IOC_RECVQCT:
	return ps2dma_get_qct(dev, DMA_RECVCH, arg);
    case PS2IOC_RECVSTOP:
	ps2dma_stop(dev, DMA_RECVCH, &pstop);
	return copy_to_user((void *)arg, &pstop, sizeof(pstop)) ? -EFAULT : 0;
    case PS2IOC_RECVLIMIT:
	return ps2dma_set_qlimit(dev, DMA_RECVCH, arg);

    }
    return -ENOIOCTLCMD;
}

static ssize_t ps2dev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_packet pkt;
    int result;

    pkt.ptr = buf;
    pkt.len = count;
    if ((result = ps2dma_recv(dev, &pkt, 0)) < 0)
	return result;

    return count;
}

static ssize_t ps2dev_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_packet pkt;

    pkt.ptr = (char *)buf;
    pkt.len = count;
    return ps2dma_write(dev, &pkt, file->f_flags & O_NONBLOCK);
}

static int ps2dev_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    return ps2dma_get_qct(dev, DMA_SENDCH, 1);
}

int ps2dev_fault(struct vm_fault *vmf)
{
    return VM_FAULT_SIGBUS;
}

static struct vm_operations_struct ps2dev_vmops = {
    fault:	ps2dev_fault,
};


/*
 *  Graphics Synthesizer (/dev/ps2gs) driver
 */

static int vb_gssreg_num = 0;
static struct ps2_gssreg *vb_gssreg_p = NULL;

void ps2gs_sgssreg_vb(void)
{
    int i;

    if (vb_gssreg_num) {
	if (vb_gssreg_p != NULL) {
		for (i = 0; i < vb_gssreg_num; i++)
		    ps2gs_set_gssreg(vb_gssreg_p[i].reg, vb_gssreg_p[i].val);
		kfree(vb_gssreg_p);
		vb_gssreg_p = NULL;
	} else {
		printk("ps2gs: error: vb_gssreg_p is NULL.\n");
	}
	__asm__("":::"memory");
	vb_gssreg_num = 0;
    }
}

static long ps2gs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_gsinfo gsinfo;
    struct ps2_image image;
    struct ps2_gssreg gssreg;
    struct ps2_gsreg gsreg;
    struct ps2_gifreg gifreg;
    struct ps2_screeninfo screeninfo;
    struct ps2_crtmode crtmode;
    struct ps2_display display;
    struct ps2_dispfb dispfb;
    struct ps2_pmode pmode;
    struct ps2_sgssreg_vb sgssreg_vb;
    int val;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_GSINFO:
	gsinfo.size = GSFB_SIZE;
	return copy_to_user((void *)arg, &gsinfo, sizeof(gsinfo)) ? -EFAULT : 0;
    case PS2IOC_GSRESET:
	val = arg;
	ps2gs_reset(val);
	return 0;

    case PS2IOC_LOADIMAGE:
    case PS2IOC_LOADIMAGEA:
	if (copy_from_user(&image, (void *)arg, sizeof(image)))
	    return -EFAULT;
	return ps2gs_loadimage(&image, dev, cmd == PS2IOC_LOADIMAGEA);
    case PS2IOC_STOREIMAGE:
	if (copy_from_user(&image, (void *)arg, sizeof(image)))
	    return -EFAULT;
	return ps2gs_storeimage(&image, dev);

    case PS2IOC_SGSSREG:
	if (copy_from_user(&gssreg, (void *)arg, sizeof(gssreg)))
	    return -EFAULT;
	if (ps2gs_set_gssreg(gssreg.reg, gssreg.val) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GGSSREG:
	if (copy_from_user(&gssreg, (void *)arg, sizeof(gssreg)))
	    return -EFAULT;
	if (ps2gs_get_gssreg(gssreg.reg, &gssreg.val) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &gssreg, sizeof(gssreg)) ? -EFAULT : 0;
    case PS2IOC_SGSREG:
	if (copy_from_user(&gsreg, (void *)arg, sizeof(gsreg)))
	    return -EFAULT;
	if (ps2gs_set_gsreg(gsreg.reg, gsreg.val) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_SGIFREG:
	if (copy_from_user(&gifreg, (void *)arg, sizeof(gifreg)))
	    return -EFAULT;
	if (gifreg.reg == PS2_GIFREG_CTRL ||
	    gifreg.reg == PS2_GIFREG_MODE)
	    SET_GIFREG(gifreg.reg, gifreg.val);
	else
	    return -EINVAL;
	return 0;
    case PS2IOC_GGIFREG:
	if (copy_from_user(&gifreg, (void *)arg, sizeof(gifreg)))
	    return -EFAULT;
	if (gifreg.reg == PS2_GIFREG_STAT ||
	    (gifreg.reg >= PS2_GIFREG_TAG0 && gifreg.reg <= PS2_GIFREG_P3TAG)) {
	    if (gifreg.reg != PS2_GIFREG_STAT)
		SET_GIFREG(PS2_GIFREG_CTRL, 1 << 3);
	    gifreg.val = GIFREG(gifreg.reg);
	    if (gifreg.reg != PS2_GIFREG_STAT)
		SET_GIFREG(PS2_GIFREG_CTRL, 0);
	} else {
	    return -EINVAL;
	}
	return copy_to_user((void *)arg, &gifreg, sizeof(gifreg)) ? -EFAULT : 0;

    case PS2IOC_SSCREENINFO:
	if (copy_from_user(&screeninfo, (void *)arg, sizeof(screeninfo)))
	    return -EFAULT;
	if (ps2gs_screeninfo(&screeninfo, NULL) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GSCREENINFO:
	if (ps2gs_screeninfo(NULL, &screeninfo) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &screeninfo, sizeof(screeninfo)) ? -EFAULT : 0;
    case PS2IOC_SCRTMODE:
	if (copy_from_user(&crtmode, (void *)arg, sizeof(crtmode)))
	    return -EFAULT;
	if (ps2gs_crtmode(&crtmode, NULL) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GCRTMODE:
	if (ps2gs_crtmode(NULL, &crtmode) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &crtmode, sizeof(crtmode)) ? -EFAULT : 0;
    case PS2IOC_SDISPLAY:
	if (copy_from_user(&display, (void *)arg, sizeof(display)))
	    return -EFAULT;
	if (ps2gs_display(display.ch, &display, NULL) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GDISPLAY:
	if (copy_from_user(&display, (void *)arg, sizeof(display)))
	    return -EFAULT;
	if (ps2gs_display(display.ch, NULL, &display) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &display, sizeof(display)) ? -EFAULT : 0;
    case PS2IOC_SDISPFB:
	if (copy_from_user(&dispfb, (void *)arg, sizeof(dispfb)))
	    return -EFAULT;
	if (ps2gs_dispfb(dispfb.ch, &dispfb, NULL) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GDISPFB:
	if (copy_from_user(&dispfb, (void *)arg, sizeof(dispfb)))
	    return -EFAULT;
	if (ps2gs_dispfb(dispfb.ch, NULL, &dispfb) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &dispfb, sizeof(dispfb)) ? -EFAULT : 0;
    case PS2IOC_SPMODE:
	if (copy_from_user(&pmode, (void *)arg, sizeof(pmode)))
	    return -EFAULT;
	if (ps2gs_pmode(&pmode, NULL) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GPMODE:
	if (ps2gs_pmode(NULL, &pmode) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &pmode, sizeof(pmode)) ? -EFAULT : 0;

    case PS2IOC_DPMS:
	val = arg;
	if (ps2gs_setdpms(val) < 0)
	    return -EINVAL;
	return 0;

    case PS2IOC_SGSSREG_VB:
	if (vb_gssreg_num)
	    return -EBUSY;
	if (copy_from_user(&sgssreg_vb, (void *)arg, sizeof(sgssreg_vb)))
	    return -EFAULT;
	if (sgssreg_vb.num <= 0)
	    return -EINVAL;
	if ((vb_gssreg_p = kmalloc(sizeof(struct ps2_gssreg) * sgssreg_vb.num, GFP_KERNEL)) == NULL)
	    return -ENOMEM;
	if (copy_from_user(vb_gssreg_p, sgssreg_vb.gssreg, sizeof(struct ps2_gssreg) * sgssreg_vb.num)) {
	    kfree(vb_gssreg_p);
	    return -EFAULT;
	}
	__asm__("":::"memory");
	vb_gssreg_num = sgssreg_vb.num;
	return 0;
    }
    return -EINVAL;
}

static void ps2gif_reset(void)
{
    int apath;

    apath = (GIFREG(PS2_GIFREG_STAT) >> 10) & 3;
    SET_GIFREG(PS2_GIFREG_CTRL, 0x00000001);	/* reset GIF */
    if (apath == 3)
	outq(0x0100ULL, GSSREG2(PS2_GSSREG_CSR));	/* reset GS */
}

static int ps2gs_open_count = 0;	/* only one process can open */

static int ps2gs_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2gs_open_count)
	return -EBUSY;
    ps2gs_open_count++;

    if ((dev = ps2dma_dev_init(DMA_GIF, -1)) == NULL) {
	ps2gs_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    return 0;
}

static int ps2gs_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2gs: DMA timeout\n");
    kfree(dev);
    ps2gs_open_count--;
    return 0;
}

/*
 *  Vector Processing Unit (/dev/ps2vpu0, /dev/ps2vpu1) driver
 */

static const struct {
    unsigned long ubase, ulen;
    unsigned long vubase, vulen;
} vumap[2] = {
    { 0x11000000,  4096, 0x11004000,  4096 },
    { 0x11008000, 16384, 0x1100c000, 16384 },
};

static long ps2vpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;
    struct ps2_vpuinfo vpuinfo;
    struct ps2_vifreg vifreg;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_VPUINFO:
	vpuinfo.umemsize = vumap[vusw].ulen;
	vpuinfo.vumemsize = vumap[vusw].vulen;
	return copy_to_user((void *)arg, &vpuinfo, sizeof(vpuinfo)) ? -EFAULT : 0;
    case PS2IOC_SVIFREG:
	if (copy_from_user(&vifreg, (void *)arg, sizeof(vifreg)))
	    return -EFAULT;
	if (vifreg.reg == PS2_VIFREG_MARK ||
	    vifreg.reg == PS2_VIFREG_FBRST ||
	    vifreg.reg == PS2_VIFREG_ERR)
	    SET_VIFnREG(vusw, vifreg.reg, vifreg.val);
	else
	    return -EINVAL;
	return 0;
    case PS2IOC_GVIFREG:
	if (copy_from_user(&vifreg, (void *)arg, sizeof(vifreg)))
	    return -EFAULT;
	if (vifreg.reg == PS2_VIFREG_STAT ||
	    (vifreg.reg >= PS2_VIFREG_ERR && vifreg.reg <= PS2_VIFREG_ITOPS) ||
	    vifreg.reg == PS2_VIFREG_ITOP ||
	    (vifreg.reg >= PS2_VIFREG_R0 && vifreg.reg <= PS2_VIFREG_C3) ||
	    (vusw == 1 &&
	     (vifreg.reg >= PS2_VIFREG_BASE && vifreg.reg <= PS2_VIFREG_TOP)))
	    vifreg.val = VIFnREG(vusw, vifreg.reg);
	else
	    return -EINVAL;
	return copy_to_user((void *)arg, &vifreg, sizeof(vifreg)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static u32 init_vif0code[] __attribute__((aligned(DMA_TRUNIT))) = {
    PS2_VIF_SET_CODE(0x0404, 0, PS2_VIF_STCYCL, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMASK, 0),
    0,
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMOD, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_ITOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
};

static void ps2vpu0_reset(void)
{
    SET_VIF0REG(PS2_VIFREG_MARK, 0);
    SET_VIF0REG(PS2_VIFREG_ERR, 2);
    SET_VIF0REG(PS2_VIFREG_FBRST, 1);		/* reset VIF0 */
    set_c0_status(ST0_CU2);
    __asm__ __volatile__(
	"	sync.l\n"
	"	cfc2	$8, $28\n"
	"	ori	$8, $8, 0x0002\n"
	"	ctc2	$8, $28\n"
	"	sync.p\n"
	::: "$8");				/* reset VU0 */
    clear_c0_status(ST0_CU2);
    move_quad(KSEG1ADDR(VIF0_FIFO), (unsigned long)&init_vif0code[0]);
    move_quad(KSEG1ADDR(VIF0_FIFO), (unsigned long)&init_vif0code[4]);
}

static u32 init_vif1code[] __attribute__((aligned(DMA_TRUNIT))) = {
    PS2_VIF_SET_CODE(0x0404, 0, PS2_VIF_STCYCL, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMASK, 0),
    0,
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMOD, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_MSKPATH3, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_BASE, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_OFFSET, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_ITOP, 0),
};

static void ps2vpu1_reset(void)
{
    int apath;

    SET_VIF1REG(PS2_VIFREG_MARK, 0);
    SET_VIF1REG(PS2_VIFREG_ERR, 2);
    SET_VIF1REG(PS2_VIFREG_FBRST, 1);		/* reset VIF1 */
    set_c0_status(ST0_CU2);
    __asm__ __volatile__(
	"	sync.l\n"
	"	cfc2	$8, $28\n"
	"	ori	$8, $8, 0x0200\n"
	"	ctc2	$8, $28\n"
	"	sync.p\n"
	::: "$8");				/* reset VU1 */
    clear_c0_status(ST0_CU2);
    move_quad(KSEG1ADDR(VIF1_FIFO), (unsigned long)&init_vif1code[0]);
    move_quad(KSEG1ADDR(VIF1_FIFO), (unsigned long)&init_vif1code[4]);

    apath = (GIFREG(PS2_GIFREG_STAT) >> 10) & 3;
    if (apath == 1 || apath == 2) {
	SET_GIFREG(PS2_GIFREG_CTRL, 0x00000001);	/* reset GIF */
	outq(0x0100ULL, GSSREG2(PS2_GSSREG_CSR));	/* reset GS */
    }
}

static void set_cop2_usable(int onoff)
{
    if (onoff)
        KSTK_STATUS(current) |= ST0_CU2;
    else
        KSTK_STATUS(current) &= ~ST0_CU2;
}

static int ps2vpu_open_count[2];	/* only one process can open */

static int ps2vpu_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;
    int vusw = MINOR_UNIT(inode->i_rdev);

    if (vusw < 0 || vusw > 1)
	return -ENODEV;
    if (ps2vpu_open_count[vusw])
	return -EBUSY;
    ps2vpu_open_count[vusw]++;

    if ((dev = ps2dma_dev_init(DMA_VIF0 + vusw, -1)) == NULL) {
	ps2vpu_open_count[vusw]--;
	return -ENOMEM;
    }
    file->private_data = dev;
    dev->data = (void *)vusw;

    if (vusw == 0) {
	ps2vpu0_reset();
	set_cop2_usable(1);
    } else if (vusw == 1) {
	ps2vpu1_reset();
    }
    return 0;
}

static int ps2vpu_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2vpu%d: DMA timeout\n", vusw);
    kfree(dev);

    if (vusw == 0)
	set_cop2_usable(0);
    ps2vpu_open_count[vusw]--;
    return 0;
}

static int ps2vpu_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;
    unsigned long start, offset, len, mlen;

    if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
        return -EINVAL;

    start = vma->vm_start;
    offset = vma->vm_pgoff << PAGE_SHIFT;
    len = vma->vm_end - vma->vm_start;
    if (offset + len > vumap[vusw].ulen + vumap[vusw].vulen)
	return -EINVAL;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    /* map micro Mem */
    if (offset < vumap[vusw].ulen) {
	mlen = vumap[vusw].ulen - offset;
	if (mlen > len)
	    mlen = len;
	if (io_remap_pfn_range(vma, start, (vumap[vusw].ubase + offset) >> PAGE_SHIFT, mlen,
			     vma->vm_page_prot))
	    return -EAGAIN;
	start += mlen;
	len -= mlen;
	offset = vumap[vusw].ulen;
    }

    /* map VU Mem */
    if (len > 0) {
	offset -= vumap[vusw].ulen;
	if (io_remap_pfn_range(vma, start, (vumap[vusw].vubase + offset) >> PAGE_SHIFT, len, 
			     vma->vm_page_prot))
	    return -EAGAIN;
    }

    vma->vm_ops = &ps2dev_vmops;
    return 0;
}

/*
 *  Image Processing Unit (/dev/ps2ipu) driver
 */

static long ps2ipu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    u32 val32;
    u64 val64;
    static struct ps2_fifo fifo __attribute__((aligned(16)));

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    if ((result = ps2dev_recv_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_SIPUCMD:
	if (copy_from_user(&val32, (void *)arg, sizeof(val32)))
	    return -EFAULT;
	outl(val32, IPUREG_CMD);
	return 0;
    case PS2IOC_GIPUCMD:
	val64 = inq(IPUREG_CMD);
	if (val64 & ((u64)1 << 63))
	    return -EBUSY;		/* data is not valid */
	val32 = (u32)val64;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_SIPUCTRL:
	if (copy_from_user(&val32, (void *)arg, sizeof(val32)))
	    return -EFAULT;
	outl(val32, IPUREG_CTRL);
	return 0;
    case PS2IOC_GIPUCTRL:
	val32 = inl(IPUREG_CTRL);
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_GIPUTOP:
	val64 = inq(IPUREG_TOP);
	if (val64 & ((u64)1 << 63))
	    return -EBUSY;		/* data is not valid */
	val32 = (u32)val64;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_GIPUBP:
	val32 = inl(IPUREG_BP);
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_SIPUFIFO:
	if (copy_from_user(&fifo, (void *)arg, sizeof(fifo)))
	    return -EFAULT;
	move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&fifo);
	return 0;
    case PS2IOC_GIPUFIFO:
	move_quad((unsigned long)&fifo, KSEG1ADDR(IPU_O_FIFO));
	return copy_to_user((void *)arg, &fifo, sizeof(fifo)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static u32 init_ipu_iq[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x13101008, 0x16161310, 0x16161616, 0x1b1a181a,
    0x1a1a1b1b, 0x1b1b1a1a, 0x1d1d1d1b, 0x1d222222,
    0x1b1b1d1d, 0x20201d1d, 0x26252222, 0x22232325,
    0x28262623, 0x30302828, 0x38382e2e, 0x5345453a,
    0x10101010, 0x10101010, 0x10101010, 0x10101010,
};

static u32 init_ipu_vq[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x04210000, 0x03e00842, 0x14a51084, 0x1ce718c6,
    0x2529001f, 0x7c00294a, 0x35ad318c, 0x39ce7fff
};

static void wait_ipu_ready(void)
{
    while (inl(IPUREG_CTRL) < 0)
	;
}

static void ps2ipu_reset(void)
{
    outl(1 << 30, IPUREG_CTRL);		/* reset IPU */
    wait_ipu_ready();
    outl(0x00000000, IPUREG_CMD);		/* BCLR */
    wait_ipu_ready();

    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[0]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[4]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[8]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[12]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[16]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[16]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[16]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_iq[16]);
    outl(0x50000000, IPUREG_CMD);		/* SETIQ (I) */
    wait_ipu_ready();
    outl(0x58000000, IPUREG_CMD);		/* SETIQ (NI) */
    wait_ipu_ready();

    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_vq[0]);
    move_quad(KSEG1ADDR(IPU_I_FIFO), (unsigned long)&init_ipu_vq[4]);
    outl(0x60000000, IPUREG_CMD);		/* SETVQ */
    wait_ipu_ready();

    outl(0x90000000, IPUREG_CMD);		/* SETTH */
    wait_ipu_ready();

    outl(1 << 30, IPUREG_CTRL);		/* reset IPU */
    wait_ipu_ready();
    outl(0x00000000, IPUREG_CMD);		/* BCLR */
    wait_ipu_ready();
}

static int ps2ipu_open_count = 0;	/* only one process can open */

static int ps2ipu_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2ipu_open_count)
	return -EBUSY;
    ps2ipu_open_count++;

    if ((dev = ps2dma_dev_init(DMA_IPU_to, DMA_IPU_from)) == NULL) {
	ps2ipu_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    ps2ipu_reset();
    return 0;
}

static int ps2ipu_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2ipu: DMA timeout\n");
    kfree(dev);
    ps2ipu_open_count--;
    return 0;
}

static int ps2ipu_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long start, offset, len;

    if (vma->vm_pgoff >= 2)
        return -EINVAL;

    start = vma->vm_start;
    offset = vma->vm_pgoff << PAGE_SHIFT;
    len = vma->vm_end - vma->vm_start;
    if (offset + len > PAGE_SIZE * 2)
	return -EINVAL;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    /* map IPU registers */
    if (offset < PAGE_SIZE) {
	if (io_remap_pfn_range(vma, vma->vm_start, 0x10002000 >> PAGE_SHIFT,
			     PAGE_SIZE, vma->vm_page_prot))
	    return -EAGAIN;
	start += PAGE_SIZE;
	len -= PAGE_SIZE;
    }
    /* map IPU FIFO */
    if (len > 0) {
	if (io_remap_pfn_range(vma, vma->vm_start, 0x10007000 >> PAGE_SHIFT,
			     PAGE_SIZE, vma->vm_page_prot))
	    return -EAGAIN;
    }

    vma->vm_ops = &ps2dev_vmops;
    return 0;
}

/*
 *  Scratchpad RAM (/dev/ps2spr) driver
 */

static long ps2spr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_sprinfo sprinfo;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    if ((result = ps2dev_recv_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_SPRINFO:
	sprinfo.size = SPR_SIZE;
	return copy_to_user((void *)arg, &sprinfo, sizeof(sprinfo)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static int ps2spr_open_count = 0;	/* only one process can open */

static int ps2spr_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2spr_open_count)
	return -EBUSY;
    ps2spr_open_count++;

    if ((dev = ps2dma_dev_init(DMA_SPR_to, DMA_SPR_from)) == NULL) {
	ps2spr_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    return 0;
}

static int ps2spr_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2spr: DMA timeout\n");
    kfree(dev);
    ps2spr_open_count--;
    return 0;
}

#define SPR_MASK		(~(SPR_SIZE - 1))
#define SPR_ALIGN(addr)		((typeof(addr))((((unsigned long) (addr)) + SPR_SIZE - 1) & SPR_MASK))

static int ps2spr_mmap(struct file *file, struct vm_area_struct *vma)
{
    if (vma->vm_pgoff != 0)
	return -ENXIO;
    if (vma->vm_end - vma->vm_start != SPR_SIZE)
	return -EINVAL;

    /* The virtual address must be an even virtual address. */
    if (vma->vm_start != SPR_ALIGN(vma->vm_start))
	return -EINVAL;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    /* Map scratchpad. SPR_PHYS_ADDR is not a valid physical address, this is
     * just used to detect scratchpad in the TLB refill handler.
     */
    if (io_remap_pfn_range(vma, vma->vm_start, SPR_PHYS_ADDR >> PAGE_SHIFT, SPR_SIZE, vma->vm_page_prot))
	return -EAGAIN;

    vma->vm_ops = &ps2dev_vmops;
    return 0;
}

static unsigned long ps2spr_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
    struct vm_area_struct *vma;

    /* SPRAM must be 16KB aligned. */

    if (len > TASK_SIZE)
	return -ENOMEM;

    if (addr) {
	addr = SPR_ALIGN(addr);
	vma = find_vma(current->mm, addr);
	if (TASK_SIZE - len >= addr &&
	    (!vma || addr + len <= vma->vm_start))
		return addr;
    }
    addr = SPR_ALIGN(TASK_UNMAPPED_BASE);

    for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
	/* At this point:  (!vma || addr < vma->vm_end). */
	if (TASK_SIZE - len < addr)
	    return -ENOMEM;
	if (!vma || addr + len <= vma->vm_start)
	    return addr;
	addr = SPR_ALIGN(vma->vm_end);
    }
}

/*
 *  file operations structures
 */

struct file_operations ps2gs_fops = {
    llseek:		no_llseek,
    write:		ps2dev_write,
    unlocked_ioctl:	ps2gs_ioctl,
    open:		ps2gs_open,
    release:		ps2gs_release,
    fsync:		ps2dev_fsync,
};

struct file_operations ps2vpu_fops = {
    llseek:		no_llseek,
    write:		ps2dev_write,
    unlocked_ioctl:	ps2vpu_ioctl,
    mmap:		ps2vpu_mmap,
    open:		ps2vpu_open,
    release:		ps2vpu_release,
    fsync:		ps2dev_fsync,
};

struct file_operations ps2ipu_fops = {
    llseek:		no_llseek,
    read:		ps2dev_read,
    write:		ps2dev_write,
    unlocked_ioctl:	ps2ipu_ioctl,
    mmap:		ps2ipu_mmap,
    open:		ps2ipu_open,
    release:		ps2ipu_release,
    fsync:		ps2dev_fsync,
};

struct file_operations ps2spr_fops = {
    llseek:		no_llseek,
    unlocked_ioctl:	ps2spr_ioctl,
    mmap:		ps2spr_mmap,
    open:		ps2spr_open,
    release:		ps2spr_release,
    fsync:		ps2dev_fsync,
    get_unmapped_area:	ps2spr_get_unmapped_area,
};

static int ps2dev_initialized;

static int ps2dev_add_device(unsigned int func, int offset, int count, const char *name, const char *devname)
{
    dev_t dev_id;
    int rv;
    int i;

    BUG_ON(func >= PS2DEV_NR_OFDEVS);
    dev_id = MKDEV(ps2dev_major, ps2dev_minor + FN2MINOR(func) + offset);
    rv = cdev_add(&ps2dev_cdev[func], dev_id, count - offset);
    if (rv < 0)
        return rv;

    if (devname == NULL)
        devname = name;
    for (i = offset; i < count; i++) {
        struct device *d;
        char buffer[16];

        snprintf(buffer, sizeof(buffer), devname, i);
        dev_id = MKDEV(ps2dev_major, ps2dev_minor + FN2MINOR(func) + i);
        d = device_create(ps2dev_class, NULL, dev_id, NULL, buffer);
        if (IS_ERR(d)) {
            return PTR_ERR(d);
        }
    }
    return 0;
}

static int ps2dev_remove_device(unsigned int func, int offset, int count)
{
    dev_t dev_id;
    int i;

    BUG_ON(func >= PS2DEV_NR_OFDEVS);

    for (i = offset; i < count; i++) {
        dev_id = MKDEV(ps2dev_major, ps2dev_minor + FN2MINOR(func) + i);
        device_destroy(ps2dev_class, dev_id);
    }
    cdev_del(&ps2dev_cdev[func]);
    return 0;
}

static void ps2_dev_remove_devices(void)
{
    ps2dev_remove_device(PS2MEM_FUNC, 1, 2);
    ps2dev_remove_device(PS2GS_FUNC, 0, 1);
    ps2dev_remove_device(PS2VPU_FUNC, 0, 2);
    ps2dev_remove_device(PS2IPU_FUNC, 0, 1);
    ps2dev_remove_device(PS2SPR_FUNC, 0, 1);
}

static int ps2_dev_add_devices(void)
{
    int rv;

    cdev_init(&ps2dev_cdev[PS2MEM_FUNC], &ps2mem_fops);
    cdev_init(&ps2dev_cdev[PS2EVENT_FUNC], &ps2ev_fops);
    cdev_init(&ps2dev_cdev[PS2GS_FUNC], &ps2gs_fops);
    cdev_init(&ps2dev_cdev[PS2VPU_FUNC], &ps2vpu_fops);
    cdev_init(&ps2dev_cdev[PS2IPU_FUNC], &ps2ipu_fops);
    cdev_init(&ps2dev_cdev[PS2SPR_FUNC], &ps2spr_fops);

    rv = ps2dev_add_device(PS2MEM_FUNC, 1, 2, "ps2mem", NULL);
    rv |= ps2dev_add_device(PS2EVENT_FUNC, 0, 1, "ps2event", NULL);
    rv |= ps2dev_add_device(PS2GS_FUNC, 0, 1, "ps2gs", NULL);
    rv |= ps2dev_add_device(PS2VPU_FUNC, 0, 2, "ps2vpu", "ps2vpu%d");
    rv |= ps2dev_add_device(PS2IPU_FUNC, 0, 1, "ps2ipu", NULL);
    rv |= ps2dev_add_device(PS2SPR_FUNC, 0, 1, "ps2spr", NULL);

    if (rv) {
        ps2_dev_remove_devices();
        return -ENOMEM;
    }

    return 0;

}

static int vpu_usage(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	switch (action) {
	case CU2_EXCEPTION:
                printk("error: CU2_EXCEPTION at 0x%08lx (VPU not enabled?).\n", KSTK_EIP(current));
                break;

        case CU2_LWC2_OP:
                printk("error: CU2_LWC2_OP at 0x%08lx\n", KSTK_EIP(current));
                break;

        case CU2_LDC2_OP:
                printk("error: CU2_LDC2_OP at 0x%08lx\n", KSTK_EIP(current));
                break;

        case CU2_SWC2_OP:
                printk("error: CU2_SWC2_OP at 0x%08lx\n", KSTK_EIP(current));
                break;

        case CU2_SDC2_OP:
                printk("error: CU2_SDC2_OP at 0x%08lx\n", KSTK_EIP(current));
                break;
	}

	return NOTIFY_OK;		/* Let default notifier send signals */
}

static struct notifier_block vpu_notifier = {
	.notifier_call = vpu_usage,
};

int __init ps2dev_init(void)
{
    u64 gs_revision;
    dev_t dev_id;
    struct vm_struct *area;
    int rv;

    if (ps2dev_major) {
        dev_id = MKDEV(ps2dev_major, ps2dev_minor);
        rv = register_chrdev_region(dev_id, PS2DEV_COUNT, "ps2dev");
    } else {
        rv = alloc_chrdev_region(&dev_id, 0, PS2DEV_COUNT, "ps2dev");
        ps2dev_major = MAJOR(dev_id);
        ps2dev_minor = MINOR(dev_id);
    }
    if (rv) {
        printk(KERN_ERR "ps2dev: unable to register chrdev region.\n");
        return rv;
    }

    ps2dev_class = class_create(THIS_MODULE, "ps2dev");
    if (IS_ERR(ps2dev_class)) {
        unregister_chrdev_region(dev_id, PS2DEV_COUNT);
        printk(KERN_ERR "ps2dev: unable to register class.\n");
        return PTR_ERR(ps2dev_class);
    }

    rv = ps2_dev_add_devices();
    if (rv) {
        unregister_chrdev_region(dev_id, PS2DEV_COUNT);
        class_destroy(ps2dev_class);
        ps2dev_class = NULL;
        printk(KERN_ERR "ps2dev: unable to allocate devices.\n");
        return rv;
    }

    ps2ev_init();
    spin_lock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2dma_channels[DMA_GIF].reset = ps2gif_reset;
    ps2dma_channels[DMA_VIF0].reset = ps2vpu0_reset;
    ps2dma_channels[DMA_VIF1].reset = ps2vpu1_reset;
    ps2dma_channels[DMA_IPU_to].reset = ps2ipu_reset;
    spin_unlock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2gs_get_gssreg(PS2_GSSREG_CSR, &gs_revision);

    /* map scratchpad RAM */
    area = __get_vm_area(2 * SPR_SIZE, VM_IOREMAP, VMALLOC_START, VMALLOC_END);
    if (area != NULL) {
	    /* Ensure that virtual address is aligned. */
	    ps2spr_vaddr = SPR_ALIGN(area->addr + SPR_SIZE);

	    /* This maps 16KByte, even if PM_4K is used. */
	    add_wired_entry(SCRATCHPAD_RAM | 0x17, 0x17, (unsigned long) ps2spr_vaddr, PM_4K);

	    /* TBD: Unmapping SPR is not possible in ps2dev_cleanup(). */
    }

    printk("PlayStation 2 device support: GIF, VIF, GS, VU, IPU, SPR\n");
    printk("Graphics Synthesizer revision: %08x\n",
	   ((u32)gs_revision >> 16) & 0xffff);

    register_cu2_notifier(&vpu_notifier);

    ps2dev_initialized = 1;

    return 0;
}

void ps2dev_cleanup(void)
{
    dev_t dev_id;

    if (!ps2dev_initialized)
	return;

    ps2_dev_remove_devices();
    dev_id = MKDEV(ps2dev_major, ps2dev_minor);
    unregister_chrdev_region(dev_id, PS2DEV_COUNT);
    class_destroy(ps2dev_class);

    spin_lock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2dma_channels[DMA_GIF].reset = NULL;
    ps2dma_channels[DMA_VIF0].reset = NULL;
    ps2dma_channels[DMA_VIF1].reset = NULL;
    ps2dma_channels[DMA_IPU_to].reset = NULL;
    spin_unlock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2ev_cleanup();

}

module_init(ps2dev_init);
module_exit(ps2dev_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 integrated device driver");
MODULE_LICENSE("GPL");
