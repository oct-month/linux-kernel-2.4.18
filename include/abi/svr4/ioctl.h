#ifndef _ABI_SVR4_IOCTL_H
#define _ABI_SVR4_IOCTL_H

/*
 *  Function prototypes used for SVR4 ioctl emulation.
 *
 * $Id: ioctl.h,v 1.5 2001/07/30 11:51:12 hch Exp $
 * $Source: /work/people/hch/cvs/abi/include/abi/svr4/ioctl.h,v $
 */

/* consio.c */
extern int	svr4_console_ioctl(int, u_int, caddr_t);
extern int	svr4_video_ioctl(int, u_int, caddr_t);

/* filio.c */
extern int	svr4_fil_ioctl(int, u_int, caddr_t);

/* sockio.c */
extern int	svr4_stream_ioctl(struct pt_regs *regs, int, u_int, caddr_t);

/* socksys.c */
extern int	abi_ioctl_socksys(int, u_int, caddr_t);

/* tapeio.c */
extern int	svr4_tape_ioctl(int, u_int, caddr_t);

/* termios.c */
extern int	bsd_ioctl_termios(int, u_int, void *);
extern int	svr4_term_ioctl(int, u_int, caddr_t);
extern int	svr4_termiox_ioctl(int, u_int, caddr_t);

/* timod.c */
extern int	svr4_sockmod_ioctl(int, u_int, caddr_t);
#endif /* _ABI_SVR4_IOCTL_H */
