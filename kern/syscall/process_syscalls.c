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
#include <kern/errno.h>
#include <copyinout.h>
#include <lib.h>

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



int
sys_waitpid(int pid, userptr_t status, int options, pid_t *ret_pid)
{
	if(((uintptr_t)status % 4) !=0 ){
		return EFAULT; // Not aligned
	}
	if (status == NULL)
	{
		return EFAULT;
	}

	if (pid < 0 || pid >= PID_LIMIT)
	{
		return ESRCH;
	}

	if (options != 0 &&
		(options & WNOHANG) != WNOHANG &&
		(options & WUNTRACED) != WUNTRACED )
	{
		return EINVAL;
	}

	struct pdesc *pd = g_pdtable[pid];
	if (pd == NULL)
	{
		return ESRCH;
	}

	if (pd->ppid != curthread->t_pid)
	{
		return ECHILD; //Not parent
	}

	if (pd->exited == false && (options & WNOHANG) == WNOHANG)
	{
		*ret_pid = 0;
		return 0;
	}

	lock_acquire(pd->wait_lock);
	while (pd->exited == false)
	{
		cv_wait(pd->wait_cv, pd->wait_lock);
	}
	lock_release(pd->wait_lock);


	*ret_pid = pid;
	int err = copyout(&pd->exitcode, status, sizeof(int)) != 0;
	if (err)
	{
		return err;
	}

	g_pdtable[pid] = NULL;
	kfree(pd);

	return 0;
}


