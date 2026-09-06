// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 SMAP Ethernet driver -- EE side, programmed I/O
 *
 * Copyright (C) 2026 Hubert Wyrzykiewicz
 *
 * Derived from the original PlayStation 2 Linux kit driver:
 *
 *	linux-2.4.17/drivers/ps2/smap.c
 *	Copyright (C) 2001, 2002 Sony Computer Entertainment Inc.
 *	GNU General Public License Version 2
 *
 * (BlackRhino GNU/Linux kit), itself a descendant of the 2.2.1 driver on
 * "Linux for PlayStation 2" DISC2.  The register layout, the FIFO and
 * buffer-descriptor protocol, the EMAC3 default values and the EEPROM
 * bit-bang sequence are Sony's.  The driver structure -- net_device_ops,
 * NAPI, phylib instead of a link-check thread -- is new for Linux 5.4.
 *
 * Hardware.  The SPEED chip in the expansion bay sits at physical
 * 0x14000000 on the EE side.  It contains an IBM EMAC3 MAC with a National
 * DP83846 PHY on MII address 1, a 4 KiB TX FIFO, a 16 KiB RX FIFO and two
 * rings of 64 buffer descriptors, all memory-mapped.  The EE reaches all of
 * it directly once the bay is powered, which drivers/ps2/iop-dev9.c does at
 * module load (without that every EE access raises a data bus error).  The
 * platform device itself, with the register window and the three relayed
 * interrupts, is registered by arch/mips/ps2/devices.c.  SPEED interrupts
 * arrive on the IOP and are relayed to the EE by iopmod's irqrelay as
 * IRQ_IOP_SPD_*; the IOP side demultiplexes the SPEED interrupt status
 * register, the EE side clears the sources through SMAP_INTR_CLR, as Sony's
 * driver did.
 *
 * Data moves by PIO: 32-bit reads and writes of the FIFO data ports from
 * the EE, through a bounce buffer because skb data is only 2-byte aligned.
 * The IOP-side DMA that Sony's DMA relay module provided is not ported;
 * DHCP and SSH do not need it.
 *
 * Measured on an SCPH-30004 (ROM 0150) with a probe module before this
 * driver was written: SPEED rev1 0011 rev3 0003, EMAC3 soft reset OK, PHY
 * id 2000/5c23 (DP83846 rev 3), link up at 100 Mbit full duplex with a
 * cable, MAC from the EEPROM with a valid checksum.
 *
 * Poll-mode fallback.  The one thing above that no measurement covers is
 * the interrupt path: the SPEED virtual IRQs have never been delivered to
 * the EE on this console, because pata_ps2 registers IRQ_IOP_SPD_ATA0 but
 * with no disk in the bay nothing ever fires it.  If the relay turns out
 * not to work, an interrupt-only driver does nothing at all and says
 * nothing about whether the data path is sound.  So a timer watches for
 * work that arrived without an interrupt, and switches the driver to
 * polling when it finds any; see the poll parameter.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>

#include <asm/addrspace.h>

#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/irq.h>

#include "ps2-smap-regs.h"

#define DRV_NAME	"ps2-smap"
#define DRV_VERSION	"8"

/* DEV9 power register on the IOP, checked before the first EE access. */
#define IOP_DEV9_POWER	0xbf80146c
#define DEV9_POWER_ON	0x4

/* Keep two descriptors in hand, as Sony did. */
#define SMAP_TXBD_LIMIT	(SMAP_BD_MAX_ENTRY - 2)

/* Bounce buffers: one maximum frame, whole 32-bit words. */
#define SMAP_BOUNCE_SIZE	ALIGN(SMAP_RXMAXSIZE, 4)

/* Everything the EE ever clears in SMAP_INTR_CLR. */
#define SMAP_INTR_ALL	(INTR_EMAC3 | INTR_RXEND | INTR_TXEND | \
			 INTR_RXDNV | INTR_TXDNV)

/* Interrupts we actually take: frame done in each direction, MAC errors. */
#define SMAP_INTR_USED	(INTR_RXEND | INTR_TXEND | INTR_EMAC3)

/*
 * EMAC3 events worth an interrupt: a dead transmitter and TX errors, the
 * set Open PS2 Loader's smap module uses.  RX errors are already visible
 * in the descriptor status and are counted there.
 */
#define SMAP_E3_INTR_USED	(E3_DEAD_ALL | E3_INTR_TX_ERR_0 | \
				 E3_INTR_SQE_ERR_0 | E3_INTR_TX_ERR_1 | \
				 E3_INTR_SQE_ERR_1)

#define SMAP_TX_ERR_MASK	(SMAP_BD_TX_BADFCS | SMAP_BD_TX_BADPKT | \
				 SMAP_BD_TX_LOSSCR | SMAP_BD_TX_EDEFER | \
				 SMAP_BD_TX_ECOLL | SMAP_BD_TX_LCOLL | \
				 SMAP_BD_TX_UNDERRUN | SMAP_BD_TX_SQE)

/*
 * Poll-mode fallback.
 *
 *   poll=0  interrupts only, fail visibly if the relay is dead
 *   poll=1  interrupts, with an automatic switch to polling (default)
 *   poll=2  polling from the start, no interrupts requested at all
 *
 * The watchdog runs while the interface is up and only trips on proof: a
 * descriptor the hardware has finished with while not one interrupt has
 * been delivered.  A quiet network never trips it.
 */
enum {
	SMAP_POLL_IRQ	= 0,
	SMAP_POLL_AUTO	= 1,
	SMAP_POLL_ONLY	= 2,
};

static int poll_mode = SMAP_POLL_AUTO;
module_param_named(poll, poll_mode, int, 0444);
MODULE_PARM_DESC(poll, "0 = interrupts only, 1 = interrupts with automatic fallback to polling (default), 2 = polling only");

static int poll_ms = 10;
module_param_named(poll_ms, poll_ms, int, 0444);
MODULE_PARM_DESC(poll_ms, "polling interval in ms once polling (default 10)");

#define SMAP_WATCHDOG_MS	500

/*
 * Watchdog ticks with work pending and no interrupt before we give up on the
 * interrupt path.  More than one, so that a descriptor which completed just
 * before a tick is not mistaken for a dead relay.
 */
#define SMAP_STALL_TICKS	3

#define SMAP_RX_ERR_MASK	(SMAP_BD_RX_OVERRUN | SMAP_BD_RX_PFRM | \
				 SMAP_BD_RX_BADFRM | SMAP_BD_RX_RUNTFRM | \
				 SMAP_BD_RX_SHORTEVNT | SMAP_BD_RX_ALIGNERR | \
				 SMAP_BD_RX_BADFCS | SMAP_BD_RX_FRMTOOLONG | \
				 SMAP_BD_RX_OUTRANGE | SMAP_BD_RX_INRANGE)

struct smap_priv {
	struct net_device *ndev;
	struct platform_device *pdev;
	void __iomem *base;

	/*
	 * Protects the SMAP interrupt enable register, the TX ring state
	 * and the FIFO write pointer, and MAC enable/disable sequences.
	 * Taken from hard IRQ context, so always irqsave elsewhere.
	 */
	spinlock_t lock;

	struct napi_struct napi;

	struct mii_bus *mii;
	struct phy_device *phydev;
	u32 mode1;		/* EMAC3 MODE1 as currently programmed */
	bool link;

	/* TX ring: FIFO write offset, free FIFO bytes, descriptor indices. */
	u16 txbwp;
	int txfree;
	int txbds;		/* next descriptor to fill */
	int txbdi;		/* next descriptor to reclaim */
	int txused;
	u32 *txbuf;

	/* RX ring. */
	int rxbdi;
	u32 *rxbuf;

	u8 ppwc;		/* shadow of the write-only EEPROM port */

	int irq_rx, irq_tx, irq_emac3;
	bool irqs_requested;

	/* Poll-mode fallback. */
	struct timer_list timer;
	bool polling;
	unsigned int irq_count;
	unsigned int irq_count_seen;	/* irq_count at the previous tick */
	unsigned int stall_ticks;	/* ticks with work, no interrupt */
	unsigned int dnv_events;	/* descriptor-not-valid bits cleared */
	unsigned long poll_period;

	u32 emac3_events;
};

/* -------------------------------------------------------- register access */

static inline u8 smap_r8(struct smap_priv *p, u32 off)
{
	return readb(p->base + off);
}

static inline u16 smap_r16(struct smap_priv *p, u32 off)
{
	return readw(p->base + off);
}

static inline u32 smap_r32(struct smap_priv *p, u32 off)
{
	return readl(p->base + off);
}

static inline void smap_w8(struct smap_priv *p, u32 off, u8 v)
{
	writeb(v, p->base + off);
}

static inline void smap_w16(struct smap_priv *p, u32 off, u16 v)
{
	writew(v, p->base + off);
}

static inline void smap_w32(struct smap_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

/* EMAC3 is a 32-bit core behind a 16-bit bus: high half first, as Sony. */
static u32 emac3_read(struct smap_priv *p, u32 off)
{
	u16 hi = smap_r16(p, off);
	u16 lo = smap_r16(p, off + 2);

	return ((u32)hi << 16) | lo;
}

static void emac3_write(struct smap_priv *p, u32 off, u32 v)
{
	smap_w16(p, off, (v >> 16) & 0xffff);
	smap_w16(p, off + 2, v & 0xffff);
}

/* Buffer descriptors live in SPEED memory as four 16-bit words each. */
#define TXBD(i, f)	(SMAP_BD_BASE_TX + (i) * SMAP_BD_ENTRY_SIZE + (f))
#define RXBD(i, f)	(SMAP_BD_BASE_RX + (i) * SMAP_BD_ENTRY_SIZE + (f))

/* Called with p->lock held. */
static void smap_intr_set(struct smap_priv *p, u16 bits, bool on)
{
	u16 v = smap_r16(p, SMAP_INTR_ENABLE);

	v = on ? (v | bits) : (v & ~bits);
	smap_w16(p, SMAP_INTR_ENABLE, v);
}

/* ------------------------------------------------------------------ MDIO */

static int smap_mdio_wait(struct smap_priv *p)
{
	int i;

	for (i = 0; i < 1000; i++) {
		if (emac3_read(p, SMAP_EMAC3_STA_CTRL) & E3_PHY_OP_COMP)
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

static int __smap_mdio_read(struct smap_priv *p, int addr, int reg)
{
	u32 sta;
	int err;

	err = smap_mdio_wait(p);
	if (err)
		return err;

	emac3_write(p, SMAP_EMAC3_STA_CTRL,
		    E3_PHY_READ | E3_PHY_50M |
		    ((addr & E3_PHY_ADDR_MSK) << E3_PHY_ADDR_BITSFT) |
		    (reg & E3_PHY_REG_ADDR_MSK));

	err = smap_mdio_wait(p);
	if (err)
		return err;

	/*
	 * Sony re-reads here: "it may be needed to re-read to get correct
	 * phy data".
	 */
	sta = emac3_read(p, SMAP_EMAC3_STA_CTRL);
	if (sta & E3_PHY_ERR_READ)
		return -EIO;

	return (sta >> E3_PHY_DATA_BITSFT) & E3_PHY_DATA_MSK;
}

static int __smap_mdio_write(struct smap_priv *p, int addr, int reg, u16 val)
{
	int err;

	err = smap_mdio_wait(p);
	if (err)
		return err;

	emac3_write(p, SMAP_EMAC3_STA_CTRL,
		    ((u32)val << E3_PHY_DATA_BITSFT) |
		    E3_PHY_WRITE | E3_PHY_50M |
		    ((addr & E3_PHY_ADDR_MSK) << E3_PHY_ADDR_BITSFT) |
		    (reg & E3_PHY_REG_ADDR_MSK));

	return smap_mdio_wait(p);
}

static int smap_mdio_read(struct mii_bus *bus, int addr, int reg)
{
	return __smap_mdio_read(bus->priv, addr, reg);
}

static int smap_mdio_write(struct mii_bus *bus, int addr, int reg, u16 val)
{
	return __smap_mdio_write(bus->priv, addr, reg, val);
}

/*
 * Take the PHY out of power-down if it is there.
 *
 * phy_disconnect() -> phy_detach() -> phy_suspend() -> genphy_suspend() sets
 * BMCR_PDOWN, so every unload of this module leaves the DP83846 powered down.
 * EMAC3's soft reset needs the clock the PHY supplies, and with the PHY
 * asleep the reset never completes: measured as "EMAC3 soft reset did not
 * complete" and "probe of ps2-smap failed with error -145" (ETIMEDOUT on
 * MIPS) on every reload, and never on a cold boot, where the PHY is running.
 *
 * Talks to the PHY over raw MDIO because it is needed in two places where
 * phylib is not around: before the soft reset in probe, and after
 * phy_disconnect() at teardown.
 *
 * Returns 1 if the PHY was asleep and has been woken, 0 if it was already
 * awake, and a negative error if MDIO did not answer.  Version 9 returned
 * false for the last two cases alike, which are opposite diagnoses; the
 * callers now log what actually happened, because on hardware it turned out
 * to be the MDIO error every time.
 */
static int smap_phy_wake(struct smap_priv *p)
{
	int bmcr = __smap_mdio_read(p, DsPHYTER_ADDRESS, MII_BMCR);

	if (bmcr < 0)
		return bmcr;

	if (!(bmcr & BMCR_PDOWN))
		return 0;

	__smap_mdio_write(p, DsPHYTER_ADDRESS, MII_BMCR, bmcr & ~BMCR_PDOWN);
	/* The DP83846 wants a moment before it clocks again. */
	msleep(20);

	return 1;
}

/* ---------------------------------------------------------------- EEPROM */

/*
 * 93C46-style serial EEPROM bit-banged through the SMAP PIO port, holding
 * the MAC address in words 0-2 and their sum in word 3.  Straight from
 * Sony's smap.c; only the accessors changed.
 */
static void ee_clk(struct smap_priv *p, int c)
{
	p->ppwc = c ? (p->ppwc | PP_SCLK) : (p->ppwc & ~PP_SCLK);
	smap_w8(p, SMAP_PIOPORT_OUT, p->ppwc);
}

static void ee_set_d(struct smap_priv *p, int d)
{
	p->ppwc = d ? (p->ppwc | PP_DIN) : (p->ppwc & ~PP_DIN);
}

static void ee_set_s(struct smap_priv *p, int s)
{
	p->ppwc = s ? (p->ppwc | PP_CSEL) : (p->ppwc & ~PP_CSEL);
}

static void ee_clock_out(struct smap_priv *p, int val)
{
	ee_set_d(p, val);
	ee_clk(p, 0);
	udelay(1);
	ee_clk(p, 1);
	udelay(1);
	ee_clk(p, 0);
	udelay(1);
}

static int ee_clock_in(struct smap_priv *p)
{
	int r;

	ee_set_d(p, 0);
	ee_clk(p, 0);
	udelay(1);
	ee_clk(p, 1);
	udelay(1);
	r = (smap_r8(p, SMAP_PIOPORT_IN) >> 4) & 1;
	ee_clk(p, 0);
	udelay(1);

	return r;
}

static void ee_read_words(struct smap_priv *p, u8 addr, u16 *data, int n)
{
	int i;

	smap_w8(p, SMAP_PIOPORT_DIR, PP_SCLK | PP_CSEL | PP_DIN);

	ee_set_s(p, 0);
	ee_set_d(p, 0);
	ee_clk(p, 0);
	udelay(1);

	ee_set_s(p, 1);
	ee_set_d(p, 0);
	ee_clk(p, 0);
	udelay(1);

	ee_clock_out(p, 1);			/* start bit */
	ee_clock_out(p, (PP_OP_READ >> 1) & 1);	/* op code */
	ee_clock_out(p, PP_OP_READ & 1);

	addr &= 0x3f;
	for (i = 0; i < 6; i++) {
		ee_clock_out(p, (addr & 0x20) ? 1 : 0);
		addr <<= 1;
	}

	while (n--) {
		u16 w = 0;

		for (i = 0; i < 16; i++) {
			w <<= 1;
			w |= ee_clock_in(p);
		}
		*data++ = w;
	}

	ee_set_s(p, 0);
	ee_set_d(p, 0);
	ee_clk(p, 0);
	udelay(2);
}

static int smap_read_mac(struct smap_priv *p, u8 *mac)
{
	unsigned long flags;
	u16 raw[3], cksum, sum = 0;
	int i;

	spin_lock_irqsave(&p->lock, flags);
	ee_read_words(p, 0x0, raw, 3);
	ee_read_words(p, 0x3, &cksum, 1);
	spin_unlock_irqrestore(&p->lock, flags);

	for (i = 0; i < 3; i++) {
		sum += raw[i];
		mac[2 * i] = raw[i] & 0xff;
		mac[2 * i + 1] = raw[i] >> 8;
	}

	return sum == cksum ? 0 : -EINVAL;
}

/* -------------------------------------------------------------- hardware */

static int smap_wait_clear8(struct smap_priv *p, u32 off, u8 bit)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if (!(smap_r8(p, off) & bit))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int smap_fifo_reset(struct smap_priv *p)
{
	int err = 0;

	smap_w8(p, SMAP_TXFIFO_CTRL, TXFIFO_RESET);
	smap_w8(p, SMAP_RXFIFO_CTRL, RXFIFO_RESET);

	if (smap_wait_clear8(p, SMAP_TXFIFO_CTRL, TXFIFO_RESET)) {
		netdev_err(p->ndev, "TX FIFO reset did not complete\n");
		err = -ETIMEDOUT;
	}
	if (smap_wait_clear8(p, SMAP_RXFIFO_CTRL, RXFIFO_RESET)) {
		netdev_err(p->ndev, "RX FIFO reset did not complete\n");
		err = -ETIMEDOUT;
	}

	return err;
}

static int smap_emac3_soft_reset(struct smap_priv *p)
{
	int i;

	emac3_write(p, SMAP_EMAC3_MODE0, E3_SOFT_RESET);
	for (i = 0; i < 10000; i++) {
		if (!(emac3_read(p, SMAP_EMAC3_MODE0) & E3_SOFT_RESET))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/* Called with p->lock held. */
static void smap_mac_enable(struct smap_priv *p)
{
	emac3_write(p, SMAP_EMAC3_MODE0, E3_TXMAC_ENABLE | E3_RXMAC_ENABLE);
}

/* Called with p->lock held.  Waits for both MACs to go idle, as Sony did. */
static void smap_mac_disable(struct smap_priv *p)
{
	u32 v;
	int i;

	v = emac3_read(p, SMAP_EMAC3_MODE0);
	v &= ~(E3_TXMAC_ENABLE | E3_RXMAC_ENABLE);
	emac3_write(p, SMAP_EMAC3_MODE0, v);

	for (i = 0; i < 10000; i++) {
		v = emac3_read(p, SMAP_EMAC3_MODE0);
		if ((v & E3_RXMAC_IDLE) && (v & E3_TXMAC_IDLE))
			return;
		udelay(1);
	}

	netdev_warn(p->ndev,
		    "EMAC3 still running after disable (mode0 %08x)\n", v);
}

static void smap_emac3_set_defaults(struct smap_priv *p)
{
	const u8 *mac = p->ndev->dev_addr;

	emac3_write(p, SMAP_EMAC3_ADDR_HI, (mac[0] << 8) | mac[1]);
	emac3_write(p, SMAP_EMAC3_ADDR_LO,
		    ((u32)mac[2] << 24) | ((u32)mac[3] << 16) |
		    ((u32)mac[4] << 8) | mac[5]);

	emac3_write(p, SMAP_EMAC3_INTER_FRAME_GAP, 4);

	emac3_write(p, SMAP_EMAC3_RxMODE,
		    E3_RX_STRIP_PAD | E3_RX_STRIP_FCS |
		    E3_RX_INDIVID_ADDR | E3_RX_BCAST);

	/* TX FIFO request priorities: low 7*8 = 56, urgent 15*8 = 120. */
	emac3_write(p, SMAP_EMAC3_TxMODE1,
		    (7 << E3_TX_LOW_REQ_BITSFT) | (15 << E3_TX_URG_REQ_BITSFT));

	/* TX threshold (12+1)*64 = 832 bytes. */
	emac3_write(p, SMAP_EMAC3_TX_THRESHOLD,
		    (12 & E3_TX_THRESHLD_MSK) << E3_TX_THRESHLD_BITSFT);

	/* RX watermarks: low 16*8 = 128, high 128*8 = 1024. */
	emac3_write(p, SMAP_EMAC3_RX_WATERMARK,
		    ((16 & E3_RX_LO_WATER_MSK) << E3_RX_LO_WATER_BITSFT) |
		    ((128 & E3_RX_HI_WATER_MSK) << E3_RX_HI_WATER_BITSFT));
}

static void smap_bd_init(struct smap_priv *p)
{
	int i;

	for (i = 0; i < SMAP_BD_MAX_ENTRY; i++) {
		smap_w16(p, TXBD(i, SMAP_BD_CTRL_STAT), 0);
		smap_w16(p, TXBD(i, SMAP_BD_RESERVED), 0);
		smap_w16(p, TXBD(i, SMAP_BD_LENGTH), 0);
		smap_w16(p, TXBD(i, SMAP_BD_POINTER), 0);

		smap_w16(p, RXBD(i, SMAP_BD_CTRL_STAT), SMAP_BD_RX_EMPTY);
		smap_w16(p, RXBD(i, SMAP_BD_RESERVED), 0);
		smap_w16(p, RXBD(i, SMAP_BD_LENGTH), 0);
		smap_w16(p, RXBD(i, SMAP_BD_POINTER), 0);
	}

	p->txbwp = 0;
	p->txfree = SMAP_TXBUFSIZE;
	p->txbds = p->txbdi = p->txused = 0;
	p->rxbdi = 0;
}

/*
 * Sony's smap_reset(RESET_INIT) minus the PHY, which phylib owns: quiet
 * the interrupts, reset the FIFOs and the EMAC3, program the defaults and
 * clear the rings.  Leaves the MACs disabled.
 */
static int smap_hw_init(struct smap_priv *p)
{
	unsigned long flags;
	u32 mode0;
	int woken;
	int err;

	spin_lock_irqsave(&p->lock, flags);
	smap_intr_set(p, SMAP_INTR_ALL, false);
	smap_w16(p, SMAP_INTR_CLR, SMAP_INTR_ALL);
	emac3_write(p, SMAP_EMAC3_INTR_ENABLE, 0);
	emac3_write(p, SMAP_EMAC3_INTR_STAT, E3_INTR_ALL);
	spin_unlock_irqrestore(&p->lock, flags);

	smap_w8(p, SMAP_BD_MODE, 0);	/* no byte swap */

	/*
	 * Version 10, and this is the whole point of it.  Measured on hardware
	 * on 2026-09-06: after a failed reset MODE0 reads 20000000, that is
	 * E3_SOFT_RESET still asserted, and the BMCR read then fails with
	 * -ETIMEDOUT.  MDIO runs through EMAC3's own STA_CTRL register, so
	 * while EMAC3 sits in reset there is no way to reach the PHY at all -
	 * which means the wake-up that versions 7 to 9 attempted after a failed
	 * reset could never work.  It has to happen before it.
	 *
	 * So: clear a reset left over by a previous attempt, then wake the PHY
	 * while MDIO is still usable, and only then reset the block.
	 */
	mode0 = emac3_read(p, SMAP_EMAC3_MODE0);
	if (mode0 & E3_SOFT_RESET) {
		netdev_warn(p->ndev,
			    "EMAC3 was left in soft reset (mode0 %08x), clearing it\n",
			    mode0);
		emac3_write(p, SMAP_EMAC3_MODE0, 0);
		msleep(20);
	}

	woken = smap_phy_wake(p);
	if (woken < 0)
		netdev_warn(p->ndev,
			    "PHY BMCR read failed with %d before the reset: MDIO is not answering\n",
			    woken);
	else if (woken)
		netdev_info(p->ndev,
			    "PHY was powered down, woke it up before the EMAC3 reset\n");

	err = smap_fifo_reset(p);
	if (err)
		return err;

	err = smap_emac3_soft_reset(p);
	if (err) {
		/*
		 * Kept as the second line of defence for the case where the
		 * PHY falls asleep between the wake-up above and this reset.
		 * The MODE0 read-back tells two failures apart: 20000000 means
		 * the reset bit is still set and the block never cleared it,
		 * 00000000 means the register window itself reads as zero and
		 * the whole SPEED side is gone.
		 */
		netdev_err(p->ndev, "EMAC3 reset timed out, mode0 %08x\n",
			   emac3_read(p, SMAP_EMAC3_MODE0));

		woken = smap_phy_wake(p);
		if (woken < 0)
			netdev_err(p->ndev,
				   "PHY BMCR read failed with %d: MDIO is not answering, so the PHY cannot be woken here\n",
				   woken);
		else if (!woken)
			netdev_err(p->ndev,
				   "PHY is not powered down, so the stuck EMAC3 reset has another cause\n");
		else {
			netdev_info(p->ndev, "PHY was powered down, woke it up and retrying the EMAC3 reset\n");
			err = smap_emac3_soft_reset(p);
		}

		if (err) {
			netdev_err(p->ndev,
				   "EMAC3 soft reset did not complete\n");
			return err;
		}
	}

	emac3_write(p, SMAP_EMAC3_MODE1, p->mode1);

	smap_w16(p, SMAP_INTR_CLR, SMAP_INTR_ALL);
	emac3_write(p, SMAP_EMAC3_INTR_STAT, E3_INTR_ALL);

	smap_emac3_set_defaults(p);
	smap_bd_init(p);

	return 0;
}

/* -------------------------------------------------------------------- TX */

/* Called with p->lock held. */
static void smap_tx_reclaim(struct smap_priv *p)
{
	struct net_device *ndev = p->ndev;

	while (p->txused > 0) {
		u16 stat = smap_r16(p, TXBD(p->txbdi, SMAP_BD_CTRL_STAT));
		u16 len;

		if (stat & SMAP_BD_TX_READY)
			break;

		len = smap_r16(p, TXBD(p->txbdi, SMAP_BD_LENGTH));
		p->txfree += ALIGN(len, 4);

		if (stat & SMAP_TX_ERR_MASK) {
			ndev->stats.tx_errors++;
			if (stat & (SMAP_BD_TX_ECOLL | SMAP_BD_TX_LCOLL))
				ndev->stats.tx_aborted_errors++;
			if (stat & SMAP_BD_TX_LOSSCR)
				ndev->stats.tx_carrier_errors++;
			if (stat & SMAP_BD_TX_UNDERRUN)
				ndev->stats.tx_fifo_errors++;
			netdev_dbg(ndev, "tx bd %d status %04x\n",
				   p->txbdi, stat);
		} else {
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += len;
		}
		if (stat & (SMAP_BD_TX_MCOLL | SMAP_BD_TX_SCOLL))
			ndev->stats.collisions++;

		p->txused--;
		p->txbdi = (p->txbdi + 1) % SMAP_BD_MAX_ENTRY;
	}
}

/* Room for one more maximum-size frame?  Called with p->lock held. */
static bool smap_tx_room(struct smap_priv *p)
{
	return p->txused < SMAP_TXBD_LIMIT &&
	       p->txfree >= ALIGN(SMAP_TXMAXSIZE, 4);
}

static netdev_tx_t smap_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned int len = skb->len;
	unsigned int txlen, off, i;
	unsigned long flags;
	const u32 *w;

	if (len > SMAP_TXMAXSIZE) {
		ndev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	txlen = ALIGN(len, 4);

	spin_lock_irqsave(&p->lock, flags);

	smap_tx_reclaim(p);
	if (p->txused >= SMAP_TXBD_LIMIT || p->txfree < txlen) {
		netif_stop_queue(ndev);
		spin_unlock_irqrestore(&p->lock, flags);
		return NETDEV_TX_BUSY;
	}

	/*
	 * Bounce: skb data is 2 (mod 4) aligned, the FIFO wants whole 32-bit
	 * words, and the padding bytes must be zero.
	 */
	memcpy(p->txbuf, skb->data, len);
	if (txlen > len)
		memset((u8 *)p->txbuf + len, 0, txlen - len);

	/* Memory -> FIFO, then a descriptor, then the MAC. Sony's order. */
	off = p->txbwp;
	smap_w16(p, SMAP_TXFIFO_WR_PTR, off);
	w = p->txbuf;
	for (i = 0; i < txlen; i += 4)
		smap_w32(p, SMAP_TXFIFO_DATA, *w++);

	smap_w16(p, TXBD(p->txbds, SMAP_BD_LENGTH), len);
	smap_w16(p, TXBD(p->txbds, SMAP_BD_POINTER), SMAP_TXBUFBASE + off);
	smap_w8(p, SMAP_TXFIFO_FRAME_INC, 1);
	smap_w16(p, TXBD(p->txbds, SMAP_BD_CTRL_STAT),
		 SMAP_BD_TX_READY | SMAP_BD_TX_GENFCS | SMAP_BD_TX_GENPAD);

	p->txused++;
	p->txbds = (p->txbds + 1) % SMAP_BD_MAX_ENTRY;
	p->txfree -= txlen;
	p->txbwp = (off + txlen) % SMAP_TXBUFSIZE;

	emac3_write(p, SMAP_EMAC3_TxMODE0, E3_TX_GNP_0);

	if (!smap_tx_room(p))
		netif_stop_queue(ndev);

	spin_unlock_irqrestore(&p->lock, flags);

	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static void smap_tx_timeout(struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	netdev_warn(ndev, "tx timeout: used %d free %d bd[%d] %04x intr %04x mode0 %08x\n",
		    p->txused, p->txfree, p->txbdi,
		    smap_r16(p, TXBD(p->txbdi, SMAP_BD_CTRL_STAT)),
		    smap_r16(p, SMAP_INTR_STAT),
		    emac3_read(p, SMAP_EMAC3_MODE0));
	smap_tx_reclaim(p);
	emac3_write(p, SMAP_EMAC3_TxMODE0, E3_TX_GNP_0);
	ndev->stats.tx_errors++;
	if (smap_tx_room(p))
		netif_wake_queue(ndev);
	spin_unlock_irqrestore(&p->lock, flags);
}

/* -------------------------------------------------------------------- RX */

/*
 * Every access to the SPEED register block goes through p->lock, including
 * the receive FIFO drain below.  Sony's driver does the same in its PIO
 * path -- rhino-2.4.17-smap/smap.c:642 wraps the RXFIFO_RD_PTR store and
 * the RXFIFO_DATA loop in the very spinlock that guards the transmit FIFO.
 * Reading the receive FIFO unlocked from NAPI works for as long as nothing
 * transmits at the same time, which is why ping, DHCP and short SSH
 * commands never showed it, and a sustained transfer killed the interface
 * within seconds.
 *
 * The buffer copy, the skb allocation and napi_gro_receive() stay outside
 * the lock: they touch no register, and holding a spinlock with interrupts
 * off across an allocation would be far worse than the race it closes.
 */
static int smap_rx(struct smap_priv *p, int budget)
{
	struct net_device *ndev = p->ndev;
	unsigned long flags;
	int work = 0;

	while (work < budget) {
		u16 stat, len, ptr;
		unsigned int rxlen, i;
		struct sk_buff *skb;
		bool deliver;
		u32 *w;

		spin_lock_irqsave(&p->lock, flags);

		stat = smap_r16(p, RXBD(p->rxbdi, SMAP_BD_CTRL_STAT));
		if (stat & SMAP_BD_RX_EMPTY) {
			spin_unlock_irqrestore(&p->lock, flags);
			break;
		}

		len = smap_r16(p, RXBD(p->rxbdi, SMAP_BD_LENGTH));
		ptr = smap_r16(p, RXBD(p->rxbdi, SMAP_BD_POINTER));

		deliver = !(stat & SMAP_RX_ERR_MASK) && len >= SMAP_RXMINSIZE &&
			  len <= SMAP_RXMAXSIZE;

		if (deliver) {
			/* FIFO -> memory: whole words via the bounce buffer. */
			rxlen = ALIGN(len, 4);
			smap_w16(p, SMAP_RXFIFO_RD_PTR, ptr & 0x3ffc);
			w = p->rxbuf;
			for (i = 0; i < rxlen; i += 4)
				*w++ = smap_r32(p, SMAP_RXFIFO_DATA);
		} else {
			ndev->stats.rx_errors++;
			if (stat & SMAP_BD_RX_BADFCS)
				ndev->stats.rx_crc_errors++;
			if (stat & (SMAP_BD_RX_FRMTOOLONG | SMAP_BD_RX_RUNTFRM |
				    SMAP_BD_RX_SHORTEVNT))
				ndev->stats.rx_length_errors++;
			if (stat & SMAP_BD_RX_ALIGNERR)
				ndev->stats.rx_frame_errors++;
			if (stat & SMAP_BD_RX_OVERRUN)
				ndev->stats.rx_fifo_errors++;
			netdev_dbg(ndev, "rx bd %d status %04x len %u\n",
				   p->rxbdi, stat, len);
		}

		/* Release the descriptor before handing the packet up. */
		smap_w8(p, SMAP_RXFIFO_FRAME_DEC, 1);
		smap_w16(p, RXBD(p->rxbdi, SMAP_BD_CTRL_STAT),
			 SMAP_BD_RX_EMPTY);
		p->rxbdi = (p->rxbdi + 1) % SMAP_BD_MAX_ENTRY;

		spin_unlock_irqrestore(&p->lock, flags);

		work++;
		if (!deliver)
			continue;

		skb = netdev_alloc_skb_ip_align(ndev, len);
		if (!skb) {
			ndev->stats.rx_dropped++;
			continue;
		}
		skb_put_data(skb, p->rxbuf, len);
		skb->protocol = eth_type_trans(skb, ndev);
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += len;
		napi_gro_receive(&p->napi, skb);
	}

	return work;
}

static int smap_poll(struct napi_struct *napi, int budget)
{
	struct smap_priv *p = container_of(napi, struct smap_priv, napi);
	struct net_device *ndev = p->ndev;
	unsigned long flags;
	u16 stat;
	int work;

	/*
	 * Acknowledge everything we own before touching the rings.
	 *
	 * RXEND/TXEND: normally the handler has just done this, but this
	 * path is also reached from the watchdog timer -- in polling mode
	 * and when the interrupt path has stalled -- and then nothing else
	 * drops the line.  Clearing first and reading the rings second is
	 * the safe order: a frame that lands after the clear sets the bit
	 * again and raises a fresh edge.
	 *
	 * TXDNV/RXDNV: not in SMAP_INTR_USED, so the IOP never acknowledges
	 * them (it clears STAT & MASK only).  Whether a stale one can hold
	 * the line on its own is not established -- the proven killer is
	 * the END race in smap_rxtx_irq() -- but Sony's driver never lets
	 * them sit either (rhino-2.4.17-smap/smap.c:331-335, :755-758), and
	 * clearing costs one write and needs no mask bit.
	 */
	spin_lock_irqsave(&p->lock, flags);
	stat = smap_r16(p, SMAP_INTR_STAT) &
	       (INTR_RXEND | INTR_TXEND | INTR_TXDNV | INTR_RXDNV);
	if (stat) {
		smap_w16(p, SMAP_INTR_CLR, stat);
		if (stat & (INTR_TXDNV | INTR_RXDNV))
			p->dnv_events++;
	}
	spin_unlock_irqrestore(&p->lock, flags);

	work = smap_rx(p, budget);

	spin_lock_irqsave(&p->lock, flags);
	smap_tx_reclaim(p);
	if (netif_queue_stopped(ndev) && smap_tx_room(p))
		netif_wake_queue(ndev);
	spin_unlock_irqrestore(&p->lock, flags);

	/*
	 * Nothing to unmask: the sources were never masked.  A frame that
	 * lands between the last ring check and napi_complete_done() has
	 * raised a fresh edge, so the handler runs again; if it finds us
	 * still running, napi_schedule_prep() records NAPI_STATE_MISSED and
	 * napi_complete_done() reschedules us.  Nothing is lost.
	 */
	if (work < budget)
		napi_complete_done(napi, work);

	return work;
}

/* --------------------------------------------------- poll-mode fallback */

/*
 * Has the hardware finished with a descriptor?  Read without the lock: the
 * two descriptor reads go straight to the chip, and p->txused only makes
 * the test miss one round if it changes underneath, which the next tick
 * catches.
 */
static bool smap_work_pending(struct smap_priv *p)
{
	if (!(smap_r16(p, RXBD(p->rxbdi, SMAP_BD_CTRL_STAT)) &
	      SMAP_BD_RX_EMPTY))
		return true;

	if (p->txused > 0 &&
	    !(smap_r16(p, TXBD(p->txbdi, SMAP_BD_CTRL_STAT)) &
	      SMAP_BD_TX_READY))
		return true;

	return false;
}

static void smap_timer_fn(struct timer_list *t)
{
	struct smap_priv *p = from_timer(p, t, timer);
	struct net_device *ndev = p->ndev;
	unsigned long period = msecs_to_jiffies(SMAP_WATCHDOG_MS);

	if (!p->polling) {
		unsigned int count = p->irq_count;

		/*
		 * Two faults look identical from here and both kill the
		 * interface, so both end the same way -- in polling mode:
		 * a relay that never delivers anything at all, and a relay
		 * that falls silent after having worked for a while.  The
		 * earlier version of this test only caught the first, by
		 * asking whether irq_count was still zero; on hardware the
		 * interrupts stopped after 834 of them and the interface
		 * stayed dead with the fallback never arming.  Comparing
		 * against the previous tick catches both.
		 */
		if (!smap_work_pending(p) || count != p->irq_count_seen) {
			p->irq_count_seen = count;
			p->stall_ticks = 0;
			goto rearm;
		}

		if (++p->stall_ticks < SMAP_STALL_TICKS)
			goto rearm;

		/*
		 * Registers, not guesses: this is the only moment at which
		 * the chip can be asked what it thinks is going on, and the
		 * answer says whether the interrupt mask was lost (enable
		 * missing the RXEND/TXEND bits) or whether the chip stopped
		 * raising the line with the mask still intact.
		 */
		netdev_warn(ndev, "SPEED interrupts stopped after %u (dnv cleared %u): intr stat %04x enable %04x, rx bd[%d] %04x, tx bd[%d] %04x, txused %d\n",
			    count, p->dnv_events,
			    smap_r16(p, SMAP_INTR_STAT),
			    smap_r16(p, SMAP_INTR_ENABLE),
			    p->rxbdi,
			    smap_r16(p, RXBD(p->rxbdi, SMAP_BD_CTRL_STAT)),
			    p->txbdi,
			    smap_r16(p, TXBD(p->txbdi, SMAP_BD_CTRL_STAT)),
			    p->txused);
		netdev_warn(ndev, "switching to polling every %d ms; the data path is unaffected, this is the interrupt path alone\n",
			    poll_ms);
		p->polling = true;
	}

	if (napi_schedule_prep(&p->napi))
		__napi_schedule(&p->napi);

	period = p->poll_period;

rearm:
	mod_timer(&p->timer, jiffies + period);
}

/* ------------------------------------------------------------ interrupts */

/*
 * RXEND and TXEND, relayed from the IOP.  Clear the source first and only
 * then look at the rings: anything that arrives after the clear raises the
 * line again on its own.
 *
 * We deliberately do NOT mask the sources for the duration of the NAPI run,
 * although that is what a normal driver would do.  The interrupt mask at
 * offset 0x2a belongs to the IOP: iopmod's spd_enable_irq____() and
 * spd_disable_irq____() (iopmod/builtin/spd-irq.c:61 and :68) read-modify-
 * write it, and drivers/ps2/iop-irq.c wires only irq_startup and
 * irq_shutdown, so the IOP touches that register at request_irq() and
 * free_irq() time and never again.  Two processors doing an unsynchronised
 * read-modify-write on one hardware register lose each other's stores, and
 * a lost unmask is permanent -- nothing on the IOP side ever puts the bit
 * back.  Measured on hardware: the status register ends up at 0x4034 with
 * RXEND and TXEND pending and no interrupts being delivered at all.
 *
 * Leaving the sources unmasked costs an interrupt entry that finds NAPI
 * already scheduled, which napi_schedule_prep() rejects for free.  That is
 * far cheaper than losing the interface.
 */
static irqreturn_t smap_rxtx_irq(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct smap_priv *p = netdev_priv(ndev);

	/*
	 * Clear BOTH END sources, whichever virtual IRQ brought us here.
	 *
	 * The SPEED chip has one interrupt line, into an edge-triggered INTC
	 * on the IOP.  On the rising edge the IOP snapshots STAT & MASK and
	 * relays one virtual IRQ per bit it saw (iopmod/builtin/spd-irq.c
	 * :71-84).  A source that sets after that snapshot raises no new
	 * edge -- the line is already high.  Versions 1-7 then cleared only
	 * the bit their own IRQ stood for, so the other source kept the line
	 * high, the INTC never saw another edge, and no SPEED interrupt was
	 * delivered again.  That is exactly the state the hardware showed:
	 * STAT 4034 with RXEND and TXEND both pending, MASK 0070 intact,
	 * counters frozen, while USB (relayed the same way) kept working.
	 *
	 * Sony's driver had one handler for the whole chip: it read STAT,
	 * cleared every END it found, and even cross-checked the other
	 * ring's frame counter "for race condition of TxEND/RxEND"
	 * (rhino-2.4.17-smap/smap.c:876-936).  With one handler per source
	 * we get the same effect by acknowledging both here; NAPI drains
	 * both rings regardless, so nothing is lost by clearing a source
	 * before its ring has been read.
	 */
	smap_w16(p, SMAP_INTR_CLR, INTR_RXEND | INTR_TXEND);
	p->irq_count++;

	if (napi_schedule_prep(&p->napi))
		__napi_schedule(&p->napi);

	return IRQ_HANDLED;
}

static irqreturn_t smap_emac3_irq(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct smap_priv *p = netdev_priv(ndev);
	u32 stat;

	smap_w16(p, SMAP_INTR_CLR, INTR_EMAC3);
	p->irq_count++;

	stat = emac3_read(p, SMAP_EMAC3_INTR_STAT);
	emac3_write(p, SMAP_EMAC3_INTR_STAT, stat);
	p->emac3_events++;

	if (stat & E3_DEAD_ALL) {
		/* Transmitter stalled: ask for the next packet again. */
		ndev->stats.tx_errors++;
		emac3_write(p, SMAP_EMAC3_TxMODE0, E3_TX_GNP_0);
	}
	if (stat & (E3_INTR_TX_ERR_0 | E3_INTR_TX_ERR_1))
		ndev->stats.tx_errors++;

	if (net_ratelimit())
		netdev_dbg(ndev, "emac3 interrupt status %08x\n", stat);

	return IRQ_HANDLED;
}

static void smap_free_irqs(struct smap_priv *p)
{
	if (!p->irqs_requested)
		return;
	free_irq(p->irq_rx, p->ndev);
	free_irq(p->irq_tx, p->ndev);
	free_irq(p->irq_emac3, p->ndev);
	p->irqs_requested = false;
}

/*
 * Each request is an RPC to the IOP IRQ relay, which also sets the bit in
 * the SPEED interrupt mask on its side; each free releases it again.
 */
static int smap_request_irqs(struct smap_priv *p)
{
	struct net_device *ndev = p->ndev;
	int err;

	err = request_irq(p->irq_rx, smap_rxtx_irq, 0, "ps2-smap-rx", ndev);
	if (err) {
		netdev_err(ndev, "cannot get RXEND irq %d (%d)\n",
			   p->irq_rx, err);
		return err;
	}
	err = request_irq(p->irq_tx, smap_rxtx_irq, 0, "ps2-smap-tx", ndev);
	if (err) {
		netdev_err(ndev, "cannot get TXEND irq %d (%d)\n",
			   p->irq_tx, err);
		goto err_tx;
	}
	err = request_irq(p->irq_emac3, smap_emac3_irq, 0, "ps2-smap-emac3",
			  ndev);
	if (err) {
		netdev_err(ndev, "cannot get EMAC3 irq %d (%d)\n",
			   p->irq_emac3, err);
		goto err_emac3;
	}
	p->irqs_requested = true;

	return 0;

err_emac3:
	free_irq(p->irq_tx, ndev);
err_tx:
	free_irq(p->irq_rx, ndev);
	return err;
}

/* ----------------------------------------------------------------- phylib */

static void smap_adjust_link(struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	struct phy_device *phydev = p->phydev;
	unsigned long flags;
	u32 mode1;

	if (!phydev->link) {
		if (p->link) {
			p->link = false;
			phy_print_status(phydev);
		}
		return;
	}

	mode1 = SMAP_EMAC3_MODE1_DEF &
		~(E3_FDX_ENABLE | E3_FLOWCTRL_ENABLE | E3_ALLOW_PF |
		  E3_MEDIA_MSK);
	if (phydev->duplex == DUPLEX_FULL)
		mode1 |= E3_FDX_ENABLE | E3_FLOWCTRL_ENABLE | E3_ALLOW_PF;
	mode1 |= phydev->speed == SPEED_100 ? E3_MEDIA_100M : E3_MEDIA_10M;

	if (!p->link || mode1 != p->mode1) {
		spin_lock_irqsave(&p->lock, flags);
		smap_mac_disable(p);
		emac3_write(p, SMAP_EMAC3_MODE1, mode1);
		p->mode1 = mode1;
		if (netif_running(ndev))
			smap_mac_enable(p);
		spin_unlock_irqrestore(&p->lock, flags);
	}

	p->link = true;
	phy_print_status(phydev);
}

static int smap_mdio_setup(struct smap_priv *p)
{
	struct net_device *ndev = p->ndev;
	struct phy_device *phydev;
	struct mii_bus *bus;
	int err;

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	bus->name = "ps2-smap-mdio";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", DRV_NAME);
	bus->read = smap_mdio_read;
	bus->write = smap_mdio_write;
	bus->priv = p;
	bus->parent = &p->pdev->dev;
	bus->phy_mask = ~BIT(DsPHYTER_ADDRESS);	/* only address 1 is wired */

	err = mdiobus_register(bus);
	if (err) {
		netdev_err(ndev, "mdiobus_register failed (%d)\n", err);
		mdiobus_free(bus);
		return err;
	}
	p->mii = bus;

	phydev = phy_find_first(bus);
	if (!phydev) {
		netdev_err(ndev, "no PHY found on MII address %d\n",
			   DsPHYTER_ADDRESS);
		err = -ENODEV;
		goto err_unregister;
	}

	err = phy_connect_direct(ndev, phydev, smap_adjust_link,
				 PHY_INTERFACE_MODE_MII);
	if (err) {
		netdev_err(ndev, "phy_connect_direct failed (%d)\n", err);
		goto err_unregister;
	}
	p->phydev = phydev;

	/* 10/100 only; the PHY reports so itself, but be explicit. */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);

	phy_attached_info(phydev);

	return 0;

err_unregister:
	mdiobus_unregister(bus);
	mdiobus_free(bus);
	p->mii = NULL;
	return err;
}

static void smap_mdio_teardown(struct smap_priv *p)
{
	if (p->phydev) {
		phy_disconnect(p->phydev);
		p->phydev = NULL;
		/*
		 * phy_disconnect() has just powered the PHY down.  Wake it
		 * while the chip is still ours, or the next probe of this
		 * driver fails at the EMAC3 reset; see smap_phy_wake().
		 */
		smap_phy_wake(p);
	}
	if (p->mii) {
		mdiobus_unregister(p->mii);
		mdiobus_free(p->mii);
		p->mii = NULL;
	}
}

/* --------------------------------------------------------- net_device_ops */

static int smap_open(struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned long flags;
	int err;

	err = smap_hw_init(p);
	if (err)
		return err;

	p->irq_count = 0;
	p->irq_count_seen = 0;
	p->stall_ticks = 0;
	p->dnv_events = 0;
	p->polling = (poll_mode == SMAP_POLL_ONLY);

	if (!p->polling) {
		err = smap_request_irqs(p);
		if (err)
			return err;
	}

	napi_enable(&p->napi);

	spin_lock_irqsave(&p->lock, flags);
	smap_w16(p, SMAP_INTR_CLR, SMAP_INTR_ALL);
	emac3_write(p, SMAP_EMAC3_INTR_STAT, E3_INTR_ALL);
	emac3_write(p, SMAP_EMAC3_INTR_ENABLE, SMAP_E3_INTR_USED);
	smap_intr_set(p, SMAP_INTR_USED, true);
	smap_mac_enable(p);
	spin_unlock_irqrestore(&p->lock, flags);

	p->link = false;
	phy_start(p->phydev);

	netif_start_queue(ndev);

	if (poll_mode != SMAP_POLL_IRQ)
		mod_timer(&p->timer, jiffies +
			  (p->polling ? p->poll_period :
			   msecs_to_jiffies(SMAP_WATCHDOG_MS)));

	netdev_info(ndev, "open: %s, intr enable %04x stat %04x, emac3 mode0 %08x mode1 %08x\n",
		    p->polling ? "polling only" :
		    poll_mode == SMAP_POLL_AUTO ?
				"interrupts, polling on standby" :
				"interrupts only",
		    smap_r16(p, SMAP_INTR_ENABLE), smap_r16(p, SMAP_INTR_STAT),
		    emac3_read(p, SMAP_EMAC3_MODE0),
		    emac3_read(p, SMAP_EMAC3_MODE1));

	return 0;
}

static int smap_close(struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned long flags;

	netif_stop_queue(ndev);
	del_timer_sync(&p->timer);
	phy_stop(p->phydev);
	napi_disable(&p->napi);

	spin_lock_irqsave(&p->lock, flags);
	smap_intr_set(p, SMAP_INTR_ALL, false);
	emac3_write(p, SMAP_EMAC3_INTR_ENABLE, 0);
	smap_mac_disable(p);
	smap_w16(p, SMAP_INTR_CLR, SMAP_INTR_ALL);
	emac3_write(p, SMAP_EMAC3_INTR_STAT, E3_INTR_ALL);
	spin_unlock_irqrestore(&p->lock, flags);

	smap_free_irqs(p);

	netdev_info(ndev, "closed: %lu rx, %lu tx, %u interrupts, %u emac3 events, %u dnv cleared, %s\n",
		    ndev->stats.rx_packets, ndev->stats.tx_packets,
		    p->irq_count, p->emac3_events, p->dnv_events,
		    p->polling ? "ran in polling mode" : "ran on interrupts");

	return 0;
}

static void smap_set_rx_mode(struct net_device *ndev)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned long flags;
	u32 v;

	v = E3_RX_STRIP_PAD | E3_RX_STRIP_FCS | E3_RX_INDIVID_ADDR |
	    E3_RX_BCAST;
	if (ndev->flags & IFF_PROMISC)
		v |= E3_RX_PROMISC;
	else if ((ndev->flags & IFF_ALLMULTI) || !netdev_mc_empty(ndev))
		v |= E3_RX_PROMISC_MCAST;	/* no hash filter: take all */

	spin_lock_irqsave(&p->lock, flags);
	smap_mac_disable(p);
	emac3_write(p, SMAP_EMAC3_RxMODE, v);
	if (netif_running(ndev))
		smap_mac_enable(p);
	spin_unlock_irqrestore(&p->lock, flags);
}

static int smap_set_mac_address(struct net_device *ndev, void *addr)
{
	struct smap_priv *p = netdev_priv(ndev);
	unsigned long flags;
	int err;

	err = eth_mac_addr(ndev, addr);
	if (err)
		return err;

	spin_lock_irqsave(&p->lock, flags);
	smap_mac_disable(p);
	smap_emac3_set_defaults(p);
	if (netif_running(ndev))
		smap_mac_enable(p);
	spin_unlock_irqrestore(&p->lock, flags);

	return 0;
}

static int smap_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	struct smap_priv *p = netdev_priv(ndev);

	if (!netif_running(ndev) || !p->phydev)
		return -EINVAL;

	return phy_mii_ioctl(p->phydev, ifr, cmd);
}

static const struct net_device_ops smap_netdev_ops = {
	.ndo_open		= smap_open,
	.ndo_stop		= smap_close,
	.ndo_start_xmit		= smap_start_xmit,
	.ndo_set_rx_mode	= smap_set_rx_mode,
	.ndo_set_mac_address	= smap_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= smap_tx_timeout,
	.ndo_do_ioctl		= smap_ioctl,
};

static void smap_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, "ee:14000000", sizeof(info->bus_info));
}

static const struct ethtool_ops smap_ethtool_ops = {
	.get_drvinfo		= smap_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
};

/* ----------------------------------------------------------- platform glue */

static int smap_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct smap_priv *p;
	struct resource *res, *res_smap;
	void __iomem *base;
	u16 power, rev1, rev3;
	u8 mac[ETH_ALEN];
	int irq_rx, irq_tx, irq_emac3;
	int err;

	/*
	 * The EE window raises a data bus error while the bay is unpowered,
	 * so ask the IOP first.  drivers/ps2/iop-dev9.c powers the bay at
	 * module load when it carries the 2026-09-05 fixes.
	 */
	err = iop_readw(&power, IOP_DEV9_POWER);
	if (err < 0) {
		pr_err(DRV_NAME ": cannot read the DEV9 power register (%d); is iop-dev9 loaded?\n",
		       err);
		return err;
	}
	if (!(power & DEV9_POWER_ON)) {
		pr_err(DRV_NAME ": expansion bay is not powered (DEV9 power %04x); this needs a kernel whose iop-dev9 initialises the bay\n",
		       power);
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "speed");
	res_smap = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smap");
	if (!res || !res_smap)
		return -ENODEV;

	/*
	 * One mapping spanning both resources, so that register offsets stay
	 * relative to the start of the chip window, as the hardware has them.
	 * The gap in the middle holds the ATA registers of the same chip,
	 * which pata-ps2 owns; that is why the two resources above describe
	 * only what this driver touches.
	 */
	base = devm_ioremap(&pdev->dev, res->start,
			    res_smap->end - res->start + 1);
	if (!base)
		return -ENOMEM;

	rev1 = readw(base + SPD_R_REV_1);
	rev3 = readw(base + SPD_R_REV_3);
	if (rev1 == 0xffff || rev1 == 0x0000 || !(rev3 & SPD_CAPS_SMAP)) {
		pr_err(DRV_NAME ": SPEED rev1 %04x rev3 %04x: no SMAP here\n",
		       rev1, rev3);
		return -ENODEV;
	}

	irq_rx = platform_get_irq_byname(pdev, "rx");
	irq_tx = platform_get_irq_byname(pdev, "tx");
	irq_emac3 = platform_get_irq_byname(pdev, "emac3");
	if (irq_rx < 0 || irq_tx < 0 || irq_emac3 < 0)
		return -ENODEV;

	ndev = alloc_etherdev(sizeof(*p));
	if (!ndev)
		return -ENOMEM;

	p = netdev_priv(ndev);
	p->ndev = ndev;
	p->pdev = pdev;
	p->base = base;
	SET_NETDEV_DEV(ndev, &pdev->dev);
	p->mode1 = SMAP_EMAC3_MODE1_DEF;
	p->irq_rx = irq_rx;
	p->irq_tx = irq_tx;
	p->irq_emac3 = irq_emac3;
	spin_lock_init(&p->lock);

	if (poll_mode < SMAP_POLL_IRQ || poll_mode > SMAP_POLL_ONLY) {
		pr_warn(DRV_NAME ": poll=%d out of range, using %d\n",
			poll_mode, SMAP_POLL_AUTO);
		poll_mode = SMAP_POLL_AUTO;
	}
	if (poll_ms < 1)
		poll_ms = 1;
	p->poll_period = max(msecs_to_jiffies(poll_ms), 1UL);
	timer_setup(&p->timer, smap_timer_fn, 0);

	p->txbuf = kzalloc(SMAP_BOUNCE_SIZE, GFP_KERNEL);
	p->rxbuf = kzalloc(SMAP_BOUNCE_SIZE, GFP_KERNEL);
	if (!p->txbuf || !p->rxbuf) {
		err = -ENOMEM;
		goto err_free;
	}

	if (smap_read_mac(p, mac) == 0) {
		ether_addr_copy(ndev->dev_addr, mac);
	} else {
		pr_warn(DRV_NAME ": EEPROM checksum mismatch, using a random MAC\n");
		eth_hw_addr_random(ndev);
	}

	err = smap_hw_init(p);
	if (err)
		goto err_free;

	err = smap_mdio_setup(p);
	if (err)
		goto err_free;

	ndev->netdev_ops = &smap_netdev_ops;
	ndev->ethtool_ops = &smap_ethtool_ops;
	ndev->watchdog_timeo = 5 * HZ;
	netif_napi_add(ndev, &p->napi, smap_poll, 16);
	netif_carrier_off(ndev);

	err = register_netdev(ndev);
	if (err) {
		pr_err(DRV_NAME ": register_netdev failed (%d)\n", err);
		goto err_mdio;
	}

	netdev_info(ndev, "PlayStation 2 SMAP at %08x, SPEED rev1 %04x rev3 %04x, MAC %pM, irqs %d/%d/%d\n",
		    (unsigned int)res->start, rev1, rev3, ndev->dev_addr,
		    p->irq_rx, p->irq_tx, p->irq_emac3);

	platform_set_drvdata(pdev, ndev);

	return 0;

err_mdio:
	netif_napi_del(&p->napi);
	smap_mdio_teardown(p);
err_free:
	kfree(p->txbuf);
	kfree(p->rxbuf);
	free_netdev(ndev);
	return err;
}

static int smap_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smap_priv *p = netdev_priv(ndev);

	unregister_netdev(ndev);
	del_timer_sync(&p->timer);
	netif_napi_del(&p->napi);
	smap_mdio_teardown(p);
	kfree(p->txbuf);
	kfree(p->rxbuf);
	free_netdev(ndev);

	return 0;
}

static struct platform_driver smap_driver = {
	.remove	= smap_remove,
	.driver	= {
		.name	= DRV_NAME,
	},
};

/* ------------------------------------------------------------ module init */

/*
 * Nothing in the board setup or in a device tree describes the SMAP, so the
 * module creates the platform device it then binds to.  The parent device is
 * not cosmetic: phy_attach_direct() reads netdev->dev.parent->driver->owner
 * without checking either pointer, so connecting a PHY to a parentless
 * net_device oopses the kernel at virtual address 0x38.
 */
/*
 * platform_driver_probe() reports a probe that bound no device as -ENODEV, so
 * loading the module fails outright instead of leaving a driver with no eth0
 * behind.  The reason itself has already gone to the log by then.
 */
module_platform_driver_probe(smap_driver, smap_probe);

MODULE_AUTHOR("Hubert Wyrzykiewicz");
MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 SMAP Ethernet driver");
MODULE_LICENSE("GPL");
