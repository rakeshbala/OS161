#ifndef FILESYS_H
#define FILESYS_H


#define MAX_FILENAME_LEN 255

#include <types.h>

struct fdesc
{
	char name[MAX_FILENAME_LEN];
	int flags;
	off_t offset;
	int ref_count;
	struct lock *lock;
	struct vnode *vn;
};

void fdesc_destroy(struct fdesc * fd);

#endif
