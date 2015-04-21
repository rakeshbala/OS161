#ifndef _VM_ENUM_H_
#define _VM_ENUM_H_

typedef enum {
  AX_READ = 4,
  AX_WRITE = 2,
  AX_EXECUTE = 1
} ax_permssion;


typedef enum {
	PS_FREE,
	PS_FIXED,
	PS_CLEAN,
	PS_DIRTY,
	PS_VICTIM
} page_state;

typedef enum{
	PTE_ONDISK = 1,
	PTE_LOCKED = 2
} pte_state;

#endif
