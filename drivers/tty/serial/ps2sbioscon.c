/*
 * PS2 SBIOS serial driver
 *
 * Copyright (C) 2010 Mega Man
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; version 2 of the License.
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

/* 20 ms */
#define DELAY_TIME 2

#define DUMMY ((void *) ps2sbios_dummy)

static struct console ps2sbios_console;

static struct timer_list *timer;

static int ps2sbios_dummy(void)
{
	return 0;
}

static void ps2sbios_con_stop_tx(struct uart_port *port)
{
}

static void ps2sbios_con_stop_rx(struct uart_port *port)
{
}

static void ps2sbios_con_enable_ms(struct uart_port *port)
{
}

static void ps2sbios_con_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int count;

	if (port->x_char) {
		prom_putchar(port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		ps2sbios_con_stop_tx(port);
		return;
	}

	count = port->fifosize >> 1;
	do {
		prom_putchar(xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		ps2sbios_con_stop_tx(port);
}

static void ps2sbios_con_start_tx(struct uart_port *port)
{
}

static void ps2sbios_con_timer (unsigned long data)
{
	struct uart_port *port;
	struct tty_struct *tty;

	port = (struct uart_port *)data;
	if (!port)
		return;
	if (!port->state)
		return;
	tty = port->state->port.tty;
	if (!tty)
		return;

	/* Restart the timer */
	timer->expires = jiffies + DELAY_TIME;
	add_timer (timer);

	/* Transmit data */
	ps2sbios_con_tx_chars(port);
}

static unsigned int ps2sbios_con_tx_empty(struct uart_port *port)
{
	return 0;
}

static unsigned int ps2sbios_con_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void ps2sbios_con_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void ps2sbios_con_break_ctl(struct uart_port *port, int break_state)
{
}

static void ps2sbios_con_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
}

static int ps2sbios_con_startup(struct uart_port *port)
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
	timer->expires = jiffies + DELAY_TIME;
	timer->function = ps2sbios_con_timer;
	add_timer (timer);
	return 0;
}

static void ps2sbios_con_shutdown(struct uart_port *port)
{
	/* Stop timer */
	del_timer (timer);
}

static const char *ps2sbios_con_type(struct uart_port *port)
{
	return PS2_SBIOS_SERIAL_DEVICE_NAME;
}

static void ps2sbios_con_release_port(struct uart_port *port)
{
}

static int ps2sbios_con_request_port(struct uart_port *port)
{
	return 0;
}

static void ps2sbios_con_config_port(struct uart_port *port, int flags)
{
}

static int ps2sbios_con_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops ps2sbios_con_ops = {
	.tx_empty		= ps2sbios_con_tx_empty,
	.set_mctrl		= ps2sbios_con_set_mctrl,
	.get_mctrl		= ps2sbios_con_get_mctrl,
	.stop_tx		= ps2sbios_con_stop_tx,
	.start_tx		= ps2sbios_con_start_tx,
	.stop_rx		= ps2sbios_con_stop_rx,
	.enable_ms		= ps2sbios_con_enable_ms,
	.break_ctl		= ps2sbios_con_break_ctl,
	.startup		= ps2sbios_con_startup,
	.shutdown		= ps2sbios_con_shutdown,
	.set_termios	= ps2sbios_con_set_termios,
	.type			= ps2sbios_con_type,
	.release_port	= ps2sbios_con_release_port,
	.request_port	= ps2sbios_con_request_port,
	.config_port	= ps2sbios_con_config_port,
	.verify_port	= ps2sbios_con_verify_port,
};

static struct uart_port ps2sbios_con_port = {
	.ops		= &ps2sbios_con_ops,
	.type		= PORT_PS2_SBIOS,
	.flags		= UPF_BOOT_AUTOCONF,
};

static struct uart_driver ps2sbios_con_reg = {
	.owner			= THIS_MODULE,
	.driver_name	= "ps2sbios-uart",
	.dev_name		= PS2_SBIOS_SERIAL_DEVICE_NAME,
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr				= 1,
	.cons			= &ps2sbios_console,
};

static int serial_ps2sbios_probe(struct platform_device *dev)
{
	return uart_add_one_port(&ps2sbios_con_reg, &ps2sbios_con_port);
}

static int serial_ps2sbios_remove(struct platform_device *dev)
{
	struct uart_port *port = platform_get_drvdata(dev);

	uart_remove_one_port(&ps2sbios_con_reg, port);
	kfree(port);

	return 0;
}

static const struct dev_pm_ops serial_ps2sbios_pm_ops = {
	.suspend	= DUMMY,
	.resume		= DUMMY,
};

static struct platform_driver serial_ps2sbios_driver = {
	.probe          = serial_ps2sbios_probe,
	.remove         = serial_ps2sbios_remove,

	.driver		= {
		.name	= "ps2sbios-uart",
		.owner	= THIS_MODULE,
		.bus	= &platform_bus_type,
#ifdef CONFIG_PM
		.pm	= &serial_ps2sbios_pm_ops,
#endif
	},
};

static int	ps2sbios_console_setup(struct console *con, char *options)
{
	printk("SBIOS console registered as port %s%d\n", con->name, con->index);
	return 0;
}

static void ps2sbios_console_write(struct console *con, const char *s, unsigned n)
{
	while (n-- && *s) {
		if (*s == '\n')
			prom_putchar('\r');
		prom_putchar(*s);
		s++;
	}
}

static struct console ps2sbios_console = {
	.name		= PS2_SBIOS_SERIAL_DEVICE_NAME,
	.write		= ps2sbios_console_write,
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &ps2sbios_con_reg,
	.setup		= ps2sbios_console_setup,
};

static int __init ps2sbios_console_init(void)
{
	register_console(&ps2sbios_console);
	return 0;
}

static int __init ps2sbios_con_init(void)
{
	int result;

	printk("PS2 SBIOS serial driver");

	result = uart_register_driver(&ps2sbios_con_reg);
	if (result != 0)
		return result;

	result = platform_driver_register(&serial_ps2sbios_driver);
	if (result != 0)
		uart_unregister_driver(&ps2sbios_con_reg);

	return result;
}

void __exit ps2sbios_con_exit(void)
{
	platform_driver_unregister(&serial_ps2sbios_driver);
	uart_unregister_driver(&ps2sbios_con_reg);

	if (timer) {
		kfree(timer);
		timer = NULL;
	}
}

console_initcall(ps2sbios_console_init);
module_init (ps2sbios_con_init);
module_exit(ps2sbios_con_exit);

/* Module information */
MODULE_AUTHOR("Mega Man");
MODULE_DESCRIPTION("PS2 SBIOS serial driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ps2sbios-uart");
