#ifndef FILESYS_H
#define FILESYS_H

#define MAX_FILENAME_LEN 255

struct fdesc
{
	char name[MAX_FILENAME_LEN];
	int flags;
	off_t offset;
	int ref_count;
	struct lock *lock;
	struct vnode *vn;
};


int sys_open(const char *filename,int flags,int mode);

#endif
