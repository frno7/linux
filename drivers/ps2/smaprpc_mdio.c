/*
 * PlayStation 2 Ethernet device driver -- MDIO bus implementation
 *
 * Provides Bus interface for MII registers
 *
 * Copyright (C) 2016 Rick Gaiser
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/slab.h>

#include "smaprpc.h"

static int smaprpc_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

extern int smaprpc_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg);
extern int smaprpc_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg, u16 phydata);

int smaprpc_mdio_register(struct net_device *ndev)
{
	struct smaprpc_chan *smap = netdev_priv(ndev);
	struct mii_bus *new_bus = mdiobus_alloc();
	int addr;
	int err;

	if (new_bus == NULL)
		return -ENOMEM;

	/* Interrupt not supported for PHY, so set all IRQs to PHY_POLL. */
	for (addr = 0; addr < PHY_MAX_ADDR; addr++)
		new_bus->irq[addr] = PHY_POLL;

	new_bus->name = "smaprpc";
	new_bus->read = &smaprpc_mdio_read;
	new_bus->write = &smaprpc_mdio_write;
	new_bus->reset = &smaprpc_mdio_reset;
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x", new_bus->name, 0);
	new_bus->priv = ndev;
	new_bus->phy_mask = 0;	/* FIXME: 0xfffffffd? */
	new_bus->parent = ndev->dev.parent;

	err = mdiobus_register(new_bus);
	if (err != 0) {
		pr_err("%s: Cannot register as MDIO bus (error %d)\n",
			new_bus->name, err);
		goto bus_register_fail;
	}

	smap->mii = new_bus;

	if (!phy_find_first(new_bus))
		pr_warning("%s: No PHY found\n", ndev->name);

	return 0;

bus_register_fail:
	mdiobus_free(new_bus);
	return err;
}

int smaprpc_mdio_unregister(struct net_device *ndev)
{
	struct smaprpc_chan *smap = netdev_priv(ndev);

	mdiobus_unregister(smap->mii);
	smap->mii->priv = NULL;
	mdiobus_free(smap->mii);
	smap->mii = NULL;

	return 0;
}
