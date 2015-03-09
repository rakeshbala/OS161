/*
 * Authors: Rakesh Balasubramanian, Ramya Rao
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
#include <kern/seek.h>
#include <vnode.h>
#include <synch.h>
#include <uio.h>
#include <copyinout.h>

int
sys_open(userptr_t filename, int flags, int mode, int *fd)
{

	char kbuf[NAME_MAX];
	size_t actual;
	int err;
	if ((err = copyinstr(filename, kbuf, NAME_MAX, &actual)) != 0)
	{
		return err;
	}

	if (filename == NULL)
	{
		return EFAULT;
	}

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
		int err = vfs_open(kbuf, flags, mode, &f_vnode);
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
				if (file_fd == NULL)
				{
					return ENOMEM;
				}
				strcpy(file_fd->name,(char *)filename);

					/************ RB:Change this lock name later - Not nice ************/
				file_fd->lock = lock_create(file_fd->name);
				if (file_fd->lock == NULL)
				{
					kfree(file_fd);
					return ENOMEM;
				}
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

int
sys_read(int fd, userptr_t buf, size_t nbytes, size_t *bytes_read)
{
	struct iovec iov;
	struct uio u;

	if (buf == NULL)
	{
		return EFAULT;
	}

	if (fd<0 || fd>=OPEN_MAX)
	{
		return EBADF;
	}

	struct fdesc *t_fd = curthread->t_fdtable[fd];
	if (t_fd == NULL)
	{
		return EBADF;
	}

	lock_acquire(t_fd->lock);
		if (!((t_fd->flags & O_RDONLY) == O_RDONLY
			|| (t_fd->flags & O_RDWR) == O_RDWR)){
			lock_release(t_fd->lock);
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
			lock_release(t_fd->lock);
			return result;
		}
		*bytes_read = nbytes-u.uio_resid;
		t_fd->offset += (*bytes_read);
	lock_release(t_fd->lock);
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
	if ( fd<0 || fd >= OPEN_MAX)
	{
		return EBADF;
	}
	struct fdesc *t_fd = curthread->t_fdtable[fd];
	if (t_fd == NULL)
	{
		return EBADF;
	}
	lock_acquire(t_fd->lock);
		if (!( (t_fd->flags & O_WRONLY) == O_WRONLY
			|| (t_fd->flags & O_RDWR) == O_RDWR))
		{
			lock_release(t_fd->lock);
			return EBADF;
		}
		iov.iov_ubase = (userptr_t)buf;
		iov.iov_len = nbytes;		 // length of the memory space
		u.uio_iov = &iov;
		u.uio_iovcnt = 1;
		u.uio_resid = nbytes;          // amount to write to file
		u.uio_offset = t_fd->offset;
		u.uio_segflg = UIO_USERSPACE;
		u.uio_rw = UIO_WRITE;
		u.uio_space = curthread->t_addrspace;
		int result = VOP_WRITE(t_fd->vn, &u);
		if (result) {
			lock_release(t_fd->lock);
			return result;
		}
		*bytes_written = nbytes-u.uio_resid;
		t_fd->offset += (*bytes_written);
	lock_release(t_fd->lock);
	return 0;
}

int sys_close(int fd)
{

	if (fd < 0 || fd >= OPEN_MAX)
	{
		return EBADF;
	}
	struct fdesc *t_fdesc = curthread->t_fdtable[fd];
	if(t_fdesc == NULL) {
		return EBADF;
	}
	lock_acquire(t_fdesc->lock);
	t_fdesc->ref_count--;
	curthread->t_fdtable[fd] = NULL;
	if(t_fdesc->ref_count == 0) {
		vfs_close(t_fdesc->vn);
		lock_release(t_fdesc->lock);
		fdesc_destroy(t_fdesc);
	}else{
		lock_release(t_fdesc->lock);
	}
	return 0;
}
/*********** RR: 26Feb2015 ***********/
int sys_lseek(int fd, off_t pos, int whence, off_t *new_pos)
{
	if (fd < 0 || fd >= OPEN_MAX)
	{
		return EBADF;
	}
	struct fdesc *t_fdesc = curthread->t_fdtable[fd];
	if(t_fdesc == NULL) {
		return EBADF;
	}
	if (strcmp(t_fdesc->name,"con:") == 0)
	{
		return ESPIPE;
	}

	lock_acquire(t_fdesc->lock);
		off_t offset = 0;
		int err = 0;
		if(whence == SEEK_SET) {
			offset = pos;
		} else if (whence == SEEK_CUR) {
			offset = t_fdesc->offset + pos;
		} else if (whence == SEEK_END) {
			struct stat f_stat;
			err = VOP_STAT(t_fdesc->vn, &f_stat);
			if (err) {
				lock_release(t_fdesc->lock);
				return err;
			}
			offset = f_stat.st_size + pos;
		} else {
			lock_release(t_fdesc->lock);
			return EINVAL;
		}
		err = VOP_TRYSEEK(t_fdesc->vn, offset);
		if (err) {
			lock_release(t_fdesc->lock);
			return err;
		}
		t_fdesc->offset = offset;
		*new_pos = t_fdesc->offset;
	lock_release(t_fdesc->lock);
	return 0;
}
/*********** RR: 27Feb2015 ***********/
int sys_chdir (userptr_t pathname)
{
	if(pathname == NULL) {
		return EFAULT;
	}
	char kbuf[PATH_MAX];
	size_t actual;
	int err;
	if ((err = copyinstr(pathname, kbuf, PATH_MAX, &actual)) != 0)
	{
		return err;
	}
	err = vfs_chdir(kbuf);
	if (err) {
		return err;
	}
	return 0;
}
/*********** RR: 27Feb2015 ***********/
int sys___getcwd(userptr_t buf, size_t buflen, size_t *data_len)
{
	if(buf == NULL) {
		return EFAULT;
	}

	struct uio u;
	struct iovec iov;
	iov.iov_ubase = buf;
	iov.iov_len = buflen;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;
	u.uio_offset = 0;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curthread->t_addrspace;
	int err = vfs_getcwd(&u);
	if (err) {
		return err;
	}
	*data_len = buflen-u.uio_resid;
	return 0;
}

/*********** RR: 28Feb2015 ***********/
int sys_dup2(int oldfd, int newfd, int *ret_fd)
{
	int err = 0;
	if ((oldfd < 0 || oldfd >= OPEN_MAX) || (newfd < 0 || newfd >= OPEN_MAX))
	{
		return EBADF;
	}
	struct fdesc *old_fdesc = curthread->t_fdtable[oldfd];
	struct fdesc *new_fdesc = curthread->t_fdtable[newfd];
	if(old_fdesc == NULL) {
		return EBADF;
	}
	if (oldfd == newfd)
	{
		*ret_fd = newfd;
		return 0;
	}
	if (new_fdesc != NULL)
	{
		err = sys_close(newfd);
		if (err) {
			return err;
		}
	}
	curthread->t_fdtable[oldfd]->ref_count++;
	curthread->t_fdtable[newfd] = curthread->t_fdtable[oldfd];
	*ret_fd = newfd;
	return 0;
}

void fdesc_destroy (struct fdesc *file_fd )
{
	lock_destroy(file_fd->lock);
	kfree(file_fd);
}
