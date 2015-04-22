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
#include <kern/fcntl.h>
#include <copyinout.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <vfs.h>
#include <copyinout.h>
#include <vm.h>


#define MAX_ARG_NUM 100
#define MAX_ARG_LENGTH 100

struct pdesc* g_pdtable[PID_LIMIT];
void childfork_func(void * ptr, unsigned long data2);
void cleanup_dirtyproc(struct addrspace * as, char **kbuf, int argc);

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
	pdesc_destroy(pd);
	g_pdtable[pid] = NULL;

	return 0;
}

int
sys_fork(struct trapframe *tf, pid_t *ret_pid)
{
	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
	if (child_tf == NULL)
	{
		return ENOMEM;
	}
	memmove(child_tf,tf,sizeof(struct trapframe));
	int err;

	struct thread *child_thread;
	err = thread_fork("child", childfork_func, child_tf, (vaddr_t)NULL, &child_thread);
	if (err)
	{
		return err;
	}
	*ret_pid = child_thread->t_pid;
	return 0;

}


void childfork_func(void * tf_ptr, unsigned long as)
{

	/************ RB:Need trap frame on stack instead of heap ************/
	struct trapframe tf;
	memmove(&tf,tf_ptr,sizeof(struct trapframe));
	kfree(tf_ptr);
	/************ RB:Prepare trap fame ************/
	tf.tf_v0 = 0;
	tf.tf_a3 = 0;
	tf.tf_epc += 4;
	(void)as;
	mips_usermode(&tf);
}

int
sys_execv(userptr_t u_program, userptr_t u_uargs)
{

	char program[NAME_MAX];
	size_t actual;
	int err = 0;

	err = copyinstr(u_program, program, NAME_MAX, &actual);
	if (err)
	{
		return err;
	}


	if (strcmp(program,"") == 0)
	{
		return EISDIR;
	}

	char *uargs[MAX_ARG_NUM];
	err = copyin(u_uargs,uargs,MAX_ARG_NUM);
	if (err)
	{
		return err;
	}

	int index = 0;
	int copylength = 0;
	char *kbuf[MAX_ARG_NUM];
	while(uargs[index] != NULL) {
		kbuf[index] = kmalloc(MAX_ARG_LENGTH);
		if (kbuf[index] == NULL)
		{
			return ENOMEM;
		}
		err = copyin((const userptr_t) uargs[index], (char *)kbuf[index], (size_t)MAX_ARG_LENGTH);
		if (err) {

			cleanup_dirtyproc(NULL, kbuf, index+1);
			return err;
		}
		int arg_length = strlen(uargs[index]);
		int padding = (4-((arg_length+1)%4))%4;		//align by 4 (including \0 at the end)
		copylength += arg_length+padding +1;
		index++;
	}
	int argc = index;
	copylength += 4*(argc+1);

	/************ RR:Rest of run program ************/
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int prog_len = strlen(program);
	char temp_progname[prog_len];
	strcpy(temp_progname,program);
	err = vfs_open(temp_progname, O_RDONLY, 0, &v);
	if (err) {
		cleanup_dirtyproc(NULL, kbuf, index+1);
		return err;
	}


	/* Free . */
	struct addrspace *parent_as = NULL;
	if (curthread->t_addrspace != NULL)
	{
		parent_as = curthread->t_addrspace;
	}

	// /* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		cleanup_dirtyproc(NULL, kbuf, argc);
		vfs_close(v);
		return ENOMEM;
	}
	/* Activate it. */
	as_activate(curthread->t_addrspace);
	/* Load the executable. */
	err = load_elf(v, &entrypoint);
	if (err) {
		cleanup_dirtyproc(parent_as, kbuf, argc);
		vfs_close(v);
		return err;
	}
	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	err = as_define_stack(curthread->t_addrspace, &stackptr);
	if (err) {
		cleanup_dirtyproc(parent_as, kbuf, argc);
		return err;
	}

	/************ RR:Pack the variables and dispatch them ************/
	stackptr -= copylength;
	int prev_offset = 0;
	char* ret_buf[argc+1];
	/*********** RR:moving contents from kernel buffer to user stack ***********/
	for (int l = 0; l < argc; ++l)
	{
		int arg_length = strlen(kbuf[l]);
		int padding = (4-((arg_length+1)%4))%4;
		char * dest = (char *)stackptr+(index+1)*4+prev_offset;
		err = copyout(kbuf[l],(userptr_t)dest,(size_t)arg_length+1);
		if (err) {
			cleanup_dirtyproc(parent_as, kbuf, argc);
			return err;
		}
		for (int i = arg_length; i < arg_length+padding+1; ++i)
		{
			dest[i] = '\0';
		}
		ret_buf[l] = (char *)dest;
		prev_offset += (arg_length+padding+1);
	}
	ret_buf[argc] = NULL;
	err = copyout(ret_buf,(userptr_t)stackptr, sizeof(ret_buf));
	if (err) {
		cleanup_dirtyproc(parent_as, kbuf, argc);
		return err;
	}

	cleanup_dirtyproc(NULL, kbuf, argc);
	as_destroy(parent_as);

	enter_new_process(argc, (userptr_t)stackptr /*userspace addr of argv*/,
			  stackptr, entrypoint);
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");

	(void)u_program;
	(void)u_uargs;
	return EINVAL;
}

void pdesc_destroy(struct pdesc * pd)
{
	cv_destroy(pd->wait_cv);
	lock_destroy(pd->wait_lock);
	kfree(pd);
}

void cleanup_dirtyproc(struct addrspace * as, char **kbuf, int argc)
{
	if (as)
	{
		as_destroy(curthread->t_addrspace);
		curthread->t_addrspace = as;
	}
	for (int i = 0; i < argc; ++i)
	{
		kfree(kbuf[i]);
	}
}

int
sys_sbrk(intptr_t amount,struct addrspace* as,int *returnVal)
{

	/*********** RR: logic for alignment checking ***********/
	//if amount is not aligned, reject
	vaddr_t new_heap = as->heap_end + amount;
	unsigned int tempAmount = amount > 0 ?amount:(amount * -1);
	if(amount > 0 || tempAmount <= as->heap_end - as->heap_start)
	{
		if ((tempAmount <= USERSTACKBASE - as->heap_end) &&
			(tempAmount < USERHEAPLIMIT))
		{
			*returnVal = as->heap_end;
			as->heap_end = new_heap;
			// kprintf("Heap moved to %lx\n",(long unsigned int)as->heap_end);
			return 0;
		}else{
			return ENOMEM;
		}
	}
	else
	{
		*returnVal = -1;
		return EINVAL;
	}
}
