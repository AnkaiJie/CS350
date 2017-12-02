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
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A3.h"

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3
struct coremapData {
	bool open;
	paddr_t frameAddress;
	int contiguous;
};

struct coremapData *coremap;
int totalFrames;
bool coremapSetup = false;
paddr_t low;
#endif

void
vm_bootstrap(void)
{
#if OPT_A3
	paddr_t high;
	ram_getsize(&low, &high);

	totalFrames = (high - low) / PAGE_SIZE;
	int coreSize = totalFrames * sizeof(struct coremapData);
	while (coreSize > PAGE_SIZE) {
		totalFrames -= 1;
		coreSize -= PAGE_SIZE;
	}

	coreSize = totalFrames * sizeof(struct coremapData);
	paddr_t frameStartAddr = ROUNDUP(low + coreSize, PAGE_SIZE);

	struct coremapData * entry = (struct coremapData *) PADDR_TO_KVADDR(low);
	coremap = entry;
	for (int i=0; i<totalFrames; ++i) {
		struct coremapData data = {true, frameStartAddr + i*PAGE_SIZE, 1};
		*entry = data;
		entry += 1;
	}

	kprintf("Lo: %d, Hi: %d, %d, totalFrames: %d, frameStartAddr: %d\n", 
		low, high, coreSize, totalFrames, frameStartAddr);

	coremapSetup = true;

#endif
}

static
paddr_t
getppages(unsigned long npages)
{
#if OPT_A3
	if (coremapSetup) {
		for (int i=0; i<totalFrames; ++i) {
			if (coremap[i].open) {
				bool contiguousMatch = true;
				for (unsigned int j=0; j<npages; ++j) {
					if (!(coremap[i + j].open)) {
						contiguousMatch = false;
						break;
					}
				}
				
				if (contiguousMatch) {
					for (unsigned int j=0; j<npages; ++j) {
						coremap[i + j].open = false;
					}
					coremap[i].open = false;
					coremap[i].contiguous = npages;
					return coremap[i].frameAddress;
				} else {
					i += npages - 1;
				}
			}
		}

		return 0;
	} else {

		paddr_t addr;

		spinlock_acquire(&stealmem_lock);

		addr = ram_stealmem(npages);
		
		spinlock_release(&stealmem_lock);
		return addr;
	}
#else
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
	return addr;	
#endif
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	// kprintf("Found address: %x, %x\n", pa, 
	// 						PADDR_TO_KVADDR(pa));
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{	
	if (addr >= PADDR_TO_KVADDR(low)) {
		for (int i=0; i<totalFrames; ++i) {
			if (!coremap[i].open && PADDR_TO_KVADDR(coremap[i].frameAddress) == addr) {
				int blocks = coremap[i].contiguous;
				for (int j=0; j<blocks; ++j) {
					coremap[i+j].open = true;
					coremap[i+j].contiguous = 1;
				}
				break;
			}
		}
	}
	// (void)addr;
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

#if OPT_A3
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		return EROFS;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);

	KASSERT(as->as_pbase1_pt != NULL);
	KASSERT(as->as_pbase2_pt != NULL);
	KASSERT(as->as_stack_pt != NULL);

	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	bool inTextSegment = false;

	struct pageTable *thisPt;


	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		thisPt = as->as_pbase1_pt;
		int frameno = thisPt->frameNumberArray[(faultaddress - as->as_vbase1)/PAGE_SIZE];
		int offset = faultaddress%PAGE_SIZE;
		paddr = frameno + offset;
		inTextSegment = true;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		thisPt = as->as_pbase2_pt;
		int frameno = thisPt->frameNumberArray[(faultaddress - as->as_vbase1)/PAGE_SIZE];
		int offset = faultaddress%PAGE_SIZE;
		paddr = frameno + offset;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		thisPt = as->as_stack_pt;
		int frameno = thisPt->frameNumberArray[(faultaddress - stackbase)/PAGE_SIZE];
		int offset = faultaddress%PAGE_SIZE;
		paddr = frameno + offset;
	}
	else {
		return EFAULT;
	}


	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

		if (inTextSegment && as->doneLoadElf) {
			elo &= ~TLBLO_DIRTY;
		}

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}

#else
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;


	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
#endif

#if OPT_A3
struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1_pt = NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2_pt = NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	as->as_stack_pt = NULL;

	as->doneLoadElf = false;


	return as;
}
#else
struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	return as;
}
#endif

void
as_destroy(struct addrspace *as)
{	

	for (int i=0; i<as->as_npages1; ++i) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase1_pt->frameNumberArray[i]));
	}

	for (int i=0; i<as->as_npages2; ++i) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase2_pt->frameNumberArray[i]));
	}

	for (int i=0; i<DUMBVM_STACKPAGES; ++i) {
		free_kpages(PADDR_TO_KVADDR(as->as_stack_pt->frameNumberArray[i]));
	}

	kfree(as->as_pbase1_pt->frameNumberArray);
	kfree(as->as_pbase2_pt->frameNumberArray);
	kfree(as->as_stack_pt->frameNumberArray);
	kfree(as->as_pbase1_pt);
	kfree(as->as_pbase2_pt);
	kfree(as->as_stack_pt);
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}


#if OPT_A3
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

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;

		// Initialize page table
		as->as_pbase1_pt = kmalloc(sizeof (struct pageTable));
		as->as_pbase1_pt->frameNumberArray = kmalloc(npages * sizeof(paddr_t));
		as->as_pbase1_pt->size = npages;
		for (unsigned int i=0; i < npages; ++i) {
			as->as_pbase1_pt->frameNumberArray[i] = 0;
		}
		as->as_pbase1_pt->readable = readable;
		as->as_pbase1_pt->writeable = writeable;
		as->as_pbase1_pt->executable = executable;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;

		// Initialize page table
		as->as_pbase2_pt = kmalloc(sizeof (struct pageTable));
		as->as_pbase2_pt->size = 0;
		as->as_pbase2_pt->frameNumberArray = kmalloc(npages * sizeof(paddr_t));
		as->as_pbase2_pt->size = npages;
		for (unsigned int i=0; i < npages; ++i) {
			as->as_pbase2_pt->frameNumberArray[i] = 0;
		}
		as->as_pbase2_pt->readable = readable;
		as->as_pbase2_pt->writeable = writeable;
		as->as_pbase2_pt->executable = executable;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}
#else
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

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}
#endif

#if OPT_A3
#else
static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
#endif


#if OPT_A3
int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_stackpbase == 0);


	struct pageTable *pt1 = as->as_pbase1_pt;

	for (int i=0; i<pt1->size; ++i) {
		paddr_t get = getppages(1);
		pt1->frameNumberArray[i] = get;
		bzero((void *)PADDR_TO_KVADDR(get), PAGE_SIZE);
		if (pt1->frameNumberArray[i] == 0) {
			return ENOMEM;
		}
	}

	struct pageTable *pt2 = as->as_pbase2_pt;

	for (int i=0; i<pt2->size; ++i) {
		paddr_t get = getppages(1);
		pt2->frameNumberArray[i] = get;
		bzero((void *)PADDR_TO_KVADDR(get), PAGE_SIZE);
		if (pt2->frameNumberArray[i] == 0) {
			return ENOMEM;
		}
	}


	as->as_stack_pt = kmalloc(sizeof (struct pageTable));
	as->as_stack_pt->frameNumberArray = kmalloc(DUMBVM_STACKPAGES * sizeof(paddr_t));
	as->as_stack_pt->size = DUMBVM_STACKPAGES;
	struct pageTable *stack_pt = as->as_stack_pt;

	for (int i=0; i<stack_pt->size; ++i) {
		paddr_t get = getppages(1);
		stack_pt->frameNumberArray[i] = get;
		if (i == 0) {
			as->as_stackpbase = get;
		}
		bzero((void *)PADDR_TO_KVADDR(get), PAGE_SIZE);
		if (stack_pt->frameNumberArray[i] == 0) {
			return ENOMEM;
		}
	}

	return 0;
}

#else
int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}
#endif


int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}


int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}


#if OPT_A3
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	// Allocate page tables

	// Initialize page tables
	new->as_pbase1_pt = kmalloc(sizeof (struct pageTable));
	if (!new->as_pbase1_pt) return ENOMEM;
	new->as_pbase1_pt->frameNumberArray = kmalloc(new->as_npages1 * sizeof(paddr_t));
	if (!new->as_pbase1_pt->frameNumberArray) return ENOMEM;
	new->as_pbase1_pt->size = new->as_npages1;
	for (int i=0; i < new->as_npages1; ++i) {
		new->as_pbase1_pt->frameNumberArray[i] = 0;
	}
	new->as_pbase1_pt->readable = old->as_pbase1_pt->readable;
	new->as_pbase1_pt->writeable = old->as_pbase1_pt->readable;
	new->as_pbase1_pt->executable = old->as_pbase1_pt->readable;
	//////////
	new->as_pbase2_pt = kmalloc(sizeof (struct pageTable));
	if (!new->as_pbase2_pt) return ENOMEM;
	new->as_pbase2_pt->frameNumberArray = kmalloc(new->as_npages2 * sizeof(paddr_t));
	if (!new->as_pbase2_pt->frameNumberArray) return ENOMEM;
	new->as_pbase2_pt->size = new->as_npages2;
	for (int i=0; i < new->as_npages2; ++i) {
		new->as_pbase1_pt->frameNumberArray[i] = 0;
	}
	new->as_pbase2_pt->readable = old->as_pbase2_pt->readable;
	new->as_pbase2_pt->writeable = old->as_pbase2_pt->readable;
	new->as_pbase2_pt->executable = old->as_pbase2_pt->readable;
	////////

	/* Allocate physical memory */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}


	for (int i=0; i<new->as_npages1; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase1_pt->frameNumberArray[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase1_pt->frameNumberArray[i]),
			PAGE_SIZE);
	}
	for (int i=0; i<new->as_npages1; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase2_pt->frameNumberArray[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase2_pt->frameNumberArray[i]),
			PAGE_SIZE);
	}
	for (int i=0; i<new->as_npages1; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_stack_pt->frameNumberArray[i]),
			(const void *)PADDR_TO_KVADDR(old->as_stack_pt->frameNumberArray[i]),
			PAGE_SIZE);
	}
	
	*ret = new;
	return 0;
}


#else
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}

#endif

