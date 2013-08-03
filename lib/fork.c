// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	if (((err & 0x2) == 0) ||
			((uvpd[PDX(addr)] & PTE_P) == 0) ||
			((uvpt[PGNUM(addr)] & (PTE_P | PTE_U | PTE_COW)) != 
			 (PTE_P | PTE_U | PTE_COW))) {
		cprintf("[%08x] user fault %p ip %08x\n",
				sys_getenvid(), addr, utf->utf_eip);
		cprintf("Invalid fault access: Err = 0x%08x, ", err);
		if ((uvpd[PDX(addr)] & PTE_P) == 0) {
			cprintf("PDE_FLAGS = 0x%08x\n", 
					(uvpd[PDX(addr)] & PTE_SYSCALL));
		}
		else {
			cprintf("PTE_FLAGS = 0x%08x\n", 
					(uvpt[PGNUM(addr)] & PTE_SYSCALL));
		}
		panic("");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.

	// panic("pgfault not implemented");
	r = sys_page_alloc(0, (void *)PFTEMP, (PTE_P | PTE_U | PTE_W));
	if (r < 0)
		panic("sys_page_alloc: %e", r);

	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE),
			(PTE_P | PTE_U | PTE_W));
	if (r < 0)
		panic("sys_page_map: %e", r);

	r = sys_page_unmap(0, PFTEMP);
	if (r < 0)
		panic("sys_page_unmap: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	// panic("duppage not implemented");
	void *addr = (void *)(pn * PGSIZE);
	int perm;

	if (addr >= (void *)UTOP)
		panic("invalid page number: %d (0x%08x)", pn, addr);

	perm = (uvpt[pn] & PTE_SYSCALL);

	if ((perm & PTE_P) == 0)
		return 0;

	if (((perm & PTE_SHARE) == 0) && ((perm & PTE_W) != 0)) {
		perm &= ~PTE_W;
		perm |= PTE_COW;
	}

	r = sys_page_map(0, addr, envid, addr, perm);
	if (r < 0)
		panic("sys_page_map: %e", r);

	if (perm & PTE_COW) {
		r = sys_page_map(0, addr, 0, addr, perm);
		if (r < 0)
			panic("sys_page_map: %e", r);
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// panic("fork not implemented");
	envid_t envid;
	void *addr, *limit;
	int r;

	set_pgfault_handler(pgfault);

	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// child
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// parent
//	for (addr = (void *)UTEXT; addr < (void *)end; addr += PGSIZE)
//		duppage(envid, PGNUM(addr));

	for (addr = (void *)UTEXT; 
			addr < (void *)ROUNDDOWN(&addr, PGSIZE); 
			addr += (1 << PDXSHIFT)) {
		if ((uvpd[PDX(addr)] & PTE_P) == 0)
			continue;
		limit = (addr + (1 << PDXSHIFT));
		if (limit > (void *)ROUNDDOWN(&addr, PGSIZE))
			limit = ROUNDDOWN(&addr, PGSIZE);
		for (; addr < limit; addr += PGSIZE)
			duppage(envid, PGNUM(addr));
	}

	// copy the stack
	addr = ROUNDDOWN(&addr, PGSIZE);
	r = sys_page_alloc(envid, addr, (uvpt[PGNUM(addr)] & PTE_SYSCALL));
	if (r < 0)
		panic("sys_page_alloc: %e", r);
	r = sys_page_map(envid, addr, 0, UTEMP, (uvpt[PGNUM(addr)] & PTE_SYSCALL));
	if (r < 0)
		panic("sys_page_map: %e", r);
	memmove(UTEMP, addr, PGSIZE);
	r = sys_page_unmap(0, UTEMP);
	if (r < 0)
		panic("sys_page_unmap: %e", r);

	// allocate a new page for the child's user exception stack
	addr = (void *)(UXSTACKTOP - PGSIZE);
	r = sys_page_alloc(envid, addr, (uvpt[PGNUM(addr)] & PTE_SYSCALL));
	if (r < 0)
		panic("sys_page_alloc: %e", r);
	r = sys_page_map(envid, addr, 0, UTEMP, (uvpt[PGNUM(addr)] & PTE_SYSCALL));
	if (r < 0)
		panic("sys_page_map: %e", r);
	memmove(UTEMP, addr, PGSIZE);
	r = sys_page_unmap(0, UTEMP);
	if (r < 0)
		panic("sys_page_unmap: %e", r);

	// set the page fault handler
	extern void _pgfault_upcall(void);
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (r < 0)
		panic("sys_env_set_pgfault_upcall: %e", r);

	// set the child environment runnable
	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
