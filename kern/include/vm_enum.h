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
	PS_DIRTY
} page_state;

#endif
