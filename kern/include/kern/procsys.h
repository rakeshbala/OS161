#ifndef PROCSYS_H
#define PROCSYS_H



#include <types.h>
#include <synch.h>


#define PID_LIMIT 512

struct pdesc
{
	struct cv *wait_cv;
	struct lock *wait_lock;
	bool exited;
	pid_t ppid;
	int exitcode;
	struct thread * self;

};

extern struct pdesc* g_pdtable[PID_LIMIT];
extern struct lock * process_lock;


void pdesc_destroy(struct pdesc * pd);

#endif
