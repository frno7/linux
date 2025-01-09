// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 serial input/output (SIO) registers
 */

#ifndef __ASM_MACH_PS2_SIO_REGISTERS_H
#define __ASM_MACH_PS2_SIO_REGISTERS_H

/**
 * DOC: Serial input/output (SIO) registers
 *
 * Most of these are based off of Toshiba documentation for the TX49 and the
 * TX79 companion chip. However, looking at the kernel SIOP (Debug) exception
 * handler, it looks like some registers are borrowed from the TX7901 UART
 * (0x1000f110 or LSR, in particular). I'm still trying to find the correct
 * register names and values.
 */

#define SIO_LCR		  0x1000f100 /* Line control register */
#define SIO_LCR_UMODE_8BIT	0x00 /* UART mode */
#define SIO_LCR_UMODE_7BIT	0x01
#define SIO_LCR_USBL_1BIT	0x00 /* UART stop bit length */
#define SIO_LCR_USBL_2BITS	0x01
#define SIO_LCR_UPEN_OFF	0x00 /* UART parity enable */
#define SIO_LCR_UPEN_ON		0x01
#define SIO_LCR_UEPS_ODD	0x00 /* UART even parity select */
#define SIO_LCR_UEPS_EVEN	0x01

#define SIO_LSR		  0x1000f110 /* Line status register */
#define SIO_LSR_DR		0x01 /* Data ready (not tested) */
#define SIO_LSR_OE		0x02 /* Overrun error */
#define SIO_LSR_PE		0x04 /* Parity error */
#define SIO_LSR_FE		0x08 /* Framing error */

#define SIO_IER		  0x1000f120 /* Interrupt enable register */
#define SIO_IER_ERDAI		0x01 /* Enable received data available interrupt */
#define SIO_IER_ELSI		0x04 /* Enable line status interrupt */

#define SIO_ISR		  0x1000f130 /* Interrupt status register (?) */
#define SIO_ISR_RX_DATA		0x01
#define SIO_ISR_TX_EMPTY	0x02
#define SIO_ISR_RX_ERROR	0x04

#define SIO_FCR		  0x1000f140 /* FIFO control register */
#define SIO_FCR_FRSTE		0x01 /* FIFO reset enable */
#define SIO_FCR_RFRST		0x02 /* RX FIFO reset */
#define SIO_FCR_TFRST		0x04 /* TX FIFO reset */

#define SIO_BGR		  0x1000f150 /* Baud rate control register */

#define SIO_TXFIFO	  0x1000f180 /* Transmit FIFO */
#define SIO_RXFIFO	  0x1000f1c0 /* Receive FIFO */

#endif /* __ASM_MACH_PS2_SIO_REGISTERS_H */
