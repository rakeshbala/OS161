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
//
int copy_page_table(struct page_table_entry *oldpt, struct page_table_entry *newpt);
void copy_regions(struct region_entry *old_regions, struct region_entry *new_region);

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
	int result = copy_page_table(old->page_table, newas->page_table);
	if (result != 0)
	{
		return ENOMEM;
	}
	copy_regions(old->regions, newas->regions);
	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;
	newas->stack_end = old->stack_end;
	*ret = newas;
	return 0;
}

int
copy_page_table(struct page_table_entry *oldpt, struct page_table_entry *newpt)
{
	if (oldpt->next == NULL)
	{
		return 0;
	}else{
		newpt = kmalloc(sizeof(struct page_table_entry));
		newpt->vaddr = oldpt->vaddr;
		int result = page_alloc(newpt);
		if (result != 0)
		{
			return ENOMEM;
		}
		memmove((void *)PADDR_TO_KVADDR(oldpt->paddr),
			(const void *)PADDR_TO_KVADDR(newpt->paddr),PAGE_SIZE);
		newpt->permission = oldpt->permission;
		newpt->on_disk = oldpt->on_disk;
		copy_page_table(oldpt->next,newpt->next);
		return 0;
	}
}

void
copy_regions(struct region_entry *old_regions, struct region_entry *new_region)
{
	if (old_regions->next == NULL)
	{
		return;
	}else{
		new_region = kmalloc(sizeof(struct region_entry));
		new_region->reg_base = old_regions->reg_base;
		new_region->bounds = old_regions->reg_base;
		new_region->original_perm = old_regions->original_perm;
		new_region->backup_perm = old_regions->backup_perm;
		copy_regions(old_regions->next,new_region->next);
	}
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
			page_free(temp_page_t);
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

	// for (size_t i = 0; i < npages; ++i)
	// {
	// 	struct page_table_entry *entry = addPTE(as,
	// 		vaddr+i*PAGE_SIZE, 0);
	// 	if (entry == NULL)
	// 	{
	// 		return ENOMEM;
	// 	}

	// 	if (readable) entry->permission = entry->permission|AX_READ;
	// 	if (writeable) entry->permission = entry->permission|AX_WRITE;
	// 	if (executable) entry->permission = entry->permission|AX_EXECUTE;

	// }

	struct region_entry * region = addRegion(as, vaddr,sz,readable,
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
		temp_region = temp_region->next;
	}
	KASSERT(as->heap_start != 0);
	KASSERT(as->heap_end != 0);
	KASSERT (as->stack_end != 0);
}

int page_alloc(struct page_table_entry *pte){
	KASSERT(pte != NULL);
	for (unsigned int i = search_start; i < coremap_size; ++i)
	{
		spinlock_acquire(&coremap_lock);
		struct coremap_entry entry = coremap[i];
		if (entry.p_state == PS_FREE)
		{
			entry.p_state = PS_DIRTY;
			entry.chunk_size = 1;
			entry.va = pte->vaddr;
			entry.as = curthread->t_addrspace;
			coremap[i] = entry;
			spinlock_release(&coremap_lock);
			pte->paddr = i*PAGE_SIZE;
			return 0;
		}
		spinlock_release(&coremap_lock);

	}
	return ENOMEM;
}

void page_free(struct page_table_entry *pte){
	KASSERT(pte != NULL);
	if (pte->paddr != 0)
	{
		int core_index = pte->paddr/PAGE_SIZE;
		pte->paddr = (vaddr_t)NULL;
		spinlock_acquire(&coremap_lock);
		coremap[core_index].chunk_size = -1;
		coremap[core_index].p_state = PS_FREE;
		spinlock_release(&coremap_lock);
	}

}


/************ RB:Add page table entry to the page table ************/
struct page_table_entry *
addPTE(struct addrspace *as, vaddr_t vaddr, paddr_t paddr)
{

	KASSERT(as != NULL);
	struct page_table_entry *new_entry = kmalloc(sizeof(struct page_table_entry));
	if (new_entry == NULL)
	{
		return NULL;
	}
	new_entry->vaddr = vaddr & PAGE_FRAME;
	new_entry->paddr = paddr;
	new_entry->on_disk = false;
	new_entry->next = NULL;
	new_entry->permission = 0;

	if (as->page_table == NULL)
	{
		as->page_table = new_entry;
	}else{
		struct page_table_entry *temp = as->page_table;
		while(temp->next != NULL){
			temp = temp->next;
		}
		temp->next = new_entry;
	}


	return new_entry;
}

/************ RB: Get page table entry based on passed in vaddr ************/
struct page_table_entry *
getPTE(struct page_table_entry* page_table, vaddr_t vaddr)
{
	struct page_table_entry *navig_entry = page_table;
	bool found = false;
	vaddr_t search_page = vaddr & PAGE_FRAME;

	while(navig_entry != NULL){
		if (search_page == navig_entry->vaddr)
		{
			found = true;
			break;
		}
		navig_entry = navig_entry->next;
	}
	return found?navig_entry:NULL;
}

/************ RB: Add region to the regions linked list ************/
struct region_entry * addRegion(struct addrspace* as, vaddr_t rbase,size_t sz,int r,int w,int x)
{
	struct region_entry *new_entry = kmalloc(sizeof(struct region_entry));;
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
	new_entry->backup_perm = new_entry->original_perm;

	if (as->regions == NULL)
	{
		as->regions = new_entry;
	}else{
		struct region_entry *temp = as->regions;
		while(temp->next != NULL){
			temp = temp->next;
		}
		temp->next = new_entry;
	}
	return new_entry;
}

/************ RB: Get region based on passed in vaddr ************/
struct region_entry * getRegion(struct region_entry* regions, vaddr_t vaddr)
{
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
