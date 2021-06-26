#ifndef _ASMi386_PARAM_H
#define _ASMi386_PARAM_H

#include <linux/config.h>

#ifdef CONFIG_HZ
#define HZ CONFIG_HZ
#endif

#ifndef HZ
#define HZ 100
#endif

#ifdef __KERNEL__
#if HZ == 100
/* X86 is defined to provide userspace with a world where HZ=100
   We have to do this, (x*const)/const2 isnt optimised out because its not
   a null operation as it might overflow.. */
#define hz_to_std(a) (a)
#else
#if HZ == 1000
#define hz_to_std(a) ( (a) / 10 )
#else
#if HZ == 800
#define hz_to_std(a) ( (a) >>3 )
#else
#if HZ == 512
#define hz_to_std(a) ( (((a)) - ((a+2)>>2) + ((a+16)>>5))>>2 )
#else
#define hz_to_std(a) (((a)*100)/HZ)
#endif
#endif
#endif
#endif
#endif


#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#ifdef __KERNEL__
# define CLOCKS_PER_SEC	100	/* frequency at which times() counts */
#endif

#endif
