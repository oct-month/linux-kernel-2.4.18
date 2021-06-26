/* @(#)isp_linux.c 1.48 */
/*
 * Qlogic ISP Host Adapter Common Bus Linux routies
 *---------------------------------------
 *
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
 *
 *--------
 * Bug fixes from Janice McLaughlin (janus@somemore.com)
 * gratefully acknowledged.
 */

#define	ISP_MODULE	1
#include "isp_linux.h"
#include "linux/smp_lock.h"

#ifdef	ISP_PRIVATE_ASYNC
#define	isp_async	isp_async_level1
#endif

extern void scsi_add_timer(Scsi_Cmnd *, int, void ((*)(Scsi_Cmnd *)));
extern int scsi_delete_timer(Scsi_Cmnd *);

static int isp_task_thread(void *);
struct ispsoftc *isplist = NULL;
int isp_debug = 0;
int isp_throttle = 0;
int isp_cmd_per_lun = 0;
int isp_unit_seed = 0;
int isp_disable = 0;
int isp_nofwreload = 0;
int isp_nonvram = 0;
int isp_maxluns = 8;
int isp_fcduplex = 0;
int isp_nport_only = 0;
int isp_loop_only = 0;
int isp_deadloop_time = 30;	/* how long to wait before assume loop dead */
int isp_xtime = 0;
static char *isp_roles;
static char *isp_wwpns;
static char *isp_wwnns;

#ifdef	LINUX_ISP_TARGET_MODE
#ifndef	ISP_PARENT_TARGET
#define	ISP_PARENT_TARGET	scsi_target_handler
#endif
extern void ISP_PARENT_TARGET (qact_e, void *);
static void isp_attach_target(struct ispsoftc *);
static void isp_detach_target(struct ispsoftc *);
static void isp_taction(qact_e, void *);
static INLINE int nolunsenabled(struct ispsoftc *, int);
static void isp_target_start_ctio(struct ispsoftc *, tmd_cmd_t *);
static int isp_handle_platform_atio(struct ispsoftc *, at_entry_t *);
static int isp_handle_platform_atio2(struct ispsoftc *, at2_entry_t *);
static int isp_handle_platform_ctio(struct ispsoftc *, void *);
static void isp_target_putback_atio(struct ispsoftc *, tmd_cmd_t *);
static void isp_complete_ctio(struct ispsoftc *, tmd_cmd_t *);
static int isp_en_dis_lun(struct ispsoftc *, int, int, int, int);
#endif

#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,3,27)
struct proc_dir_entry proc_scsi_qlc = {
    PROC_SCSI_QLOGICISP, 3, "isp", S_IFDIR | S_IRUGO | S_IXUGO, 2
};
#endif
static const char *class3_roles[4] = {
    "None", "Target", "Initiator", "Target/Initiator"
};

extern int isplinux_pci_detect(Scsi_Host_Template *);
extern void isplinux_pci_release(struct Scsi_Host *);

int
isplinux_proc_info(char *buf, char **st, off_t off, int len, int host, int io)
{
#define	PBF	(&buf[size])
    int size, pos, begin, i, lim;
    struct ispsoftc *isp;

    isp = isplist;
    while (isp) {
	if (isp->isp_host->host_no == host) {
	    break;
	}
	isp = isp->isp_next;
    }
    if (isp == NULL) {
	return (-ENODEV);
    }
    if (io) {
	buf[len] = 0;
	io = -ENOSYS;
	if (strncmp(buf, "debug=", 6) == 0) {
	    unsigned long debug;
	    char *p = &buf[6], *q;
	    debug = simple_strtoul(p, &q, 16);
	    if (q == &buf[6]) {
		isp_prt(isp, ISP_LOGERR, "Garbled Debug Line '%s'", buf);
		return (-EINVAL);
	    }
	    isp_prt(isp, ISP_LOGINFO, "setting debug level to 0x%lx", debug);
	    ISP_LOCKU_SOFTC(isp);
	    isp->isp_dblev = debug;
	    ISP_UNLKU_SOFTC(isp);
	    io = len;
	} else if (strncmp(buf, "rescan", 6) == 0) {
	    if (IS_FC(isp)) {
		SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 1);
		io = len;
	    }
	} else if (strncmp(buf, "lip", 3) == 0) {
	    if (IS_FC(isp)) {
		ISP_LOCKU_SOFTC(isp);
		(void) isp_control(isp, ISPCTL_SEND_LIP, 0);
		ISP_UNLKU_SOFTC(isp);
		io = len;
	    }
	} else if (strncmp(buf, "busreset=", 9) == 0) {
	    char *p = &buf[6], *q;
	    int bus = (int) simple_strtoul(p, &q, 16);
	    if (q == &buf[6]) {
		isp_prt(isp, ISP_LOGERR, "Garbled Bus Reset Line '%s'", buf);
		return (-EINVAL);
	    }
	    ISP_LOCKU_SOFTC(isp);
	    (void) isp_control(isp, ISPCTL_RESET_BUS, &bus);
	    ISP_UNLKU_SOFTC(isp);
	    io = len;
	} else if (strncmp(buf, "devreset=", 9) == 0) {
	    char *p = &buf[6], *q;
	    int dev = (int) simple_strtoul(p, &q, 16);
	    if (q == &buf[6]) {
		isp_prt(isp, ISP_LOGERR, "Garbled Dev Reset Line '%s'", buf);
		return (-EINVAL);
	    }
	    /* always bus 0 */
	    ISP_LOCKU_SOFTC(isp);
	    (void) isp_control(isp, ISPCTL_RESET_DEV, &dev);
	    ISP_UNLKU_SOFTC(isp);
	    io = len;
	}
#ifdef	LINUX_ISP_TARGET_MODE
	/*
	 * Note that this cannot enable or disable luns on other than bus 0.
	 */
	else if (strncmp(buf, "enable_lun=", 11) == 0) {
	    unsigned long lun;
	    char *p = &buf[11], *q;
	    lun = simple_strtoul(p, &q, 10);
	    if (q == &buf[11]) {
		isp_prt(isp, ISP_LOGERR,
		    "attempted enable of invalid lun (%s)", buf);
		return (-EINVAL);
	    }
	    io = isp_en_dis_lun(isp, 1, 0, -1, (int) lun);
	    if (io >= 0)
		io = len;
	} else if (strncmp(buf, "disable_lun=", 12) == 0) {
	    unsigned long lun;
	    char *p = &buf[12], *q; 
	    lun = simple_strtoul(p, &q, 10);
	    if (q == &buf[12]) {
		isp_prt(isp, ISP_LOGERR,
		    "attempted disable of invalid lun (%s)", buf);
		return (-EINVAL);
	    }
	    io = isp_en_dis_lun(isp, 0, 0, -1, (int) lun);
	    if (io >= 0)
		io = len;
	}
#endif
#ifdef	ISP_FW_CRASH_DUMP
	else if (strncmp(buf, "fwcrash", 7) == 0) {
	    if (IS_FC(isp)) {
		ISP_LOCKU_SOFTC(isp);
		SEND_THREAD_EVENT(isp, ISP_THREAD_FW_CRASH_DUMP, 0);
		ISP_UNLKU_SOFTC(isp);
		io = len;
	    }
	}
#endif
	return (io);
    }
    ISP_LOCKU_SOFTC(isp);
    begin = size = 0;
    size += sprintf(PBF, isplinux_info(isp->isp_host));
#ifdef	HBA_VERSION
    size += sprintf(PBF, "\n HBA Version %s, built %s, %s",
	HBA_VERSION, __DATE__, __TIME__);
#endif
    size += sprintf(PBF, "\n DEVID %x role %d\n",
	isp->isp_osinfo.device_id, isp->isp_role);
    size += sprintf(PBF,
        " Interrupt Stats:\n"
	"  total=0x%08x%08x bogus=0x%08x%08x\n"
	"  MboxC=0x%08x%08x async=0x%08x%08x\n"
	"  CRslt=0x%08x%08x CPost=0x%08x%08x\n"
	"  RspnsCHiWater=0x%04x FastPostC_Hiwater=0x%04x\n",
	(u_int32_t) (isp->isp_intcnt >> 32),
	(u_int32_t) (isp->isp_intcnt & 0xffffffff),
	(u_int32_t) (isp->isp_intbogus >> 32),
	(u_int32_t) (isp->isp_intbogus & 0xffffffff),
	(u_int32_t) (isp->isp_intmboxc >> 32),
	(u_int32_t) (isp->isp_intmboxc & 0xffffffff),
	(u_int32_t) (isp->isp_intoasync >> 32),
	(u_int32_t) (isp->isp_intoasync & 0xffffffff),
	(u_int32_t) (isp->isp_rsltccmplt >> 32),
	(u_int32_t) (isp->isp_rsltccmplt & 0xffffffff),
	(u_int32_t) (isp->isp_fphccmplt >> 32),
	(u_int32_t) (isp->isp_fphccmplt & 0xffffffff),
	isp->isp_rscchiwater, isp->isp_fpcchiwater);
    size += sprintf(PBF,
	" Request In %d Request Out %d Result %d Nactv %d"
	" HiWater %u QAVAIL %d WtQHi %d\n",
	isp->isp_reqidx, isp->isp_reqodx, isp->isp_residx, isp->isp_nactive,
	isp->isp_osinfo.hiwater, ISP_QAVAIL(isp),
	isp->isp_osinfo.wqhiwater);
    for (lim = i = 0; i < isp->isp_maxcmds; i++) {
	if (isp->isp_xflist[i]) {
	    size += sprintf(PBF, " %d:%p", i, isp->isp_xflist[i]);
	    if (lim++ > 5) {
		size += sprintf(PBF, "...");
		break;
	    }
	}
    }
    size += sprintf(PBF, "\n");
    if (isp->isp_osinfo.wqnext) {
	Scsi_Cmnd *f = isp->isp_osinfo.wqnext;
	size += sprintf(PBF, "WaitQ(%d)", isp->isp_osinfo.wqcnt);
	lim = 0;
	while (f) {
	    size += sprintf(PBF, "->%p", f);
	    f = (Scsi_Cmnd *) f->host_scribble;
	    if (lim++ > 5) {
		size += sprintf(PBF, "...");
		break;
	    }
	}
	size += sprintf(PBF, "\n");
    }
    if (isp->isp_osinfo.dqnext) {
	Scsi_Cmnd *f = isp->isp_osinfo.dqnext;
	size += sprintf(PBF, "DoneQ");
	lim = 0;
	while (f) {
	    size += sprintf(PBF, "->%p", f);
	    f = (Scsi_Cmnd *) f->host_scribble;
	    if (lim++ > 5) {
		size += sprintf(PBF, "...");
		break;
	    }
	}
        size += sprintf(PBF, "\n");
    }
    if (IS_FC(isp)) {
	fcparam *fcp = isp->isp_param;
	size += sprintf(PBF,
	    "Loop ID: %d AL_PA 0x%x Port ID 0x%x FW State %x Loop State %x\n",
	    fcp->isp_loopid, fcp->isp_alpa, fcp->isp_portid, fcp->isp_fwstate,
	    fcp->isp_loopstate);
	size += sprintf(PBF, "Port WWN 0x%08x%08x Node WWN 0x%08x%08x\n",
	    (unsigned int) (ISP_PORTWWN(isp) >> 32),
	    (unsigned int) (ISP_PORTWWN(isp) & 0xffffffff),
	    (unsigned int) (ISP_NODEWWN(isp) >> 32),
	    (unsigned int) (ISP_NODEWWN(isp) & 0xffffffff));
	for (i = 0; i < MAX_FC_TARG; i++) {
	    if (fcp->portdb[i].valid == 0 && i < FL_PORT_ID)
		continue;
	    if (fcp->portdb[i].port_wwn == 0)
		continue;
	    size += sprintf(PBF, "TGT % 3d Loop ID % 3d Port id 0x%04x, role %s"
		"\n Port WWN 0x%08x%08x Node WWN 0x%08x%08x\n\n", i,
		fcp->portdb[i].loopid,
		fcp->portdb[i].portid, class3_roles[fcp->portdb[i].roles],
		(unsigned int) (fcp->portdb[i].port_wwn >> 32),
		(unsigned int) (fcp->portdb[i].port_wwn & 0xffffffff),
		(unsigned int) (fcp->portdb[i].node_wwn >> 32),
		(unsigned int) (fcp->portdb[i].node_wwn & 0xffffffff));
	}
    } else {
	sdparam *sdp = (sdparam *)isp->isp_param;

	size += sprintf(PBF, "Initiator ID: %d\n", sdp->isp_initiator_id);
	size += sprintf(PBF, "Target Flag  Period Offset\n");
	for (i = 0; i < MAX_TARGETS; i++) {
	    size += sprintf(PBF, "%6d: 0x%04x 0x%04x 0x%x\n",
		i, sdp->isp_devparam[i].actv_flags,
		sdp->isp_devparam[i].actv_offset,
		sdp->isp_devparam[i].actv_period);
	}
	if (IS_DUALBUS(isp)) {
	    sdp++;
  	    size += sprintf(PBF, "\nInitiator ID: %d, Channel B\n",
		sdp->isp_initiator_id);
	    size += sprintf(PBF,
		"Target     CurFlag    DevFlag  Period Offset B-Channel\n");
	    for (i = 0; i < MAX_TARGETS; i++) {
		    size += sprintf(PBF, "%6d: 0x%04x 0x%04x 0x%x\n",
			i, sdp->isp_devparam[i].actv_flags,
			sdp->isp_devparam[i].actv_offset,
			sdp->isp_devparam[i].actv_period);
	    }
	}
    }
    ISP_UNLKU_SOFTC(isp);
    pos = size;
    if (pos < off) {
	size = 0;
	begin = pos;
    }
    *st = buf + (off - begin);
    size -= (off - begin);
    if (size > len)
	size = len;
    return (size);
}

int
isplinux_detect(Scsi_Host_Template *tmpt)
{
    int rval;
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    tmpt->proc_name = "isp";
#else
    tmpt->proc_dir = &proc_scsi_qlc;
#endif
    ISP_DRIVER_ENTRY_LOCK(isp);
    rval = isplinux_pci_detect(tmpt);
    ISP_DRIVER_EXIT_LOCK(isp);
    return (rval);
}

#ifdef	MODULE
/* io_request_lock *not* held here */
int
isplinux_release(struct Scsi_Host *host)
{
    struct ispsoftc *isp = (struct ispsoftc *) host->hostdata;
    if (isp->isp_osinfo.task_thread) {
        SEND_THREAD_EVENT(isp, ISP_THREAD_EXIT, 1);
    }
    ISP_LOCKU_SOFTC(isp);
    isp->dogactive = 0;
    del_timer(&isp->isp_osinfo.timer);
    DISABLE_INTS(isp);
    if (isp->isp_bustype == ISP_BT_PCI) {
	isplinux_pci_release(host);
    }
    ISP_UNLKU_SOFTC(isp);
#ifdef	ISP_FW_CRASH_DUMP
    if (FCPARAM(isp)->isp_dump_data) {
	isp_prt(isp, ISP_LOGCONFIG, "freeing crash dump area");
	vfree(FCPARAM(isp)->isp_dump_data);
	FCPARAM(isp)->isp_dump_data = 0;
    }
#endif
#ifdef	ISP_TARGET_MODE
    isp_detach_target(isp);
#endif
    return (1);
}
#endif

const char *
isplinux_info(struct Scsi_Host *host)
{
    struct ispsoftc *isp = (struct ispsoftc *) host->hostdata;
    if (IS_FC(isp)) {
	static char *foo = "Driver for a Qlogic ISP 2X00 Host Adapter";
	if (isp->isp_type == ISP_HA_FC_2100)
	    foo[25] = '1';
	else if (isp->isp_type == ISP_HA_FC_2200)
	    foo[25] = '2';
	else if (isp->isp_type == ISP_HA_FC_2300)
	    foo[25] = '3';
	else if (isp->isp_type == ISP_HA_FC_2312) {
	    foo[25] = '3';
	    foo[26] = '1';
	    foo[27] = '2';
	}
	return (foo);
    } else if (IS_1240(isp)) {
	return ("Driver for a Qlogic ISP 1240 Host Adapter");
    } else if (IS_1080(isp)) {
	return ("Driver for a Qlogic ISP 1080 Host Adapter");
    } else if (IS_1280(isp)) {
	return ("Driver for a Qlogic ISP 1280 Host Adapter");
    } else if (IS_12160(isp)) {
	return ("Driver for a Qlogic ISP 12160 Host Adapter");
    } else {
	return ("Driver for a Qlogic ISP 1020/1040 Host Adapter");
    }
}

static INLINE void
isplinux_append_to_waitq(struct ispsoftc *isp, Scsi_Cmnd *Cmnd)
{
    /*
     * If we're a fibre channel card and we consider the loop to be
     * down, we just finish the command here and now.
     */
    if (IS_FC(isp) && isp->isp_deadloop) {
	XS_INITERR(Cmnd);
	XS_SETERR(Cmnd, DID_NO_CONNECT);

	/*
	 * Add back a timer else scsi_done drops this on the floor.
	 */
	scsi_add_timer(Cmnd, Cmnd->timeout_per_command, Cmnd->done);
	isp_prt(isp, ISP_LOGDEBUG0, "giving up on target %d", Cmnd->target);
	ISP_UNLK_SOFTC(isp);
	ISP_LOCK_SCSI_DONE(isp);
	(*Cmnd->scsi_done)(Cmnd);
	ISP_UNLK_SCSI_DONE(isp);
	ISP_LOCK_SOFTC(isp);
	return;
    }

    isp->isp_osinfo.wqcnt++;
    if (isp->isp_osinfo.wqhiwater < isp->isp_osinfo.wqcnt)
	isp->isp_osinfo.wqhiwater = isp->isp_osinfo.wqcnt;
    if (isp->isp_osinfo.wqnext == NULL) {
	isp->isp_osinfo.wqtail = isp->isp_osinfo.wqnext = Cmnd;
    } else {
	isp->isp_osinfo.wqtail->host_scribble = (unsigned char *) Cmnd;
	isp->isp_osinfo.wqtail = Cmnd;
    }
    Cmnd->host_scribble = NULL;


    /*
     * Stop the clock for this command.
     */
    (void) scsi_delete_timer(Cmnd);
}

static INLINE void
isplinux_insert_head_waitq(struct ispsoftc *isp, Scsi_Cmnd *Cmnd)
{
    isp->isp_osinfo.wqcnt++;
    if (isp->isp_osinfo.wqnext == NULL) {
	isp->isp_osinfo.wqtail = isp->isp_osinfo.wqnext = Cmnd;
	Cmnd->host_scribble = NULL;
    } else {
	Cmnd->host_scribble = (unsigned char *) isp->isp_osinfo.wqnext;
	isp->isp_osinfo.wqnext = Cmnd;
    }
}

static INLINE Scsi_Cmnd *
isp_remove_from_waitq(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp;
    Scsi_Cmnd *f;
    if (Cmnd == NULL)
	return (Cmnd);
    isp = (struct ispsoftc *) Cmnd->host->hostdata;
    if ((f = isp->isp_osinfo.wqnext) == Cmnd) {
	isp->isp_osinfo.wqnext = (Scsi_Cmnd *) Cmnd->host_scribble;
    } else {
	Scsi_Cmnd *b = f;
	while (f) {
	    f = (Scsi_Cmnd *) b->host_scribble;
	    if (f == Cmnd) {
		b->host_scribble = f->host_scribble;
		if (isp->isp_osinfo.wqtail == Cmnd)
		     isp->isp_osinfo.wqtail = b;
		break;
	    }
	    b = f;
	}
    }
    if (f) {
	f->host_scribble = NULL;
	isp->isp_osinfo.wqcnt -= 1;
    }
    return (f);
}

static INLINE void
isplinux_runwaitq(struct ispsoftc *isp)
{
    Scsi_Cmnd *f;
    while ((f = isp_remove_from_waitq(isp->isp_osinfo.wqnext)) != NULL) {
	int result = isp_start(f);
	/*
	 * Restart the timer for this command if it is queued or completing.
	 */
	if (result == CMD_QUEUED || result == CMD_COMPLETE) {
	    int ntime = f->timeout_per_command;
	    if (isp_xtime)
		ntime *= isp_xtime;
	    scsi_add_timer(f, ntime, f->done);
	}
	if (result == CMD_QUEUED) {
	    if (isp->isp_osinfo.hiwater < isp->isp_nactive)
		isp->isp_osinfo.hiwater = isp->isp_nactive;
	    continue;
	}

	/*
	 * If we cannot start a command on a fibre channel card, it means
	 * that loop state isn't ready for us to do so. Activate the FC
	 * thread to rediscover loop and fabric residency- but not if
	 * we consider the loop to be dead. If the loop is considered dead,
	 * we wait until a PDB Changed after a Loop UP activates the FC
	 * thread.
	 */
	if (result == CMD_RQLATER && IS_FC(isp) && isp->isp_deadloop == 0) {
	    SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 0);
	}

	/*
	 * Put the command back on the wait queue. Don't change any
	 * timer parameters for it because they were established
	 * when we originally put the command on the waitq in the first
	 * place.
	 */
	if (result == CMD_EAGAIN || result == CMD_RQLATER) {
	    isplinux_insert_head_waitq(isp, f);
	    break;
	}
	if (result == CMD_COMPLETE) {
	    isp_done(f);
	} else {
	    panic("isplinux_runwaitq: result %d", result);
	}
    }
}

static INLINE void
isplinux_flushwaitq(struct ispsoftc *isp)
{
    Scsi_Cmnd *Cmnd, *Ncmnd;
   
    if ((Cmnd = isp->isp_osinfo.wqnext) == NULL) {
	return;
    }
    isp->isp_osinfo.wqnext = isp->isp_osinfo.wqtail = NULL;
    isp->isp_osinfo.wqcnt = 0;
    ISP_UNLK_SOFTC(isp);
    do {
        Ncmnd = (Scsi_Cmnd *) Cmnd->host_scribble;
        Cmnd->host_scribble = NULL;
	XS_INITERR(Cmnd);
	XS_SETERR(Cmnd, DID_NO_CONNECT);
	/*
	 * Add back a timer else scsi_done drops this on the floor.
	 */
	scsi_add_timer(Cmnd, Cmnd->timeout_per_command, Cmnd->done);
	ISP_LOCK_SCSI_DONE(isp);
	(*Cmnd->scsi_done)(Cmnd);
	ISP_UNLK_SCSI_DONE(isp);
    } while ((Cmnd = Ncmnd) != NULL);
    ISP_LOCK_SOFTC(isp);
}

static INLINE Scsi_Cmnd *
isplinux_remove_from_doneq(Scsi_Cmnd *Cmnd)
{
    Scsi_Cmnd *f;
    struct ispsoftc *isp;

    if (Cmnd == NULL)
	return (NULL);
    isp = (struct ispsoftc *) Cmnd->host->hostdata;
    if (isp->isp_osinfo.dqnext == NULL)
	return (NULL);
    if ((f = isp->isp_osinfo.dqnext) == Cmnd) {
	isp->isp_osinfo.dqnext = (Scsi_Cmnd *) Cmnd->host_scribble;
    } else {
	Scsi_Cmnd *b = f;
	while (f) {
	    f = (Scsi_Cmnd *) b->host_scribble;
	    if (f == Cmnd) {
		b->host_scribble = f->host_scribble;
		if (isp->isp_osinfo.dqtail == Cmnd)
		     isp->isp_osinfo.dqtail = b;
		break;
	    }
	    b = f;
	}
    }
    if (f) {
	f->host_scribble = NULL;
    }
    return (f);
}

int
isplinux_queuecommand(Scsi_Cmnd *Cmnd, void (*donecmd)(Scsi_Cmnd *))
{
    struct Scsi_Host *host = Cmnd->host;
    struct ispsoftc *isp = (struct ispsoftc *) (host->hostdata);
    int result;


    Cmnd->scsi_done = donecmd;
    Cmnd->sense_buffer[0] = 0;
    if (isp_xtime) {
	Cmnd->timeout *= isp_xtime;
    }

    ISP_DRIVER_ENTRY_LOCK(isp);
    ISP_LOCK_SOFTC(isp);

    /*
     * First off, see whether we need to (re)init the HBA.
     * If we need to and fail to, pretend that this was a selection timeout.
     */
    if (isp->isp_state != ISP_RUNSTATE) {
	if (isp->isp_role != ISP_ROLE_NONE) {
	    isplinux_reinit(isp);
	}
	if (isp->isp_state != ISP_RUNSTATE) {
	    ISP_UNLK_SOFTC(isp);
	    ISP_DRIVER_EXIT_LOCK(isp);
	    XS_INITERR(Cmnd);
	    XS_SETERR(Cmnd, DID_NO_CONNECT);
	    donecmd(Cmnd);
	    return (0);
	}
    }

   /*
    * Next see if we have any stored up commands to run. If so, run them.
    * If we get back from this with commands still ready to run, put the
    * current command at the tail of waiting commands to be run later.
    */

    isplinux_runwaitq(isp);
    if (isp->isp_osinfo.wqnext) {
	isplinux_append_to_waitq(isp, Cmnd);
	ISP_UNLK_SOFTC(isp);
	ISP_DRIVER_EXIT_LOCK(isp);
	return (0);
    }

   /*
    * Finally, try and run this command.
    */

    result = isp_start(Cmnd);
    if (result == CMD_QUEUED) {
	if (isp->isp_osinfo.hiwater < isp->isp_nactive)
	    isp->isp_osinfo.hiwater = isp->isp_nactive;
	result = 0;
	if (isp_xtime) {
		scsi_delete_timer(Cmnd);
		scsi_add_timer(Cmnd, isp_xtime * Cmnd->timeout_per_command,
		    Cmnd->done);
	}
    } else if (result == CMD_EAGAIN) {
	/*
	 * We ran out of request queue space (or could not
	 * get DMA resources). Tell the upper layer to try
	 * later.
	 */
	result = 1;
    } else if (result == CMD_RQLATER) {
	/*
	 * Temporarily hold off on this one.
	 * Typically this means for fibre channel
	 * that the loop is down or we're processing
	 * some other change (e.g., fabric membership
	 * change)
	 */
	isplinux_append_to_waitq(isp, Cmnd);
	if (IS_FC(isp) && isp->isp_deadloop == 0) {
	    SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 0);
	}
	result = 0;
    } else if (result == CMD_COMPLETE) {
	result = -1;
    } else {
	panic("unknown return code %d from isp_start", result);
    }
    ISP_UNLK_SOFTC(isp);
    ISP_DRIVER_EXIT_LOCK(isp);
    if (result == -1) {
	Cmnd->result &= ~0xff;
	Cmnd->result |= Cmnd->SCp.Status;
        Cmnd->host_scribble = NULL;
	(*Cmnd->scsi_done)(Cmnd);
	result = 0;
    }
    return (result);
}

static INLINE void isplinux_scsi_probe_done(Scsi_Cmnd *);

static INLINE void
isplinux_scsi_probe_done(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp = (struct ispsoftc *) Cmnd->host->hostdata;

    /*
     * If we haven't seen this target yet, check the command result. If
     * it was an inquiry and it succeeded okay, then we can update our
     * notions about this target's capabilities.
     *
     * If the command did *not* succeed, we also update our notions about
     * this target's capabilities (pessimistically) - it's probably not there.
     * All of this so we can know when we're done so we stop wasting cycles
     * seeing whether we can enable sync mode or not.
     */

    if (isp->isp_psco[Cmnd->channel][Cmnd->target] == 0) {
	int i, b;
	caddr_t iqd;
	sdparam *sdp = (sdparam *) isp->isp_param;

	sdp += Cmnd->channel;
	if (Cmnd->cmnd[0] == 0x12 && host_byte(Cmnd->result) == DID_OK) {
	    if (Cmnd->use_sg == 0) {
		iqd = (caddr_t) Cmnd->buffer;
	    } else {
		iqd = ((struct scatterlist *) Cmnd->request_buffer)->address;
	    }
	    sdp->isp_devparam[Cmnd->target].goal_flags &=
		~(DPARM_TQING|DPARM_SYNC|DPARM_WIDE);
	    if (iqd[7] & 0x2) {
		sdp->isp_devparam[Cmnd->target].goal_flags |= DPARM_TQING;
	    }
	    if (iqd[7] & 0x10) {
		sdp->isp_devparam[Cmnd->target].goal_flags |= DPARM_SYNC;
	    }
	    if (iqd[7] & 0x20) {
		sdp->isp_devparam[Cmnd->target].goal_flags |= DPARM_WIDE;
	    }
	    sdp->isp_devparam[Cmnd->target].dev_update = 1;
	    isp->isp_psco[Cmnd->channel][Cmnd->target] = 1;
	} else if (host_byte(Cmnd->result) != DID_OK) {
	    isp->isp_psco[Cmnd->channel][Cmnd->target] = 1;
	}

	isp->isp_dutydone = 1;
	for (b = 0; b < (IS_DUALBUS(isp)?2 : 1) && isp->isp_dutydone; b++) {
	    for (i = 0; i < MAX_TARGETS; i++) {
		if (i != sdp->isp_initiator_id) {
		    if (isp->isp_psco[b][i] == 0) {
			isp->isp_dutydone = 0;
			break;
		    }
		}
	    }
	}

	/*
	 * Have we scanned all busses and all targets? You only get
	 * one chance (per reset) to see what devices on this bus have
	 * to offer.
	 */
	if (isp->isp_dutydone) {
	    for (b = 0; b < (IS_DUALBUS(isp)?2 : 1) && isp->isp_dutydone; b++) {
		for (i = 0; i < MAX_TARGETS; i++) {
		    isp->isp_psco[b][i] = 0;
		}
		isp->isp_update |= (1 << b);
	    }
	}	    
    }
}

void
isp_done(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp = (struct ispsoftc *) (Cmnd->host->hostdata);

    if (IS_SCSI(isp) && isp->isp_dutydone == 0)  {
	isplinux_scsi_probe_done(Cmnd);
    }

    Cmnd->result &= ~0xff;
    Cmnd->result |= Cmnd->SCp.Status;

    if (Cmnd->SCp.Status != GOOD) {
	isp_prt(isp, ISP_LOGDEBUG0, "%d.%d.%d: cmd finishes with status 0x%x",
	    XS_CHANNEL(Cmnd), XS_TGT(Cmnd), XS_LUN(Cmnd), Cmnd->SCp.Status);
    }

    /*
     * If we had a way handling residuals, this is where we'd do it
     */

    /*
     * Queue command on completion queue.
     */
    if (isp->isp_osinfo.dqnext == NULL) {
	isp->isp_osinfo.dqnext = Cmnd;
    } else {
	isp->isp_osinfo.dqtail->host_scribble = (unsigned char *) Cmnd;
    }
    isp->isp_osinfo.dqtail = Cmnd;
    Cmnd->host_scribble = NULL;
}

/*
 * Error handling routines
 */

int
isplinux_abort(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp;
    u_int16_t handle;

    if (Cmnd == NULL || Cmnd->host == NULL) {
	return (FAILED);
    }

    isp = (struct ispsoftc *) Cmnd->host->hostdata;
    if (Cmnd->serial_number != Cmnd->serial_number_at_timeout) {
	isp_prt(isp, ISP_LOGWARN, "isplinux_abort: serial number mismatch");
	return (FAILED);
    }
    ISP_DRIVER_ENTRY_LOCK(isp);
    ISP_LOCKU_SOFTC(isp);
    handle = isp_find_handle(isp, Cmnd);
    if (handle == 0) {
	int wqfnd = 0;
	Scsi_Cmnd *NewCmnd = isp_remove_from_waitq(Cmnd);
	if (NewCmnd == NULL) {
		NewCmnd = isplinux_remove_from_doneq(Cmnd);
		wqfnd++;
	}
	ISP_UNLKU_SOFTC(isp);
	isp_prt(isp, ISP_LOGINFO,
	    "isplinux_abort: found %d:%p for non-running cmd for %d.%d.%d",
	    wqfnd, NewCmnd, XS_CHANNEL(Cmnd), XS_TGT(Cmnd), XS_LUN(Cmnd));
	if (NewCmnd == NULL) {
	    ISP_DRIVER_EXIT_LOCK(isp);
	    return (FAILED);
	}
    } else {
	if (isp_control(isp, ISPCTL_ABORT_CMD, Cmnd)) {
	    ISP_UNLKU_SOFTC(isp);
	    ISP_DRIVER_EXIT_LOCK(isp);
	    return (FAILED);
	}
	if (isp->isp_nactive > 0)
	    isp->isp_nactive--;
	isp_destroy_handle(isp, handle);
	ISP_UNLKU_SOFTC(isp);
	ISP_DRIVER_EXIT_LOCK(isp);
	isp_prt(isp, ISP_LOGINFO,
	    "isplinux_abort: aborted running cmd (handle 0x%x) for %d.%d.%d",
	    handle, XS_CHANNEL(Cmnd), XS_TGT(Cmnd), XS_LUN(Cmnd));
    }
    Cmnd->result = DID_ABORT << 16;
    (*Cmnd->scsi_done)(Cmnd);
    return (SUCCESS);
}

/*
 * XXX: What does the midlayer expect for commands in process?
 * XXX: Are we supposed to clean up dead commands ourselves?
 */
int
isplinux_bdr(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp;
    int arg;

    if (Cmnd == NULL || Cmnd->host == NULL) {
	return (FAILED);
    }

    isp = (struct ispsoftc *) Cmnd->host->hostdata;
    arg = Cmnd->channel << 16 | Cmnd->target;
    ISP_DRIVER_ENTRY_LOCK(isp);
    ISP_LOCKU_SOFTC(isp);
    arg = isp_control(isp, ISPCTL_RESET_DEV, &arg);
    ISP_UNLKU_SOFTC(isp);
    ISP_DRIVER_EXIT_LOCK(isp);
    isp_prt(isp, ISP_LOGINFO, "Bus Device Reset %succesfully sent to %d.%d.%d",
	arg == 0? "s" : "uns", Cmnd->channel, Cmnd->target, Cmnd->lun);
    return ((arg == 0)? SUCCESS : FAILED);
}

/*
 * XXX: What does the midlayer expect for commands in process?
 */
int
isplinux_sreset(Scsi_Cmnd *Cmnd)
{
    struct ispsoftc *isp;
    int arg;

    if (Cmnd == NULL || Cmnd->host == NULL)
	return (FAILED);

    isp = (struct ispsoftc *) Cmnd->host->hostdata;
    arg = Cmnd->channel;
    ISP_DRIVER_ENTRY_LOCK(isp);
    ISP_LOCKU_SOFTC(isp);
    arg = isp_control(isp, ISPCTL_RESET_BUS, &arg);
    ISP_UNLKU_SOFTC(isp);
    ISP_DRIVER_EXIT_LOCK(isp);
    isp_prt(isp, ISP_LOGINFO, "SCSI Bus Reset on Channel %d %succesful",
	Cmnd->channel, arg == 0? "s" : "uns");
    return ((arg == 0)? SUCCESS : FAILED);
}

/*
 * We call completion on any commands owned here-
 * except the one we were called with.
 */
int
isplinux_hreset(Scsi_Cmnd *Cmnd)
{
    Scsi_Cmnd *tmp, *dq, *wq, *xqf, *xql;
    struct ispsoftc *isp;
    u_int16_t handle;

    if (Cmnd == NULL || Cmnd->host == NULL)
	return (FAILED);

    isp = (struct ispsoftc *) Cmnd->host->hostdata;

    isp_prt(isp, ISP_LOGINFO, "Resetting Host Adapter");

    ISP_DRIVER_ENTRY_LOCK(isp);
    ISP_LOCKU_SOFTC(isp);

    /*
     * Save pending, running, and completed commands.
     */
    xql = xqf = NULL;
    for (handle = 1; handle <= isp->isp_maxcmds; handle++) {
	tmp = isp_find_xs(isp, handle);
	if (tmp == NULL) {
	    continue;
        }
	isp_destroy_handle(isp, handle);
	tmp->host_scribble = NULL;
	if (xqf) {
	    xql->host_scribble = (unsigned char *) tmp;
	} else {
	    xqf = xql = tmp;
	}
        xql = tmp;
    }
    dq = isp->isp_osinfo.dqnext;
    isp->isp_osinfo.dqnext = NULL;
    wq = isp->isp_osinfo.wqnext;
    isp->isp_osinfo.wqnext = NULL;
    isp->isp_nactive = 0;

    isplinux_reinit(isp);

    ISP_UNLKU_SOFTC(isp);
    ISP_DRIVER_EXIT_LOCK(isp);

    /*
     * Call completion on the detritus, skipping the one we were called with.
     */
    while ((tmp = xqf) != NULL) {
	xqf = (Scsi_Cmnd *) tmp->host_scribble;
	tmp->host_scribble = NULL;
	if (tmp == Cmnd)
	    continue;
	tmp->result = DID_RESET << 16;
	/*
	 * Get around silliness in midlayer.
	 */
	tmp->flags |= IS_RESETTING;
	if (tmp->scsi_done)
	    (*tmp->scsi_done)(tmp);
    }
    while ((tmp = wq) != NULL) {
	wq = (Scsi_Cmnd *) tmp->host_scribble;
	tmp->host_scribble = NULL;
	if (tmp == Cmnd)
	    continue;
	tmp->result = DID_RESET << 16;
	/*
	 * Get around silliness in midlayer.
	 */
	tmp->flags |= IS_RESETTING;
	if (tmp->scsi_done)
	    (*tmp->scsi_done)(tmp);
    }
    while ((tmp = dq) != NULL) {
	dq = (Scsi_Cmnd *) tmp->host_scribble;
	tmp->host_scribble = NULL;
	if (tmp == Cmnd)
	    continue;
	tmp->result = DID_RESET << 16;
	/*
	 * Get around silliness in midlayer.
	 */
	tmp->flags |= IS_RESETTING;
	if (tmp->scsi_done)
	    (*tmp->scsi_done)(tmp);
    }
    Cmnd->result = DID_RESET << 16;
    return (SUCCESS);
}


#ifdef	LINUX_ISP_TARGET_MODE
static void
isp_attach_target(struct ispsoftc *isp)
{
    int i;
    hba_register_t hba;

    hba.r_identity = isp;
    sprintf(hba.r_name, "isp");
    hba.r_inst = isp->isp_unit;
    if (IS_DUALBUS(isp))
	hba.r_buswidth = 2;
    else
	hba.r_buswidth = 1;
    hba.r_lunwidth = IS_FC(isp)? TM_MAX_LUN_FC : TM_MAX_LUN_SCSI;
    for (i = 0; i < NTGT_CMDS-1; i++) {
	isp->isp_osinfo.pool[i].cd_private = &isp->isp_osinfo.pool[i+1];
    }
    isp->isp_osinfo.pending_t = NULL;
    isp->isp_osinfo.tfreelist = isp->isp_osinfo.pool;
    hba.r_action = (void (*)(int, void *))isp_taction;
    ISP_UNLKU_SOFTC(isp);
    ISP_PARENT_TARGET (QOUT_HBA_REG, &hba);
    ISP_LOCKU_SOFTC(isp);
}

static void
isp_detach_target(struct ispsoftc *isp)
{
    tmd_cmd_t tmd;
    tmd.cd_hba = isp;
    tmd.cd_private = isp->isp_osinfo.hcb_token;
    ISP_PARENT_TARGET (QOUT_HBA_UNREG, &tmd);
}

static void
isp_taction(qact_e action, void *arg)
{
    tmd_cmd_t *tmd = arg;
    struct ispsoftc *isp = (tmd != NULL)? tmd->cd_hba : NULL;

    switch (action) {
    case QIN_HBA_REG:
    {
	hba_register_t *hp = tmd->cd_data;
	isp_prt(isp, ISP_LOGINFO, "completed target registration");
	isp->isp_osinfo.hcb = hp->r_action;
	isp->isp_osinfo.hcb_token = hp->r_identity;
	break;
    }

    case QIN_ENABLE:
    case QIN_DISABLE:
    {
	int r, bus, lun, tgt;

	bus = (int) tmd->cd_bus;
	lun = (int) tmd->cd_lun;
	tgt = (int) tmd->cd_tgt;
	r = isp_en_dis_lun(isp, (action == QIN_ENABLE)? 1 : 0, bus, tgt, lun);
	if (r) {
	    tmd->cd_error = r;
	    tmd->cd_lflags |= CDFL_ERROR;
	}
	break;
    }

    case QIN_TMD_CONT:
	isp_target_start_ctio(isp, tmd);
	break;

    case QIN_TMD_FIN:
	MEMZERO(tmd, sizeof (tmd_cmd_t));
	ISP_LOCK_SOFTC(isp);
	tmd->cd_private = isp->isp_osinfo.tfreelist;
	isp->isp_osinfo.tfreelist = tmd;
	ISP_UNLK_SOFTC(isp);
	break;

    case QIN_HBA_UNREG:
	isp->isp_osinfo.hcb = NULL;
	break;

    default:
	break;
   }
}

static INLINE int
nolunsenabled(struct ispsoftc *isp, int port)
{
    int i, wbase, wend;

    if (IS_FC(isp)) {
	wbase = 0;
	wend = TM_MAX_LUN_FC >> 5;
    } else {
	if (port) {
		wend = TM_MAX_LUN_FC >> 5;
		wbase = wend >> 1;
	} else {
		wend = (TM_MAX_LUN_FC >> 5) >> 1;
		wbase = 0;
	}
    }
    for (i = wbase; i < wend; i++) {
	if (isp->isp_osinfo.lunbmap[i]) {
	    return (0);
	}
    }
    return (1);
}



static void
isp_target_start_ctio(struct ispsoftc *isp, tmd_cmd_t *tmd)
{
    void *qe;
    u_int16_t *hp, save_handle;
    u_int32_t *rp;
    u_int16_t nxti, optr;
    u_int8_t local[QENTRY_LEN];

    /*
     * If the transfer length is zero, we have to be sending status.
     * If we're sending data, we have to have one and only one data
     * direction set.
     */
    if (tmd->cd_xfrlen == 0) {
	if ((tmd->cd_hflags & CDFH_STSVALID) == 0) {
	    isp_prt(isp, ISP_LOGERR, "CTIO, no data, and no status is wrong");
	    tmd->cd_error = -EINVAL;
	    tmd->cd_lflags |= CDFL_ERROR;
	    return;
	}
    } else {
	if ((tmd->cd_hflags & CDFH_DATA_MASK) == 0) {
	    isp_prt(isp, ISP_LOGERR, "data CTIO with no direction is wrong");
	    tmd->cd_error = -EINVAL;
	    tmd->cd_lflags |= CDFL_ERROR;
	    return;
	}
	if ((tmd->cd_hflags & CDFH_DATA_MASK) == CDFH_DATA_MASK) {
	    isp_prt(isp, ISP_LOGERR, "data CTIO with both directions is wrong");
	    tmd->cd_error = -EINVAL;
	    tmd->cd_lflags |= CDFL_ERROR;
	    return;
	}
    }
    tmd->cd_lflags &= ~CDFL_ERROR;
    MEMZERO(local, QENTRY_LEN);

    ISP_LOCK_SOFTC(isp);
    if (isp_getrqentry(isp, &nxti, &optr, &qe)) {
	isp_prt(isp, ISP_LOGWARN,
	    "isp_target_start_ctio: request queue overflow");
	tmd->cd_error = -ENOMEM;
	tmd->cd_lflags |= CDFL_ERROR;
	ISP_UNLK_SOFTC(isp);
	return;
    }

    /*
     * We're either moving data or completing a command here (or both).
     */

    if (IS_FC(isp)) {
	ct2_entry_t *cto = (ct2_entry_t *) local;
	u_int16_t *ssptr = NULL;

	cto->ct_header.rqs_entry_type = RQSTYPE_CTIO2;
	cto->ct_header.rqs_entry_count = 1;
	cto->ct_iid = tmd->cd_iid;
	if ((FCPARAM(isp)->isp_fwattr & ISP_FW_ATTR_SCCLUN) == 0)
		cto->ct_lun = tmd->cd_lun;
	else
		cto->ct_lun = 0;
	cto->ct_rxid = tmd->cd_tagval;
	if (cto->ct_rxid == 0) {
	    isp_prt(isp, ISP_LOGERR, "a tagval of zero is not acceptable");
	    tmd->cd_error = -EINVAL;
	    tmd->cd_lflags |= CDFL_ERROR;
	    ISP_UNLK_SOFTC(isp);
	    return;
	}
	cto->ct_flags = 0;

	if (tmd->cd_xfrlen == 0) {
	    cto->ct_flags |= CT2_FLAG_MODE1 | CT2_NO_DATA |
		CT2_SENDSTATUS | CT2_CCINCR;
	    ssptr = &cto->rsp.m1.ct_scsi_status;
	    *ssptr = tmd->cd_scsi_status;
	    if ((tmd->cd_hflags & CDFH_SNSVALID) != 0) {
		MEMCPY(cto->rsp.m1.ct_resp, tmd->cd_sense, QLTM_SENSELEN);
		cto->rsp.m1.ct_senselen = QLTM_SENSELEN;
		cto->rsp.m1.ct_scsi_status |= CT2_SNSLEN_VALID;
	    }
	} else {
	    cto->ct_flags |= CT2_FLAG_MODE0;
	    if (tmd->cd_hflags & CDFH_DATA_IN) {
		cto->ct_flags |= CT2_DATA_IN;
	    } else {
		cto->ct_flags |= CT2_DATA_OUT;
	    }
	    if (tmd->cd_hflags & CDFH_STSVALID) {
		ssptr = &cto->rsp.m0.ct_scsi_status;
		cto->ct_flags |= CT2_SENDSTATUS | CT2_CCINCR;
		cto->rsp.m0.ct_scsi_status = tmd->cd_scsi_status;
		if ((tmd->cd_hflags & CDFH_SNSVALID) &&
		    tmd->cd_scsi_status == SCSI_CHECK) {
		    MEMCPY(cto->rsp.m0.ct_dataseg, tmd->cd_sense,
			QLTM_SENSELEN);
		    cto->rsp.m0.ct_scsi_status |= CT2_SNSLEN_VALID;
		}
	    }
	    /*
	     * We assume we'll transfer what we say we'll transfer.
	     * Otherwise, the command is dead.
	     */
	    tmd->cd_resid -= tmd->cd_xfrlen;
	}

	if (ssptr && tmd->cd_resid) {
	    cto->ct_resid = tmd->cd_resid;
	    *ssptr |= CT2_DATA_UNDER;
	} else {
	    cto->ct_resid = 0;
	}
	isp_prt(isp, ISP_LOGTDEBUG0,
	    "CTIO2[%x] ssts %x flags %x resid %d",
	    cto->ct_rxid, tmd->cd_scsi_status, cto->ct_flags, cto->ct_resid);
	hp = &cto->ct_syshandle;
	rp = &cto->ct_resid;
	if (cto->ct_flags & CT2_SENDSTATUS)
	    cto->ct_flags |= CT2_CCINCR;
    } else {
	ct_entry_t *cto = (ct_entry_t *) local;

	cto->ct_header.rqs_entry_type = RQSTYPE_CTIO;
	cto->ct_header.rqs_entry_count = 1;
	cto->ct_iid = tmd->cd_iid;
	cto->ct_tgt = tmd->cd_tgt;
	cto->ct_lun = tmd->cd_lun;
	cto->ct_flags = 0;
	cto->ct_fwhandle = AT_GET_HANDLE(tmd->cd_tagval);
	if (AT_HAS_TAG(tmd->cd_tagval)) {
	    cto->ct_tag_val = AT_GET_TAG(tmd->cd_tagval);
	    cto->ct_flags |= CT_TQAE;
	}
	if (tmd->cd_lflags & CDFL_NODISC) {
	    cto->ct_flags |= CT_NODISC;
	}
	if (tmd->cd_xfrlen == 0) {
	    cto->ct_flags |= CT_NO_DATA | CT_SENDSTATUS;
	    cto->ct_scsi_status = tmd->cd_scsi_status;
	    cto->ct_resid = 0;
	} else {
	    if (tmd->cd_hflags & CDFH_STSVALID) {
		cto->ct_flags |= CT_SENDSTATUS;
	    }
	    if (tmd->cd_hflags & CDFH_DATA_IN) {
		cto->ct_flags |= CT_DATA_IN;
	    } else {
		cto->ct_flags |= CT_DATA_OUT;
	    }
	    /*
	     * We assume we'll transfer what we say we'll transfer.
	     * Otherwise, the command is dead.
	     */
	    tmd->cd_resid -= tmd->cd_xfrlen;
	    if (tmd->cd_hflags & CDFH_STSVALID) {
		cto->ct_resid = tmd->cd_resid;
	    }
	}
	isp_prt(isp, ISP_LOGTDEBUG0, "CTIO[%x] ssts %x resid %d cd_hflags %x",
	    AT_GET_HANDLE(tmd->cd_tagval), tmd->cd_scsi_status, tmd->cd_resid,
	    tmd->cd_hflags);
	hp = &cto->ct_syshandle;
	rp = &cto->ct_resid;
	if (cto->ct_flags & CT_SENDSTATUS)
	    cto->ct_flags |= CT_CCINCR;
    }

    if (isp_save_xs(isp, (XS_T *)tmd, hp)) {
	isp_prt(isp, ISP_LOGERR, "isp_target_start_ctio: No XFLIST pointers");
	tmd->cd_error = -ENOMEM;
	tmd->cd_lflags |= CDFL_ERROR;
	ISP_UNLK_SOFTC(isp);
	tmd->cd_private = isp->isp_osinfo.hcb_token;
	(*isp->isp_osinfo.hcb)(QOUT_TMD_DONE, tmd);
	return;
    }

    /*
     * Call the dma setup routines for this entry (and any subsequent
     * CTIOs) if there's data to move, and then tell the f/w it's got
     * new things to play with. As with isp_start's usage of DMA setup,
     * any swizzling is done in the machine dependent layer. Because
     * of this, we put the request onto the queue area first in native
     * format.
     */

    save_handle = *hp;
    switch (ISP_DMASETUP(isp, (XS_T *)tmd, (ispreq_t *) local, &nxti, optr)) {
    case CMD_QUEUED:
	ISP_ADD_REQUEST(isp, nxti);
	ISP_UNLK_SOFTC(isp);
	return;

    case CMD_EAGAIN:
	tmd->cd_error = -ENOMEM;
	tmd->cd_lflags |= CDFL_ERROR;
	isp_destroy_handle(isp, save_handle);
	break;

    case CMD_COMPLETE:
	tmd->cd_error = *rp;	/* propagated back */
	tmd->cd_lflags |= CDFL_ERROR;
	isp_destroy_handle(isp, save_handle);
	break;

    default:
	tmd->cd_error = -EFAULT;	/* probably dma mapping failure */
	tmd->cd_lflags |= CDFL_ERROR;
	isp_destroy_handle(isp, save_handle);
	break;
    }
    ISP_UNLK_SOFTC(isp);
    tmd->cd_private = isp->isp_osinfo.hcb_token;
    (*isp->isp_osinfo.hcb)(QOUT_TMD_DONE, tmd);
}

/*
 * Handle ATIO stuff that the generic code can't.
 * This means handling CDBs.
 */

static int
isp_handle_platform_atio(struct ispsoftc *isp, at_entry_t *aep)
{
    tmd_cmd_t *tmd;
    int status;

    /*
     * The firmware status (except for the QLTM_SVALID bit)
     * indicates why this ATIO was sent to us.
     *
     * If QLTM_SVALID is set, the firware has recommended Sense Data.
     *
     * If the DISCONNECTS DISABLED bit is set in the flags field,
     * we're still connected on the SCSI bus.
     */
    status = aep->at_status;

    if ((status & ~QLTM_SVALID) == AT_PHASE_ERROR) {
	/*
	 * Bus Phase Sequence error. We should have sense data
	 * suggested by the f/w. I'm not sure quite yet what
	 * to do about this.
	 */
	isp_prt(isp, ISP_LOGERR, "PHASE ERROR in atio");
	isp_endcmd(isp, aep, SCSI_BUSY, 0);
	return (0);
    }

    if ((status & ~QLTM_SVALID) != AT_CDB) {
	isp_prt(isp, ISP_LOGERR, "bad atio (0x%x) leaked to platform", status);
	isp_endcmd(isp, aep, SCSI_BUSY, 0);
	return (0);
    }

    if ((tmd = isp->isp_osinfo.tfreelist) == NULL) {
	/*
	 * We're out of resources.
	 *
	 * Because we can't autofeed sense data back with a command for
	 * parallel SCSI, we can't give back a CHECK CONDITION. We'll give
	 * back a QUEUE FULL or BUSY status instead.
	 */
	isp_prt(isp, ISP_LOGERR,
	    "no ATIOS for lun %d from initiator %d on channel %d",
	    aep->at_lun, GET_IID_VAL(aep->at_iid), GET_BUS_VAL(aep->at_iid));
	if (aep->at_flags & AT_TQAE)
	    isp_endcmd(isp, aep, SCSI_QFULL, 0);
	else
	    isp_endcmd(isp, aep, SCSI_BUSY, 0);
	return (0);
    }
    isp->isp_osinfo.tfreelist = tmd->cd_private;
    tmd->cd_lflags = CDFL_BUSY;
    tmd->cd_bus = GET_BUS_VAL(aep->at_iid);
    tmd->cd_iid = GET_IID_VAL(aep->at_iid);
    tmd->cd_tgt = aep->at_tgt;
    tmd->cd_lun = aep->at_lun;
    if (aep->at_flags & AT_NODISC) {
	tmd->cd_lflags |= CDFL_NODISC;
    }
    if (status & QLTM_SVALID) {
	MEMCPY(tmd->cd_sense, aep->at_sense, QLTM_SENSELEN);
	tmd->cd_lflags |= CDFL_SNSVALID;
    }
    MEMCPY(tmd->cd_cdb, aep->at_cdb, ATIO_CDBLEN);
    AT_MAKE_TAGID(tmd->cd_tagval, aep);
    tmd->cd_tagtype = aep->at_tag_type;
    tmd->cd_hba = isp;
    tmd->cd_data = NULL;
    tmd->cd_hflags = 0;
    tmd->cd_totlen = tmd->cd_resid = tmd->cd_xfrlen = tmd->cd_error = 0;
    tmd->cd_scsi_status = 0;
    isp_prt(isp, ISP_LOGTDEBUG1,
        "ATIO[%x] CDB=0x%x bus %d iid%d->lun%d tag 0x%x ttype 0x%x %s",
	aep->at_handle, aep->at_cdb[0] & 0xff, GET_BUS_VAL(aep->at_iid),
	GET_IID_VAL(aep->at_iid), aep->at_lun, aep->at_tag_val & 0xff,
	aep->at_tag_type, (aep->at_flags & AT_NODISC)?
	"nondisc" : "disconnecting");
    if (isp->isp_osinfo.hcb == NULL) {
	isp_endcmd(isp, aep, SCSI_BUSY, 0);
    } else {
	tmd->cd_reserved[0] = QOUT_TMD_START;
	tmd->cd_private = isp->isp_osinfo.pending_t;
	isp->isp_osinfo.pending_t = tmd;
    }
    return (0);
}

static int
isp_handle_platform_atio2(struct ispsoftc *isp, at2_entry_t *aep)
{
    tmd_cmd_t *tmd;
    int lun;

    /*
     * The firmware status (except for the QLTM_SVALID bit)
     * indicates why this ATIO was sent to us.
     *
     * If QLTM_SVALID is set, the firware has recommended Sense Data.
     */
    if ((aep->at_status & ~QLTM_SVALID) != AT_CDB) {
	isp_prt(isp, ISP_LOGERR, "bad atio (0x%x) leaked to platform",
	    aep->at_status);
	isp_endcmd(isp, aep, SCSI_BUSY, 0);
	return (0);
    }
    if (FCPARAM(isp)->isp_fwattr & ISP_FW_ATTR_SCCLUN)
	lun = aep->at_scclun;
    else
	lun = aep->at_lun;

    /*
     * If we're out of resources, just send a QFULL status back.
     */
    if ((tmd = isp->isp_osinfo.tfreelist) == NULL) {
	isp_endcmd(isp, aep, SCSI_QFULL, 0);
	return (0);
    }
    tmd->cd_lflags = CDFL_BUSY;
    tmd->cd_iid = aep->at_iid;
    tmd->cd_tgt = ((fcparam *)isp->isp_param)->isp_loopid;
    tmd->cd_lun = lun;
    tmd->cd_bus = 0;
    MEMCPY(tmd->cd_cdb, aep->at_cdb, ATIO_CDBLEN);
    switch (aep->at_taskflags & ATIO2_TC_ATTR_MASK) {
    case ATIO2_TC_ATTR_SIMPLEQ:
	tmd->cd_tagtype = MSG_SIMPLE_Q_TAG;
	break;
    case ATIO2_TC_ATTR_HEADOFQ:
	tmd->cd_tagtype = MSG_HEAD_OF_Q_TAG;
	break;
    case ATIO2_TC_ATTR_ORDERED:
	tmd->cd_tagtype = MSG_ORDERED_Q_TAG;
	break;
    case ATIO2_TC_ATTR_ACAQ:		/* ?? */
    case ATIO2_TC_ATTR_UNTAGGED:
    default:
	tmd->cd_tagtype = 0;
	break;
    }
    tmd->cd_tagval = aep->at_rxid;
    tmd->cd_hba = isp;
    tmd->cd_data = NULL;
    tmd->cd_hflags = 0;
    tmd->cd_totlen = aep->at_datalen;
    tmd->cd_resid = tmd->cd_xfrlen = tmd->cd_error = 0;
    tmd->cd_scsi_status = 0;
    if ((isp->isp_dblev & ISP_LOGTDEBUG0) || isp->isp_osinfo.hcb == NULL) {
	isp_prt(isp, ISP_LOGALL,
	    "ATIO2[%x] CDB=0x%x iid %d for lun %d tcode 0x%x dlen %d",
	    aep->at_rxid, aep->at_cdb[0] & 0xff, aep->at_iid,
	    lun, aep->at_taskcodes, aep->at_datalen);
    }
    if (isp->isp_osinfo.hcb == NULL) {
	if (aep->at_cdb[0] == INQUIRY && lun == 0) {
	    if (aep->at_cdb[1] == 0 && aep->at_cdb[2] == 0) {
		static u_int8_t inqdata[] = {
		    DEFAULT_DEVICE_TYPE, 0x0, 0x2, 0x2, 32, 0, 0, 0x40,
		    'L', 'I', 'N', 'U', 'X', ' ', ' ', ' ',
		    'T', 'A', 'R', 'G', 'E', 'T', ' ', 'D',
		    'D', 'E', 'V', 'I', 'C', 'E', ' ', ' ',
		    '0', '0', '0', '1'
		};
		struct scatterlist single, *dp = &single;
		dp->address = inqdata;
		dp->alt_address = NULL;
		dp->length = sizeof (inqdata);
		tmd->cd_data = dp;
		tmd->cd_resid = tmd->cd_xfrlen = sizeof (inqdata);
		tmd->cd_hflags |= CDFH_DATA_IN|CDFH_STSVALID;
		ISP_DROP_LK_SOFTC(isp);
		isp_target_start_ctio(isp, tmd);
		ISP_IGET_LK_SOFTC(isp);
	    } else {
		/*
		 * Illegal field in CDB
		 *  0x24 << 24 | 0x5 << 12 | ECMD_SVALID | SCSI_CHECK
		 */
		isp_endcmd(isp, aep, 0x24005102, 0);
	    }
	} else if (lun == 0) {
		/*
		 * Not Ready, Cause Not Reportable
		 *
		 *  0x4 << 24 | 0x2 << 12 | ECMD_SVALID | SCSI_CHECK
		 */
		isp_endcmd(isp, aep, 0x04002102, 0);
	} else {
	    /*
	     * Logical Unit Not Supported:
	     * 	0x25 << 24 | 0x5 << 12 | ECMD_SVALID | SCSI_CHECK
	     */
	    isp_endcmd(isp, aep, 0x25005102, 0);
	}
	MEMZERO(tmd, sizeof (tmd_cmd_t));
	return (0);
    }
    tmd->cd_reserved[0] = QOUT_TMD_START;
    isp->isp_osinfo.tfreelist = tmd->cd_private;
    tmd->cd_private = isp->isp_osinfo.pending_t;
    isp->isp_osinfo.pending_t = tmd;
    return (0);
}

static int
isp_handle_platform_ctio(struct ispsoftc *isp, void *arg)
{
    tmd_cmd_t *tmd;
    int sentstatus, ok, resid = 0, sts;

    /*
     * CTIO and CTIO2 are close enough....
     */
    tmd = (tmd_cmd_t *) isp_find_xs(isp, ((ct_entry_t *)arg)->ct_syshandle);
    if (tmd == NULL) {
	isp_prt(isp, ISP_LOGERR, "isp_handle_platform_ctio: null tmd");
	return (0);
    }
    isp_destroy_handle(isp, ((ct_entry_t *)arg)->ct_syshandle);

    if (IS_FC(isp)) {
	ct2_entry_t *ct = arg;
	sentstatus = ct->ct_flags & CT2_SENDSTATUS;
	if (sentstatus) {
	    tmd->cd_lflags |= CDFL_SENTSTATUS;
	}
	sts = ct->ct_status & ~QLTM_SVALID;
	ok = (ct->ct_status & ~QLTM_SVALID) == CT_OK;
	if (ok && sentstatus && (tmd->cd_hflags & CDFH_SNSVALID)) {
	    tmd->cd_lflags |= CDFL_SENTSENSE;
	}
	isp_prt(isp, ISP_LOGTDEBUG1,
	    "CTIO2[%x] sts 0x%x flg 0x%x sns %d %s",
	    ct->ct_rxid, ct->ct_status, ct->ct_flags,
	    (tmd->cd_lflags & CDFL_SENTSENSE) != 0,
	    sentstatus? "FIN" : "MID");
	if ((ct->ct_flags & CT2_DATAMASK) != CT2_NO_DATA) {
	    resid = ct->ct_resid;
	}
    } else {
	ct_entry_t *ct = arg;
	sts = ct->ct_status & ~QLTM_SVALID;
	sentstatus = ct->ct_flags & CT_SENDSTATUS;
	if (sentstatus) {
	    tmd->cd_lflags |= CDFL_SENTSTATUS;
	}
	ok = (ct->ct_status & ~QLTM_SVALID) == CT_OK;
	if (ok && sentstatus && (tmd->cd_hflags & CDFH_SNSVALID)) {
	    tmd->cd_lflags |= CDFL_SENTSENSE;
	}
	isp_prt(isp, ISP_LOGTDEBUG1,
	    "CTIO[%x] tag %x iid %x tgt %d lun %d sts 0x%x flg %x %s",
	    ct->ct_fwhandle, ct->ct_tag_val, ct->ct_iid, ct->ct_tgt,
	    ct->ct_lun, ct->ct_status, ct->ct_flags,
	    sentstatus? "FIN" : "MID");
	if (ct->ct_status & QLTM_SVALID) {
	    char *sp = (char *)ct;
	    sp += CTIO_SENSE_OFFSET;
	    MEMCPY(tmd->cd_sense, sp, QLTM_SENSELEN);
	    tmd->cd_lflags |= CDFL_SNSVALID;
	}
	if ((ct->ct_flags & CT_DATAMASK) != CT_NO_DATA) {
	    resid = ct->ct_resid;
	}
    }
    tmd->cd_resid += resid;

    /*
     * We're here either because intermediate data transfers are done
     * and/or the final status CTIO (which may have joined with a
     * Data Transfer) is done.
     *
     * In any case, for this platform, the upper layers figure out
     * what to do next, so all we do here is collect status and
     * pass information along.
     */
    isp_prt(isp, ISP_LOGTDEBUG0, "%s CTIO done (resid %d)",
	(sentstatus)? "  FINAL " : "MIDTERM ", tmd->cd_resid);

    if (!ok) {
	isp_prt(isp, ISP_LOGERR, "CTIO ended with badstate (0x%x)", sts);
	tmd->cd_lflags |= CDFL_ERROR;
	tmd->cd_error = -EIO;
	isp_target_putback_atio(isp, tmd);
    } else {
	isp_complete_ctio(isp, tmd);
    }
    return (0);
}

static void
isp_target_putback_atio(struct ispsoftc *isp, tmd_cmd_t *tmd)
{
    u_int16_t nxti;
    u_int8_t local[QENTRY_LEN];
    void *qe;

    if (isp_getrqentry(isp, &nxti, NULL, &qe)) {
	isp_prt(isp, ISP_LOGWARN,
	    "isp_target_putback_atio: Request Queue Overflow");
	/* XXXX */
	isp_complete_ctio(isp, tmd);
	return;
    }
    MEMZERO(local, sizeof (local));
    if (IS_FC(isp)) {
	at2_entry_t *at = (at2_entry_t *) local;
	at->at_header.rqs_entry_type = RQSTYPE_ATIO2;
	at->at_header.rqs_entry_count = 1;
	if (FCPARAM(isp)->isp_fwattr & ISP_FW_ATTR_SCCLUN)
	    at->at_scclun = (uint16_t) tmd->cd_lun;
	else
	    at->at_lun = (uint8_t) tmd->cd_lun;
	at->at_status = CT_OK;
	at->at_rxid = tmd->cd_tagval;
	isp_put_atio2(isp, at, qe);
    } else {
	at_entry_t *at = (at_entry_t *)local;
	at->at_header.rqs_entry_type = RQSTYPE_ATIO;
	at->at_header.rqs_entry_count = 1;
	at->at_iid = tmd->cd_iid;
	at->at_iid |= tmd->cd_bus << 7;
	at->at_tgt = tmd->cd_tgt;
	at->at_lun = tmd->cd_lun;
	at->at_status = CT_OK;
	at->at_tag_val = AT_GET_TAG(tmd->cd_tagval);
	at->at_handle = AT_GET_HANDLE(tmd->cd_tagval);
	isp_put_atio(isp, at, qe);
    }
    ISP_TDQE(isp, "isp_target_putback_atio", isp->isp_reqidx, qe);
    ISP_ADD_REQUEST(isp, nxti);
    isp_complete_ctio(isp, tmd);
}

static void
isp_complete_ctio(struct ispsoftc *isp, tmd_cmd_t *tmd)
{
    if (isp->isp_osinfo.hcb == NULL) {
	isp_prt(isp, ISP_LOGWARN, "nobody to tell about completing command");
	MEMZERO(tmd, sizeof (tmd_cmd_t));
	tmd->cd_private = isp->isp_osinfo.tfreelist;
	isp->isp_osinfo.tfreelist = tmd;
    } else {
	tmd->cd_reserved[0] = QOUT_TMD_DONE;
	tmd->cd_private = isp->isp_osinfo.pending_t;
	isp->isp_osinfo.pending_t = tmd;
    }
}

static int
isp_en_dis_lun(struct ispsoftc *isp, int enable, int bus, int tgt, int lun)
{
    DECLARE_MUTEX_LOCKED(rsem);
    u_int16_t rstat;
    int rv, enabled, cmd;

    /*
     * First, we can't do anything unless we have an upper
     * level target driver to route commands to.
     */
    if (isp->isp_osinfo.hcb == NULL) {
	return (-EINVAL);
    }

    /*
     * Second, check for sanity of enable argument.
     */
    enabled = ((isp->isp_osinfo.tmflags & (1 << bus)) != 0);
    if (enable == 0 && enabled == 0) {
	return (-EINVAL);
    }

    /*
     * Third, check to see if we're enabling on fibre channel
     * and don't yet have a notion of who the heck we are (no
     * loop yet).
     */
    if (IS_FC(isp) && !enabled) {
	ISP_LOCK_SOFTC(isp);
	if ((isp->isp_role & ISP_ROLE_TARGET) == 0) {
	    isp->isp_role |= ISP_ROLE_TARGET;
	    if (isp_drain_reset(isp, "lun enables")) {
		return (-EIO);
	    }
	}
	ISP_UNLK_SOFTC(isp);
	SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 1);
    }

    /*
     * If this is a wildcard target, select our initiator
     * id/loop id for use as what we enable as.
     */

    if (tgt == -1) {
	if (IS_FC(isp)) {
	    tgt = ((fcparam *)isp->isp_param)->isp_loopid;
	} else {
	    tgt = ((sdparam *)isp->isp_param)->isp_initiator_id;
	}
    }

    /*
     * Do some sanity checking on lun arguments.
     */

    if (lun < 0 || lun >= (IS_FC(isp)? TM_MAX_LUN_FC : TM_MAX_LUN_SCSI)) {
	return (-EINVAL);
    }

    /*
     * Snag the semaphore on the return state value on enables/disables.
     */
    if (down_interruptible(&isp->isp_osinfo.tgt_inisem)) {
	return (-EINTR);
    }

    if (enable && LUN_BTST(isp, bus, lun)) {
	up(&isp->isp_osinfo.tgt_inisem);
	return (-EEXIST);
    }
    if (!enable && !LUN_BTST(isp, bus, lun)) {
	up(&isp->isp_osinfo.tgt_inisem);
	return (-NODEV);
    }

    if (enable && nolunsenabled(isp, bus)) {
	int av = (bus << 31) | ENABLE_TARGET_FLAG;
	ISP_LOCK_SOFTC(isp);
	rv = isp_control(isp, ISPCTL_TOGGLE_TMODE, &av);
	ISP_UNLK_SOFTC(isp);
	if (rv) {
	    up(&isp->isp_osinfo.tgt_inisem);
	    return (-EIO);
	}
    }

    ISP_LOCK_SOFTC(isp);
    isp->isp_osinfo.rsemap = &rsem;
    if (enable) {
	u_int32_t seq = isp->isp_osinfo.rollinfo++;
	int n, ulun = lun;

	cmd = RQSTYPE_ENABLE_LUN;
	n = DFLT_INOT_CNT;
	if (IS_FC(isp) && lun != 0) {
	    cmd = RQSTYPE_MODIFY_LUN;
	    n = 0;
	    /*
	     * For SCC firmware, we only deal with setting
	     * (enabling or modifying) lun 0.
	     */
	    ulun = 0;
	}
	rstat = LUN_ERR;
	if (isp_lun_cmd(isp, cmd, bus, tgt, ulun, DFLT_CMND_CNT, n, seq)) {
	    isp_prt(isp, ISP_LOGERR, "isp_lun_cmd failed");
	    goto out;
	}
	ISP_UNLK_SOFTC(isp);
	down(isp->isp_osinfo.rsemap);
	ISP_LOCK_SOFTC(isp);
	isp->isp_osinfo.rsemap = NULL;
	rstat = isp->isp_osinfo.rstatus;
	if (rstat != LUN_OK) {
	    isp_prt(isp, ISP_LOGERR, "MODIFY/ENABLE LUN returned 0x%x", rstat);
	    goto out;
	}
    } else {
	int n, ulun = lun;
	u_int32_t seq;

	rstat = LUN_ERR;
	seq = isp->isp_osinfo.rollinfo++;
	cmd = -RQSTYPE_MODIFY_LUN;

	n = DFLT_INOT_CNT;
	if (IS_FC(isp) && lun != 0) {
	    n = 0;
	    /*
	     * For SCC firmware, we only deal with setting
	     * (enabling or modifying) lun 0.
	     */
	    ulun = 0;
	}
	if (isp_lun_cmd(isp, cmd, bus, tgt, ulun, DFLT_CMND_CNT, n, seq)) {
	    isp_prt(isp, ISP_LOGERR, "isp_lun_cmd failed");
	    goto out;
	}
	ISP_UNLK_SOFTC(isp);
	down(isp->isp_osinfo.rsemap);
	ISP_LOCK_SOFTC(isp);
	isp->isp_osinfo.rsemap = NULL;
	rstat = isp->isp_osinfo.rstatus;
	if (rstat != LUN_OK) {
	    isp_prt(isp, ISP_LOGERR, "MODIFY LUN returned 0x%x", rstat);
	    goto out;
	}
	if (IS_FC(isp) && lun) {
	    goto out;
	}
	seq = isp->isp_osinfo.rollinfo++;
	isp->isp_osinfo.rsemap = &rsem;

	rstat = LUN_ERR;
	cmd = -RQSTYPE_ENABLE_LUN;
	if (isp_lun_cmd(isp, cmd, bus, tgt, lun, 0, 0, seq)) {
	    isp_prt(isp, ISP_LOGERR, "isp_lun_cmd failed");
	    goto out;
	}
	ISP_UNLK_SOFTC(isp);
	down(isp->isp_osinfo.rsemap);
	ISP_LOCK_SOFTC(isp);
	isp->isp_osinfo.rsemap = NULL;
	rstat = isp->isp_osinfo.rstatus;
	if (rstat != LUN_OK) {
	    isp_prt(isp, ISP_LOGERR, "DISABLE LUN returned 0x%x", rstat);
	    goto out;
	}
    }
out:

    if (rstat != LUN_OK) {
	isp_prt(isp, ISP_LOGERR, "lun %d %sable failed", lun,
	    (enable) ? "en" : "dis");
	ISP_UNLK_SOFTC(isp);
	up(&isp->isp_osinfo.tgt_inisem);
	return (-EIO);
    } else {
	isp_prt(isp, ISP_LOGINFO,
	    "lun %d now %sabled for target mode on channel %d", lun,
	    (enable)? "en" : "dis", bus);
	if (enable == 0) {
	    LUN_BCLR(isp, bus, lun);
	    if (nolunsenabled(isp, bus)) {
		int av = bus << 31;
		rv = isp_control(isp, ISPCTL_TOGGLE_TMODE, &av);
		if (rv) {
		    isp_prt(isp, ISP_LOGERR,
			"failed to disable target mode on channel %d", bus);
		    /* but proceed */
		    ISP_UNLK_SOFTC(isp);
		    return (-EIO);
		}
		isp->isp_osinfo.tmflags &= ~(1 << bus);
		isp->isp_role &= ~ISP_ROLE_TARGET;
		if (IS_FC(isp)) {
		    if (isplinux_drain_reset(isp, "lun disables")) {
			return (-EIO);
		    }
		    if ((isp->isp_role & ISP_ROLE_INITIATOR) != 0) {
			ISP_UNLK_SOFTC(isp);
			SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 1);
			ISP_LOCK_SOFTC(isp);
		    }
		}
	    }
	} else {
	    isp->isp_osinfo.tmflags |= (1 << bus);
	    LUN_BSET(isp, bus, lun);
	}
	ISP_UNLK_SOFTC(isp);
	up(&isp->isp_osinfo.tgt_inisem);
	return (0);
    }
}
#endif

int
isp_async(struct ispsoftc *isp, ispasync_t cmd, void *arg)
{
    switch (cmd) {
    case ISPASYNC_NEW_TGT_PARAMS:
	if (IS_SCSI(isp)) {
	    sdparam *sdp = isp->isp_param;
	    char *wt;
	    int mhz, flags, bus, tgt, period;

	    tgt = *((int *) arg);
	    bus = (tgt >> 16) & 0xffff;
	    tgt &= 0xffff;

	    sdp += bus;
	    flags = sdp->isp_devparam[tgt].actv_flags;
	    period = sdp->isp_devparam[tgt].actv_period;
	    if ((flags & DPARM_SYNC) && period &&
		(sdp->isp_devparam[tgt].actv_offset) != 0) {
		if (sdp->isp_lvdmode || period < 0xc) {
		    switch (period) {
		    case 0x9:
			mhz = 80;
			break;
		    case 0xa:
			mhz = 40;
			break;
		    case 0xb:
			mhz = 33;
			break;
		    case 0xc:
			mhz = 25;
			break;
		    default:
			mhz = 1000 / (period * 4);
			break;
		    }
		} else {
		    mhz = 1000 / (period * 4);
		}
	    } else {
		mhz = 0;
	    }
	    switch (flags & (DPARM_WIDE|DPARM_TQING)) {
	    case DPARM_WIDE:
		wt = ", 16 bit wide";
		break;
	    case DPARM_TQING:
		wt = ", Tagged Queueing Enabled";
		break;
	    case DPARM_WIDE|DPARM_TQING:
		wt = ", 16 bit wide, Tagged Queueing Enabled";
		break;
	    default:
		wt = " ";
		break;
	    }
	    if (mhz) {
		isp_prt(isp, ISP_LOGINFO,
		    "Channel %d Target %d at %dMHz Max Offset %d%s",
		    bus, tgt, mhz, sdp->isp_devparam[tgt].actv_offset, wt);
	    } else {
		isp_prt(isp, ISP_LOGINFO, "Channel %d Target %d Async Mode%s",
		    bus, tgt, wt);
	    }
	}
	break;
    case ISPASYNC_LIP:
	isp_prt(isp, ISP_LOGINFO, "LIP Received");
	break;
    case ISPASYNC_LOOP_RESET:
	isp_prt(isp, ISP_LOGINFO, "Loop Reset Received");
	break;
    case ISPASYNC_BUS_RESET:
	isp_prt(isp, ISP_LOGINFO, "SCSI bus %d reset detected", *((int *) arg));
	break;
    case ISPASYNC_LOOP_DOWN:
	isp_prt(isp, ISP_LOGINFO, "Loop DOWN");
	break;
    case ISPASYNC_LOOP_UP:
	isp_prt(isp, ISP_LOGINFO, "Loop UP");
	break;
    case ISPASYNC_PROMENADE:
    {
	fcparam *fcp = isp->isp_param;
	struct lportdb *lp;
	int tgt;

	tgt = *((int *) arg);
	lp = &fcp->portdb[tgt];

	if (lp->valid) {
	    isp_prt(isp, ISP_LOGINFO,
		"ID %d (Loop 0x%x) Port WWN 0x%08x%08x arrived, role %s",
		tgt, lp->loopid, (unsigned int) (lp->port_wwn >> 32),
		(unsigned int) (lp->port_wwn & 0xffffffff),
		class3_roles[fcp->portdb[tgt].roles]);
	} else {
	    isp_prt(isp, ISP_LOGINFO,
		"ID %d (Loop 0x%x) Port WWN 0x%08x%08x departed", tgt,
		lp->loopid, (unsigned int) (lp->port_wwn >> 32),
		(unsigned int) (lp->port_wwn & 0xffffffff));
	}
	break;
    }
    case ISPASYNC_CHANGE_NOTIFY:
	if (arg == ISPASYNC_CHANGE_PDB) {
		isp_prt(isp, ISP_LOGINFO,
		    "Port Database Changed");
	} else if (arg == ISPASYNC_CHANGE_SNS) {
		isp_prt(isp, ISP_LOGINFO,
		    "Name Server Database Changed");
	}
	SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 0);
	break;
    case ISPASYNC_FABRIC_DEV:
    {
	int target, base, lim;
	fcparam *fcp = isp->isp_param;
	struct lportdb *lp = NULL;
	struct lportdb *clp = (struct lportdb *) arg;
	char *pt;

	switch (clp->port_type) {
	case 1:
		pt = "   N_Port";
		break;
	case 2:
		pt = "  NL_Port";
		break;
	case 3:
		pt = "F/NL_Port";
		break;
	case 0x7f:
		pt = "  Nx_Port";
		break;
	case 0x81:
		pt = "  F_port";
		break;
	case 0x82:
		pt = "  FL_Port";
		break;
	case 0x84:
		pt = "   E_port";
		break;
	default:
		pt = " ";
		break;
	}

	isp_prt(isp, ISP_LOGINFO,
	    "%s Fabric Device @ PortID 0x%x", pt, clp->portid);

	/*
	 * If we don't have an initiator role we bail.
	 *
	 * We just use ISPASYNC_FABRIC_DEV for announcement purposes.
	 */

	if ((isp->isp_role & ISP_ROLE_INITIATOR) == 0) {
		break;
	}

	/*
	 * Is this entry for us? If so, we bail.
	 */

	if (fcp->isp_portid == clp->portid) {
		break;
	}

	/*
	 * Else, the default policy is to find room for it in
	 * our local port database. Later, when we execute
	 * the call to isp_pdb_sync either this newly arrived
	 * or already logged in device will be (re)announced.
	 */

	if (fcp->isp_topo == TOPO_FL_PORT)
		base = FC_SNS_ID+1;
	else
		base = 0;

	if (fcp->isp_topo == TOPO_N_PORT)
		lim = 1;
	else
		lim = MAX_FC_TARG;

	/*
	 * Is it already in our list?
	 */
	for (target = base; target < lim; target++) {
		if (target >= FL_PORT_ID && target <= FC_SNS_ID) {
			continue;
		}
		lp = &fcp->portdb[target];
		if (lp->port_wwn == clp->port_wwn &&
		    lp->node_wwn == clp->node_wwn) {
			lp->fabric_dev = 1;
			break;
		}
	}
	if (target < lim) {
		break;
	}
	for (target = base; target < lim; target++) {
		if (target >= FL_PORT_ID && target <= FC_SNS_ID) {
			continue;
		}
		lp = &fcp->portdb[target];
		if (lp->port_wwn == 0) {
			break;
		}
	}
	if (target == lim) {
		isp_prt(isp, ISP_LOGWARN,
		    "out of space for fabric devices");
		break;
	}
	lp->port_type = clp->port_type;
	lp->fc4_type = clp->fc4_type;
	lp->node_wwn = clp->node_wwn;
	lp->port_wwn = clp->port_wwn;
	lp->portid = clp->portid;
	lp->fabric_dev = 1;
	break;
    }
#ifdef	LINUX_ISP_TARGET_MODE
    case ISPASYNC_TARGET_MESSAGE:
    {
	tmd_msg_t *mp = arg;
	isp_prt(isp, ISP_LOGTDEBUG2,
	    "bus %d iid %d tgt %d lun %d ttype %x tval %x msg[0]=%x",
	    mp->nt_bus, (int) mp->nt_iid, (int) mp->nt_tgt, (int) mp->nt_lun,
	    mp->nt_tagtype, mp->nt_tagval, mp->nt_msg[0]);
	break;
    }
    case ISPASYNC_TARGET_EVENT:
    {
	tmd_event_t *ep = arg;
	isp_prt(isp, ISP_LOGTDEBUG2,
	    "bus %d event code 0x%x", ep->ev_bus, ep->ev_event);
	break;
    }
    case ISPASYNC_TARGET_ACTION:
	switch (((isphdr_t *)arg)->rqs_entry_type) {
	default:
	    isp_prt(isp, ISP_LOGWARN, "event 0x%x for unhandled target action",
		((isphdr_t *)arg)->rqs_entry_type);
	    break;
	case RQSTYPE_ATIO:
	    (void) isp_handle_platform_atio(isp, (at_entry_t *) arg);
	    break;
	case RQSTYPE_ATIO2:
	    (void) isp_handle_platform_atio2(isp, (at2_entry_t *)arg);
	    break;
	case RQSTYPE_CTIO2:
	case RQSTYPE_CTIO:
	    (void) isp_handle_platform_ctio(isp, arg);
	    break;
	case RQSTYPE_ENABLE_LUN:
	case RQSTYPE_MODIFY_LUN:
	    isp->isp_osinfo.rstatus = ((lun_entry_t *)arg)->le_status;
	    if (isp->isp_osinfo.rsemap) {
		up(isp->isp_osinfo.rsemap);
	    }
	    break;
	}
	break;
#endif
    case ISPASYNC_FW_CRASH:
    {
	u_int16_t mbox1, mbox6;
	mbox1 = ISP_READ(isp, OUTMAILBOX1);
	if (IS_DUALBUS(isp)) {
	    mbox6 = ISP_READ(isp, OUTMAILBOX6);
	} else {
	    mbox6 = 0;
	}
	isp_prt(isp, ISP_LOGERR,
	    "Internal F/W Error on bus %d @ RISC Address 0x%x", mbox6, mbox1);
	if (IS_FC(isp)) {
	    /* XXX isp->isp_blocked = 1; */
	    SEND_THREAD_EVENT(isp, ISP_THREAD_FW_CRASH_DUMP, 0);
	}
	break;
    }
    case ISPASYNC_UNHANDLED_RESPONSE:
	break;
    default:
	return (-1);
    }
    return (0);
}

int
isplinux_biosparam(Disk *disk, kdev_t n, int ip[])
{
    int size = disk->capacity;
    ip[0] = 64;
    ip[1] = 32;
    ip[2] = size >> 11;
    if (ip[2] > 1024) {
	ip[0] = 255;
	ip[1] = 63;
	ip[2] = size / (ip[0] * ip[1]);
    }
    return (0);
}

/*
 * Set the queue depth for this device.
 */

void
isplinux_sqd(struct Scsi_Host *host, Scsi_Device *devs)
{
    while (devs) {
	if (devs->host == host && devs->tagged_supported == 0) {
	    /*
	     * If this device doesn't support tagged operations, don't waste
	     * queue space for it, even if it has multiple luns.
	     */
	    devs->queue_depth = 2;
	} else if (devs->host == host) {
	    int depth = 2;
	    struct ispsoftc *isp = (struct ispsoftc *) host->hostdata;

	    if (IS_SCSI(isp)) {
		sdparam *sdp = isp->isp_param;
		sdp += devs->channel;
		depth = sdp->isp_devparam[devs->id].exc_throttle;
	    } else {
		depth = FCPARAM(isp)->isp_execthrottle;
	    }
	    if (isp_throttle) {
		/*
		 * This limit is due to the size of devs->queue_depth
		 */
		depth = (unsigned char) min(isp_throttle, 255);;
	    }
	    if (depth < 4) {
		depth = 4;
	    }
	    devs->queue_depth = depth;
	}
	devs = devs->next;
    }
}

/*
 * Periodic watchdog timer.. the main purpose here is to restart
 * commands that were pegged on resources, etc...
 */
void
isplinux_timer(unsigned long arg)
{
    Scsi_Cmnd *Cmnd;
    struct ispsoftc *isp = (struct ispsoftc *) arg;

    ISP_ILOCK_SOFTC(isp);
    if (IS_FC(isp)) {
	int rql;
	if (isp->isp_role & ISP_ROLE_INITIATOR)
	    rql = LOOP_READY;
	else
	    rql = LOOP_LSCAN_DONE;
	if (isp->isp_fcrswdog || FCPARAM(isp)->isp_fwstate != FW_READY ||
	    FCPARAM(isp)->isp_loopstate < rql) {
	    isp->isp_fcrswdog = 0;
	    if (isp->isp_deadloop == 0 && isp->isp_role != ISP_ROLE_NONE) {
		SEND_THREAD_EVENT(isp, ISP_THREAD_FC_RESCAN, 0);
	    }
	}
    }
    isplinux_runwaitq(isp);
    if ((Cmnd = isp->isp_osinfo.dqnext) != NULL) {
	isp->isp_osinfo.dqnext = isp->isp_osinfo.dqtail = NULL;
    }
    if (isp->dogactive) {
	isp->isp_osinfo.timer.expires = jiffies + ISP_WATCH_TIME;
	add_timer(&isp->isp_osinfo.timer);
    }
    ISP_IUNLK_SOFTC(isp);
    if (Cmnd) {
	ISP_LOCK_SCSI_DONE(isp);
        while (Cmnd) {
	    Scsi_Cmnd *f = (Scsi_Cmnd *) Cmnd->host_scribble;
	    Cmnd->host_scribble = NULL;
	    /*
	     * Get around silliness in midlayer.
	     */
	    if (host_byte(Cmnd->result) == DID_RESET) {
		Cmnd->flags |= IS_RESETTING;
	    }
	    (*Cmnd->scsi_done)(Cmnd);
	    Cmnd = f;
	}
	ISP_UNLK_SCSI_DONE(isp);
    }
}

void
isplinux_mbtimer(unsigned long arg)
{
    struct ispsoftc *isp = (struct ispsoftc *) arg;
    ISP_ILOCK_SOFTC(isp);
    if (isp->mbox_waiting) {
	isp->mbox_waiting = 0;
	up(&isp->mbox_c_sem);
    }
    ISP_IUNLK_SOFTC(isp);
}

void
isplinux_intr(int irq, void *arg, struct pt_regs *pt)
{
    struct ispsoftc *isp = arg;
    u_int16_t isr, sema, mbox;
    Scsi_Cmnd *Cmnd;

    ISP_ILOCK_SOFTC(isp);
    isp->isp_intcnt++;
    if (ISP_READ_ISR(isp, &isr, &sema, &mbox) == 0) {
	isp->isp_intbogus++;
	ISP_IUNLK_SOFTC(isp);
	return;
    }
    isp_intr(isp, isr, sema, mbox);
    isplinux_runwaitq(isp);
    if ((Cmnd = isp->isp_osinfo.dqnext) != NULL) {
	isp->isp_osinfo.dqnext = isp->isp_osinfo.dqtail = NULL;
    }
#ifdef	LINUX_ISP_TARGET_MODE
    if (isp->isp_osinfo.pending_t) {
	struct tmd_cmd *tmd = isp->isp_osinfo.pending_t;
	isp->isp_osinfo.pending_t = NULL;
	ISP_IUNLK_SOFTC(isp);
	do {
	    struct tmd_cmd *next = tmd->cd_private;
	    tmd->cd_private = isp->isp_osinfo.hcb_token;
	    (*isp->isp_osinfo.hcb)(tmd->cd_reserved[0], tmd);
	    tmd = next;
	} while (tmd != NULL);
    } else {
	ISP_IUNLK_SOFTC(isp);
    }
#else
    ISP_IUNLK_SOFTC(isp);
#endif
    if (Cmnd) {
	ISP_LOCK_SCSI_DONE(isp);
        while (Cmnd) {
	    Scsi_Cmnd *f = (Scsi_Cmnd *) Cmnd->host_scribble;
	    Cmnd->host_scribble = NULL;
	    /*
	     * Get around silliness in midlayer.
	     */
	    if (host_byte(Cmnd->result) == DID_RESET) {
		Cmnd->flags |= IS_RESETTING;
	    }
	    (*Cmnd->scsi_done)(Cmnd);
	    Cmnd = f;
	}
	ISP_UNLK_SCSI_DONE(isp);
    }
}

static INLINE int
isp_parse_rolearg(struct ispsoftc *isp, char *roles)
{
    char *role = roles;

    while (role && *role) {
	unsigned int id;
	char *eqtok, *commatok, *p, *q;
	
	eqtok = role;
	eqtok = strchr(role, '=');
	if (eqtok == NULL)
	   break;
	*eqtok = 0;
	commatok = strchr(eqtok+1, ',');
	if (commatok)
	    *commatok = 0;
	if (strncmp(role, "0x", 2) == 0)
	    q = role + 2;
	else
	    q = role;
	id = simple_strtoul(q, &p, 16);
	*eqtok = '=';
	if (p != q && id == isp->isp_osinfo.device_id) {
	    p = eqtok + 1;
	    if (strcmp(p, "none") == 0) {
		if (commatok) {
		    *commatok = ',';
		}
		return (ISP_ROLE_NONE);
	    }
	    if (strcmp(p, "target") == 0) {
		if (commatok) {
		    *commatok = ',';
		}
		return (ISP_ROLE_TARGET);
	    }
	    if (strcmp(p, "initiator") == 0) {
		if (commatok) {
		    *commatok = ',';
		}
		return (ISP_ROLE_INITIATOR);
	    }
	    if (strcmp(p, "both") == 0) {
		if (commatok) {
		    *commatok = ',';
		}
		return (ISP_ROLE_BOTH);
	    }
	    break;
	}
        if (commatok) {
	    role = commatok+1;
	    *commatok = ',';
	} else {
	    break;
	}
    }
    return (ISP_DEFAULT_ROLES);
}

static INLINE u_int64_t
isp_parse_wwnarg(struct ispsoftc *isp, char *wwns)
{
    char *wwnt = wwns;
    u_int64_t wwn = 0;

    while (wwn == 0 && wwnt && *wwnt) {
	unsigned int id;
	char *eqtok, *commatok, *p, *q;
	
	eqtok = wwnt;
	eqtok = strchr(wwnt, '=');
	if (eqtok == NULL)
	   break;
	*eqtok = 0;
	commatok = strchr(eqtok+1, ',');
	if (commatok)
	    *commatok = 0;
	if (strncmp(wwnt, "0x", 2) == 0)
	    q = wwnt + 2;
	else
	    q = wwnt;
	id = simple_strtoul(q, &p, 16);
	if (p != q && id == isp->isp_osinfo.device_id) {
	    unsigned long t, t2;
	    p = eqtok + 1;
	    while (*p) {
		p++;
	    }
	    p -= 8;
	    if (p > eqtok + 1) {
		char *q;
		char c;
		q = p;
		t = simple_strtoul(p, &q, 16);
		c = *p;
		*p = 0;
		t2 = simple_strtoul(eqtok+1, NULL, 16);
		*p = c;
	    } else {
		t = simple_strtoul(eqtok+1, NULL, 16);
		t2 = 0;
	    }
	    wwn = (((u_int64_t) t2) << 32) | (u_int64_t) t;
	}
	*eqtok = '=';
        if (commatok) {
	    wwnt = commatok+1;
	    *commatok = ',';
	} else {
	    break;
	}
    }
    return (wwn);
}

void
isplinux_common_init(struct ispsoftc *isp)
{
    /*
     * Set up config options, etc...
     */
    if (isp_debug) {
	isp->isp_dblev = isp_debug;
    } else {
	isp->isp_dblev = ISP_LOGCONFIG|ISP_LOGWARN|ISP_LOGERR;
    }

    if (isp_nofwreload & (1 << isp->isp_unit)) {
	isp->isp_confopts |= ISP_CFG_NORELOAD;
    }
    if (isp_nonvram & (1 << isp->isp_unit)) {
	isp->isp_confopts |= ISP_CFG_NONVRAM;
    }
    if (IS_FC(isp)) {
	if (isp_fcduplex & (1 << isp->isp_unit)) {
	    isp->isp_confopts |= ISP_CFG_FULL_DUPLEX;
	}
        isp->isp_defwwpn = isp_parse_wwnarg(isp, isp_wwpns);
	if (isp->isp_defwwpn == 0) {
	    isp->isp_defwwpn = (u_int64_t) 0x400000007F7F7F01;
	} else {
	    isp->isp_confopts |= ISP_CFG_OWNWWPN;
	}
        isp->isp_defwwnn = isp_parse_wwnarg(isp, isp_wwnns);
	if (isp->isp_defwwnn == 0) {
	    isp->isp_defwwnn = (u_int64_t) 0x400000007F7F7F02;
	} else {
	    isp->isp_confopts |= ISP_CFG_OWNWWNN;
	}
	isp->isp_osinfo.host->max_id = MAX_FC_TARG; 
	if (IS_2200(isp) || IS_2300(isp)) {
	    if (isp_nport_only & (1 << isp->isp_unit)) {
		isp->isp_confopts |= ISP_CFG_NPORT_ONLY;
	    } else if (isp_loop_only & (1 << isp->isp_unit)) {
		isp->isp_confopts |= ISP_CFG_LPORT_ONLY;
	    } else {
		isp->isp_confopts |= ISP_CFG_NPORT;
	    }
	}
	isp->isp_osinfo.host->this_id = MAX_FC_TARG+1;
#ifdef	ISP_FW_CRASH_DUMP
	if (IS_2200(isp))
	    FCPARAM(isp)->isp_dump_data = vmalloc(QLA2200_RISC_IMAGE_DUMP_SIZE);
	else if (IS_23XX(isp))
	    FCPARAM(isp)->isp_dump_data = vmalloc(QLA2300_RISC_IMAGE_DUMP_SIZE);
	if (FCPARAM(isp)->isp_dump_data) {
	    isp_prt(isp, ISP_LOGCONFIG, "f/w crash dump area allocated");
	    FCPARAM(isp)->isp_dump_data[0] = 0;
	}
#endif
    } else {
	isp->isp_osinfo.host->max_id = MAX_TARGETS;
	isp->isp_osinfo.host->this_id = MAX_TARGETS+1;
    }
    isp->isp_role = isp_parse_rolearg(isp, isp_roles);


    /*
     * Initialize locks
     */
    ISP_LOCK_INIT(isp);
    ISP_TLOCK_INIT(isp);
    sema_init(&isp->mbox_sem, 1);
    sema_init(&isp->mbox_c_sem, 0);
    sema_init(&isp->fcs_sem, 1);

    /*
     * Start watchdog timer
     */
    ISP_LOCK_SOFTC(isp);
    init_timer(&isp->isp_osinfo.timer);
    isp->isp_osinfo.timer.data = (unsigned long) isp;
    isp->isp_osinfo.timer.function = isplinux_timer;
    isp->isp_osinfo.timer.expires = jiffies + ISP_WATCH_TIME;
    add_timer(&isp->isp_osinfo.timer);
    isp->dogactive = 1;
    if (IS_FC(isp)) {
	DECLARE_MUTEX_LOCKED(sem);
	ISP_UNLK_SOFTC(isp);
	isp->isp_osinfo.task_ctl_sem = &sem;
	kernel_thread(isp_task_thread, isp, 0);
	down(&sem);
	isp->isp_osinfo.task_ctl_sem = NULL;
	ISP_LOCK_SOFTC(isp);
    }
    isplinux_reinit(isp);
#ifdef	LINUX_ISP_TARGET_MODE
    sema_init(&isp->isp_osinfo.tgt_inisem, 1);
#endif
#ifdef	ISP_TARGET_MODE
    isp_attach_target(isp);
#endif
    ISP_UNLK_SOFTC(isp);
}

void
isplinux_reinit(struct ispsoftc *isp)
{
    isp_reset(isp);
    if (isp->isp_state != ISP_RESETSTATE) {
	isp_prt(isp, ISP_LOGERR, "failed to enter RESET state");
	return;
    } 
    isp->isp_osinfo.host->max_lun = min(isp_maxluns, isp->isp_maxluns);
    isp_init(isp);
    if (isp->isp_role == ISP_ROLE_NONE) {
	return;
    }
    if (isp->isp_state != ISP_INITSTATE) {
	isp_prt(isp, ISP_LOGERR, "failed to enter INIT state");
	return;
    }
    isp->isp_state = ISP_RUNSTATE;

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    isp->isp_osinfo.host->can_queue = isp->isp_maxcmds;
#else
    isp->isp_osinfo.host->can_queue = min(255, isp->isp_maxcmds);
#endif
    if (isp->isp_osinfo.host->can_queue == 0)
	isp->isp_osinfo.host->can_queue = 1;

    if (IS_FC(isp)) {
	isp->isp_osinfo.host->this_id = MAX_FC_TARG;
	/*
	 * This is *not* the same as execution throttle- that is set
	 * in isplinux_sqd and is per-device.
	 *
	 * What we try and do here is take how much we can queue at
	 * a given time and spread it, reasonably, over all the luns
	 * we expect to run at a time.
	 */
	if (isp_cmd_per_lun) {
	    isp->isp_osinfo.host->cmd_per_lun = isp_cmd_per_lun;
	} else {
	    /*
	     * JAWAG.
	     */
	    isp->isp_osinfo.host->cmd_per_lun = isp->isp_maxcmds >> 3;
	}

	/*
	 * We seem to need a bit of settle time.
	 */
	USEC_DELAY(1 * 1000000);
    } else {
	int bus;

	if (isp_cmd_per_lun) {
	    isp->isp_osinfo.host->cmd_per_lun = isp_cmd_per_lun;
	} else {
	    /*
	     * Maximum total commands spread over either 8 targets,
	     * or 4 targets, 2 luns, etc.
	     */
	    isp->isp_osinfo.host->cmd_per_lun = isp->isp_maxcmds >> 3;
	}

	/*
	 * No way to give different ID's for the second bus.
	 */
	isp->isp_osinfo.host->this_id = SDPARAM(isp)->isp_initiator_id;
	bus = 0;
	(void) isp_control(isp, ISPCTL_RESET_BUS, &bus);
	if (IS_DUALBUS(isp)) {
	    bus = 1;
	    (void) isp_control(isp, ISPCTL_RESET_BUS, &bus);
	}
	/*
	 * Bus Reset delay handled by firmware.
	 */
    }
}

int
isp_drain_reset(struct ispsoftc *isp, char *msg)
{
    /*
     * Drain active commands.
     */
    if (isp_drain(isp, msg)) {
	isp->isp_failed = 1;
	return (-1);
    }
    isp_reinit(isp);
    if (isp->isp_state != ISP_RUNSTATE) {
	return (-1);
    }
    isp->isp_failed = 0;
    return (0);
}

int
isp_drain(struct ispsoftc *isp, char *whom)
{
    /* XXX */
    return (0);
}

static int
isp_task_thread(void *arg)
{
    DECLARE_MUTEX_LOCKED(thread_sleep_semaphore);
    struct ispsoftc *isp = arg;
    unsigned long flags;
    int action, nactions, exit_thread = 0;
    isp_thread_action_t curactions[MAX_THREAD_ACTION];

    if (isp->isp_host->loaded_as_module) {
	siginitsetinv(&current->blocked, sigmask(SIGHUP));
    } else {
	siginitsetinv(&current->blocked, 0);
    }
    lock_kernel();
    daemonize();
    sprintf(current->comm, "isp_thrd%d", isp->isp_unit);
    isp->isp_osinfo.task_thread = current;
    isp->isp_osinfo.task_request = &thread_sleep_semaphore;
    unlock_kernel();

    if (isp->isp_osinfo.task_ctl_sem) {
	up(isp->isp_osinfo.task_ctl_sem);
    }
    isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread starting (%d)",
	in_interrupt());

    while (exit_thread == 0) {
	isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread sleeping");
	down_interruptible(&thread_sleep_semaphore);
	if (isp->isp_host->loaded_as_module) {
	    if (signal_pending(current))
		break;
	}
	isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread running");
	if (in_interrupt()) panic("in interrupt");

	spin_lock_irqsave(&isp->isp_osinfo.tlock, flags);
	nactions = isp->isp_osinfo.nt_actions;
	isp->isp_osinfo.nt_actions = 0;
	for (action = 0; action < nactions; action++) {
		curactions[action] = isp->isp_osinfo.t_actions[action];
		isp->isp_osinfo.t_actions[action].thread_action = 0;
		isp->isp_osinfo.t_actions[action].thread_waiter = 0;
	}
	spin_unlock_irqrestore(&isp->isp_osinfo.tlock, flags);

	for (action = 0; action < nactions; action++) {
	    isp_thread_action_t *tap = &curactions[action];
	    isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread[%d]: action %d (%p)",
		action, tap->thread_action, tap->thread_waiter);
	    switch (tap->thread_action) {
	    case ISP_THREAD_NIL:
		break;
	    case ISP_THREAD_FW_CRASH_DUMP:
		ISP_LOCKU_SOFTC(isp);
		FCPARAM(isp)->isp_fwstate = FW_CONFIG_WAIT;
		FCPARAM(isp)->isp_loopstate = LOOP_NIL;
#ifdef	ISP_FW_CRASH_DUMP
		isp_fw_dump(isp);
#else
		ISP_DUMPREGS(isp, "firmware crash");
#endif
		isplinux_reinit(isp);
		if (isp_fc_runstate(isp, 250000) == 0) {
		    isp->isp_deadloop = 0;
		    isp->isp_downcnt = 0;
		    isp->isp_fcrspend = 0;
		    isplinux_runwaitq(isp);
#ifdef	ISP_TARGET_MODE
		    /*
		     * If we don't re-establish routes here, we're toast.
		     */
		    isp_target_async(isp, 0, ASYNC_SYSTEM_ERROR);
#endif
		} else {
		    isp->isp_fcrswdog = 1;
		}
		ISP_UNLKU_SOFTC(isp);
		break;
	    case ISP_THREAD_REINIT:
		ISP_LOCKU_SOFTC(isp);
		isplinux_reinit(isp);
		ISP_UNLKU_SOFTC(isp);
		break;
	    case ISP_THREAD_FC_RESCAN:
		ISP_LOCKU_SOFTC(isp);
		if (isp_fc_runstate(isp, 250000) == 0) {
		    isp->isp_deadloop = 0;
		    isp->isp_downcnt = 0;
		    isp->isp_fcrspend = 0;
		    isplinux_runwaitq(isp);
		} else {
		    /*
		     * Try again in a little while.
		     */
		    isp->isp_fcrspend = 0;
		    if (++isp->isp_downcnt == isp_deadloop_time) {
			isp_prt(isp, ISP_LOGWARN, "assuming loop is dead");
			FCPARAM(isp)->loop_seen_once = 0;
			isp->isp_deadloop = 1;
			isp->isp_downcnt = 0;
			isplinux_flushwaitq(isp);
		    } else {
			isp->isp_fcrswdog = 1;
		    }
		}
		ISP_UNLKU_SOFTC(isp);
		break;
	    case ISP_THREAD_EXIT:
		if (isp->isp_host->loaded_as_module) {
		    exit_thread = 1;
		}
		break;
	   default:
		break;
	   }
	   if (tap->thread_waiter) {
		isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread signalling %p",
		    tap->thread_waiter);
		up(tap->thread_waiter);
	   }
	}
    }
    isp_prt(isp, ISP_LOGDEBUG1, "isp_task_thread exiting");
    isp->isp_osinfo.task_request = NULL;
    return (0);
}

void
isp_prt(struct ispsoftc *isp, int level, const char *fmt, ...)
{
    char buf[256];
    char *prefl;
    va_list ap;

    if (level != ISP_LOGALL && (level & isp->isp_dblev) == 0) {
	return;
    }
    if (level & ISP_LOGERR) {
	prefl = KERN_ERR "%s: ";
    } else if (level & ISP_LOGWARN) {
	prefl = KERN_WARNING "%s: ";
    } else if (level & ISP_LOGINFO) {
	prefl = KERN_NOTICE "%s: ";
    } else if (level & ISP_LOGCONFIG) {
	prefl = KERN_INFO "%s: ";
    } else {
	prefl = "%s: ";
    }
    printk(prefl, isp->isp_name);
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    printk("%s\n", buf);
}

char *
isp_snprintf(char *buf, size_t amt, const char *fmt, ...)
{
    va_list ap;
    ARGSUSED(amt);
    va_start(ap, fmt);
    (void) vsprintf(buf, fmt, ap);
    va_end(ap);
    return (buf);
}

MODULE_PARM(isp_debug, "i");
MODULE_PARM(isp_disable, "i");
MODULE_PARM(isp_nonvram, "i");
MODULE_PARM(isp_nofwreload, "i");
MODULE_PARM(isp_maxluns, "i");
MODULE_PARM(isp_throttle, "i");
MODULE_PARM(isp_cmd_per_lun, "i");
MODULE_PARM(isp_roles, "s");
MODULE_PARM(isp_fcduplex, "i");
MODULE_PARM(isp_wwpns, "s");
MODULE_PARM(isp_wwnns, "s");
MODULE_PARM(isp_nport_only, "i");
MODULE_PARM(isp_loop_only, "i");
MODULE_PARM(isp_deadloop_time, "i");
MODULE_PARM(isp_xtime, "i");
MODULE_LICENSE("Dual BSD/GPL");
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) || defined(MODULE)
Scsi_Host_Template driver_template = QLOGICISP;
#include "scsi_module.c"
#endif
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
