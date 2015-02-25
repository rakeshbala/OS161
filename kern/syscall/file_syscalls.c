/*
 * Author: Rakesh Balasubramanian
 * Created on: 24th Feb2015
 */
#include <kern/errno.h>
#include <types.h>
#include <syscall.h>
#include <thread.h>
#include <lib.h>
#include <current.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <vnode.h>
#include <synch.h>

int errno;

int
sys_open(userptr_t filename, userptr_t flags, userptr_t mode)
{

	if (filename == NULL)
	{
		errno = EFAULT;
		return -1;
	}else{
		int open_flags = (int)flags;
		/************ RB:Remove all 'OR'able flags ************/
		open_flags = open_flags & 3;
		/************ RB:Validate for multiple flags ************/
		if (open_flags == 3)
		{
			errno = EINVAL;
			return -1;
		}else{
			struct vnode * f_vnode;
			int err = vfs_open((char *)filename, (int)flags,(int)mode, &f_vnode);
			if (err)
			{
				errno = err;
				return -1;
			}else{
				open_flags = (int)flags;
				off_t offset = 0;
				/************ RB:If append, set offset at end of file ************/
				if ((open_flags & O_APPEND) == O_APPEND)
				{
					struct stat f_stat;
					int err = VOP_STAT(f_vnode, &f_stat);
					if (err)
					{
						errno = err;
						return -1;
					}
					offset = f_stat.st_size;
					int i;
					for (i=3; i<OPEN_MAX; i++){
						if (curthread->t_fdtable[i] == 0)
						{
							break;
						}
					}
					if (i>=OPEN_MAX)
					{
						errno = EMFILE;
						return -1;
					}else{

						struct fdesc *file_fd = kmalloc(sizeof(struct fdesc));
						strcpy(file_fd->name,(char *)filename);
						// int flags;
						// off_t offset;
						// int ref_count;
						// struct lock *lock;
						// struct vnode *vn;
						/************ RB:Change this lock name later - Not nice ************/
						file_fd->lock = lock_create((char *)filename);
						file_fd->offset = offset;
						file_fd->ref_count = 0;
						file_fd->vn = f_vnode;
						curthread->t_fdtable[i] = file_fd;
						return i;
					}

				}

			}
		}

	}

	// vfs_open(filename,)
	return 1;
}


void fdesc_destroy (struct fdesc *file_fd ){

	lock_destroy(file_fd->lock);
	kfree(file_fd);
}
