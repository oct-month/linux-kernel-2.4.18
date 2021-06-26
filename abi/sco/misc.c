/*
 * Copyright (c) 2001 Christoph Hellwig.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ident "$Id: misc.c,v 1.1 2001/05/28 22:48:48 hch Exp $"

/*
 * Misc SCO syscalls.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/syscall.h>


int
sco_lseek(int fd, u_long offset, int whence)
{
	int			error;

	error = sys_lseek(fd, offset, whence);
	if (error == -ESPIPE) {
		struct file	*fp = fget(fd);
		struct inode	*ip;

		if (fp == NULL)
			goto out;
		ip = fp->f_dentry->d_inode;
		if (ip && (S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode)))
			error = 0;
		fput(fp);
	}
out:
	return (error);
}
