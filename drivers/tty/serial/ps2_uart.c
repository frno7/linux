/*
 * ps2_uart.c - PS2 Emotional Engine (EE) UART driver
 *
 * Copyright (C) 2010 Mega Man
 *  - PS2 SBIOS serial driver (ps2sbioscon.c)
 * 
 * Copyright (C) 2015 Rick Gaiser
 *  - PS2 Emotional Engine (EE) UART driver (ps2_uart.c)
 * 
 * Copyright (C) 2025 Xavier Brassoud
 *  - ported to kernel v5
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/mach-ps2/ee-registers.h>

#define PS2_UART_DRIVER_NAME "ps2_uart"
#define PS2_UART_DEVICE_NAME "ttyS"

/* 20 ms */
#define DELAY_TIME_MS 20

struct ps2_uart {
	struct uart_port port;
	struct timer_list timer;
};
static struct ps2_uart *ps2_uart_dev;

static void ps2_uart_putchar_block(char c)
{
	while ((inw(SIO_ISR) & 0xf000) == 0x8000)
		;
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

static void ps2_uart_set_termios(struct uart_port *port,
				 struct ktermios *termios, struct ktermios *old)
{
}

static int ps2_uart_rx_chars(struct uart_port *port)
{
	unsigned char ch, flag;
	unsigned short status;
	int rv = 0;

	if (!(inw(SIO_ISR) & 0x0f00))
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

	tty_flip_buffer_push(&port->state->port);

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

static void ps2_uart_timer(struct timer_list *t)
{
	struct uart_port *port;
	struct tty_struct *tty;
	struct circ_buf *xmit;
	int irx;

	struct ps2_uart *pp = from_timer(pp, t, timer);
	port = &pp->port;
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
		mod_timer(&pp->timer, jiffies + 1);
	else
		/* Normal polling */
		mod_timer(&pp->timer, jiffies + DELAY_TIME_MS * HZ / 1000);
}

static void ps2_uart_config_port(struct uart_port *port, int flags)
{
}

static int ps2_uart_startup(struct uart_port *port)
{
	/* Create timer for transmit */
	timer_setup(&ps2_uart_dev->timer, ps2_uart_timer, 0);
	mod_timer(&ps2_uart_dev->timer, jiffies + DELAY_TIME_MS * HZ / 1000);

	return 0;
}

static void ps2_uart_shutdown(struct uart_port *port)
{
	/* Stop timer */
	del_timer(&ps2_uart_dev->timer);
}

static const char *ps2_uart_type(struct uart_port *port)
{
	return (port->type == PORT_PS2_UART) ? PS2_UART_DRIVER_NAME : NULL;
}

static int ps2_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void ps2_uart_release_port(struct uart_port *port)
{
}

static int ps2_uart_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops ps2_uart_ops = {
	.tx_empty = ps2_uart_tx_empty,
	.get_mctrl = ps2_uart_get_mctrl,
	.set_mctrl = ps2_uart_set_mctrl,
	.start_tx = ps2_uart_start_tx,
	.stop_tx = ps2_uart_stop_tx,
	.stop_rx = ps2_uart_stop_rx,
	.enable_ms = ps2_uart_enable_ms,
	.break_ctl = ps2_uart_break_ctl,
	.startup = ps2_uart_startup,
	.shutdown = ps2_uart_shutdown,
	.set_termios = ps2_uart_set_termios,
	.type = ps2_uart_type,
	.request_port = ps2_uart_request_port,
	.release_port = ps2_uart_release_port,
	.config_port = ps2_uart_config_port,
	.verify_port = ps2_uart_verify_port,
};

#if defined(CONFIG_SERIAL_PS2_UART_CONSOLE)

static void ps2_uart_console_write(struct console *con, const char *s,
				   unsigned n)
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
	pr_info(PS2_UART_DRIVER_NAME ": UART console registered as port %s%d\n",
		con->name, con->index);
	return 0;
}

static struct console ps2_uart_console;

static struct uart_driver ps2_uart_driver;

static struct console ps2_uart_console = {
	.name = PS2_UART_DEVICE_NAME,
	.write = ps2_uart_console_write,
	.device = uart_console_device,
	.setup = ps2_uart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &ps2_uart_driver,
};

static int __init ps2_uart_console_init(void)
{
	register_console(&ps2_uart_console);
	return 0;
}

console_initcall(ps2_uart_console_init);

#define PS2_UART_CONSOLE (&ps2_uart_console)

#else

#define PS2_UART_CONSOLE NULL

#endif /* CONFIG_SERIAL_PS2_UART_CONSOLE */

static struct uart_driver ps2_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = PS2_UART_DRIVER_NAME,
	.dev_name = PS2_UART_DEVICE_NAME,
	.major = TTY_MAJOR,
	.minor = 64,
	.nr = 1,
	.cons = PS2_UART_CONSOLE,
};

static int ps2_uart_probe(struct platform_device *dev)
{
	int result;
	result = uart_add_one_port(&ps2_uart_driver, &ps2_uart_dev->port);
	if (result != 0) {
		pr_err(PS2_UART_DRIVER_NAME ": Failed to register UART port\n");
	}
	return result;
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
		.name	= PS2_UART_DRIVER_NAME,
		.owner	= THIS_MODULE,
		.bus	= &platform_bus_type,
	},
};

static struct platform_device *ps2_uart_plat_devs;

static int __init ps2_uart_init(void)
{
	int result;

	ps2_uart_dev = kzalloc(sizeof(*ps2_uart_dev), GFP_KERNEL);
	if (!ps2_uart_dev) {
		return -ENOMEM;
	}

	result = uart_register_driver(&ps2_uart_driver);
	if (result != 0) {
		pr_err(PS2_UART_DRIVER_NAME
		       ": Failed to register uart driver\n");
		kfree(ps2_uart_dev);
		return result;
	}

	ps2_uart_plat_devs = platform_device_alloc(PS2_UART_DRIVER_NAME, -1);
	if (!ps2_uart_plat_devs) {
		pr_err(PS2_UART_DRIVER_NAME
		       ": Failed to alloc platform device\n");
		uart_unregister_driver(&ps2_uart_driver);
		return -ENOMEM;
	}

	result = platform_device_add(ps2_uart_plat_devs);
	if (result)
		platform_device_put(ps2_uart_plat_devs);

	ps2_uart_dev->port.line = 0;
	ps2_uart_dev->port.ops = &ps2_uart_ops;
	ps2_uart_dev->port.type = PORT_PS2_UART;
	ps2_uart_dev->port.flags = UPF_BOOT_AUTOCONF;

	result = platform_driver_register(&ps2_uart_platform_driver);
	if (result != 0) {
		pr_err(PS2_UART_DRIVER_NAME
		       ": Failed to register platform driver\n");
		uart_unregister_driver(&ps2_uart_driver);
	}

	pr_info(PS2_UART_DRIVER_NAME ": module loaded\n");

	return result;
};

void __exit ps2_uart_exit(void)
{
	platform_driver_unregister(&ps2_uart_platform_driver);
	uart_unregister_driver(&ps2_uart_driver);
	del_timer_sync(&ps2_uart_dev->timer);
	kfree(&ps2_uart_dev);

	pr_info(PS2_UART_DRIVER_NAME ": module unloaded\n");
};

module_init(ps2_uart_init);
module_exit(ps2_uart_exit);

MODULE_DESCRIPTION("PS2 UART driver");
MODULE_AUTHOR("Mega Man, Rick Gaiser, Xavier Brassoud");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" PS2_UART_DRIVER_NAME);
