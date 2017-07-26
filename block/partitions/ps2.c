/*
 *  fs/partitions/ps2.c
 *  support for PlayStation 2 partition(APA) 
 *
 *        Copyright (C) 2002  Sony Computer Entertainment Inc.
 *        Copyright (C) 2013  Mega Man
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id$
 */


#include "check.h"
#include "ps2.h"

static int ps2_partition_one(struct parsed_partitions *state,
			     struct ps2_partition *pp, int resv_m, int resv_s,
			     int *slot)
{
	int i;
#ifndef CONFIG_PS2_PARTITION_SPLIT
	int pno;
	char *p;
	char *pe;
#endif

	if (le32_to_cpu(pp->magic) != PS2_PARTITION_MAGIC)
		return 0;

	if ((le16_to_cpu(pp->flag) & PS2_PART_FLAG_SUB) != 0)
		return 1;

	if (pp->id[0] == '\0' || pp->id[1] == '\0'
#ifndef CONFIG_PS2_PARTITION_ALL
		|| strncmp((char*)(&pp->id), PS2_LINUX_ID, strlen(PS2_LINUX_ID)) != 0
#endif
		) {
#if 0
		if (strnlen(pp->id, PS2_PART_NID) < PS2_PART_NID) {
			printk("Ignoring PS2 partition '%s'.\n", pp->id);
		}
#endif
		/* not PS2 Linux partition */
		return 1;
	}

#ifndef CONFIG_PS2_PARTITION_SPLIT
	/* PS2 Linux partition */
	p = &pp->id[strlen(PS2_LINUX_ID)];

	pe = &pp->id[PS2_PART_NID];

	pno = 0;
	while (p < pe && *p >= '0' && *p <= '9')
		pno = pno * 10 + (*p++ - '0');
	if (pno == 0)
		pno = 1;
	if (pno < 1 || pno > state->limit - 1)
		return 1;

	*slot = pno;
	/* Add only partitions which are complete. */
	/* TBD: The other partitions should be merged.  */
	if (le32_to_cpu(pp->nsub) == 0) {
		put_partition(state, *slot, le32_to_cpu(pp->start) + resv_m,
			le32_to_cpu(pp->nsector) - resv_m);
	}
#else
	put_partition(state, *slot, le32_to_cpu(pp->start) + resv_m,
		le32_to_cpu(pp->nsector) - resv_m);
	if (le32_to_cpu(pp->nsub) != 0) {
		/* The device mapper must be used to merge the partitions. */
		state->parts[*slot].flags = ADDPART_FLAG_RAID;
		printk(" %s [1 of %d]\n", pp->id, pp->nsub + 1);
	} else {
		printk(" %s [complete]\n", pp->id);
	}
	(*slot)++;
	for (i = 0; i < le32_to_cpu(pp->nsub); i++) {
		put_partition(state, *slot, le32_to_cpu(pp->subs[i].start) + resv_s,
			le32_to_cpu(pp->subs[i].nsector) - resv_s);
		printk(" %s [%d of %d]\n", pp->id, i + 2, pp->nsub + 1);
		state->parts[*slot].flags = ADDPART_FLAG_RAID;
		(*slot)++;
	}
#endif

	return 1;
}

int ps2_partition(struct parsed_partitions *state)
{
	struct block_device *bdev = state->bdev;
	int resv_m;
	int resv_s;
	struct ps2_partition *pp;
	sector_t sector;
	unsigned char *data;
	Sector sect;
	int slot;

	resv_m = PS2_PART_RESV_MAIN / bdev_logical_block_size(bdev);
	if (resv_m == 0)
		resv_m = 1;
	resv_s = PS2_PART_RESV_SUB / bdev_logical_block_size(bdev);
	if (resv_s == 0)
		resv_s = 1;

	sector = 0;
	slot = 1;
	do {
		data = read_part_sector(state, sector, &sect);
		if (!data) {
			return -1;
		}

		pp = (struct ps2_partition *) data;
		if (sector == 0) {
			if (memcmp(pp->mbr.magic, PS2_MBR_MAGIC, 32) != 0 ||
			    le32_to_cpu(pp->mbr.version) > PS2_MBR_VERSION) {
				put_dev_sector(sect);
				return 0;
			}
		}
		if (!ps2_partition_one(state, pp, resv_m, resv_s, &slot)) {
			put_dev_sector(sect);
			break;
		}
		sector = le32_to_cpu(pp->next);
		put_dev_sector(sect);
	} while (sector != 0);

	return 1;
}
