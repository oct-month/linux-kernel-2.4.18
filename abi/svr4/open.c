/* $Id: open.c,v 1.16 2001/11/14 19:52:51 hch Exp $
 * open.c - svr4 open(2), statfs(2), fcntl(2) and getdents(2) emulation
 *
 * Copyright (c) 1993  Joe Portman (baron@hebron.connected.com)
 * Copyright (c) 1993, 1994  Drew Sullivan (re-worked for iBCS2)
 * Copyright (c) 2000  Christoph Hellwig (rewrote lookup-related code)
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/vfs.h>
#include <linux/types.h>
#include <linux/utime.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/tty.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/un.h>
#include <linux/file.h>
#include <linux/dirent.h>
#include <linux/personality.h>
#include <linux/syscall.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <asm/abi_machdep.h>
#include <abi/svr4/statfs.h>

#include <abi/util/trace.h>
#include <abi/util/map.h>


/* ISC (at least) assumes O_CREAT if O_TRUNC is given. This is emulated
 * here but is it correct for iBCS in general? Do we care?
 */
unsigned short fl_ibcs_to_linux[] = {
	0x0001, 0x0002, 0x0800, 0x0400, 0x1000, 0x0000, 0x0000, 0x0800,
	0x0040, 0x0240, 0x0080, 0x0100, 0x0000, 0x0000, 0x0000, 0x0000
};

unsigned short fl_linux_to_ibcs[] = {
	0x0001, 0x0002, 0x0000, 0x0000, 0x0000, 0x0000, 0x0100, 0x0400,
	0x0800, 0x0200, 0x0008, 0x0004, 0x0010, 0x0000, 0x0000, 0x0000
};

static int
copy_statfs(struct svr4_statfs *buf, struct statfs *st)
{
	struct svr4_statfs ibcsstat;

	ibcsstat.f_type = st->f_type;
	ibcsstat.f_bsize = st->f_bsize;
	ibcsstat.f_frsize = 0;
	ibcsstat.f_blocks = st->f_blocks;
	ibcsstat.f_bfree = st->f_bfree;
	ibcsstat.f_files = st->f_files;
	ibcsstat.f_ffree = st->f_ffree;
	memset(ibcsstat.f_fname, 0, sizeof(ibcsstat.f_fname));
	memset(ibcsstat.f_fpack, 0, sizeof(ibcsstat.f_fpack));
	
	/* Finally, copy it to the user's buffer */
	return copy_to_user(buf, &ibcsstat, sizeof(struct svr4_statfs));
}

int svr4_statfs(const char * path, struct svr4_statfs * buf, int len, int fstype)
{
	struct svr4_statfs ibcsstat;

	if (len > (int)sizeof(struct svr4_statfs))
		return -EINVAL;
	
	if (!fstype) {
		struct nameidata nd;
		int error;

		error = user_path_walk(path, &nd);
		if (!error) {
			struct statfs tmp;
			error = vfs_statfs(nd.dentry->d_inode->i_sb, &tmp);
			if (!error && copy_statfs(buf, &tmp))
				error = -EFAULT;
			path_release(&nd);
		}
	
		return error;
	}

	/*
	 * Linux can't stat unmounted filesystems so we
	 * simply lie and claim 500MB of 8GB is free. Sorry.
	 */
	ibcsstat.f_bsize = 1024;
	ibcsstat.f_frsize = 0;
	ibcsstat.f_blocks = 8 * 1024 * 1024;	/* 8GB */
	ibcsstat.f_bfree = 500 * 1024;		/* 100MB */
	ibcsstat.f_files = 60000;
	ibcsstat.f_ffree = 50000;
	memset(ibcsstat.f_fname, 0, sizeof(ibcsstat.f_fname));
	memset(ibcsstat.f_fpack, 0, sizeof(ibcsstat.f_fpack));

	/* Finally, copy it to the user's buffer */
	return copy_to_user(buf, &ibcsstat, len) ? -EFAULT : 0;
}

int svr4_fstatfs(unsigned int fd, struct svr4_statfs * buf, int len, int fstype)
{
	struct svr4_statfs ibcsstat;

	if (len > (int)sizeof(struct svr4_statfs))
		return -EINVAL;

	if (!fstype) {
		struct file * file;
		struct statfs tmp;
		int error;
	
		error = -EBADF;
		file = fget(fd);
		if (!file)
			goto out;
		error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
		if (!error && copy_statfs(buf, &tmp))
			error = -EFAULT;
		fput(file);

out:
		return error;
	}

	/*
	 * Linux can't stat unmounted filesystems so we
	 * simply lie and claim 500MB of 8GB is free. Sorry.
	 */
	ibcsstat.f_bsize = 1024;
	ibcsstat.f_frsize = 0;
	ibcsstat.f_blocks = 8 * 1024 * 1024;	/* 8GB */
	ibcsstat.f_bfree = 500 * 1024;		/* 100MB */
	ibcsstat.f_files = 60000;
	ibcsstat.f_ffree = 50000;
	memset(ibcsstat.f_fname, 0, sizeof(ibcsstat.f_fname));
	memset(ibcsstat.f_fpack, 0, sizeof(ibcsstat.f_fpack));

	/* Finally, copy it to the user's buffer */
	return copy_to_user(buf, &ibcsstat, len) ? -EFAULT : 0;
}

int svr4_open(const char *fname, int flag, int mode)
{
#ifdef __sparc__
	return sys_open(fname, map_flags(flag, fl_ibcs_to_linux), mode);
#else
	u_long args[3];
	int error, fd;
	struct file *file;
	mm_segment_t old_fs;
	char *p;
	struct sockaddr_un addr;

	fd = sys_open(fname, map_flags(flag, fl_ibcs_to_linux), mode);
	if (fd < 0)
		return fd;

	/* Sometimes a program may open a pathname which it expects
	 * to be a named pipe (or STREAMS named pipe) when the
	 * Linux domain equivalent is a Unix domain socket. (e.g.
	 * UnixWare uses a STREAMS named pipe /dev/X/Nserver.0 for
	 * X :0 but Linux uses a Unix domain socket /tmp/.X11-unix/X0)
	 * It isn't enough just to make the symlink because you cannot
	 * open() a socket and read/write it. If we spot the error we can
	 * switch to socket(), connect() and things will likely work
	 * as expected however.
	 */
	file = fget(fd);
	if (!file)
		return fd; /* Huh?!? */
	if (!S_ISSOCK(file->f_dentry->d_inode->i_mode)) {
		fput(file);
		return fd;
	}
	fput(file);

	sys_close(fd);
	args[0] = AF_UNIX;
	args[1] = SOCK_STREAM;
	args[2] = 0;
	old_fs = get_fs();
	set_fs(get_ds());
	fd = sys_socketcall(SYS_SOCKET, args);
	set_fs(old_fs);
	if (fd < 0)
		return fd;

	p = getname(fname);
	if (IS_ERR(p)) {
		sys_close(fd);
		return PTR_ERR(p);
	}
	if (strlen(p) >= UNIX_PATH_MAX) {
		putname(p);
		sys_close(fd);
		return -E2BIG;
	}
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, p);
	putname(p);

	args[0] = fd;
	args[1] = (int)&addr;
	args[2] = sizeof(struct sockaddr_un);
	set_fs(get_ds());
	error = sys_socketcall(SYS_CONNECT, args);
	set_fs(old_fs);
	if (error) {
		sys_close(fd);
		return error;
	}

	return fd;
#endif
}

#define NAME_OFFSET(de)	((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x)	(((x)+sizeof(long)-1) & ~(sizeof(long)-1))

struct svr4_getdents_callback {
	struct dirent * current_dir;
	struct dirent * previous;
	int count;
	int error;
};

static int svr4_filldir(void * __buf, const char * name, int namlen,
	loff_t offset, ino_t ino, unsigned int d_type)
{
	struct dirent * dirent;
	struct svr4_getdents_callback * buf = (struct svr4_getdents_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;   /* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;

	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	buf->previous = dirent;
	
	if (current->personality & SHORT_INODE) {
		/* read() on a directory only handles
		 * short inodes but cannot use 0 as that
		 * indicates an empty directory slot.
		 * Therefore stat() must also fold
		 * inode numbers avoiding 0. Which in
		 * turn means that getdents() must fold
		 * inodes avoiding 0 - if the program
		 * was built in a short inode environment.
		 * If we have short inodes in the dirent
		 * we also have a two byte pad so we
		 * can let the high word fall in the pad.
		 * This makes it a little more robust if
		 * we guessed the inode size wrong.
		 */
		if (!((unsigned long)dirent->d_ino & 0xffff))
			dirent->d_ino = 0xfffffffe;
	}

	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}
			


int svr4_getdents(int fd, char *dirent, int count)
{
	struct file * file;
	struct dirent * lastdirent;
	struct svr4_getdents_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;
	error = vfs_readdir(file, svr4_filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	fput(file);

out:
	return error;
}

struct ibcs_flock {
	short l_type;	/* numbers don't match */
	short l_whence;
	off_t l_start;
	off_t l_len;	/* 0 means to end of file */
	short l_sysid;
	short l_pid;
};


int svr4_fcntl(struct pt_regs *regs)
{
	int arg1, arg2, arg3;
	int error, retval;

#ifndef __sparc__
	error = verify_area(VERIFY_READ,
			((unsigned long *)regs->esp)+1,
			3*sizeof(long));
	if (error)
		return error;
#endif /* __sparc__ */
	arg1 = get_syscall_parameter (regs, 0);
	arg2 = get_syscall_parameter (regs, 1);
	arg3 = get_syscall_parameter (regs, 2);

	switch (arg2) {
		/* These match the Linux commands. */
		case 0: /* F_DUPFD */
		case 1: /* F_GETFD */
		case 2: /* F_SETFD */
			return sys_fcntl(arg1, arg2, arg3);

		/* The iBCS flags don't match Linux flags. */
		case 3: /* F_GETFL */
			return map_flags(sys_fcntl(arg1, arg2, arg3),
					fl_linux_to_ibcs);
		case 4: /* F_SETFL */
			arg3 = map_flags(arg3, fl_ibcs_to_linux);
			return sys_fcntl(arg1, arg2, arg3);

		/* The lock stucture is different. */
		case 14: /* F_GETLK SVR4 */
			arg2 = 5;
			/* fall through */
		case 5: /* F_GETLK */
		case 6: /* F_SETLK */
		case 7: /* F_SETLKW */
		{
			struct ibcs_flock fl;
			struct flock l_fl;
			mm_segment_t old_fs;

			error = verify_area(VERIFY_WRITE, (void *)arg3,
					sizeof(fl));
			if (error)
				return error;
			error = copy_from_user(&fl, (void *)arg3, sizeof(fl));
			if (error)
				return -EFAULT;

			l_fl.l_type = fl.l_type - 1;
			l_fl.l_whence = fl.l_whence;
			l_fl.l_start = fl.l_start;
			l_fl.l_len = fl.l_len;
			l_fl.l_pid = fl.l_pid;

			abi_trace(ABI_TRACE_API,
					"lock l_type: %d l_whence: %d "
					"l_start: %lu l_len: %lu "
					"l_sysid: %d l_pid: %d\n",
					fl.l_type, fl.l_whence,
					fl.l_start, fl.l_len,
					fl.l_sysid, fl.l_pid);

			old_fs = get_fs();
			set_fs(get_ds());
			retval = sys_fcntl(arg1, arg2, (u_long)&l_fl);
			set_fs(old_fs);

			if (!retval) {
				fl.l_type = l_fl.l_type + 1;
				fl.l_whence = l_fl.l_whence;
				fl.l_start = l_fl.l_start;
				fl.l_len = l_fl.l_len;
				fl.l_sysid = 0;
				fl.l_pid = l_fl.l_pid;
				/* This should not fail... */
				copy_to_user((void *)arg3, &fl, sizeof(fl));
			}

			return retval;
		}

		case 10: /* F_ALLOCSP */
			/* Extend allocation for specified portion of file. */
		case 11: /* F_FREESP */
			/* Free a portion of a file. */
			return 0;

		/* These are intended to support the Xenix chsize() and
		 * rdchk() system calls. I don't know if these may be
		 * generated by applications or not.
		 */
		case 0x6000: /* F_CHSIZE */
			return sys_ftruncate(arg1, arg3);
#ifndef __sparc__
		case 0x6001: /* F_RDCHK */
			{
				int		error, nbytes;
				mm_segment_t	fs;
							 
				fs = get_fs();
				set_fs(get_ds());
				error = sys_ioctl(arg1, FIONREAD, &nbytes);
				set_fs(fs);

				if (error < 0)
					return (error);
				return (nbytes ? 1 : 0);
			}
#endif /* __sparc__ */

#if defined(CONFIG_ABI_SCO) || defined(CONFIG_ABI_SCO_MODULE)
		/*
		 * This could be SCO's get highest fd open if the fd we
		 * are called on is -1 otherwise it could be F_CHKFL.
		 */
		case  8: /* F_GETHFDO */
			if (arg1 == -1)
				return find_first_zero_bit(current->files->open_fds,
						current->files->max_fdset);
			/* else fall through to fail */
#else
		/* The following are defined but reserved and unknown. */
		case  8: /* F_CHKFL */
#endif

		/* These are made from the Xenix locking() system call.
		 * According to available documentation these would
		 * never be generated by an application - only by the
		 * kernel Xenix support.
		 */
		case 0x6300: /* F_LK_UNLCK */
		case 0x7200: /* F_LK_LOCK */
		case 0x6200: /* F_LK_NBLCK */
		case 0x7100: /* F_LK_RLCK */
		case 0x6100: /* F_LK_NBRLCK */

		default:
			abi_trace(ABI_TRACE_API,
					"unsupported fcntl 0x%lx, arg 0x%lx\n",
					(u_long)arg2, (u_long)arg3);

			return -EINVAL;
			break;
	}
}
