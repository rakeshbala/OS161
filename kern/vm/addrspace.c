/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
// /*
//  * Note! If OPT_DUMBVM is set, as is the case until you start the VM
//  * assignment, this file is not compiled or linked or in any way
//  * used. The cheesy hack versions in dumbvm.c are used instead.
//  */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->heap_start = 0;
	as->heap_end = 0;
	as->regions = NULL;
	as->stack_end = USERSTACK;
	as->page_table = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	if (as != NULL)
	{
		while(as->page_table != NULL){
			struct page_table_entry *temp_page_t = as->page_table;
			as->page_table = as->page_table->next;
			kfree(temp_page_t);
		}

		while(as->regions != NULL){
			struct region_entry *temp_region = as->regions;
			as->regions = as->regions->next;
			kfree(temp_region);
		}
	}
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	int i, spl;

	(void)as;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}



 // * Set up a segment at virtual address VADDR of size MEMSIZE. The
 // * segment in memory extends from VADDR up to (but not including)
 // * VADDR+MEMSIZE.
//  *
//  * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
//  * write, or execute permission should be set on the segment. At the
//  * moment, these are ignored. When you write the VM system, you may
//  * want to implement them.

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{

	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	for (size_t i = 0; i < npages; ++i)
	{
		struct page_table_entry *entry = addPTE(as->page_table,
			vaddr+i*PAGE_SIZE, 0);
		if (entry == NULL)
		{
			return ENOMEM;
		}

		if (readable) entry->permission = entry->permission|AX_READ;
		if (writeable) entry->permission = entry->permission|AX_WRITE;
		if (executable) entry->permission = entry->permission|AX_EXECUTE;

	}

	struct region_entry * region = addRegion(as->regions, vaddr,sz,readable,
		writeable,executable);
	if (region == NULL)
	{
		return ENOMEM;
	}
	as->heap_start = vaddr + sz;
	as->heap_start += (PAGE_SIZE - (as->heap_start % PAGE_SIZE));
	as->heap_end = as->heap_start;

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as != NULL);
	KASSERT(as->regions != NULL);

	/************ RB:Loop through all regions to make them read write ************/
	/************ RB:Only for loadelf. Will change this back in as_complete_load ************/
	struct region_entry *temp_region = as->regions;
	while(temp_region != NULL){
		int rw_perm = AX_READ|AX_WRITE;
		temp_region->original_perm = rw_perm;
		temp_region = temp_region->next;
	}
	return 0;
}

int
as_complete_load(struct addrspace *as)
{

	KASSERT(as != NULL);
	KASSERT(as->regions != NULL);

	/************ RB:Loop through all regions to set back original permissions ************/
	struct region_entry *temp_region = as->regions;
	while(temp_region != NULL){
		temp_region->original_perm = temp_region->backup_perm;
		temp_region = temp_region->next;
	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}


/************ RB:Sanity checks for address space ************/
void
as_check_regions(struct addrspace *as)
{
	KASSERT(as->regions != NULL);
	struct region_entry *temp_region = as->regions;
	while(temp_region != NULL){
		KASSERT(temp_region->reg_base != 0);
		KASSERT(temp_region->bounds != 0);
		KASSERT(temp_region = temp_region->next);
	}
	KASSERT(as->heap_start != 0);
	KASSERT(as->heap_end != 0);
	KASSERT (as->stack_end != 0);
}

/************ RB:Add page table entry to the page table ************/
struct page_table_entry *
addPTE(struct page_table_entry* page_table, vaddr_t vaddr, paddr_t paddr)
{
	struct page_table_entry *new_entry = page_table;
	while(new_entry != NULL){
		new_entry = new_entry->next;
	}
	new_entry = kmalloc(sizeof(struct page_table_entry));
	if (new_entry == NULL)
	{
		return NULL;
	}
	new_entry->vaddr = vaddr;
	new_entry->paddr = paddr;
	new_entry->on_disk = false;
	new_entry->next = NULL;
	new_entry->permission = 0;
	return new_entry;
}

/************ RB:Get page table entry based on passed in vaddr ************/
struct page_table_entry *
getPTE(struct page_table_entry* page_table, vaddr_t vaddr)
{
	KASSERT(page_table != NULL);
	struct page_table_entry *new_entry = page_table;
	bool found = false;
	vaddr_t search_page = vaddr & PAGE_FRAME;

	while(new_entry->next != NULL){
		if (search_page == new_entry->vaddr)
		{
			found = true;
			break;
		}
		new_entry = new_entry->next;
	}
	return found?new_entry:NULL;


}

/************ RB:Add region to the regions linked list ************/
struct region_entry *addRegion(struct region_entry* regions, vaddr_t rbase,size_t sz,int r,int w,int x){
	struct region_entry *new_entry = regions;
	while(new_entry != NULL){
		new_entry = new_entry->next;
	}

	new_entry = kmalloc(sizeof(struct region_entry));
	if (new_entry == NULL)
	{
		return NULL;
	}
	new_entry->next = NULL;
	new_entry->reg_base = rbase;
	new_entry->bounds = sz;
	new_entry->original_perm = 0;
	if (r) new_entry->original_perm = new_entry->original_perm|AX_READ;
	if (w) new_entry->original_perm = new_entry->original_perm|AX_WRITE;
	if (x) new_entry->original_perm = new_entry->original_perm|AX_EXECUTE;
	new_entry->backup_perm = original_perm;
	return new_entry;
}

/************ RB:Get region based on passed in vaddr ************/
struct region_entry *getRegion(struct region_entry* regions, vaddr_t vaddr){
	KASSERT(regions != NULL);
	struct region_entry *new_entry = regions;
	bool found = false;

	while(new_entry->next != NULL){
		if (vaddr > new_entry->reg_base && vaddr < new_entry->reg_base+new_entry->bounds)
		{
			found = true;
			break;
		}
		new_entry = new_entry->next;
	}
	return found?new_entry:NULL;
}
