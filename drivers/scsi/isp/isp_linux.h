/* @(#)isp_linux.h 1.39 */
/*
 * Qlogic ISP SCSI Host Adapter Linux Wrapper Definitions
 *---------------------------------------
 * Copyright (c) 1998, 1999, 2000, 2001 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * PMB #825
 * 5214-F Diamond Hts Blvd
 * San Francisco, CA, 94131
 * mjacob@feral.com
 */

#ifndef _ISP_LINUX_H
#define _ISP_LINUX_H

#ifndef	ISP_MODULE
#define	__NO_VERSION__
#endif
#ifdef	LINUX_ISP_TARGET_MODE
#define	EXPORT_SYMTAB
#endif

#include <linux/version.h>
#ifndef	KERNEL_VERSION
#define KERNEL_VERSION(v,p,s)		(((v)<<16)+(p<<8)+s)
#endif
#define	_KVC	KERNEL_VERSION

#if LINUX_VERSION_CODE <= _KVC(2,2,0)
#error	"Linux 2.0 and 2.1 kernels are not supported anymore"
#endif
#if LINUX_VERSION_CODE >= _KVC(2,3,0) && LINUX_VERSION_CODE < _KVC(2,4,0)
#error	"Linux 2.3 kernels are not supported"
#endif

#ifndef	UNUSED_PARAMETER
#define	UNUSED_PARAMETER(x)	(void) x
#endif

#include <linux/autoconf.h>
#ifdef	CONFIG_SMP
#define	__SMP__	1
#endif

#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/smp.h>
#include <linux/spinlock.h>
#else
#include <asm/spinlock.h>
#endif
#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/interrupt.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"

/*
 * These bits and pieces of keeping track of Linux versions
 * and some of the various foo items for locking/unlocking
 * gratefully borrowed from (amongst others) Doug Ledford
 * and Gerard Roudier.
 */

#define	PWRB(p, o, r)	pci_write_config_byte(p->pci_dev, o, r)
#define	PWRW(p, o, r)	pci_write_config_word(p->pci_dev, o, r)
#define	PWRL(p, o, r)	pci_write_config_dword(p->pci_dev, o, r)
#define	PRDW(p, o, r)	pci_read_config_word(p->pci_dev, o, r)
#define	PRDD(p, o, r)	pci_read_config_dword(p->pci_dev, o, r)
#define	PRDB(p, o, r)	pci_read_config_byte(p->pci_dev, o, r)

#ifndef	bus_dvma_to_mem
#if defined (__alpha__)
#define bus_dvma_to_mem(p)              ((p) & 0xfffffffful)
#else
#define bus_dvma_to_mem(p)              (p)
#endif
#endif

#if defined (__powerpc__)
#undef	__pa
#define	__pa(x)	x
#endif
#if defined (__i386__) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#undef	__pa
#define	__pa(x)	x
#endif
#if defined (__sparc__) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#undef	__pa
#define	__pa(x)	x
#endif
#if defined (__alpha__) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#undef	__pa
#define	__pa(x)	x
#endif

/*
 * Efficiency- get rid of SBus code && tests unless we need them.
 */
#if	defined(__sparcv9__ ) || defined(__sparc__)
#define	ISP_SBUS_SUPPORTED	1
#else
#define	ISP_SBUS_SUPPORTED	0
#endif

#define	ISP_PLATFORM_VERSION_MAJOR	2
#define	ISP_PLATFORM_VERSION_MINOR	1

#ifndef	BIG_ENDIAN
#define	BIG_ENDIAN	4321
#endif
#ifndef	LITTLE_ENDIAN
#define	LITTLE_ENDIAN	1234
#endif

#ifdef	__BIG_ENDIAN
#define	BYTE_ORDER	BIG_ENDIAN
#endif
#ifdef	__LITTLE_ENDIAN
#define	BYTE_ORDER	LITTLE_ENDIAN
#endif

#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define	DMA_ADDR_T		unsigned long
#define	QLA_SG_C(sg)		sg->length
#define	QLA_SG_A(sg)		virt_to_bus(sg->address)
#else
#define	DMA_ADDR_T		dma_addr_t
#define	QLA_SG_C(sg)		sg_dma_len(sg)
#define	QLA_SG_A(sg)		(DMA_ADDR_T) sg_dma_address(sg)
#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,4,16)
#define	DMA_HTYPE_T		char *
#define	QLA_HANDLE(cmd)		(cmd)->SCp.ptr
#else
#define	DMA_HTYPE_T		dma_addr_t
#define	QLA_HANDLE(cmd)		(cmd)->SCp.dma_handle
#endif
#endif

#define	HANDLE_LOOPSTATE_IN_OUTER_LAYERS	1
#ifdef	min
#undef	min
#endif
#ifdef	max
#undef	max
#endif


/*
 * Normally this should be taken care of by typedefs,
 * but linux includes are a complete dog's breakfast.
 */

#define	u_int8_t	unsigned char
#define	u_int16_t	unsigned short
#define	u_int32_t	unsigned int
#if BITS_PER_LONG == 64
#define	u_int64_t	unsigned long
#else
#define	u_int64_t	unsigned long long
#endif
#define	int8_t		char
#define	int16_t		short
#define	int32_t		int
#define	u_long		unsigned long
#define	u_int		unsigned int
#define	u_char		unsigned char
typedef u_long vm_offset_t;

#ifdef	LINUX_ISP_TARGET_MODE
#define	DEFAULT_DEVICE_TYPE	3
#define	NTGT_CMDS		256

#define	_WIX(isp, b, ix)	(((b << 6)) | (ix >> 5))
#define	_BIX(isp, ix)		(1 << (ix & 0x1f))

#define	LUN_BTST(isp, b, ix)	\
	(((isp)->isp_osinfo.lunbmap[_WIX(isp, b, ix)] & _BIX(isp, ix)) != 0)

#define	LUN_BSET(isp, b, ix)	\
	isp->isp_osinfo.lunbmap[_WIX(isp, b, ix)] |= _BIX(isp, ix)

#define	LUN_BCLR(isp, b, ix)	\
	isp->isp_osinfo.lunbmap[_WIX(isp, b, ix)] &= ~_BIX(isp, ix)

#if	defined(__alpha__) || defined(__sparc_v9__)
#define	_TMD_PAD_LEN	12
#else
#define	_TMD_PAD_LEN	24
#endif
#endif

typedef struct {
	enum {
	    ISP_THREAD_NIL=1,
	    ISP_THREAD_FC_RESCAN,
	    ISP_THREAD_REINIT,
	    ISP_THREAD_FW_CRASH_DUMP,
	    ISP_THREAD_EXIT
	}			thread_action;
	struct semaphore *	thread_waiter;
} isp_thread_action_t;
#define	MAX_THREAD_ACTION	10

union pstore;
struct isposinfo {
    struct ispsoftc *		isp_next;
    struct Scsi_Host *		host;
    Scsi_Cmnd			*wqnext, *wqtail;
    Scsi_Cmnd			*dqnext, *dqtail;
    union pstore		*storep;
    char			hbaname[16];
    unsigned short		instance;
    unsigned short		wqcnt;
    unsigned short		wqhiwater;
    unsigned short		hiwater;
    struct timer_list		timer;
    struct timer_list		_mbtimer;
    struct semaphore		_mbox_sem;
    struct semaphore		_mbox_c_sem;
    struct semaphore		_fcs_sem;
    spinlock_t			slock;
    unsigned volatile int
		_downcnt	: 8,
				: 16,
				: 1,
		_deadloop	: 1,
		_blocked	: 1,
		_fcrswdog	: 1,
		_fcrspend	: 1,
    		_dogactive	: 1,
		_mbox_waiting	: 1,
    		_mbintsok	: 1;
    void *			misc[8]; /* private platform variant usage */
    unsigned long		_iflags;
    struct task_struct *	task_thread;
    struct semaphore *		task_request;
    struct semaphore *		task_ctl_sem;
    spinlock_t			tlock;
    unsigned int		nt_actions;
    unsigned int		device_id;
    isp_thread_action_t		t_actions[MAX_THREAD_ACTION];
#ifdef	LINUX_ISP_TARGET_MODE
#define	TM_WANTED		0x08
#define	TM_BUSY			0x04
#define	TM_TMODE_ENABLED	0x03
    u_int32_t		rollinfo	: 16,
    			rstatus		: 8,
					: 4,
			tmflags		: 4;
    struct semaphore		tgt_inisem;
    struct semaphore *		rsemap;
   /*
    * This is very inefficient, but is in fact big enough
    * to cover a complete bitmap for Fibre Channel, as well
    * as the dual bus SCSI cards. This works out without
    * overflow easily because the most you can enable
    * for the SCSI cards is 64 luns (x 2 busses).
    *
    * For Fibre Channel, we can run the max luns up to 65536,
    * but we'll default to the minimum we can support here.
    */
#define	TM_MAX_LUN_FC		128
#define	TM_MAX_LUN_SCSI		64
    u_int32_t			lunbmap[TM_MAX_LUN_FC >> 5];
    struct tmd_cmd *		pending_t;
    struct tmd_cmd *		tfreelist;
    struct tmd_cmd *		pool;
    void			(*hcb)(int, void *);
    void			*hcb_token;
#endif
};
#define	mbtimer		isp_osinfo._mbtimer
#define	dogactive	isp_osinfo._dogactive
#define	mbox_sem	isp_osinfo._mbox_sem
#define	mbox_c_sem	isp_osinfo._mbox_c_sem
#define	fcs_sem		isp_osinfo._fcs_sem
#define	mbintsok	isp_osinfo._mbintsok
#define	mbox_waiting	isp_osinfo._mbox_waiting
#define	isp_pbuf	isp_osinfo._pbuf
#define	isp_fcrspend	isp_osinfo._fcrspend
#define	isp_fcrswdog	isp_osinfo._fcrswdog
#define	isp_blocked	isp_osinfo._blocked
#define	isp_downcnt	isp_osinfo._downcnt
#define	isp_deadloop	isp_osinfo._deadloop

#define	iflags		isp_osinfo._iflags

#define	SEND_THREAD_EVENT(isp, action, dowait)				\
if (isp->isp_osinfo.task_request) {					\
    unsigned long flags;						\
    spin_lock_irqsave(&isp->isp_osinfo.tlock, flags);			\
    if (isp->isp_osinfo.nt_actions >= MAX_THREAD_ACTION) {		\
	spin_unlock_irqrestore(&isp->isp_osinfo.tlock, flags);		\
	isp_prt(isp, ISP_LOGERR, "thread event overflow");		\
    } else if (action == ISP_THREAD_FC_RESCAN && isp->isp_fcrspend) {	\
	spin_unlock_irqrestore(&isp->isp_osinfo.tlock, flags);		\
    } else {								\
	DECLARE_MUTEX_LOCKED(sem);					\
	isp_thread_action_t *tap;					\
	tap = &isp->isp_osinfo.t_actions[isp->isp_osinfo.nt_actions++];	\
	tap->thread_action = action;					\
	if (dowait)							\
	    tap->thread_waiter = &sem;					\
	else								\
	    tap->thread_waiter = 0;					\
	if (action == ISP_THREAD_FC_RESCAN)				\
	    isp->isp_fcrspend = 1;					\
	up(isp->isp_osinfo.task_request);				\
	spin_unlock_irqrestore(&isp->isp_osinfo.tlock, flags);		\
	if (dowait) {							\
	    down(&sem);							\
	    isp_prt(isp, ISP_LOGDEBUG1,					\
		"action %d done from %p", action, &sem);		\
	} else {							\
	    isp_prt(isp, ISP_LOGDEBUG1,					\
		"action %d sent", action);				\
	}								\
    }									\
}

/*
 * Locking macros...
 */
#define	ISP_LOCK_INIT(isp)		spin_lock_init(&isp->isp_osinfo.slock)
#define	ISP_LOCK_SOFTC(isp)		{				\
		unsigned long _flags;					\
		spin_lock_irqsave(&isp->isp_osinfo.slock, _flags);	\
		isp->iflags = _flags;					\
	}
#define	ISP_UNLK_SOFTC(isp)		{				\
		unsigned long _flags = isp->iflags;			\
		spin_unlock_irqrestore(&isp->isp_osinfo.slock, _flags);	\
	}

#define	ISP_ILOCK_SOFTC			ISP_LOCK_SOFTC
#define	ISP_IUNLK_SOFTC			ISP_UNLK_SOFTC
#define	ISP_IGET_LK_SOFTC		ISP_LOCK_SOFTC
#define	ISP_DROP_LK_SOFTC		ISP_UNLK_SOFTC
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define	ISP_LOCK_SCSI_DONE(isp)		{				\
		unsigned long _flags;					\
		spin_lock_irqsave(&io_request_lock, _flags);		\
		isp->iflags = _flags;					\
	}
#define	ISP_UNLK_SCSI_DONE(isp)		{				\
		unsigned long _flags = isp->iflags;			\
		spin_unlock_irqrestore(&io_request_lock, _flags);	\
	}
#else
#define	ISP_LOCK_SCSI_DONE(isp)		do { } while(0)
#define	ISP_UNLK_SCSI_DONE(isp)		do { } while(0)
#endif
#define	ISP_LOCKU_SOFTC			ISP_ILOCK_SOFTC
#define	ISP_UNLKU_SOFTC			ISP_IUNLK_SOFTC
#define	ISP_TLOCK_INIT(isp)		spin_lock_init(&isp->isp_osinfo.tlock)
#define	ISP_DRIVER_ENTRY_LOCK(isp)	spin_unlock_irq(&io_request_lock)
#define	ISP_DRIVER_EXIT_LOCK(isp)	spin_lock_irq(&io_request_lock)

#define	ISP_MUST_POLL(isp)	(in_interrupt() || isp->mbintsok == 0)
/*
 * Misc SCSI defines
 */
#define	MSG_SIMPLE_Q_TAG	0x21
#define	MSG_HEAD_OF_Q_TAG	0x22
#define	MSG_ORDERED_Q_TAG	0x23

/*
 * Required Macros/Defines
 */

#define	INLINE			__inline

#define	ISP2100_SCRLEN		0x800

#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define	MEMZERO			_isp_memzero
#define	MEMCPY			_isp_memcpy
#else
#define	MEMZERO(b, a)		memset(b, 0, a)
#define	MEMCPY			memcpy
#endif
#define	SNPRINTF		isp_snprintf
#define	STRNCAT			strncat
#define	USEC_DELAY		_isp_usec_delay
#define	USEC_SLEEP(isp, x)						\
		ISP_DROP_LK_SOFTC(isp);					\
		__set_current_state(TASK_UNINTERRUPTIBLE);		\
		(void) schedule_timeout(((x? x: 1) + (HZ - 1)) / HZ);	\
		ISP_IGET_LK_SOFTC(isp)

#define	NANOTIME_T		struct timeval
/* for prior to 2.2.19, use do_gettimeofday, and, well, it'll be inaccurate */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,18)
#define	GET_NANOTIME(ptr)	\
	(ptr)->tv_sec = 0, (ptr)->tv_usec = 0, get_fast_time(ptr)
#else
#define	GET_NANOTIME(ptr)	\
	(ptr)->tv_sec = 0, (ptr)->tv_usec = 0, do_gettimeofday(ptr)
#endif
#define	GET_NANOSEC(x)		\
  ((u_int64_t) ((((u_int64_t)(x)->tv_sec) * 1000000 + (x)->tv_usec)))
#define	NANOTIME_SUB		_isp_microtime_sub

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define	MAXISPREQUEST(isp)	256
#else
#define	MAXISPREQUEST(isp)	((IS_FC(isp) || IS_ULTRA2(isp))? 1024 : 256)
#endif

#if	defined(__i386__)
#define	MEMORYBARRIER(isp, type, offset, size)	barrier()
#elif	defined(__alpha__)
#define	MEMORYBARRIER(isp, type, offset, size)	mb()
#elif	defined(__sparc__)
#define	MEMORYBARRIER(isp, type, offset, size)	mb()
#elif	defined(__powerpc__)
#define	MEMORYBARRIER(isp, type, offset, size)	\
	__asm__ __volatile__("eieio" ::: "memory")
#else
#  ifdef mb
#    define	MEMORYBARRIER(isp, type, offset, size)	mb()
#  else
#    define	MEMORYBARRIER(isp, type, offset, size)	barrier()
#  endif
#endif

#define	MBOX_ACQUIRE(isp)						\
	/*								\
	 * Try and acquire semaphore the easy way first-		\
	 * with our lock already held.					\
	 */								\
	if (down_trylock(&isp->mbox_sem)) {				\
	    if (in_interrupt()) {					\
		mbp->param[0] = MBOX_HOST_INTERFACE_ERROR;		\
		isp_prt(isp, ISP_LOGERR, "cannot acquire MBOX sema");	\
		return;							\
	    }								\
	    ISP_DROP_LK_SOFTC(isp);					\
	    down(&isp->mbox_sem);					\
	    ISP_IGET_LK_SOFTC(isp);					\
	}

#define	MBOX_WAIT_COMPLETE(isp)						\
	if (ISP_MUST_POLL(isp)) {					\
		int j, lim = 5000000;					\
		if (isp->isp_mbxwrk0) {					\
			lim *= 12;					\
		}							\
		for (j = 0; j < 5000000; j += 100) {			\
			u_int16_t isr, sema, mbox;			\
			if (isp->isp_mboxbsy == 0) {			\
				break;					\
			}						\
			if (ISP_READ_ISR(isp, &isr, &sema, &mbox)) {	\
				isp_intr(isp, isr, sema, mbox);		\
				if (isp->isp_mboxbsy == 0) {		\
					break;				\
				}					\
			}						\
			ISP_DROP_LK_SOFTC(isp);				\
			udelay(100);					\
			ISP_IGET_LK_SOFTC(isp);				\
		}							\
		if (isp->isp_mboxbsy != 0) {				\
			isp_prt(isp, ISP_LOGWARN,			\
			    "Polled Mailbox Command (0x%x) Timeout",	\
			    isp->isp_lastmbxcmd);			\
			isp->isp_mboxbsy = 0;				\
		}							\
	} else {							\
		int lim = (isp->isp_mbxwrk0)? 60 : 5;			\
		init_timer(&isp->mbtimer);				\
		isp->mbtimer.data = (unsigned long) isp;		\
		isp->mbtimer.function = isplinux_mbtimer;		\
    		isp->mbtimer.expires = jiffies + (lim * HZ);		\
    		add_timer(&isp->mbtimer);				\
		isp->mbox_waiting = 1;					\
		ISP_DROP_LK_SOFTC(isp);					\
		down(&isp->mbox_c_sem);					\
		ISP_IGET_LK_SOFTC(isp);					\
		isp->mbox_waiting = 0;					\
    		del_timer(&isp->mbtimer);				\
		if (isp->isp_mboxbsy != 0) {				\
			isp_prt(isp, ISP_LOGWARN,			\
			    "Interrupting Mailbox Command (0x%x) Timeout",\
			    isp->isp_lastmbxcmd);			\
			isp->isp_mboxbsy = 0;				\
		}							\
	}

#define	MBOX_NOTIFY_COMPLETE(isp)					\
	if (isp->mbox_waiting) {					\
		isp->mbox_waiting = 0;					\
		up(&isp->mbox_c_sem);					\
	}								\
	isp->isp_mboxbsy = 0

#define	MBOX_RELEASE(isp)	up(&isp->mbox_sem)

#define	FC_SCRATCH_ACQUIRE(isp)						\
	/*								\
	 * Try and acquire semaphore the easy way first-		\
	 * with our lock already held.					\
	 */								\
	if (in_interrupt()) {						\
		while (down_trylock(&isp->fcs_sem)) {			\
			ISP_DROP_LK_SOFTC(isp);				\
			USEC_DELAY(5000);				\
			ISP_IGET_LK_SOFTC(isp);				\
		}							\
	} else {							\
	    ISP_DROP_LK_SOFTC(isp);					\
	    down(&isp->fcs_sem);					\
	    ISP_IGET_LK_SOFTC(isp);					\
	}

#define	FC_SCRATCH_RELEASE(isp)	up(&isp->fcs_sem)


#ifndef	SCSI_GOOD
#define	SCSI_GOOD	0x0
#endif
#ifndef	SCSI_CHECK
#define	SCSI_CHECK	0x2
#endif
#ifndef	SCSI_BUSY
#define	SCSI_BUSY	0x8
#endif
#ifndef	SCSI_QFULL
#define	SCSI_QFULL	0x28
#endif

#define	XS_T			Scsi_Cmnd
#define	XS_ISP(Cmnd)		((struct ispsoftc *) (Cmnd)->host->hostdata)
#define	XS_CHANNEL(Cmnd)	(Cmnd)->channel
#define	XS_TGT(Cmnd)		(Cmnd)->target
#define	XS_LUN(Cmnd)		(Cmnd)->lun
#define	XS_CDBP(Cmnd)		(Cmnd)->cmnd
#define	XS_CDBLEN(Cmnd)		(Cmnd)->cmd_len
#define	XS_XFRLEN(Cmnd)		(Cmnd)->request_bufflen
#define	XS_TIME(Cmnd)		(Cmnd)->timeout
#define	XS_RESID(Cmnd)		(Cmnd)->SCp.this_residual
#define	XS_STSP(Cmnd)		(&(Cmnd)->SCp.Status)
#define	XS_SNSP(Cmnd)		(Cmnd)->sense_buffer
#define	XS_SNSLEN(Cmnd)		(sizeof (Cmnd)->sense_buffer)
#define	XS_SNSKEY(Cmnd)		((Cmnd)->sense_buffer[2] & 0xf)
#define	XS_TAG_P(Cmnd)		(Cmnd->device->tagged_supported != 0)
#define	XS_TAG_TYPE(xs)		REQFLAG_STAG

#define	XS_SETERR(xs, v)	\
	if ((v) == HBA_TGTBSY) { \
		(xs)->SCp.Status = SCSI_BUSY; \
	} else { \
		(xs)->result &= ~0xff0000; \
		(xs)->result |= ((v) << 16); \
	}

#	define	HBA_NOERROR		DID_OK
#	define	HBA_BOTCH		DID_ERROR
#	define	HBA_CMDTIMEOUT		DID_TIME_OUT
#	define	HBA_SELTIMEOUT		DID_NO_CONNECT
#	define	HBA_TGTBSY		123456 /* special handling */
#	define	HBA_BUSRESET		DID_RESET
#	define	HBA_ABORTED		DID_ABORT
#	define	HBA_DATAOVR		DID_ERROR
#	define	HBA_ARQFAIL		DID_ERROR

#define	XS_ERR(xs)		host_byte((xs)->result)

#define	XS_NOERR(xs)		host_byte((xs)->result) == DID_OK

#define	XS_INITERR(xs)		(xs)->result = 0, (xs)->SCp.Status = 0

#define	XS_SAVE_SENSE(Cmnd, sp)				\
	MEMCPY(&Cmnd->sense_buffer, sp->req_sense_data, \
	    min(sizeof Cmnd->sense_buffer, sp->req_sense_len))

#define	XS_SET_STATE_STAT(a, b, c)

#define	DEFAULT_IID(x)		7
#define	DEFAULT_LOOPID(x)	111
#define	DEFAULT_NODEWWN(isp)	(isp)->isp_defwwnn
#define	DEFAULT_PORTWWN(isp)	(isp)->isp_defwwpn
#define	ISP_NODEWWN(isp)	(isp)->isp_nvramwwnn
#define	ISP_PORTWWN(isp)	(isp)->isp_nvramwwpn

#define	ISP_IOXPUT_8(isp, s, d)		*(d) = s
#define	ISP_IOXPUT_16(isp, s, d)	*(d) = cpu_to_le16(s)
#define	ISP_IOXPUT_32(isp, s, d)	*(d) = cpu_to_le32(s)

#define	ISP_IOXGET_8(isp, s, d)		d = *(s)
#define	ISP_IOXGET_16(isp, s, d)	d = le16_to_cpu(*((u_int16_t *)s))
#define	ISP_IOXGET_32(isp, s, d)	d = le32_to_cpu(*((u_int32_t *)s))

#define	ISP_SWIZZLE_NVRAM_WORD(isp, rp)	*rp = le16_to_cpu(*rp)


/*
 * Includes of common header files
 */
#include "ispreg.h"
#include "ispvar.h"
#include "ispmbox.h"

/*
 * isp_osinfo definitions, extensions and shorthand.
 */

/*
 * Parameter storage. The order of tags is important- sdparam && fcp
 * must come first because isp->isp_params is set to point there...
 */
union pstore {
    struct {
	sdparam _sdp[2];	/* they need to be sequential */
	u_char psc_opts[2][MAX_TARGETS];
	u_char dutydone;
    } parallel_scsi;
    struct {
	fcparam fcp;
	u_int64_t wwnn;
	u_int64_t wwpn;
	u_int64_t nvram_wwnn;
	u_int64_t nvram_wwpn;
    } fibre_scsi;
};
#define	isp_next	isp_osinfo.isp_next
#define	isp_name	isp_osinfo.hbaname
#define	isp_host	isp_osinfo.host
#define	isp_unit	isp_osinfo.instance
#define	isp_psco	isp_osinfo.storep->parallel_scsi.psc_opts
#define	isp_dutydone	isp_osinfo.storep->parallel_scsi.dutydone
#define	isp_defwwnn	isp_osinfo.storep->fibre_scsi.wwnn
#define	isp_defwwpn	isp_osinfo.storep->fibre_scsi.wwpn
#define	isp_nvramwwnn	isp_osinfo.storep->fibre_scsi.nvram_wwnn
#define	isp_nvramwwpn	isp_osinfo.storep->fibre_scsi.nvram_wwpn

/*
 * Driver prototypes..
 */
void isplinux_timer(unsigned long);
void isplinux_mbtimer(unsigned long);
void isplinux_intr(int, void *, struct pt_regs *);
void isplinux_common_init(struct ispsoftc *);
void isplinux_reinit(struct ispsoftc *);
void isplinux_sqd(struct Scsi_Host *, Scsi_Device *);

int isp_drain_reset(struct ispsoftc *, char *);
int isp_drain(struct ispsoftc *, char *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
static inline void _isp_memcpy(void *, void *, size_t);
static inline void _isp_memzero(void *,  size_t);
#endif
static inline u_int64_t _isp_microtime_sub(struct timeval *, struct timeval *);
static inline void _isp_usec_delay(unsigned int);

int isplinux_proc_info(char *, char **, off_t, int, int, int);
int isplinux_detect(Scsi_Host_Template *);
#ifdef	MODULE
int isplinux_release(struct Scsi_Host *);
#else
#define	isplinux_release	NULL
#endif
const char *isplinux_info(struct Scsi_Host *);
int isplinux_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int isplinux_biosparam(Disk *, kdev_t, int[]);


/*
 * Driver wide data...
 */
extern int isp_debug;
extern int isp_unit_seed;
extern int isp_disable;
extern int isp_nofwreload;
extern int isp_nonvram;
extern int isp_fcduplex;
extern int isp_maxlun;
extern struct ispsoftc *isplist;

/*
 * Platform private flags
 */
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define	ISP_WATCH_TIME		HZ

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define	max(a, b)		(((a) > (b)) ? (a) : (b))
#endif
#ifndef	roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif
#ifndef	ARGSUSED
#define	ARGSUSED(x)	x = x
#endif




/*
 * Platform specific 'inline' or support functions
 */

#ifdef	__sparc__
#define	_SBSWAP(isp, b, c)						\
	if (isp->isp_bustype == ISP_BT_SBUS) {				\
		u_int8_t tmp = b;					\
		b = c;							\
		c = tmp;						\
	}
#else
#define	_SBSWAP(a, b, c)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
static inline void
_isp_memcpy(void *to, void *from, size_t amt)
{
	unsigned char *x = to; unsigned char *y = from;
	while (amt-- != 0) *x++ = *y++;
}

static inline void
_isp_memzero(void *to, size_t amt)
{
	unsigned char *x = to;
	while (amt-- != 0) *x++ = 0;
}

static inline unsigned long IspOrder(int);
static inline unsigned long
IspOrder(int nelem)
{
    unsigned long order, rsize;

    order = 0;
    rsize = PAGE_SIZE;
    while (rsize < (unsigned long) ISP_QUEUE_SIZE(nelem)) {
	order++;
	rsize <<= 1;
    }
    return (order);
}
#endif

static inline u_int64_t
_isp_microtime_sub(struct timeval *b, struct timeval *a)
{
	u_int64_t elapsed;
	struct timeval x = *b;
	x.tv_sec -= a->tv_sec;
	x.tv_usec -= a->tv_usec;
        if (x.tv_usec < 0) {
                x.tv_sec--;
                x.tv_usec += 1000000;
        }
        if (x.tv_usec >= 1000000) {
                x.tv_sec++;
                x.tv_usec -= 1000000;
        }
	elapsed = GET_NANOSEC(&x);
	if (elapsed == 0)
		elapsed++;
	if ((int64_t) elapsed < 0)	/* !!!! */
		return (1000);
	return (elapsed * 1000);
}

static inline void
_isp_usec_delay(unsigned int usecs)
{
    while (usecs > 1000) {
	mdelay(1);
	usecs -= 1000;
    }
    if (usecs)
	udelay(usecs);
}

#define	GetPages(a)	__get_dma_pages(GFP_ATOMIC|GFP_DMA, a)
#define	RlsPages(a, b)	free_pages((unsigned long) a, b)

char *isp_snprintf(char *, size_t, const char *, ...);

/*
 * Common inline functions
 */

#include "isp_inline.h"

#ifdef	ISP_TARGET_MODE
void isp_attach_target(ispsoftc_t *);
void isp_detach_target(ispsoftc_t *);
void isp_target_async(struct ispsoftc *, int, int);
int isp_target_notify(struct ispsoftc *, void *, u_int16_t *);
#endif
/*
 * Config data
 */

int isplinux_abort(Scsi_Cmnd *);
int isplinux_bdr(Scsi_Cmnd *);
int isplinux_sreset(Scsi_Cmnd *);
int isplinux_hreset(Scsi_Cmnd *);
#define QLOGICISP {							\
	next:				NULL,				\
	module:				NULL,				\
	proc_info:			isplinux_proc_info,		\
	name:				"Qlogic ISP 10X0/2X00",		\
	detect:				isplinux_detect,		\
	release:			isplinux_release,		\
	info:				isplinux_info,			\
	queuecommand:			isplinux_queuecommand,		\
	use_new_eh_code:		1,				\
	eh_abort_handler:		isplinux_abort,			\
	eh_device_reset_handler:	isplinux_bdr,			\
	eh_bus_reset_handler:		isplinux_sreset,		\
	eh_host_reset_handler:		isplinux_hreset,		\
	bios_param:			isplinux_biosparam,		\
	can_queue:			1,	   			\
	sg_tablesize:			SG_ALL,				\
	use_clustering:			ENABLE_CLUSTERING,		\
	cmd_per_lun:			1				\
}
/*
 * mode: c
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * End:
 */
#endif /* _ISP_LINUX_H */
