/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PlayStation 2 SMAP (Ethernet) register definitions
 *
 * Derived from the original PlayStation 2 Linux kit driver:
 *
 *	linux-2.2.1/drivers/ps2/smap.h
 *	Copyright (C) 2001 Sony Computer Entertainment Inc.
 *	GNU General Public License Version 2
 *
 * Obtained from the "Linux for PlayStation 2" DISC2, SRPMS/KERNEL_1.RPM
 * (kernel-2.2.1_ps2-7.src.rpm).  Names are kept identical to the original
 * so that the two files can be diffed against each other; only the types
 * and the accessor macros were modernised.
 *
 * The MAC is an IBM EMAC3 core sitting inside the SPEED chip of the
 * expansion bay.  The PHY is a National Semiconductor DP83846 ("DsPHYTER")
 * on MII address 1.
 */

#ifndef __PS2_SMAP_REGS_H__
#define __PS2_SMAP_REGS_H__

/*
 * The SPEED chip is mapped at physical 0x14000000.  arch/mips/ps2/memory.c
 * calls set_io_port_base(CKSEG1), so inw(SPD_REGBASE + x) resolves to
 * 0xb4000000 + x -- exactly the SMAP_BASE the Sony driver used.  This is
 * also how drivers/ata/pata_ps2.c reaches the same chip.
 */
#define SPD_REGBASE			0x14000000

/* SPEED identification, offsets as used by pata_ps2.c and ps2sdk. */
#define SPD_R_REV			0x00
#define SPD_R_REV_1			0x02
#define SPD_R_REV_3			0x04
#define SPD_R_REV_8			0x0e

/*
 * Capability bits in SPD_R_REV_3.  These names come from ps2sdk, not from
 * any Sony header we hold, so treat a mismatch here as "unverified" rather
 * than as a hardware fault.  The raw value is always logged as well.
 */
#define SPD_CAPS_SMAP			(1 << 0)
#define SPD_CAPS_ATA			(1 << 1)
#define SPD_CAPS_UART			(1 << 3)
#define SPD_CAPS_DVR			(1 << 4)

/* SMAP register window, relative to SPD_REGBASE. */
#define SMAP_BD_BASE			0x3000
#define SMAP_BD_BASE_TX			(SMAP_BD_BASE + 0x0000)
#define SMAP_BD_BASE_RX			(SMAP_BD_BASE + 0x0200)
#define SMAP_BD_SIZE			512
#define SMAP_BD_MAX_ENTRY		64

#define SMAP_TXBUFBASE			0x1000
#define SMAP_TXBUFSIZE			(4 * 1024)
#define SMAP_RXBUFBASE			0x4000
#define SMAP_RXBUFSIZE			(16 * 1024)

/* Serial EEPROM bit-banged through the PIO port. */
#define SMAP_PIOPORT_DIR		0x2c
#define SMAP_PIOPORT_IN			0x2e
#define SMAP_PIOPORT_OUT		0x2e
#define   PP_DOUT			(1 << 4)	/* data out, read  */
#define   PP_DIN			(1 << 5)	/* data in,  write */
#define   PP_SCLK			(1 << 6)	/* clock,    write */
#define   PP_CSEL			(1 << 7)	/* chip sel, write */
#define   PP_OP_READ			2		/* 2b'10 */

/* SMAP-level interrupts (not EMAC3's own). */
#define SMAP_INTR_STAT			0x28
#define SMAP_INTR_CLR			0x128
#define SMAP_INTR_ENABLE		0x2a
#define   INTR_EMAC3			(1 << 6)
#define   INTR_RXEND			(1 << 5)
#define   INTR_TXEND			(1 << 4)
/* RXDNV and TXDNV mean the descriptor is not valid. */
#define   INTR_RXDNV			(1 << 3)
#define   INTR_TXDNV			(1 << 2)
#define   INTR_BITMSK			0x7c

#define SMAP_BD_MODE			0x102
#define   BD_SWAP			(1 << 0)

#define SMAP_TXFIFO_CTRL		0x1000
#define   TXFIFO_RESET			(1 << 0)
#define SMAP_TXFIFO_WR_PTR		0x1004
#define SMAP_TXFIFO_FRAME_CNT		0x100c
#define SMAP_TXFIFO_FRAME_INC		0x1010
#define SMAP_TXFIFO_DATA		0x1100

#define SMAP_RXFIFO_CTRL		0x1030
#define   RXFIFO_RESET			(1 << 0)
#define SMAP_RXFIFO_RD_PTR		0x1034
#define SMAP_RXFIFO_FRAME_CNT		0x103c
#define SMAP_RXFIFO_FRAME_DEC		0x1040
#define SMAP_RXFIFO_DATA		0x1200

#define SMAP_FIFO_ADDR			0x1300
#define   FIFO_CMD_READ			(1 << 1)
#define   FIFO_DATA_SWAP		(1 << 0)
#define SMAP_FIFO_DATA			0x1308

/*
 * EMAC3.  Every 32-bit EMAC3 register is reached as two 16-bit accesses,
 * high half first -- the SPEED bus is 16 bits wide.  See emac3_read().
 */
#define SMAP_EMAC3_BASE			0x2000
#define SMAP_EMAC3_MODE0		(SMAP_EMAC3_BASE + 0x00)
#define   E3_RXMAC_IDLE			(1 << 31)
#define   E3_TXMAC_IDLE			(1 << 30)
#define   E3_SOFT_RESET			(1 << 29)
#define   E3_TXMAC_ENABLE		(1 << 28)
#define   E3_RXMAC_ENABLE		(1 << 27)

#define SMAP_EMAC3_MODE1		(SMAP_EMAC3_BASE + 0x04)
#define   E3_FDX_ENABLE			(1 << 31)
#define   E3_IGNORE_SQE			(1 << 24)
#define   E3_MEDIA_10M			(0 << 22)
#define   E3_MEDIA_100M			(1 << 22)
#define   E3_MEDIA_MSK			(3 << 22)
#define   E3_RXFIFO_2K			(2 << 20)
#define   E3_TXFIFO_1K			(1 << 18)
#define   E3_TXREQ0_SINGLE		(0 << 15)
#define   E3_TXREQ1_SINGLE		(0 << 13)

#define SMAP_EMAC3_TxMODE0		(SMAP_EMAC3_BASE + 0x08)
#define SMAP_EMAC3_TxMODE1		(SMAP_EMAC3_BASE + 0x0c)
#define SMAP_EMAC3_RxMODE		(SMAP_EMAC3_BASE + 0x10)
#define   E3_RX_STRIP_PAD		(1 << 31)
#define   E3_RX_STRIP_FCS		(1 << 30)
#define   E3_RX_PROMISC			(1 << 24)
#define   E3_RX_INDIVID_ADDR		(1 << 22)
#define   E3_RX_BCAST			(1 << 20)
#define   E3_RX_MCAST			(1 << 19)

#define SMAP_EMAC3_INTR_STAT		(SMAP_EMAC3_BASE + 0x14)
#define SMAP_EMAC3_INTR_ENABLE		(SMAP_EMAC3_BASE + 0x18)
#define SMAP_EMAC3_ADDR_HI		(SMAP_EMAC3_BASE + 0x1c)
#define SMAP_EMAC3_ADDR_LO		(SMAP_EMAC3_BASE + 0x20)
#define SMAP_EMAC3_PAUSE_TIMER		(SMAP_EMAC3_BASE + 0x2c)
#define SMAP_EMAC3_INTER_FRAME_GAP	(SMAP_EMAC3_BASE + 0x58)

#define SMAP_EMAC3_STA_CTRL		(SMAP_EMAC3_BASE + 0x5c)
#define   E3_PHY_DATA_MSK		0xffff
#define   E3_PHY_DATA_BITSFT		16
#define   E3_PHY_OP_COMP		(1 << 15)	/* operation complete */
#define   E3_PHY_ERR_READ		(1 << 14)
#define   E3_PHY_READ			(1 << 12)
#define   E3_PHY_WRITE			(2 << 12)
#define   E3_PHY_50M			(0 << 10)
#define   E3_PHY_ADDR_MSK		0x1f
#define   E3_PHY_ADDR_BITSFT		5
#define   E3_PHY_REG_ADDR_MSK		0x1f

#define SMAP_EMAC3_TX_THRESHOLD		(SMAP_EMAC3_BASE + 0x60)
#define SMAP_EMAC3_RX_WATERMARK		(SMAP_EMAC3_BASE + 0x64)
#define SMAP_EMAC3_TX_OCTETS		(SMAP_EMAC3_BASE + 0x68)
#define SMAP_EMAC3_RX_OCTETS		(SMAP_EMAC3_BASE + 0x6c)

/* National Semiconductor DP83846 "DsPHYTER". */
#define NS_OUI				0x080017
#define DsPHYTER_ADDRESS		0x1

#define DsPHYTER_BMCR			0x00
#define   PHY_BMCR_RST			(1 << 15)
#define   PHY_BMCR_100M			(1 << 13)
#define   PHY_BMCR_ANEN			(1 << 12)
#define   PHY_BMCR_PWDN			(1 << 11)
#define   PHY_BMCR_RSAN			(1 << 9)
#define   PHY_BMCR_DUPM			(1 << 8)

#define DsPHYTER_BMSR			0x01
#define   PHY_BMSR_ANCP			(1 << 5)	/* autoneg complete */
#define   PHY_BMSR_LINK			(1 << 2)	/* link status     */

#define DsPHYTER_PHYIDR1		0x02
#define   PHY_IDR1_VAL			(((NS_OUI << 2) >> 8) & 0xffff)
#define DsPHYTER_PHYIDR2		0x03
#define   PHY_IDR2_VMDL			0x2
#define   PHY_IDR2_VAL			((((NS_OUI << 10) & 0xfc00)) | \
					 (((PHY_IDR2_VMDL << 4) & 0x3f0)))
#define   PHY_IDR2_MSK			0xfff0
#define   PHY_IDR2_REV_MSK		0x000f

#define DsPHYTER_ANAR			0x04
#define DsPHYTER_ANLPAR			0x05

/* Extended registers. */
#define DsPHYTER_PHYSTS			0x10
#define   PHY_STS_ANCP			(1 << 4)	/* autoneg complete  */
#define   PHY_STS_LPBK			(1 << 3)	/* loopback          */
#define   PHY_STS_DUPS			(1 << 2)	/* 1:FDX  0:HDX      */
#define   PHY_STS_SPDS			(1 << 1)	/* 1:10M  0:100M     */
#define   PHY_STS_LINK			(1 << 0)	/* link status       */

#define SMAP_LOOP_COUNT			10000

/*
 * Additions for the driver (as opposed to the probe), all transcribed from
 * linux-2.4.17/drivers/ps2/smap.h of the BlackRhino kit, same origin and
 * licence as above.
 */

/* Frame sizes. */
#define SMAP_TXMAXSIZE			(6 + 6 + 2 + 1500)
#define SMAP_RXMAXSIZE			(6 + 6 + 2 + 1500 + 4)
#define SMAP_RXMINSIZE			14

/* Buffer descriptors: 8 bytes each, 64 per ring, 16-bit fields. */
#define SMAP_BD_CTRL_STAT		0x0
#define SMAP_BD_RESERVED		0x2
#define SMAP_BD_LENGTH			0x4
#define SMAP_BD_POINTER			0x6
#define SMAP_BD_ENTRY_SIZE		8

/* TX control / status */
/* Set by the driver, cleared by the hardware. */
#define SMAP_BD_TX_READY		(1 << 15)
#define SMAP_BD_TX_GENFCS		(1 << 9)	/* generate FCS */
#define SMAP_BD_TX_GENPAD		(1 << 8)	/* generate padding */
#define SMAP_BD_TX_INSSA		(1 << 7)
#define SMAP_BD_TX_RPLSA		(1 << 6)
#define SMAP_BD_TX_INSVLAN		(1 << 5)
#define SMAP_BD_TX_RPLVLAN		(1 << 4)
#define SMAP_BD_TX_BADFCS		(1 << 9)	/* status meanings */
#define SMAP_BD_TX_BADPKT		(1 << 8)
#define SMAP_BD_TX_LOSSCR		(1 << 7)
#define SMAP_BD_TX_EDEFER		(1 << 6)
#define SMAP_BD_TX_ECOLL		(1 << 5)
#define SMAP_BD_TX_LCOLL		(1 << 4)
#define SMAP_BD_TX_MCOLL		(1 << 3)
#define SMAP_BD_TX_SCOLL		(1 << 2)
#define SMAP_BD_TX_UNDERRUN		(1 << 1)
#define SMAP_BD_TX_SQE			(1 << 0)

/* RX control / status */
/* Set by the driver, cleared by the hardware. */
#define SMAP_BD_RX_EMPTY		(1 << 15)
#define SMAP_BD_RX_OVERRUN		(1 << 9)
#define SMAP_BD_RX_PFRM			(1 << 8)
#define SMAP_BD_RX_BADFRM		(1 << 7)
#define SMAP_BD_RX_RUNTFRM		(1 << 6)
#define SMAP_BD_RX_SHORTEVNT		(1 << 5)
#define SMAP_BD_RX_ALIGNERR		(1 << 4)
#define SMAP_BD_RX_BADFCS		(1 << 3)
#define SMAP_BD_RX_FRMTOOLONG		(1 << 2)
#define SMAP_BD_RX_OUTRANGE		(1 << 1)
#define SMAP_BD_RX_INRANGE		(1 << 0)

#define SMAP_DMA_MODE			0x24

/* EMAC3 MODE1, the rest of it. */
#define   E3_INLPBK_ENABLE		(1 << 30)
#define   E3_VLAN_ENABLE		(1 << 29)
#define   E3_FLOWCTRL_ENABLE		(1 << 28)
#define   E3_ALLOW_PF			(1 << 27)
#define   E3_ALLOW_EXTMNGIF		(1 << 25)
#define   E3_TXREQ0_MULTI		(1 << 15)
#define   E3_TXREQ0_DEPEND		(2 << 15)
#define   E3_TXREQ1_MULTI		(1 << 13)
#define   E3_JUMBO_ENABLE		(1 << 12)
#define   SMAP_EMAC3_MODE1_DEF		(E3_FDX_ENABLE | E3_IGNORE_SQE | \
					 E3_MEDIA_100M | E3_RXFIFO_2K | \
					 E3_TXFIFO_1K | E3_TXREQ0_MULTI | \
					 E3_TXREQ1_SINGLE)

/* EMAC3 TxMODE0 */
#define   E3_TX_GNP_0			(1 << 31)	/* get new packet */
#define   E3_TX_GNP_1			(1 << 30)
#define   E3_TX_GNP_DEPEND		(1 << 29)
#define   E3_TX_FIRST_CHANNEL		(1 << 28)

/* EMAC3 TxMODE1 */
#define   E3_TX_LOW_REQ_MSK		0x1f
#define   E3_TX_LOW_REQ_BITSFT		27
#define   E3_TX_URG_REQ_MSK		0xff
#define   E3_TX_URG_REQ_BITSFT		16

/* EMAC3 RxMODE, the rest of it. */
#define   E3_RX_RX_RUNT_FRAME		(1 << 29)
#define   E3_RX_RX_FCS_ERR		(1 << 28)
#define   E3_RX_RX_TOO_LONG_ERR		(1 << 27)
#define   E3_RX_RX_IN_RANGE_ERR		(1 << 26)
#define   E3_RX_PROP_PF			(1 << 25)
#define   E3_RX_PROMISC_MCAST		(1 << 23)
#define   E3_RX_INDIVID_HASH		(1 << 21)

/* EMAC3 interrupt status / enable */
/* E3_INTR_OVERRUN does not work, per Sony. */
#define   E3_INTR_OVERRUN		(1 << 25)
#define   E3_INTR_PF			(1 << 24)
#define   E3_INTR_BAD_FRAME		(1 << 23)
#define   E3_INTR_RUNT_FRAME		(1 << 22)
#define   E3_INTR_SHORT_EVENT		(1 << 21)
#define   E3_INTR_ALIGN_ERR		(1 << 20)
#define   E3_INTR_BAD_FCS		(1 << 19)
#define   E3_INTR_TOO_LONG		(1 << 18)
#define   E3_INTR_OUT_RANGE_ERR		(1 << 17)
#define   E3_INTR_IN_RANGE_ERR		(1 << 16)
#define   E3_INTR_DEAD_DEPEND		(1 << 9)
#define   E3_INTR_DEAD_0		(1 << 8)
#define   E3_INTR_SQE_ERR_0		(1 << 7)
#define   E3_INTR_TX_ERR_0		(1 << 6)
#define   E3_INTR_DEAD_1		(1 << 5)
#define   E3_INTR_SQE_ERR_1		(1 << 4)
#define   E3_INTR_TX_ERR_1		(1 << 3)
#define   E3_INTR_MMAOP_SUCCESS		(1 << 1)
#define   E3_INTR_MMAOP_FAIL		(1 << 0)
#define   E3_INTR_ALL			(E3_INTR_OVERRUN | E3_INTR_PF | \
					 E3_INTR_BAD_FRAME | \
					 E3_INTR_RUNT_FRAME | \
					 E3_INTR_SHORT_EVENT | \
					 E3_INTR_ALIGN_ERR | \
					 E3_INTR_BAD_FCS | E3_INTR_TOO_LONG | \
					 E3_INTR_OUT_RANGE_ERR | \
					 E3_INTR_IN_RANGE_ERR | \
					 E3_INTR_DEAD_DEPEND | \
					 E3_INTR_DEAD_0 | \
					 E3_INTR_SQE_ERR_0 | \
					 E3_INTR_TX_ERR_0 | \
					 E3_INTR_DEAD_1 | E3_INTR_SQE_ERR_1 | \
					 E3_INTR_TX_ERR_1 | \
					 E3_INTR_MMAOP_SUCCESS | \
					 E3_INTR_MMAOP_FAIL)
#define   E3_DEAD_ALL			(E3_INTR_DEAD_DEPEND | \
					 E3_INTR_DEAD_0 | E3_INTR_DEAD_1)

/* EMAC3 TX threshold and RX watermark fields. */
#define   E3_TX_THRESHLD_MSK		0x1f
#define   E3_TX_THRESHLD_BITSFT		27
#define   E3_RX_LO_WATER_MSK		0x1ff
#define   E3_RX_LO_WATER_BITSFT		23
#define   E3_RX_HI_WATER_MSK		0x1ff
#define   E3_RX_HI_WATER_BITSFT		7

/* More EMAC3 registers. */
#define SMAP_EMAC3_GROUP_HASH1		(SMAP_EMAC3_BASE + 0x40)
#define SMAP_EMAC3_GROUP_HASH2		(SMAP_EMAC3_BASE + 0x44)
#define SMAP_EMAC3_GROUP_HASH3		(SMAP_EMAC3_BASE + 0x48)
#define SMAP_EMAC3_GROUP_HASH4		(SMAP_EMAC3_BASE + 0x4c)

#endif /* __PS2_SMAP_REGS_H__ */
