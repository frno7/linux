// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Emotion Engine (EE) registers
 *
 */


#ifndef __ASM_MACH_PS2_EE_REGISTERS_H
#define __ASM_MACH_PS2_EE_REGISTERS_H

/* SIO Registers.  */
/* Most of these are based off of Toshiba documentation for the TX49 and the
   TX79 companion chip. However, looking at the kernel SIOP (Debug) exception
   handler, it looks like some registers are borrowed from the TX7901 UART
   (0x1000f110 or LSR, in particular). I'm still trying to find the correct
   register names and values.  */
#define SIO_LCR 0x1000f100 /* Line Control Register.  */
#define SIO_LCR_UMODE_8BIT 0x00 /* UART Mode.  */
#define SIO_LCR_UMODE_7BIT 0x01
#define SIO_LCR_USBL_1BIT 0x00 /* UART Stop Bit Length.  */
#define SIO_LCR_USBL_2BITS 0x01
#define SIO_LCR_UPEN_OFF 0x00 /* UART Parity Enable.  */
#define SIO_LCR_UPEN_ON 0x01
#define SIO_LCR_UEPS_ODD 0x00 /* UART Even Parity Select.  */
#define SIO_LCR_UEPS_EVEN 0x01

#define SIO_LSR 0x1000f110 /* Line Status Register.  */
#define SIO_LSR_DR 0x01 /* Data Ready. (Not tested) */
#define SIO_LSR_OE 0x02 /* Overrun Error.  */
#define SIO_LSR_PE 0x04 /* Parity Error.  */
#define SIO_LSR_FE 0x08 /* Framing Error.  */

#define SIO_IER 0x1000f120 /* Interrupt Enable Register.  */
#define SIO_IER_ERDAI 0x01 /* Enable Received Data Available Interrupt */
#define SIO_IER_ELSI 0x04 /* Enable Line Status Interrupt.  */

#define SIO_ISR 0x1000f130 /* Interrupt Status Register (?).  */
#define SIO_ISR_RX_DATA 0x01
#define SIO_ISR_TX_EMPTY 0x02
#define SIO_ISR_RX_ERROR 0x04

#define SIO_FCR 0x1000f140 /* FIFO Control Register.  */
#define SIO_FCR_FRSTE 0x01 /* FIFO Reset Enable.  */
#define SIO_FCR_RFRST 0x02 /* RX FIFO Reset.  */
#define SIO_FCR_TFRST 0x04 /* TX FIFO Reset.  */

#define SIO_BGR 0x1000f150 /* Baud Rate Control Register.  */

#define SIO_TXFIFO 0x1000f180 /* Transmit FIFO.  */
#define SIO_RXFIFO 0x1000f1c0 /* Receive FIFO.  */

#endif /* __ASM_MACH_PS2_EE_REGISTERS_H */
