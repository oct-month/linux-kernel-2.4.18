/*
 *	SVR4 sigaction definitions
 *
 * $Id: sigaction.h,v 1.1 2001/11/12 14:00:39 hch Exp $
 * $Source: /work/people/hch/cvs/abi/include/abi/svr4/sigaction.h,v $
 */
#ifndef _ABI_SVR4_SIGACTION_H
#define _ABI_SVR4_SIGACTION_H


struct abi_sigaction {
       int          sa_flags;
       __sighandler_t sa_handler;
       unsigned long sa_mask;
       int	    sa_resv[2];  /* Reserved for something or another */
};
#define ABI_SA_ONSTACK   1
#define ABI_SA_RESETHAND 2
#define ABI_SA_RESTART   4
#define ABI_SA_SIGINFO   8
#define ABI_SA_NODEFER  16
#define ABI_SA_NOCLDWAIT 0x10000
#define ABI_SA_NOCLDSTOP 0x20000

#endif /* _ABI_SVR4_SIGACTION_H */
