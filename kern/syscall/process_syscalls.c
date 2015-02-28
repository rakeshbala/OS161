/**
 * Author: Rakesh Balasubramanian and Ramya Rao
 * Date: 27th Feb 2015
 * Process system calls
 */

#include <syscall.h>
#include <kern/wait.h>
#include <kern/procsys.h>
#include <thread.h>
#include <current.h>

struct pdesc* g_pdtable[PID_LIMIT];

void
sys__exit(int exitcode)
{

	struct pdesc *pd = g_pdtable[curthread->t_pid];
	pd->exitcode = _MKWAIT_EXIT(exitcode);
	pd->exited = true;
	lock_acquire(pd->wait_lock);
	cv_signal(pd->wait_cv,pd->wait_lock);
	lock_release(pd->wait_lock);
	thread_exit();
}

