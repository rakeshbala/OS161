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
#include <uio.h>
#include <unistd.h>

int
sys_open(userptr_t filename, int flags, int mode, int *fd)
{

	if (filename == NULL)
	{
		return EFAULT;
	}else{
		int open_flags = flags;
		/************ RB:Validate for multiple flags ************/
		if (open_flags & O_RDONLY && open_flags & O_WRONLY)
		{
			return EINVAL;
		}else if(open_flags & O_RDONLY && open_flags & O_RDWR)
		{
			return EINVAL;
		}else if(open_flags & O_WRONLY && open_flags & O_RDWR)
		{
			return EINVAL;
		}else
		{
			struct vnode * f_vnode;
			int err = vfs_open((char *)filename, flags, mode, &f_vnode);
			if (err){
				return err;
			}else{
				open_flags = flags;
				off_t offset = 0;
				/************ RB:If append, set offset at end of file ************/
				if ((open_flags & O_APPEND) == O_APPEND)
				{
					struct stat f_stat;
					int err = VOP_STAT(f_vnode, &f_stat);
					if (err)
					{
						return err;
					}
					offset = f_stat.st_size;

				}
				int i;
				for (i=3; i<OPEN_MAX; i++){
					if (curthread->t_fdtable[i] == 0)
					{
						break;
					}
				}
				if (i>=OPEN_MAX)
				{
					return EMFILE;
				}else{

					struct fdesc *file_fd = kmalloc(sizeof(struct fdesc));
					strcpy(file_fd->name,(char *)filename);

						/************ RB:Change this lock name later - Not nice ************/
					file_fd->lock = lock_create(file_fd->name);
					file_fd->offset = offset;
					file_fd->ref_count = 1;
					file_fd->vn = f_vnode;
					file_fd->flags = flags;
					curthread->t_fdtable[i] = file_fd;
					*fd = i;
					return 0;
				}

			}
		}

	}

	// vfs_open(filename,)
	return 1;
}

int
sys_read(int fd, userptr_t buf, size_t nbytes, size_t *bytes_read)
{
	struct iovec iov;
	struct uio u;

	if (buf == NULL)
	{
		return EFAULT;
	}

	struct fdesc *t_fd = curthread->t_fdtable[fd];
	if (t_fd == NULL)
	{
		return EBADF;
	}

	if (!(t_fd->flags & O_RDONLY || t_fd->flags & O_RDWR)){
		return EBADF;
	}

	iov.iov_ubase = buf;
	iov.iov_len = nbytes;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = nbytes;          // amount to read from the file
	u.uio_offset = t_fd->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curthread->t_addrspace;



	int result = VOP_READ(t_fd->vn, &u);
	if (result) {
		return result;
	}

	*bytes_read = nbytes-u.uio_resid-1;
	t_fd->offset += (*bytes_read);

	return 0;

}

int
sys_write(int fd, userptr_t buf, size_t nbytes, size_t *bytes_written)
{
	struct iovec iov;
	struct uio u;

	if (buf == NULL)
	{
		return EFAULT;
	}

	struct fdesc *t_fd = curthread->t_fdtable[fd];
	if (t_fd == NULL)
	{
		return EBADF;
	}

	if (!(t_fd->flags & O_WRONLY || t_fd->flags & O_RDWR)) {
		return EBADF;
	}


	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = nbytes;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = nbytes;          // amount to read from the file
	u.uio_offset = t_fd->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = curthread->t_addrspace;



	int result = VOP_WRITE(t_fd->vn, &u);
	if (result) {
		return result;
	}

	*bytes_written = nbytes-u.uio_resid-1;
	t_fd->offset += (*bytes_written);

	return 0;
}

int sys_close(int fd)
{

	struct fdesc *t_fdesc = curthread->t_fdtable[fd];
	if(t_fdesc == NULL) {
		return EBADF;
	}
	t_fdesc->ref_count--;
	if(t_fdesc->ref_count == 0) {
		return vfs_close(t_fdesc->vn);
	}

	return 0;
}

int sys_lseek(int fd, off_t pos, int whence, off_t *new_pos)
{
	struct fdesc *t_fdesc = curthread->t_fdtable[fd];
	if(t_fdesc == NULL) {
		return EBADF;
	}
	if(fd >=0 && fd < 3) {
		return ESPIPE;
	}
	off_t offset = 0;
	int err = 0;
	if(whence == SEEK_SET) {
		offset = pos;
	} else if (whence == SEEK_CUR) {
		offset += pos;
	} else if (whence == SEEK_END) {
		struct stat f_stat;
		err = VOP_STAT(t_fdesc->vn, &f_stat);
		if (err) {
			return err;
		}
		offset = f_stat.stat;
	} else {
		return EINVAL;
	}
	err = VOP_TRYSEEK(t_fdesc->vn, offset);
	if (err) {
		return err;
	}
	t_fdesc->offset = offset;
	return 0;
}

int sys_chdir (const char *pathname)
{
	if(pathname == NULL) {
		return EFAULT;
	}
	err = vfs_chdir(*pathname);
	if (err) {
		return err;
	}
	return 0;
}

void fdesc_destroy (struct fdesc *file_fd )
{

	lock_destroy(file_fd->lock);
	kfree(file_fd);
}
