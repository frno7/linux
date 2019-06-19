// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) IRQs
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>

/**
 * enum iop_irq_relay_rpc_ops - IOP IRQ relay RPC operations
 * @rpo_request_irq: request IRQ mapping
 * @rpo_release_irq: release IRQ mapping
 * @rpo_remap_irq: remap existing IRQ mapping
 */
enum iop_irq_relay_rpc_ops {
	rpo_request_irq = 1,
	rpo_release_irq = 2,
	rpo_remap_irq   = 3,
};

/**
 * struct iop_rpc_relay_map - IOP IRQ relay mapping
 * @u8 iop: IOP IRQ map source
 * @u8 map: main IRQ map target
 * @u8 rpc: %true for RPC relay, %false for SMFLAG relay
 */
struct iop_rpc_relay_map {
	u8 iop;
	u8 map;
	u8 rpc;
};

/**
 * struct iop_rpc_relay_release - IOP IRQ relay to release
 * @iop: IOP IRQ to release mapping for
 */
struct iop_rpc_relay_release {
	u8 iop;
};

static struct sif_rpc_client iop_irq_rpc;

static unsigned int iop_irq_startup(struct irq_data *data)
{
	static bool irq_relay = false;

	const struct iop_rpc_relay_map arg = {
		.iop = data->irq - IOP_IRQ_BASE,
		.map = data->irq,
		.rpc = true,	/* FIXME: Also implement SMFLAG relay */
	};
	s32 status;
	int err;

	BUG_ON(in_irq());

	if (!irq_relay) {
		int id;

		/*
		 * The main reason for requesting the IOP IRQ relay module here
		 * instead of in irqrelay_init() is that now the console may be
		 * visible to print messages if there are problems.
		 */
		id = iop_module_request("irqrelay", 0x0100, NULL);
		if (id < 0)
			return id;

		err = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);
		if (err < 0) {
			pr_err("%s: sif_rpc_bind failed with %d\n",
				__func__, err);
			return err;
		}

		irq_relay = true;
	}

	err = sif_rpc(&iop_irq_rpc, rpo_request_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));

	pr_debug("%s: err %d status %d\n", __func__, err, status);

	return err < 0 ? err : status;
}

static void iop_irq_shutdown(struct irq_data *data)
{
	const struct iop_rpc_relay_release arg = {
		.iop = data->irq - IOP_IRQ_BASE,
	};
	s32 status;
	int err;

	BUG_ON(in_irq());

	err = sif_rpc(&iop_irq_rpc, rpo_release_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));

	pr_debug("%s: err %d status %d\n", __func__, err, status);
}

#define IOP_IRQ_TYPE(irq_, name_)					\
	{								\
		.irq = irq_,						\
		.irq_chip = {						\
			.name = name_,					\
			.irq_startup = iop_irq_startup,			\
			.irq_shutdown = iop_irq_shutdown,		\
		}							\
	}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} iop_irqs[] = {
	IOP_IRQ_TYPE(IRQ_IOP_VBLANK,        "IOP VBLANK"),
	IOP_IRQ_TYPE(IRQ_IOP_SBUS,          "IOP SBUS"),
	IOP_IRQ_TYPE(IRQ_IOP_CDVD,          "IOP CDVD"),
	IOP_IRQ_TYPE(IRQ_IOP_DMA,           "IOP DMA"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC0,          "IOP RTC0"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC1,          "IOP RTC1"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC2,          "IOP RTC2"),
	IOP_IRQ_TYPE(IRQ_IOP_SIO0,          "IOP SIO0"),
	IOP_IRQ_TYPE(IRQ_IOP_SIO1,          "IOP SIO1"),
	IOP_IRQ_TYPE(IRQ_IOP_SPU,           "IOP SPU"),
	IOP_IRQ_TYPE(IRQ_IOP_PIO,           "IOP PIO"),
	IOP_IRQ_TYPE(IRQ_IOP_EVBLANK,       "IOP EVBLANK"),
	IOP_IRQ_TYPE(IRQ_IOP_DVD,           "IOP DVD"),
	IOP_IRQ_TYPE(IRQ_IOP_DEV9,          "IOP DEV9"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC3,          "IOP RTC3"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC4,          "IOP RTC4"),
	IOP_IRQ_TYPE(IRQ_IOP_RTC5,          "IOP RTC5"),
	IOP_IRQ_TYPE(IRQ_IOP_SIO2,          "IOP SIO2"),
	IOP_IRQ_TYPE(IRQ_IOP_HTR0,          "IOP HTR0"),
	IOP_IRQ_TYPE(IRQ_IOP_HTR1,          "IOP HTR1"),
	IOP_IRQ_TYPE(IRQ_IOP_HTR2,          "IOP HTR2"),
	IOP_IRQ_TYPE(IRQ_IOP_HTR3,          "IOP HTR3"),
	IOP_IRQ_TYPE(IRQ_IOP_USB,           "IOP USB"),
	IOP_IRQ_TYPE(IRQ_IOP_EXTR,          "IOP EXTR"),
	IOP_IRQ_TYPE(IRQ_IOP_ILINK,         "IOP iLink"),
	IOP_IRQ_TYPE(IRQ_IOP_ILNKDMA,       "IOP ILink DMA"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_MDEC_IN,  "IOP DMAC MDEC IN"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_MDEC_OUT, "IOP DMAC MDEC OUT"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SIF2,     "IOP DMAC SIF2"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_CDVD,     "IOP DMAC CDVD"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SPU,      "IOP DMAC SPU"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_PIO,      "IOP DMAC PIO"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_GPU_OTC,  "IOP DMAC GPU OTC"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_BE,       "IOP DMAC BE"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SPU2,     "IOP DMAC SPU2"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_DEV9,     "IOP DMAC DEV9"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SIF0,     "IOP DMAC SIF0"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SIF1,     "IOP DMAC SIF1"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SIO2_IN,  "IOP DMAC SIO2 IN"),
	IOP_IRQ_TYPE(IRQ_IOP_DMAC_SIO2_OUT, "IOP DMAC SIO2 OUT"),
	IOP_IRQ_TYPE(IRQ_IOP_SW1,           "IOP SW1"),
	IOP_IRQ_TYPE(IRQ_IOP_SW2,           "IOP SW2"),
};

static int __init iop_irq_init(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(iop_irqs); i++)
		irq_set_chip_and_handler(iop_irqs[i].irq,
			&iop_irqs[i].irq_chip, handle_level_irq);

	return 0;
}
// FIXME: subsys_initcall(iop_irq_init);
module_init(iop_irq_init);

MODULE_DESCRIPTION("PlayStation 2 input/output processor (IOP) IRQs");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
