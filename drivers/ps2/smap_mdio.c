/*
 *  PlayStation 2 Ethernet device driver -- MDIO bus implementation
 *  Provides Bus interface for MII registers
 *
 *  Copyright (C) 2015-2015 Rick Gaiser
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

#include "smap.h"


static int
smap_mdio_busy_wait(struct smap_chan *smap)
{
	unsigned long curr;
	unsigned long finish = jiffies + 3 * HZ;

	do {
		curr = jiffies;
		if (EMAC3REG_READ(smap, SMAP_EMAC3_STA_CTRL) & E3_PHY_OP_COMP)
			return 0;
		else
			cpu_relax();
	} while (!time_after_eq(curr, finish));

	return -EBUSY;
}

static int smap_mdio_reset(struct mii_bus *bus)
{
	//pr_info("smap_mdio_reset\n");

	return 0;
}

static int
smap_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct smap_chan *smap = netdev_priv(ndev);
	u_int32_t e3v;

	//pr_info("read(%d,%d)\n",phyaddr,phyreg);

	if (smap_mdio_busy_wait(smap))
		return -EBUSY;

	/* write phy address and register address */
	EMAC3REG_WRITE(smap, SMAP_EMAC3_STA_CTRL,
			E3_PHY_READ |
			((phyaddr&E3_PHY_ADDR_MSK)<<E3_PHY_ADDR_BITSFT) |
			(phyreg&E3_PHY_REG_ADDR_MSK) );

	if (smap_mdio_busy_wait(smap))
		return -EBUSY;

	/* workarrund: it may be needed to re-read to get correct phy data */
	e3v = EMAC3REG_READ(smap, SMAP_EMAC3_STA_CTRL);
	return(e3v >> E3_PHY_DATA_BITSFT);
}

static int
smap_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct smap_chan *smap = netdev_priv(ndev);
	u_int32_t e3v;

	//pr_info("write(%d,%d)\n",phyaddr,phyreg);

	if (smap_mdio_busy_wait(smap))
		return -EBUSY;

	/* write data, phy address and register address */
	e3v = ( ((phydata&E3_PHY_DATA_MSK)<<E3_PHY_DATA_BITSFT) |
			E3_PHY_WRITE |
			((phyaddr&E3_PHY_ADDR_MSK)<<E3_PHY_ADDR_BITSFT) |
			(phyreg&E3_PHY_REG_ADDR_MSK) );
	EMAC3REG_WRITE(smap, SMAP_EMAC3_STA_CTRL, e3v);

	if (smap_mdio_busy_wait(smap))
		return -EBUSY;

	return(0);
}

int
smap_mdio_register(struct net_device *ndev)
{
	int err = 0;
	struct mii_bus *new_bus;
	struct smap_chan *smap = netdev_priv(ndev);
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

	new_bus->name = "smap";
	new_bus->read = &smap_mdio_read;
	new_bus->write = &smap_mdio_write;
	new_bus->reset = &smap_mdio_reset;
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
smap_mdio_unregister(struct net_device *ndev)
{
	struct smap_chan *smap = netdev_priv(ndev);

	mdiobus_unregister(smap->mii);
	smap->mii->priv = NULL;
	mdiobus_free(smap->mii);
	smap->mii = NULL;

	return 0;
}
