/*
 *  PlayStation 2 Ethernet device driver -- MDIO bus implementation
 *  Provides Bus interface for MII registers
 *
 *  Copyright (C) 2016-2016 Rick Gaiser
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/slab.h>

#include "smaprpc.h"


static int
smaprpc_mdio_reset(struct mii_bus *bus)
{
	//pr_info("smap_mdio_reset\n");

	return 0;
}

extern int smaprpc_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg);
extern int smaprpc_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg, u16 phydata);

int
smaprpc_mdio_register(struct net_device *ndev)
{
	int err = 0;
	struct mii_bus *new_bus;
	struct smaprpc_chan *smap = netdev_priv(ndev);
	int addr, found;

	new_bus = mdiobus_alloc();
	if (new_bus == NULL)
		return -ENOMEM;

	/*
	 * Interrupt not supported for PHY, so set all irq's to PHY_POLL
	 */
	new_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!new_bus->irq) {
		return -ENOMEM;
	}
	for (addr = 0; addr < PHY_MAX_ADDR; addr++)
		new_bus->irq[addr] = PHY_POLL;

	new_bus->name = "smaprpc";
	new_bus->read = &smaprpc_mdio_read;
	new_bus->write = &smaprpc_mdio_write;
	new_bus->reset = &smaprpc_mdio_reset;
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		new_bus->name, 0);
	new_bus->priv = ndev;
	new_bus->phy_mask = 0;//0xfffffffd;
	new_bus->parent = ndev->dev.parent;
	err = mdiobus_register(new_bus);
	if (err != 0) {
		pr_err("%s: Cannot register as MDIO bus\n", new_bus->name);
		goto bus_register_fail;
	}

	smap->mii = new_bus;

	found = 0;
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		struct phy_device *phydev = new_bus->phy_map[addr];
		if (phydev) {
			smap->phy_addr = addr;
			pr_info("%s: PHY ID %08x at %d (%s)\n",
				ndev->name, phydev->phy_id, addr,
				dev_name(&phydev->dev));
			found = 1;
		}
	}

	if (!found)
		pr_warning("%s: No PHY found\n", ndev->name);

	return 0;

bus_register_fail:
	mdiobus_free(new_bus);
	return err;
}

int
smaprpc_mdio_unregister(struct net_device *ndev)
{
	struct smaprpc_chan *smap = netdev_priv(ndev);

	mdiobus_unregister(smap->mii);
	smap->mii->priv = NULL;
	mdiobus_free(smap->mii);
	smap->mii = NULL;

	return 0;
}
