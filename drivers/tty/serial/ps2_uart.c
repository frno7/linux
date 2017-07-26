/*
 * ps2_uart.c - PS2 UART driver
 *
 * Based on ps2sbioscon.c
 *
 * Copyright (C) 2010 Mega Man
 * Copyright (C) 2015 Rick Gaiser
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/mach-ps2/ps2.h>

#define DRV_NAME "ps2-uart"

/* 20 ms */
#define DELAY_TIME_MS 20

/* SIO Registers.  */
/* Most of these are based off of Toshiba documentation for the TX49 and the
   TX79 companion chip. However, looking at the kernel SIOP (Debug) exception
   handler, it looks like some registers are borrowed from the TX7901 UART
   (0x1000f110 or LSR, in particular). I'm still trying to find the correct
   register names and values.  */
#define SIO_LCR				0x1000f100	/* Line Control Register.  */
#define   SIO_LCR_UMODE_8BIT	0x00	/* UART Mode.  */
#define   SIO_LCR_UMODE_7BIT	0x01
#define   SIO_LCR_USBL_1BIT		0x00	/* UART Stop Bit Length.  */
#define   SIO_LCR_USBL_2BITS	0x01
#define   SIO_LCR_UPEN_OFF		0x00	/* UART Parity Enable.  */
#define   SIO_LCR_UPEN_ON		0x01
#define   SIO_LCR_UEPS_ODD		0x00	/* UART Even Parity Select.  */
#define   SIO_LCR_UEPS_EVEN		0x01

#define SIO_LSR				0x1000f110	/* Line Status Register.  */
#define   SIO_LSR_DR			0x01	/* Data Ready. (Not tested) */
#define   SIO_LSR_OE			0x02	/* Overrun Error.  */
#define   SIO_LSR_PE			0x04	/* Parity Error.  */
#define   SIO_LSR_FE			0x08	/* Framing Error.  */

#define SIO_IER				0x1000f120	/* Interrupt Enable Register.  */
#define   SIO_IER_ERDAI			0x01	/* Enable Received Data Available Interrupt */
#define   SIO_IER_ELSI			0x04	/* Enable Line Status Interrupt.  */

#define SIO_ISR				0x1000f130	/* Interrupt Status Register (?).  */
#define   SIO_ISR_RX_DATA		0x01
#define   SIO_ISR_TX_EMPTY		0x02
#define   SIO_ISR_RX_ERROR		0x04

#define SIO_FCR				0x1000f140	/* FIFO Control Register.  */
#define   SIO_FCR_FRSTE			0x01	/* FIFO Reset Enable.  */
#define   SIO_FCR_RFRST			0x02	/* RX FIFO Reset.  */
#define   SIO_FCR_TFRST			0x04	/* TX FIFO Reset.  */

#define SIO_BGR				0x1000f150	/* Baud Rate Control Register.  */

#define SIO_TXFIFO			0x1000f180	/* Transmit FIFO.  */
#define SIO_RXFIFO			0x1000f1c0	/* Receive FIFO.  */


static struct console ps2_uart_console;

static struct timer_list *timer;

static void ps2_uart_putchar_block(char c)
{
	while ((inw(SIO_ISR) & 0xf000) == 0x8000);
	outb(c, SIO_TXFIFO);
}

static unsigned int ps2_uart_tx_empty(struct uart_port *port)
{
	return 0;
}

static unsigned int ps2_uart_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void ps2_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void ps2_uart_start_tx(struct uart_port *port)
{
}

static void ps2_uart_stop_tx(struct uart_port *port)
{
}

static void ps2_uart_stop_rx(struct uart_port *port)
{
}

static void ps2_uart_break_ctl(struct uart_port *port, int break_state)
{
}

static void ps2_uart_enable_ms(struct uart_port *port)
{
}

static void ps2_uart_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
}

static int ps2_uart_rx_chars(struct uart_port *port)
{
	unsigned char ch, flag;
	unsigned short status;
	int rv = 0;

	if(!(inw(SIO_ISR) & 0x0f00))
		return rv;

	while ((status = inw(SIO_ISR)) & 0x0f00) {
		ch = inb(SIO_RXFIFO);
		flag = TTY_NORMAL;
		port->icount.rx++;
		rv++;

		outw(7, SIO_ISR);

		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, status, 0, ch, flag);
	}

	tty_flip_buffer_push(port->state->port.tty);

	return rv;
}

static void ps2_uart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		ps2_uart_putchar_block(port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		ps2_uart_stop_tx(port);
		return;
	}

	while ((inw(SIO_ISR) & 0xf000) != 0x8000) {
		if (uart_circ_empty(xmit))
			break;
		outb(xmit->buf[xmit->tail], SIO_TXFIFO);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		ps2_uart_stop_tx(port);
}

static void ps2_uart_timer (unsigned long data)
{
	struct uart_port *port;
	struct tty_struct *tty;
	struct circ_buf *xmit;
	int irx;

	port = (struct uart_port *)data;
	if (!port)
		return;
	if (!port->state)
		return;
	tty = port->state->port.tty;
	if (!tty)
		return;

	/* Receive data */
	irx = ps2_uart_rx_chars(port);

	/* Transmit data */
	ps2_uart_tx_chars(port);

	/* Restart the timer */
	xmit = &port->state->xmit;
	if ((uart_circ_chars_pending(xmit) > 0) || (irx > 0))
		/* Transmit/Receive ASAP */
		mod_timer(timer, jiffies + 1);
	else
		/* Normal polling */
		mod_timer(timer, jiffies + DELAY_TIME_MS * HZ / 1000);
}

static void ps2_uart_config_port(struct uart_port *port, int flags)
{
}

static int ps2_uart_startup(struct uart_port *port)
{
	/* Create timer for transmit */
	if (!timer) {
		timer = kmalloc (sizeof (*timer), GFP_KERNEL);
		if (!timer)
			return -ENOMEM;
	}
	memset(timer, 0, sizeof(*timer));
	init_timer(timer);
	timer->data = (unsigned long )port;
	timer->expires = jiffies + DELAY_TIME_MS * HZ / 1000;
	timer->function = ps2_uart_timer;
	add_timer (timer);

	return 0;
}

static void ps2_uart_shutdown(struct uart_port *port)
{
	/* Stop timer */
	del_timer (timer);
}

static const char *ps2_uart_type(struct uart_port *port)
{
	return (port->type == PORT_PS2_UART) ? "PS2 UART" : NULL;
}

static int ps2_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void ps2_uart_release_port(struct uart_port *port)
{
}

static int ps2_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops ps2_uart_ops = {
	.tx_empty		= ps2_uart_tx_empty,
	.get_mctrl		= ps2_uart_get_mctrl,
	.set_mctrl		= ps2_uart_set_mctrl,
	.start_tx		= ps2_uart_start_tx,
	.stop_tx		= ps2_uart_stop_tx,
	.stop_rx		= ps2_uart_stop_rx,
	.enable_ms		= ps2_uart_enable_ms,
	.break_ctl		= ps2_uart_break_ctl,
	.startup		= ps2_uart_startup,
	.shutdown		= ps2_uart_shutdown,
	.set_termios	= ps2_uart_set_termios,
	.type			= ps2_uart_type,
	.request_port	= ps2_uart_request_port,
	.release_port	= ps2_uart_release_port,
	.config_port	= ps2_uart_config_port,
	.verify_port	= ps2_uart_verify_port,
};

static struct uart_port ps2_uart_port = {
  .line		= 0,
	.ops		= &ps2_uart_ops,
	.type		= PORT_PS2_UART,
	.flags		= UPF_BOOT_AUTOCONF,
};

#if defined(CONFIG_SERIAL_PS2_UART_CONSOLE)

static void ps2_uart_console_write(struct console *con, const char *s, unsigned n)
{
	while (n-- && *s) {
		if (*s == '\n')
			ps2_uart_putchar_block('\r');
		ps2_uart_putchar_block(*s);
		s++;
	}
}

static int __init ps2_uart_console_setup(struct console *con, char *options)
{
	printk("UART console registered as port %s%d\n", con->name, con->index);
	return 0;
}

static struct uart_driver ps2_uart_driver;

static struct console ps2_uart_console = {
	.name	= "ttyS",
	.write	= ps2_uart_console_write,
	.device	= uart_console_device,
	.setup	= ps2_uart_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &ps2_uart_driver,
};

static int __init ps2_uart_console_init(void)
{
	register_console(&ps2_uart_console);
	return 0;
}

console_initcall(ps2_uart_console_init);

#define	PS2_UART_CONSOLE	(&ps2_uart_console)

#else

#define	PS2_UART_CONSOLE	NULL

#endif /* CONFIG_SERIAL_PS2_UART_CONSOLE */

static struct uart_driver ps2_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRV_NAME,
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= 1,
	.cons		= PS2_UART_CONSOLE,
};

static int ps2_uart_probe(struct platform_device *dev)
{
	return uart_add_one_port(&ps2_uart_driver, &ps2_uart_port);
}

static int ps2_uart_remove(struct platform_device *dev)
{
	struct uart_port *port = platform_get_drvdata(dev);

	uart_remove_one_port(&ps2_uart_driver, port);
	kfree(port);

	return 0;
}

static struct platform_driver ps2_uart_platform_driver = {
	.probe	= ps2_uart_probe,
	.remove	= ps2_uart_remove,
	.driver	= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.bus	= &platform_bus_type,
	},
};

static int __init ps2_uart_init(void)
{
	int result;

	printk("PS2 UART serial driver");

	result = uart_register_driver(&ps2_uart_driver);
	if (result != 0)
		return result;

	result = platform_driver_register(&ps2_uart_platform_driver);
	if (result != 0)
		uart_unregister_driver(&ps2_uart_driver);

	return result;
}

void __exit ps2_uart_exit(void)
{
	platform_driver_unregister(&ps2_uart_platform_driver);
	uart_unregister_driver(&ps2_uart_driver);

	if (timer) {
		kfree(timer);
		timer = NULL;
	}
}

module_init(ps2_uart_init);
module_exit(ps2_uart_exit);

MODULE_DESCRIPTION("PS2 UART driver");
MODULE_AUTHOR("Rick Gaiser");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
