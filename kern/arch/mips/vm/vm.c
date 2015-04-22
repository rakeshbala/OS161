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
struct cv *pte_cv;
struct lock *pte_lock;


void
vm_bootstrap(void)
{

	spinlock_init(&coremap_lock);
	for (int i = 0; i < MAX_SWAP_PG_NUM; ++i)
	{
		swapped_pages[i] = 'U';
	}
	swap_lock = lock_create("SwapLock");
	pte_lock = lock_create ("PTE_Lock");
	pte_cv = cv_create("PTE_CV");

	KASSERT(pte_lock != NULL);
	KASSERT(pte_cv != NULL);
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
	KASSERT(npages == 1);

	if (vm_is_bootstrapped == true)
	{
		int book_size = coremap_size-search_start;
		page_state pstate = 0;
		int clean_index[book_size];
		int clean_count = 0;
		int dirty_index[book_size];
		int dirty_count = 0;
		int s_index = -1;

		spinlock_acquire(&coremap_lock);
		for (unsigned int i = 0; i < coremap_size; ++i)
		{
			if (coremap[i].p_state == PS_FREE)
			{
				s_index = i;
				pstate = PS_FREE;
				break;
			}else if(coremap[i].p_state == PS_CLEAN){
				clean_index[clean_count]=i;
				clean_count++;
			}else if(coremap[i].p_state == PS_DIRTY){
				dirty_index[dirty_count]=i;
				dirty_count++;
			}
		}

		if (s_index == -1)
		{
			if (clean_count > 5)
			{
				s_index = clean_index[random()%clean_count];
				pstate = PS_CLEAN;
			}else{
				KASSERT(dirty_count > 0);
				s_index = dirty_index[random()%dirty_count];
				pstate = PS_DIRTY;
			}
		}
		coremap[s_index].p_state = PS_VICTIM;
		spinlock_release(&coremap_lock);

		KASSERT(s_index != -1);

		paddr_t pa = s_index*PAGE_SIZE;
		int result = evict_page(s_index,pstate);
		if (result)
		{
			return 0;
		}
		coremap[s_index].p_state = PS_FIXED;
		coremap[s_index].chunk_size = npages;
		coremap[s_index].va = PADDR_TO_KVADDR(pa);
		coremap[s_index].as = NULL;
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
	if (ts->ts_addrspace == curthread->t_addrspace)
	{
		int index  = tlb_probe(ts->ts_vaddr,0);
		if (index > 0)
		{
			tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(),index);
		}
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
	lock_acquire(pte_lock);
	while ((pte->pte_state.pte_lock_ondisk & PTE_LOCKED) == PTE_LOCKED)
	{
		cv_wait(pte_cv,pte_lock);
	}
	lock_release(pte_lock);

	if (pte->paddr == 0)
	{
		/************ RB:Allocate since it is page fault ************/
		result = page_alloc(pte,as);
		if (result !=0) return ENOMEM;
		KASSERT(pte->paddr != 0);
		int core_index = pte->paddr/PAGE_SIZE;
		if ((pte->pte_state.pte_lock_ondisk & PTE_ONDISK) == PTE_ONDISK)
		{
			int result = swap_in(pte);
			if (result)
			{
				panic("Swap in read failed\n");
			}
			coremap[core_index].p_state = PS_CLEAN;
		}else{
		 	coremap[core_index].p_state = PS_DIRTY;
		}
	}
	int core_index = pte->paddr/PAGE_SIZE;
	KASSERT(coremap[core_index].p_state != PS_VICTIM);

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

    // int x = splhigh();

    struct page_table_entry* pte = get_pte(as->page_table,va);
    KASSERT(pte != NULL);
    lock_acquire(swap_lock);
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
    lock_release(swap_lock);

    //locks
  	struct iovec iov;
	struct uio ku;
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_WRITE);
    int result = VOP_WRITE(swap_node, &ku);
    if (result)
    {
    	// lock_release(swap_lock);
    	panic("Errno: %d, write failed",result);
    	return result;
    }
    // lock_release(swap_lock);
    // splx(x);
    return 0;
}

int
swap_in(struct page_table_entry *pte)
{

    KASSERT(pte != NULL);
    KASSERT(pte->paddr != 0);
    KASSERT(pte->pte_state.swap_index >= 0);
    KASSERT(swap_node != NULL);

  	struct iovec iov;
	struct uio ku;
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_READ);
    int result = VOP_READ(swap_node, &ku);
    if (result)
    {
    	lock_release(swap_lock);
    	return result;
    }
    return 0;
}
