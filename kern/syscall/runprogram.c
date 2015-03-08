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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <synch.h>
#include <copyinout.h>
/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	char temp_progname[NAME_MAX];
	strcpy(temp_progname,progname);
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	// /* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/************ RB:Initialize console - Begin ************/
	// stdin
	struct vnode *stdin;
	char consolePath[5];
	strcpy(consolePath,"con:");
	result = vfs_open(consolePath, O_RDONLY, 0664, &stdin);
	if (result)
	{
		return result;
	}
	struct fdesc * stdin_fd = kmalloc(sizeof(struct fdesc));
	strcpy(stdin_fd->name,"con:");
	stdin_fd->flags = O_RDONLY;
	stdin_fd->offset = 0;
	stdin_fd->ref_count = 1;
	stdin_fd->lock = lock_create("con");
	stdin_fd->vn =stdin;

	// stdout
	struct vnode *stdout;
	strcpy(consolePath,"con:");
	result = vfs_open(consolePath, O_WRONLY, 0664, &stdout);
	if (result)
	{
		return result;
	}
	struct fdesc * stdout_fd = kmalloc(sizeof(struct fdesc));
	strcpy(stdout_fd->name,"con:");
	stdout_fd->flags = O_WRONLY;
	stdout_fd->offset = 0;
	stdout_fd->ref_count = 1;
	stdout_fd->lock = lock_create("con");
	stdout_fd->vn =stdout;

	//stderr
	struct vnode *stderr;
	strcpy(consolePath,"con:");
	result = vfs_open(consolePath, O_WRONLY, 0664, &stderr);
	if (result)
	{
		return result;
	}
	struct fdesc * stderr_fd = kmalloc(sizeof(struct fdesc));
	strcpy(stderr_fd->name,"con:");
	stderr_fd->flags = O_WRONLY;
	stderr_fd->offset = 0;
	stderr_fd->ref_count = 1;
	stderr_fd->lock = lock_create("con");
	stderr_fd->vn =stderr;

	curthread->t_fdtable[0] = stdin_fd;
	curthread->t_fdtable[1] = stdout_fd;
	curthread->t_fdtable[2] = stderr_fd;

	/************ RB:End ************/



#if 0
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
#else
	int err;
	/************ RB:Calculate total amout to be copied ************/
	int progname_len = strlen(temp_progname);
	int padding = (4-((progname_len+1)%4))%4;		//align by 4 (including \0 at the end)
	int copylen = 8+progname_len+1+padding;		//First 8 = 2*4 is the size of 2 pointers
	stackptr -= copylen;						//Reduce stackptr to accomodate full copy

	size_t actual;
	err = copyoutstr(temp_progname, (userptr_t)stackptr+8, progname_len+1, &actual);
	if(err)
	{
		return err;
	}

	char *kbuf[2];
	kbuf[0]=(char *)(stackptr+8);
	kbuf[1]= NULL;

	/************ RB: Null out padding ************/
	for (int i = progname_len; i < progname_len+padding+1; ++i)
	{
		kbuf[0][i] = '\0';
	}
	if ((err = copyout(kbuf, (userptr_t)stackptr, sizeof(kbuf))) != 0)
	{
		return err;
	}
	enter_new_process(1 /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
			  stackptr, entrypoint);

#endif
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

