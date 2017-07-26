/*
 *  PlayStation 2 DMA buffer memory allocation interface (/dev/ps2mem)
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 *  Copyright (C) 2010-2013 Juergen Urban
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

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/ps2/dev.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/addrspace.h>

#include <asm/mach-ps2/dma.h>

#include "ps2dev.h"

struct vm_area_struct *ps2mem_vma_cache = NULL;

static int ps2mem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    struct page_list *list, *newlist;
    struct page *page;

    ps2mem_vma_cache = NULL;
    list = vma->vm_file->private_data;
    if (list == NULL) {
        return VM_FAULT_SIGBUS; /* no memory - SIGBUS */
    }
    if (list->pages <= vmf->pgoff) {
	/* access to unallocated area - extend buffer */
	if ((newlist = ps2pl_realloc(list, vmf->pgoff + 1)) == NULL)
	    return VM_FAULT_SIGBUS; /* no memory - SIGBUS */
	list = vma->vm_file->private_data = newlist;
    }
    page = list->page[vmf->pgoff];
    get_page(page);	/* increment page count */
    vmf->page = page;

    return 0; /* success */
}

static struct vm_operations_struct ps2mem_vmops = {
    fault:	ps2mem_fault,
};

static int ps2mem_open(struct inode *inode, struct file *file)
{
    ps2mem_vma_cache = NULL;
    file->private_data = NULL;
    return 0;
}

static int ps2mem_release(struct inode *inode, struct file *file)
{
    ps2mem_vma_cache = NULL;
    if (file->private_data)
	ps2pl_free((struct page_list *)file->private_data);
    return 0;
}

static int ps2mem_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct page_list *list, *newlist;
    int pages;

    if (file->f_flags & O_SYNC)
	pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;

    ps2mem_vma_cache = NULL;

    pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    pages += vma->vm_pgoff;
    if (file->private_data == NULL) {
	/* 1st mmap ... allocate buffer */
	if ((list = ps2pl_alloc(pages)) == NULL)
	    return -ENOMEM;
	file->private_data = list;
    } else {
	list = (struct page_list *)file->private_data;
	if (list->pages < pages) {		/* extend buffer */
	    if ((newlist = ps2pl_realloc(list, pages)) == NULL)
		return -ENOMEM;
	    file->private_data = newlist;
	}	
    }

    vma->vm_flags |= VM_IO;
    vma->vm_ops = &ps2mem_vmops;
    return 0;
}

static long ps2mem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct page_list *list = (struct page_list *)file->private_data;
    unsigned long phys;
    unsigned long *dest;
    int i;

    switch (cmd) {
    case PS2IOC_PHYSADDR:
	if (list == NULL)
	    return 0;			/* buffer is not allocated */

	dest = (unsigned long *)arg;
	if (dest == NULL)		/* get the number of pages */
	    return list->pages;

	/* get a physical address table */
	for (i = 0; i < list->pages; i++) {
	    phys = page_to_phys(list->page[i]);
	    if (copy_to_user(dest, &phys, sizeof(phys)) != 0)
		return -EFAULT;
	    dest++;
	}
	return 0;
    default:
	return -EINVAL;
    }

    return 0;
}

struct file_operations ps2mem_fops = {
    llseek:	no_llseek,
    unlocked_ioctl:	ps2mem_ioctl,
    mmap:	ps2mem_mmap,
    open:	ps2mem_open,
    release:	ps2mem_release,
};
