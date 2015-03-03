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
#include <mips/trapframe.h>
#include <addrspace.h>
#include <copyinout.h>

struct pdesc* g_pdtable[PID_LIMIT];
void childfork_func(void * ptr, unsigned long data2);

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
	int err;
	if(((uintptr_t)status % 4) !=0 ){
		return EFAULT; // Not aligned
	}
	if (status == NULL)
	{
		return EFAULT;
	}
	// int stoplen;
	// int err = copycheck(status, sizeof(userptr_t), &stoplen);
	// if (err) {
	// 	return err;
	// }

	if (pid < 0 || pid >= PID_LIMIT)
	{
		return ESRCH;
	}

	if (options != 0 &&
		options != 1 &&
		options != 2 )
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

	if (pd->exited == false && options == WNOHANG)
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
	err = copyout(&pd->exitcode, status, sizeof(int));
	if (err)
	{
		return err;
	}
	// pdesc_destroy(pd);
	g_pdtable[pid] = NULL;

	return 0;
}

int
sys_fork(struct trapframe *tf, pid_t *ret_pid)
{
	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
	memcpy(child_tf,tf,sizeof(struct trapframe));

	struct addrspace *child_as;
	int err = as_copy(curthread->t_addrspace, &child_as);
	if (err)
	{
		return err;
	}

	// char child_name[30]="child-of-";
	// strcat(child_name,curthread->t_name);

	struct thread *child_thread;
	err = thread_fork("child", childfork_func, child_tf, (vaddr_t)child_as, &child_thread);
	if (err)
	{
		// kfree(child_tf);
		return err;
	}
	/************ RB:Maybe freeing too many times ************/
	// kfree(child_tf);

	*ret_pid = child_thread->t_pid;
	return 0;

}


void childfork_func(void * tf_ptr, unsigned long as)
{

	/************ RB:Need trap frame on stack instead of heap ************/
	struct trapframe tf;
	memcpy(&tf,tf_ptr,sizeof(struct trapframe));
	kfree(tf_ptr);
	/************ RB:Prepare trap fame ************/
	tf.tf_v0 = 0;
	tf.tf_a3 = 0;
	tf.tf_epc += 4;

	curthread->t_addrspace = (struct addrspace *)as;
	as_activate(curthread->t_addrspace);
	mips_usermode(&tf);

}

void pdesc_destroy(struct pdesc * pd){
	cv_destroy(pd->wait_cv);
	lock_destroy(pd->wait_lock);
	kfree(pd);
	pd=NULL;
}
