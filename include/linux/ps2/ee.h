#ifndef __PS2_EE_H
#define __PS2_EE_H
/* Copyright 2010 Mega Man */
/* TBD: Unfinished state. Rework code. */

#include <asm/types.h>

/* GIF */
/* GIF registers */
#define PS2_GIFREG_CTRL		0
#define PS2_GIFREG_MODE		1
#define PS2_GIFREG_STAT		2
#define PS2_GIFREG_TAG0		4
#define PS2_GIFREG_TAG1		5
#define PS2_GIFREG_TAG2		6
#define PS2_GIFREG_TAG3		7
#define PS2_GIFREG_CNT		8
#define PS2_GIFREG_P3CNT	9
#define PS2_GIFREG_P3TAG	10
//
#ifdef _MIPS_ARCH_R5900
#define PS2_GIFTAG_CLEAR_TAG(p)	\
	__asm__ __volatile__ ("sq       $0, (%0)" : : "r"(p))
#else
#if (_MIPS_SIM == _ABIO32)
#define PS2_GIFTAG_CLEAR_TAG(p) \
	do { \
		unsigned long *k = p; \
		\
		k[0] = 0; \
		k[1] = 0; \
		k[2] = 0; \
		k[3] = 0; \
	} while(0)
#else
#define PS2_GIFTAG_CLEAR_TAG(p)		*(__u128 *)(p) = 0
#endif
#endif

#if !(_MIPS_SIM == _ABIO32)
typedef unsigned int __u128 __attribute__((mode(TI)));
#endif


/* GIF types */
typedef struct {
	unsigned NLOOP: 15 __attribute__((packed));
	unsigned EOP:    1 __attribute__((packed));
	unsigned pad:   30 __attribute__((packed));
	unsigned PRE:    1 __attribute__((packed));
	unsigned PRIM:  11 __attribute__((packed));
	unsigned FLG:    2 __attribute__((packed));
	unsigned NREG:   4 __attribute__((packed));
	unsigned REGS0:  4 __attribute__((packed));
	unsigned REGS1:  4 __attribute__((packed));
	unsigned REGS2:  4 __attribute__((packed));
	unsigned REGS3:  4 __attribute__((packed));
	unsigned REGS4:  4 __attribute__((packed));
	unsigned REGS5:  4 __attribute__((packed));
	unsigned REGS6:  4 __attribute__((packed));
	unsigned REGS7:  4 __attribute__((packed));
	unsigned REGS8:  4 __attribute__((packed));
	unsigned REGS9:  4 __attribute__((packed));
	unsigned REGS10: 4 __attribute__((packed));
	unsigned REGS11: 4 __attribute__((packed));
	unsigned REGS12: 4 __attribute__((packed));
	unsigned REGS13: 4 __attribute__((packed));
	unsigned REGS14: 4 __attribute__((packed));
	unsigned REGS15: 4 __attribute__((packed));
} ps2_giftag __attribute__((aligned(16))); // 128bit
// FLG
#define PS2_GIFTAG_FLG_PACKED	0
#define PS2_GIFTAG_FLG_REGLIST	1
#define PS2_GIFTAG_FLG_IMAGE	2
// REGS
#define PS2_GIFTAG_REGS_PRIM		0x0
#define PS2_GIFTAG_REGS_RGBAQ		0x1
#define PS2_GIFTAG_REGS_ST		0x2
#define PS2_GIFTAG_REGS_UV		0x3
#define PS2_GIFTAG_REGS_XYZF2		0x4
#define PS2_GIFTAG_REGS_XYZ2		0x5
#define PS2_GIFTAG_REGS_TEX0_1		0x6
#define PS2_GIFTAG_REGS_TEX0_2		0x7
#define PS2_GIFTAG_REGS_CLAMP_1		0x8
#define PS2_GIFTAG_REGS_CLAMP_2		0x9
#define PS2_GIFTAG_REGS_FOG		0xa
#define PS2_GIFTAG_REGS_XYZF3		0xc
#define PS2_GIFTAG_REGS_XYZ3		0xd
#define PS2_GIFTAG_REGS_AD		0xe
#define PS2_GIFTAG_REGS_NOP		0xf

/* bit offset for top half of GIFTAG */
#define PS2_GIFTAG_EOP_THOFFSET		15
#define PS2_GIFTAG_PRE_THOFFSET		(PS2_GIFTAG_EOP_THOFFSET+1+30)
#define PS2_GIFTAG_PRIM_THOFFSET	(PS2_GIFTAG_PRE_THOFFSET+1)
#define PS2_GIFTAG_FLG_THOFFSET		(PS2_GIFTAG_PRIM_THOFFSET+11)
#define PS2_GIFTAG_NREG_THOFFSET	(PS2_GIFTAG_FLG_THOFFSET+2)

/* make top half of GIFTAG */
#define PS2_GIFTAG_SET_TOPHALF(nloop, eop, pre, prim, flg, nreg) \
       ((__u64)(nloop)  \
        | ((__u64)(eop) << PS2_GIFTAG_EOP_THOFFSET) \
        | ((__u64)(pre) << PS2_GIFTAG_PRE_THOFFSET) \
        | ((__u64)(prim) << PS2_GIFTAG_PRIM_THOFFSET) \
        | ((__u64)(flg) << PS2_GIFTAG_FLG_THOFFSET) \
        | ((__u64)(nreg) << PS2_GIFTAG_NREG_THOFFSET))

/* VIF */
/* VIF defines */
#define PS2_VIF_SET_CODE(immediate, num, cmd, irq) \
	((immediate) | ((num) << 16) | ((cmd) << 24) | ((irq) << 31))
#define PS2_VIF_NOP		0x00
#define PS2_VIF_STCYCL		0x01
#define PS2_VIF_OFFSET		0x02	/* VIF1 only */
#define PS2_VIF_BASE		0x03	/* VIF1 only */
#define PS2_VIF_ITOP		0x04
#define PS2_VIF_STMOD		0x05
#define PS2_VIF_MSKPATH3	0x06	/* VIF1 only */
#define PS2_VIF_MARK		0x07
#define PS2_VIF_FLUSHE		0x10
#define PS2_VIF_FLUSH		0x11	/* VIF1 only */
#define PS2_VIF_FLUSHA		0x13	/* VIF1 only */
#define PS2_VIF_MSCAL		0x14
#define PS2_VIF_MSCNT		0x17
#define PS2_VIF_MSCALF		0x15	/* VIF1 only */
#define PS2_VIF_STMASK		0x20
#define PS2_VIF_STROW		0x30
#define PS2_VIF_STCOL		0x31
#define PS2_VIF_MPG		0x4a
#define PS2_VIF_DIRECT		0x50	/* VIF1 only */
#define PS2_VIF_DIRECTHL	0x51	/* VIF1 only */
#define PS2_VIF_UNPACK		0x60

#define PS2_VIFREG_STAT		0
#define PS2_VIFREG_FBRST	1
#define PS2_VIFREG_ERR		2
#define PS2_VIFREG_MARK		3
#define PS2_VIFREG_CYCLE	4
#define PS2_VIFREG_MODE		5
#define PS2_VIFREG_NUM		6
#define PS2_VIFREG_MASK		7
#define PS2_VIFREG_CODE		8
#define PS2_VIFREG_ITOPS	9
#define PS2_VIFREG_BASE		10	/* VIF1 only */
#define PS2_VIFREG_OFST		11	/* VIF1 only */
#define PS2_VIFREG_TOPS		12	/* VIF1 only */
#define PS2_VIFREG_ITOP		13
#define PS2_VIFREG_TOP		14	/* VIF1 only */
#define PS2_VIFREG_R0		16
#define PS2_VIFREG_R1		17
#define PS2_VIFREG_R2		18
#define PS2_VIFREG_R3		19
#define PS2_VIFREG_C0		20
#define PS2_VIFREG_C1		21
#define PS2_VIFREG_C2		22
#define PS2_VIFREG_C3		23

/* DMA */
typedef struct {
	__u32 QWC:   16 __attribute__((packed));
	__u32 pad16: 10 __attribute__((packed));
	__u32 PCE:    2 __attribute__((packed));
	__u32 ID:     3 __attribute__((packed));
	__u32 IRQ:    1 __attribute__((packed));
	__u32 ADDR:  32 __attribute__((packed));
	__u64 pad64: 64 __attribute__((packed));
} ps2_dmatag __attribute__((aligned(16))); // 128bit
#define PS2_DMATAG_REFE		0
#define PS2_DMATAG_CNT		1
#define PS2_DMATAG_NEXT		2
#define PS2_DMATAG_REF		3
#define PS2_DMATAG_REFS		4
#define PS2_DMATAG_CALL		5
#define PS2_DMATAG_RET		6
#define PS2_DMATAG_END		7

#endif /* __PS2_EE_H */
