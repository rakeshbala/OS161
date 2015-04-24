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
#include <cpu.h>


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
	copy_lock = lock_create("CopyLock");

	KASSERT(pte_lock != NULL);
	KASSERT(pte_cv != NULL);
	KASSERT(swap_lock != NULL);
	KASSERT(copy_lock != NULL);

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
	// KASSERT(npages == 1);
	if (vm_is_bootstrapped == true)
	{
		spinlock_acquire(&coremap_lock);
		bool found = false;
		while(found == false)
		{
			unsigned int i = search_start + random()%(coremap_size-search_start);
			if (i+npages >= coremap_size)
			{
				continue;
			}
			if (coremap[i].p_state == PS_FREE
				|| coremap[i].p_state == PS_CLEAN
				|| coremap[i].p_state == PS_DIRTY)
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
					found = true;
					start_index = i;
					for (int j = 0; j < npages; ++j)
					{
						victims[j]=coremap[i+j].p_state;
						coremap[i+j].p_state = PS_VICTIM;
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
		KASSERT(pa <= coremap_size * PAGE_SIZE);
		unsigned int limit = npages + start_index;
		for (unsigned i = start_index; i < limit && i< coremap_size; ++i)
		{
			int result = evict_page(i,victims[i-start_index]);
			if (result)
			{
				return 0;
			}
			coremap[i].p_state = PS_FIXED;
			coremap[i].chunk_size = npages;
			KASSERT(pa <= coremap_size * PAGE_SIZE);
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

	paddr_t temp_paddr = 0;
	if (pte->paddr == 0)
	{
	/************ RB:Allocate since it is page fault ************/
		result = page_alloc(pte,as, &temp_paddr);
		if (result !=0) return ENOMEM;
		KASSERT(temp_paddr != 0);
		int core_index = temp_paddr/PAGE_SIZE;
		if ((pte->pte_state.pte_lock_ondisk & PTE_ONDISK) == PTE_ONDISK)
		{
			int result = swap_in(pte, temp_paddr);
			if (result)
			{
				panic("Swap in read failed\n");
			}
			coremap[core_index].p_state = PS_CLEAN;
		}else{
			coremap[core_index].p_state = PS_DIRTY;
		}
		KASSERT(pte->paddr == 0);
		pte->paddr = temp_paddr;
	}
	int core_index = pte->paddr/PAGE_SIZE;
	KASSERT(coremap[core_index].p_state != PS_VICTIM);
	KASSERT(coremap[core_index].p_state != PS_FIXED);

	/* make sure it's page-aligned */
	KASSERT((pte->paddr & PAGE_FRAME) == pte->paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = faultaddress;
	if ((region_perm & AX_WRITE) == AX_WRITE)
	{
		coremap[core_index].p_state = PS_DIRTY;
		pte->pte_state.pte_lock_ondisk &= ~(PTE_ONDISK);
		elo = pte->paddr | TLBLO_DIRTY | TLBLO_VALID;
	}else{
		elo = pte->paddr|TLBLO_VALID;
	}
	KASSERT(pte->paddr <= coremap_size * PAGE_SIZE);

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

/************ RB:User page allocation. Always returns a PS_VICTIM page ************/
int
page_alloc(struct page_table_entry *pte, struct addrspace *as, paddr_t *ret_paddr){

/************ RB:Find page for allocation ************/
	int book_size = coremap_size-search_start;
	page_state pstate = 0;
	int s_index = -1;
	spinlock_acquire(&coremap_lock);
	for (unsigned int i = 0; i < coremap_size; ++i)
	{
		if (coremap[i].p_state == PS_FREE)
		{
			s_index = i;
			pstate = PS_FREE;
			break;
		}
	}

	if (s_index == -1)
	{
		while(s_index == -1){
			unsigned int i = search_start+random()%book_size;
			if (coremap[i].p_state == PS_CLEAN)
			{
				pstate = PS_CLEAN;
				s_index = i;
				break;
			}else if(coremap[i].p_state == PS_DIRTY)
			{
				pstate = PS_DIRTY;
				s_index = i;
				break;
			}
		}

	}
	KASSERT(s_index != -1);
	coremap[s_index].p_state = PS_VICTIM;
	spinlock_release(&coremap_lock);


// Make  decisions victim page
	int result = evict_page(s_index, pstate);
	if (result)
	{
		coremap[s_index].p_state = pstate;
		return result;
	}

	coremap[s_index].chunk_size = 1;
	coremap[s_index].va = pte->vaddr & PAGE_FRAME;
	coremap[s_index].as = as;
	*ret_paddr = s_index*PAGE_SIZE;
	KASSERT(*ret_paddr <= coremap_size * PAGE_SIZE);
	bzero((void *)PADDR_TO_KVADDR(*ret_paddr), PAGE_SIZE);
	return 0;
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

	if ((pte->pte_state.pte_lock_ondisk & PTE_ONDISK) == PTE_ONDISK)
	{
		KASSERT(pte->pte_state.swap_index >= 0);
		lock_acquire(swap_lock);
		swapped_pages[pte->pte_state.swap_index] = 'U';
		pte->pte_state.swap_index = -1;
		lock_release(swap_lock);
	}
}

int evict_page(int c_index, page_state pstate)
{
/************ RB:Clear the easy cases first ************/
	if (pstate == PS_FREE)
	{
		return 0;
	}else if(pstate != PS_CLEAN && pstate != PS_DIRTY){
		panic("Selected a non selectable page\n");
		return EINVAL;
	}
/************ RB:On to swapping decisions ************/
	vaddr_t ev_vaddr = coremap[c_index].va;
	struct addrspace * ev_as = coremap[c_index].as;

	struct page_table_entry *evict_pte = get_pte(ev_as->page_table,ev_vaddr);
	KASSERT(evict_pte != NULL);
	lock_acquire(pte_lock);
	evict_pte->pte_state.pte_lock_ondisk |= PTE_LOCKED; //lock
	lock_release(pte_lock);
	int result = allcpu_tlbshootdown(ev_vaddr, ev_as);
	if (result)
	{
		panic("TLB shootdown failed\n");
		return result;
	}
	KASSERT(coremap[c_index].p_state == PS_VICTIM);
	paddr_t paddr = evict_pte->paddr;
	evict_pte->paddr = 0;

	if (pstate == PS_DIRTY)
	{

		int result = swap_out(evict_pte, paddr);
		if (result)
		{
			panic("Swap space exhausted\n" );
			return result;
		}
		evict_pte->pte_state.pte_lock_ondisk |= PTE_ONDISK;
	}
	lock_acquire(pte_lock);
	evict_pte->pte_state.pte_lock_ondisk &= ~(PTE_LOCKED); //unlock
	cv_broadcast(pte_cv,pte_lock);
	lock_release(pte_lock);
	KASSERT((evict_pte->pte_state.pte_lock_ondisk & PTE_ONDISK) == PTE_ONDISK);
	return 0;
}

int
swap_out(struct page_table_entry *pte, paddr_t swap_paddr)
{
	if (swap_node == NULL)
	{
		int err = vfs_open((char *)"swapfile", O_RDWR|O_CREAT|O_TRUNC, 0, &swap_node);
		if (err != 0) {
			return err;
		}
	}

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

	struct iovec iov;
	struct uio ku;
	KASSERT(swap_paddr <= coremap_size * PAGE_SIZE);

	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(swap_paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_WRITE);
	int result = VOP_WRITE(swap_node, &ku);
	if (result)
	{
		panic("Errno: %d, write failed",result);
		return result;
	}

	return 0;
}

int
swap_in(struct page_table_entry *pte, paddr_t paddr)
{

	KASSERT(pte != NULL);
	KASSERT(paddr != 0);
	KASSERT(pte->pte_state.swap_index >= 0);
	KASSERT(swap_node != NULL);

	struct iovec iov;
	struct uio ku;
	KASSERT(paddr <= coremap_size * PAGE_SIZE);

	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
		pte->pte_state.swap_index*PAGE_SIZE, UIO_READ);
	int result = VOP_READ(swap_node, &ku);
	if (result)
	{
		return result;
	}
	return 0;
}
