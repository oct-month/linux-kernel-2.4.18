/*
 * This is straight from linux/fs/stat.c.
 */
#ifndef _ABI_UTIL_REVALIDATE_H
#define _ABI_UTIL_REVALIDATE_H

#ident "$Id: revalidate.h,v 1.1 2001/10/03 22:29:07 hch Exp $"

/*
 * This is required for proper NFS attribute caching (so it says there).
 * Maybe the kernel should export it - but it is basically simple...
 */
static __inline int
do_revalidate(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	if (inode->i_op && inode->i_op->revalidate)
		return inode->i_op->revalidate(dentry);
	return 0;
}

#endif /* _ABI_UTIL_REVALIDATE_H */
