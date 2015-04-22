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
#include <synch.h>
#include <wchan.h>
#include <kern/fcntl.h>
#include <kern/iovec.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>


/* under dumbvm, always have 48k of user stack */
// #define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
struct vnode *swap_node = NULL;
char swapped_pages[MAX_SWAP_PG_NUM];
struct lock *swap_lock;

void
vm_bootstrap(void)
{

	spinlock_init(&coremap_lock);
	for (int i = 0; i < MAX_SWAP_PG_NUM; ++i)
	{
		swapped_pages[i] = 'U';
	}
	swap_lock = lock_create("SwapLock");
	KASSERT(swap_lock != NULL);

	// dbflags = dbflags | DB_VM;
	/************ RB:Accomodate for last address misalignment ************/
	paddr_t fpaddr,lpaddr;
	ram_getsize(&fpaddr,&lpaddr);
	paddr_t availLast = lpaddr - (lpaddr%PAGE_SIZE);
	coremap_size = availLast/PAGE_SIZE;

	coremap = (struct coremap_entry *)PADDR_TO_KVADDR(fpaddr);
	KASSERT(coremap != NULL);
	paddr_t freeaddr_start = fpaddr + coremap_size*sizeof(struct coremap_entry);

	/************ RB:Accomodate for free address start misalignment ************/
	freeaddr_start = freeaddr_start + (PAGE_SIZE - (freeaddr_start%PAGE_SIZE));

	/************ RB:Mark fixed ************/
	unsigned int fixedIndex = freeaddr_start/PAGE_SIZE;
	search_start = fixedIndex;
	for (unsigned int i = 0; i <= availLast/PAGE_SIZE; ++i)
	{
		struct coremap_entry entry;
		if (i <=fixedIndex)
		{
			entry.p_state = PS_FIXED;
		}else{
			entry.p_state = PS_FREE;
		}
		entry.chunk_size = 1;
		entry.va = 0;
		entry.as = NULL;
		coremap[i] = entry;

	}
	vm_is_bootstrapped = true;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{

	int start_index = -1;
	page_state victims[npages];

	if (vm_is_bootstrapped == true)
	{
		spinlock_acquire(&coremap_lock);
		for (unsigned int i = 0; i < coremap_size; ++i)
		{
			if (coremap[i].p_state == PS_FREE ||
				coremap[i].p_state == PS_CLEAN ||
				coremap[i].p_state == PS_DIRTY)
			{
				bool allFree = true;
				for (unsigned int j = i+1; j < ((unsigned int)npages+i) && j<coremap_size; ++j)
				{
					if (coremap[j].p_state == PS_FIXED
						|| coremap[j].p_state == PS_VICTIM)
					{
						allFree = false;
						break;
					}
				}
				if (allFree)
				{
					start_index = i;
					for (int j = 0; j < npages; ++j)
					{
						victims[j]=coremap[i+j].p_state;
						coremap[i+j].p_state = PS_FIXED;
					}
					break;
				}
			}

		}
		spinlock_release(&coremap_lock);

		if (start_index == -1)
		{
			return 0;
		}
		paddr_t pa = start_index*PAGE_SIZE;
		unsigned int limit = npages + start_index;
		for (unsigned i = start_index; i < limit && i< coremap_size; ++i)
		{
			int result = evict_page(i,victims[start_index-i]);
			if (result)
			{
				return 0;
			}
			coremap[i].chunk_size = npages;
			coremap[i].va = PADDR_TO_KVADDR(pa);
			coremap[i].as = NULL;
		}
		bzero((void *)PADDR_TO_KVADDR(pa), npages * PAGE_SIZE);
		return PADDR_TO_KVADDR(pa);

	}else{
		paddr_t pa;
		pa = getppages(npages);
		if (pa==0) {
			return 0;
		}
		return PADDR_TO_KVADDR(pa);
	}

}

void
free_kpages(vaddr_t addr)
{
	if (vm_is_bootstrapped == true)
	{
		paddr_t pa = KVADDR_TO_PADDR(addr);
		int core_index = pa/PAGE_SIZE;
		spinlock_acquire(&coremap_lock);
		struct coremap_entry entry = coremap[core_index];
		int j = entry.chunk_size;
		for (int i = 0; i < j; ++i)
		{
			entry = coremap[core_index+i];
			entry.chunk_size = -1;
			entry.p_state = PS_FREE;
			coremap[core_index+i] = entry;
		}
		spinlock_release(&coremap_lock);
	}
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	int x = splhigh();
	int index  = tlb_probe(ts->ts_vaddr,0);
	if (index > 0)
	{
		tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(),index);
	}
	splx(x);
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{

	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	faultaddress &= PAGE_FRAME;
	ax_permssion region_perm;
	as = curthread->t_addrspace;
	if (as == NULL) {
		return EFAULT; //as not setup
	}
	int result = vm_validitycheck(faultaddress, curthread->t_addrspace, &region_perm);
	if (result == false)
	{
		return EFAULT;
	}
	DEBUG(DB_VM, "VM: fault: 0x%x\n", faultaddress);
	switch (faulttype) {
		case VM_FAULT_READONLY:
			if (!((region_perm & AX_WRITE) == AX_WRITE))
			{
				return EFAULT;
			}
			break;
		case VM_FAULT_READ:
			if (!((region_perm & AX_READ) == AX_READ))
			{
				return EFAULT;
			}
			region_perm &= AX_READ;
			break;
		case VM_FAULT_WRITE:
			if (!((region_perm & AX_WRITE) == AX_WRITE))
			{
				return EFAULT;
			}
			break;
		default:
		return EINVAL;
	}

	/************ RB:Check if page fault ************/
	struct page_table_entry *pte = get_pte(as->page_table,faultaddress);
	if (pte == NULL)
	{
		pte = add_pte(as, faultaddress, 0);
		if (pte == NULL)
		{
			return ENOMEM;
		}
	}
	/************ RB:Prevent access while swapping ************/
	while ((pte->pte_state.pte_lock_ondisk & PTE_LOCKED) == PTE_LOCKED)
	{
		wchan_lock(as->swap_wc);
		wchan_sleep(as->swap_wc);
	}

	if (pte->paddr == 0)
	{
		/************ RB:Allocate since it is page fault ************/
		result = page_alloc(pte,as);
		if (result !=0) return ENOMEM;
		KASSERT(pte->paddr != 0);
		int core_index = pte->paddr/PAGE_SIZE;
		if ((pte->pte_state.pte_lock_ondisk & PTE_ONDISK) == PTE_ONDISK)
		{
			int result = swap_in(pte->vaddr, as);
			if (result)
			{
				panic("Swap in read failed\n");
			}
			coremap[core_index].p_state = PS_CLEAN;
		}else{
		 	coremap[core_index].p_state = PS_DIRTY;
		}
	}

	KASSERT(coremap[pte->paddr/PAGE_SIZE].p_state != PS_VICTIM);


	int core_index = pte->paddr/PAGE_SIZE;

	/* make sure it's page-aligned */
	KASSERT((pte->paddr & PAGE_FRAME) == pte->paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = faultaddress;
	if ((region_perm & AX_WRITE) == AX_WRITE)
	{
		coremap[core_index].p_state = PS_DIRTY;
		elo = pte->paddr | TLBLO_DIRTY | TLBLO_VALID;
	}else{
		elo = pte->paddr|TLBLO_VALID;
	}
	DEBUG(DB_VM, "VM: 0x%x -> 0x%x\n", faultaddress, pte->paddr);

	int index = tlb_probe(ehi,0);
	if (index > 0)
	{
		tlb_write(ehi,elo,index);
	}else{
		tlb_random(ehi, elo);
	}
	splx(spl);
	return 0;
}

bool
vm_validitycheck(vaddr_t faultaddress,struct addrspace* pas, ax_permssion *perm)
{
	KASSERT(pas != NULL);
	/* Assert that the address space has been set up properly. */
	as_check_regions(pas);
	struct region_entry* process_regions = pas->regions;
	KASSERT(process_regions!= NULL);
	while(process_regions != NULL)
	{
		vaddr_t base = process_regions->reg_base;
		vaddr_t top = base + process_regions->bounds;
		if(faultaddress >= base && faultaddress <= top)
		{
			*perm = process_regions->original_perm;
			return true;
		}
		process_regions = process_regions->next;
	}
	if(faultaddress >= pas->heap_start && faultaddress <= pas->heap_end)
	{
		*perm = AX_READ|AX_WRITE;
		return true;
	}
	/*********** RR: check for stack range within 4MB from stack top ***********/
	if((faultaddress >= pas->stack_end - PAGE_SIZE && faultaddress <= USERSPACETOP)&&
		(faultaddress >= USERSTACKBASE))
	{
		*perm = AX_READ|AX_WRITE;
		if (faultaddress < pas->stack_end)
		{
			pas->stack_end -= PAGE_SIZE;
		}
		return true;
	}

	*perm = 0;
	return false;
}

int
swap_out(vaddr_t va,struct addrspace *as)
{
    if (swap_node == NULL)
    {
        int err = vfs_open((char *)"lhd0raw:",O_RDWR,0,&swap_node);
        if(err != 0)
        {
            return err;
        }
    }

    struct page_table_entry* pte = get_pte(as->page_table,va);
    KASSERT(pte != NULL);
    // lock_acquire(swap_lock);
    if (pte->pte_state.swap_index < 0)
    {
    	for (int i = 0; i < MAX_SWAP_PG_NUM; ++i)
    	{
    		if (swapped_pages[i] == 'U')
    		{
    			pte->pte_state.swap_index = i;
    			swapped_pages[i] = 'A';
    			break;
    		}
    	}
    	if (pte->pte_state.swap_index < 0)
    	{
    		lock_release(swap_lock);
    		return ENOMEM;
    	}
    }

    //locks
  	struct iovec iov;
	struct uio ku;
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_WRITE);
    int result = VOP_WRITE(swap_node, &ku);
    if (result)
    {
    	// lock_release(swap_lock);
    	return result;
    }
    // lock_release(swap_lock);
    return 0;
}

int
swap_in(vaddr_t va,struct addrspace *as)
{

    struct page_table_entry* pte = get_pte(as->page_table,va);
    KASSERT(pte != NULL);
    KASSERT(pte->paddr != 0);
    KASSERT(pte->pte_state.swap_index >= 0);

    // lock_acquire(swap_lock);
  	struct iovec iov;
	struct uio ku;
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_READ);
    int result = VOP_READ(swap_node, &ku);
    if (result)
    {
    	// lock_release(swap_lock);
    	return result;
    }
    // lock_release(swap_lock);
    return 0;
}
