#ifndef FILESYS_H
#define FILESYS_H



#include <types.h>
#include <limits.h>

struct fdesc
{
	char name[NAME_MAX];
	int flags;
	off_t offset;
	int ref_count;
	struct lock *lock;
	struct vnode *vn;
};

void fdesc_destroy(struct fdesc * fd);

#endif
