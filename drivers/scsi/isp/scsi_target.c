/* @(#)scsi_target.c 1.10 */
/*
 * SCSI Target Mode "stub" driver for Linux.
 *
 * Copyright (c) 2000, 2001 by Matthew Jacob
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

#ifdef	MODULE
#define	EXPORT_SYMTAB
#endif
#include <linux/version.h>
#ifndef	KERNEL_VERSION
#define KERNEL_VERSION(v,p,s)		(((v)<<16)+(p<<8)+s)
#endif
#include <linux/autoconf.h>
#ifdef	CONFIG_SMP
#define	__SMP__	1
#endif
#define	_KVC	LINUX_VERSION_CODE

#if _KVC <= KERNEL_VERSION(2,2,0)
#error	"Linux 2.0 and 2.1 kernels are not supported anymore"
#endif
#if _KVC >= KERNEL_VERSION(2,3,0) && _KVC < KERNEL_VERSION(2,4,0)
#error	"Linux 2.3 kernels are not supported"
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
#include <linux/malloc.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/smp.h>
#include <linux/spinlock.h>
#else
#include <asm/spinlock.h>
#endif
#include <asm/scatterlist.h>
#include <asm/system.h>
#include <linux/miscdevice.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif

#ifdef	min
#undef	min
#endif
#define min(a,b) (((a)<(b))?(a):(b))
#ifndef	roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif

#include "scsi_target.h"
#include "isp_tpublic.h"
#include "linux/smp_lock.h"

#define	DEFAULT_DEVICE_TYPE	3	/* PROCESSOR */
#define	MAX_BUS			8
#define	MAX_LUN			64
#define	N_SENSE_BUFS		64

typedef struct sdata {
    struct sdata *next;
    u_int8_t sdata[QLTM_SENSELEN];
} sdata_t;

struct bus;
typedef struct initiator {
    struct initiator *	ini_next;
    struct bus *	ini_bus;	/* backpointer to containing bus */
    struct sdata *	ini_sdata;	/* pending sense data list */
    u_int64_t 		ini_iid;	/* initiator identifier */
} ini_t;

#define	HASH_WIDTH	16
#define	INI_HASH_LISTP(busp, ini_id)	busp->list[ini_id & (HASH_WIDTH - 1)]

typedef struct {
    hba_register_t h;		/* must be first */
    ini_t *list[HASH_WIDTH];	/* hash list of known initiators */
    u_int8_t luns[2][MAX_LUN];
} bus_t;

#define	SDprintk		if (scsi_tdebug) printk
#define	SDprintk2		if (scsi_tdebug > 1) printk
static int scsi_tdebug = 0;
static int scsi_target_open(struct inode *, struct file *);
static int scsi_target_close(struct inode *, struct file *);
static int
scsi_target_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static void scsi_target_handler(qact_e, void *);

static inline bus_t *bus_from_tmd(tmd_cmd_t *, int);
static inline bus_t *bus_from_name(char *);
static inline ini_t *ini_from_tmd(bus_t *, tmd_cmd_t *);

static void add_sdata(ini_t *, void *);
static void rem_sdata(ini_t *);
static void scsi_target_start_cmd(tmd_cmd_t *, bus_t *);
static int scsi_target_thread(void *);
static int scsi_target_endis(char *, int, int, int, int);

/*
 * Local Declarations
 */
static u_int8_t inqdata[36] = {
    DEFAULT_DEVICE_TYPE, 0x0, 0x2, 0x2, 32, 0, 0, 0x32,
    'L', 'I', 'N', 'U', 'X', ' ', ' ', ' ',
    'S', 'C', 'S', 'I', ' ', 'B', 'L', 'A',
    'C', 'K', 'H', 'O', 'L', 'E', ' ', ' ',
    '0', '0', '0', '1'
};
static u_int8_t illfld[QLTM_SENSELEN] = {
    0xf0, 0, 0x5, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0x24
};
static u_int8_t nolun[QLTM_SENSELEN] = {
    0xf0, 0, 0x5, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0x25
};
#if	0
static u_int8_t notrdy[QLTM_SENSELEN] = {
    0xf0, 0, 0x2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0x04
};
#endif
static u_int8_t ifailure[QLTM_SENSELEN] = {
    0xf0, 0, 0x4, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0x44
};
static u_int8_t ua[QLTM_SENSELEN] = {
    0xf0, 0, 0x0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0x29
};
static u_int8_t nosense[QLTM_SENSELEN] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static bus_t busses[MAX_BUS];
static sdata_t *sdp;

DECLARE_MUTEX_LOCKED(scsi_thread_sleep_semaphore);
DECLARE_MUTEX_LOCKED(scsi_thread_entry_exit_semaphore);
static tmd_cmd_t *p_front = NULL, *p_last = NULL;
static spinlock_t scsi_target_lock = SPIN_LOCK_UNLOCKED;
static int scsi_target_thread_exit = 0;
static int scsi_target_thread_notified = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
static struct proc_dir_entry *st = NULL;
extern struct proc_dir_entry *proc_scsi;
static int stp(char *, char **, off_t, int);
#endif

static struct file_operations scsi_target_fops = {
ioctl:		scsi_target_ioctl,
open:		scsi_target_open,	/* open */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
release:	scsi_target_close	/* close */
#else
release:	(void (*)(struct inode *, struct file *)) scsi_target_close
#endif
};

static struct miscdevice scsi_target_dev = {
    MISC_DYNAMIC_MINOR, "scsi_target", &scsi_target_fops
};


static int
scsi_target_open(struct inode * inode, struct file * file)
{
    return (0);
}

static int
scsi_target_close(struct inode * inode, struct file * file)
{
    return (0);
}

static int
scsi_target_ioctl(struct inode *ip, struct file *fp, unsigned int cmd,
	unsigned long arg)
{
    sc_enable_t *sc;
    switch(cmd) {
    case SC_ENABLE_LUN:
    case SC_DISABLE_LUN:
	sc = (sc_enable_t *) arg;
	SDprintk("scsi_target_ioctl: %sable %s, chan %d, target %d, lun %d\n",
	    (cmd == SC_ENABLE_LUN)? "en" : "dis", sc->hba_name_unit,
	    sc->channel, sc->target, sc->lun);
	return (scsi_target_endis(sc->hba_name_unit, sc->channel,
	    sc->target, sc->lun, (cmd == SC_ENABLE_LUN)? 1 : 0));
    default:
	break;
    }
    return (-EINVAL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
static int
stp(char *buf, char **start, off_t offset, int length)
{
    int len;
    len = sprintf(buf, "%d\n", scsi_target_dev.minor);
    *start = buf + offset;
    len -= offset;
    if (len > length)
	len = length;
    if (len < 0)
	len = 0;
    return (len);
}
#endif

static int
scsi_target_init(void)
{
    int r = misc_register(&scsi_target_dev);
    if (r == 0) {
	printk("scsi_target: allocated misc minor number %d (0x%x)\n",
	    scsi_target_dev.minor, scsi_target_dev.minor);
	spin_lock_init(&scsi_target_lock);
	kernel_thread(scsi_target_thread, NULL, 0);
	down(&scsi_thread_entry_exit_semaphore);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
	st = create_proc_info_entry("scsi/scsi_target_minor", 0, 0, stp);
#endif
	
    } else {
	printk("scsi_target: unable to register with misc driver (%d)\n", r);
    }
    return (r);
}

static inline bus_t *
bus_from_tmd(tmd_cmd_t *tmd, int doset)
{
    bus_t *bp;
    if (tmd->cd_private)
	return (tmd->cd_private);
    for (bp = busses; bp < &busses[MAX_BUS]; bp++) {
    	if (bp->h.r_action == NULL)
	    continue;
 	if (bp->h.r_identity == tmd->cd_hba) {
	    if (doset)
		tmd->cd_private = bp;
	    return (bp);
	}
    }
    return (NULL);
}

static inline bus_t *
bus_from_name(char *name)
{
    bus_t *bp;
    for (bp = busses; bp < &busses[MAX_BUS]; bp++) {
	char localbuf[32];
    	if (bp->h.r_action == NULL)
	    continue;
	sprintf(localbuf, "%s%d", bp->h.r_name, bp->h.r_inst);
 	if (strncmp(name, localbuf, 32) == 0) {
	    return (bp);
	}
    }
    return (NULL);
}

static inline ini_t *
ini_from_tmd(bus_t *bp, tmd_cmd_t *tmd)
{
   ini_t *ptr = INI_HASH_LISTP(bp, tmd->cd_iid);
   if (ptr) {
	do {
	    if (ptr->ini_iid == tmd->cd_iid)
		return (ptr);
	} while ((ptr = ptr->ini_next) != NULL);
   }
   return (ptr);
}

/*
 * Make an initiator structure
 */
static int
make_ini(bus_t *bp, u_int64_t iid)
{
   ini_t *nptr, **ptrlptr = &INI_HASH_LISTP(bp, iid);
   nptr = kmalloc(sizeof (ini_t), GFP_KERNEL);
   if (nptr == NULL) {
       printk("no mem to create initiator structure\n");
       return (-ENOMEM);
   }
   memset(nptr, 0, sizeof (ini_t));
   nptr->ini_iid = iid;
   nptr->ini_bus = (struct bus *) bp;
   nptr->ini_next = *ptrlptr;
   /*
    * Start off with a Unit Attention condition.
    */
   add_sdata(nptr, ua);

   *ptrlptr = nptr;
   return (0);
}

/*
 * Add this sense data from the list of
 * sense data structures for this initiator.
 * We always add to the front of the list.
 */
static void
add_sdata(ini_t *ini, void *sd)
{
   sdata_t *t = sdp;
   if (t == NULL) {
	printk("outta sense data structures\n");
	t = kmalloc(sizeof (sdata_t), GFP_KERNEL);
	if (t == NULL) {
	    printk("REALLY outta sense data structures\n");
	    return;
	}
	t->next = NULL;
   }
   memcpy(t->sdata, sd, sizeof (t->sdata));
   t->next = ini->ini_sdata;
   ini->ini_sdata = t;
}

/*
 * Remove one sense data item from the list of
 * sense data structures for this initiator.
 */
static void
rem_sdata(ini_t *ini)
{
    sdata_t *t = ini->ini_sdata;
    if (t) {
	ini->ini_sdata = t->next;
	t->next = sdp;
	sdp = t;
    }
}

/* XXXX: NOT READY YET FOR NON-FC DEVICES - CONTINGENT ALLEGIANCE HANDLING */
#ifndef	INQUIRY
#define	INQUIRY 0x12
#endif
#ifndef	REQUEST_SENSE
#define	REQUEST_SENSE	0x3
#endif
#ifndef	TEST_UNIT_READY
#define	TEST_UNIT_READY	0
#endif

#define	_SGS0	 roundup(sizeof (struct scatterlist), sizeof (void *))
#define	SGS_SIZE(x)		(roundup((x), sizeof (void *)) + _SGS0)
#define	SGS_PAYLOAD(x, ptr)	(ptr) ((char *)x + _SGS0)

static void
scsi_target_start_cmd(tmd_cmd_t *tmd, bus_t *bp)
{
    unsigned long flags;
    ini_t *ini;

    spin_lock_irqsave(&scsi_target_lock, flags);
    ini = ini_from_tmd(bp, tmd);
    tmd->cd_hflags = 0;
    tmd->cd_scsi_status = SCSI_GOOD;
    tmd->cd_data = NULL;
    tmd->cd_resid =  tmd->cd_totlen;

    if (ini == NULL) {
	if (make_ini(bp, tmd->cd_iid)) {
	    tmd->cd_xfrlen = 0;
	    tmd->cd_scsi_status = SCSI_BUSY;
	    tmd->cd_hflags |= CDFH_STSVALID;
            spin_unlock_irqrestore(&scsi_target_lock, flags);
	    (*bp->h.r_action)(QIN_TMD_CONT, tmd);
	    return;
	}
	ini = ini_from_tmd(bp, tmd);
    }
    spin_unlock_irqrestore(&scsi_target_lock, flags);

    /*
     * Commands get lumped into 4 rough groups:
     *
     *   + Commands which don't ever really return CHECK CONDITIONS and
     *     always work. These are typically INQUIRY (future: Reserve/Release)
     *
     *   + Commands that we accept, but also report CHECK CONDITIONS against if
     *     we have pending contingent allegiance (e..g, TEST UNIT READY).
     *
     *   + Commands that retrieve Sense Data (REQUEST SENSE)
     *
     *   + All others (which we bounce with either ILLEGAL COMMAND or BAD LUN).
     */

    if (tmd->cd_cdb[0] == INQUIRY) {
	if (tmd->cd_totlen == 0)
	    tmd->cd_totlen = tmd->cd_cdb[4];

	if (tmd->cd_totlen == 0) {
	    tmd->cd_hflags |= CDFH_STSVALID;
	} else if ((tmd->cd_cdb[1] & 0x1f) == 0 && tmd->cd_cdb[2] == 0) {
	    struct scatterlist *dp = NULL;
	    dp = (struct scatterlist *)
		kmalloc(SGS_SIZE(sizeof (inqdata)), GFP_ATOMIC|GFP_DMA);
	    if (dp == NULL) {
		printk("scsi_target_alloc: out of memory for inquiry data\n");
		spin_lock_irqsave(&scsi_target_lock, flags);
		add_sdata(ini, ifailure);
		spin_unlock_irqrestore(&scsi_target_lock, flags);
		tmd->cd_hflags |= CDFH_SNSVALID;
	    } else {
		dp->alt_address = NULL;
		dp->address = SGS_PAYLOAD(dp, char *);
		dp->length = min(sizeof(inqdata), tmd->cd_totlen);
		tmd->cd_xfrlen = dp->length;
		memcpy(dp->address, inqdata, sizeof (inqdata));

		/*
		 * If we're not here, say we aren't here.
		 */
		if (tmd->cd_lun >= MAX_LUN || tmd->cd_bus >= 2) {
		    dp->address[0] = (3 << 5) | DEFAULT_DEVICE_TYPE;
		} else if (bp->luns[tmd->cd_bus][tmd->cd_lun] == 0) {
		    dp->address[0] = (1 << 5) | DEFAULT_DEVICE_TYPE;
		}
		tmd->cd_data = dp;
		tmd->cd_hflags |= CDFH_DATA_IN|CDFH_STSVALID;
		SDprintk2("scsi_target(%s%d): %p (%p) length %d byte0 0x%x\n",
		    bp->h.r_name, bp->h.r_inst, dp->address, dp, dp->length,
		    inqdata[0] & 0xff);
		tmd->cd_hflags |= CDFH_PRIVATE_0;
	    }
	} else {
	    SDprintk2("scsi_target(%s%d): illegal field for inquiry data\n",
		bp->h.r_name, bp->h.r_inst);
	    spin_lock_irqsave(&scsi_target_lock, flags);
	    add_sdata(ini, illfld);
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    tmd->cd_hflags |= CDFH_SNSVALID;
	}
	goto doit;
    }

#if	0
    if (tmd->cd_lun != 0) {
        spin_lock_irqsave(&scsi_target_lock, flags);
	add_sdata(ini, notrdy);
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	tmd->cd_hflags |= CDFH_SNSVALID;
	goto doit;
    }
#endif

    if (tmd->cd_cdb[0] == REQUEST_SENSE) {
	struct scatterlist *dp = NULL;
	if (tmd->cd_totlen == 0)
	    tmd->cd_totlen = tmd->cd_cdb[4];
	if (tmd->cd_totlen != 0) {
	    dp = (struct scatterlist *)
		kmalloc(SGS_SIZE(QLTM_SENSELEN), GFP_ATOMIC|GFP_DMA);

	    if (dp == NULL) {
		printk("scsi_target_alloc: out of memory for sense data\n");
		spin_lock_irqsave(&scsi_target_lock, flags);
		add_sdata(ini, ifailure);
		spin_unlock_irqrestore(&scsi_target_lock, flags);
		tmd->cd_hflags |= CDFH_SNSVALID;
	    } else {
		if (ini->ini_sdata == NULL) {
		    spin_lock_irqsave(&scsi_target_lock, flags);
		    add_sdata(ini, nosense);
		    spin_unlock_irqrestore(&scsi_target_lock, flags);
		}
		dp->alt_address = NULL;
		dp->address = SGS_PAYLOAD(dp, char *);
		dp->length = min(QLTM_SENSELEN, tmd->cd_totlen);
		tmd->cd_xfrlen = dp->length;
		memcpy(dp->address, ini->ini_sdata->sdata, QLTM_SENSELEN);
		tmd->cd_data = dp;
		tmd->cd_hflags |= CDFH_DATA_IN;
		SDprintk2("sense data in scsi_target for %s%d: %p (%p) len %d, "
		    "key/asc/ascq 0x%x/0x%x/0x%x\n", bp->h.r_name,
		    bp->h.r_inst, dp->address, dp, dp->length,
		    dp->address[2]&0xf, dp->address[12]&0xff,
		    dp->address[13]);
		tmd->cd_hflags |= CDFH_PRIVATE_0;
	    }
	}
	tmd->cd_hflags |= CDFH_STSVALID;
    } else if (tmd->cd_cdb[0] == TEST_UNIT_READY) {
	tmd->cd_hflags |= CDFH_STSVALID;
	if (ini->ini_sdata)
		tmd->cd_hflags |= CDFH_SNSVALID;
    } else {
	tmd->cd_hflags |= CDFH_SNSVALID;
	spin_lock_irqsave(&scsi_target_lock, flags);
	add_sdata(ini, nolun);
	spin_unlock_irqrestore(&scsi_target_lock, flags);
    }

    if ((tmd->cd_hflags & CDFH_SNSVALID) == 0) {
	if (ini->ini_sdata) {
	    tmd->cd_hflags |= CDFH_SNSVALID;
	}
    }

doit:
    tmd->cd_resid =  tmd->cd_totlen;
    if (tmd->cd_hflags & CDFH_SNSVALID) {
	tmd->cd_scsi_status = SCSI_CHECK;
	tmd->cd_hflags |= CDFH_STSVALID;
	memcpy(tmd->cd_sense, ini->ini_sdata->sdata, QLTM_SENSELEN);
    }

    SDprintk("INI%d-Lun%d: tag0x%x cdb0=0x%x totlen=%d ssts=0x%x hflags 0x%x\n",
	(int) tmd->cd_iid, (int) tmd->cd_lun, tmd->cd_tagval,
	tmd->cd_cdb[0] & 0xff, tmd->cd_totlen, tmd->cd_scsi_status,
	tmd->cd_hflags);

    (*bp->h.r_action)(QIN_TMD_CONT, tmd);
}

static inline void
scsi_target_ldfree(tmd_cmd_t *tmd)
{
    if (tmd->cd_hflags & CDFH_PRIVATE_0) {
	SDprintk("scsi_target: freeing tmd->cd_data %p\n", tmd->cd_data);
	kfree(tmd->cd_data);
	tmd->cd_hflags &= ~0x80;
	tmd->cd_data = NULL;
    }
}


void
scsi_target_handler(qact_e action, void *arg)
{
    unsigned long flags;
    bus_t *bp;

    switch (action) {
    case QOUT_HBA_REG:
    {
	tmd_cmd_t local;
	hba_register_t local1;
	spin_lock_irqsave(&scsi_target_lock, flags);
	for (bp = busses; bp < &busses[MAX_BUS]; bp++) {
	   if (bp->h.r_action == NULL) {
		break;
	   }
	}
	if (bp == &busses[MAX_BUS]) {
	    printk("scsi_target: cannot register any more SCSI busses\n");
	    break;
	}
	bp->h = *(hba_register_t *) arg;
	bp->h.r_name[6] = 0;
	bp->h.r_name[7] = 0;
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	printk("scsi_target: registering %s%d\n", bp->h.r_name, bp->h.r_inst);
	local.cd_hba = bp->h.r_identity;
	local.cd_data = &local1;
	local1.r_identity = bp;
	local1.r_action = (void (*)(int, void *)) scsi_target_handler;
	(*bp->h.r_action)(QIN_HBA_REG, &local);
	break;
    }
    case QOUT_TMD_START:
    {
	tmd_cmd_t *tmd = arg;
	tmd->cd_private = NULL;

	spin_lock_irqsave(&scsi_target_lock, flags);
	bp = bus_from_tmd(arg, 0);
	if (bp == NULL) {
	    printk("scsi_target: bogus QOUT_TMD_START (%p)\n", arg);
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    break;
	}
	SDprintk2("scsi_target: TMD_START from %s%d\n",
	    bp->h.r_name, bp->h.r_inst);
	if (p_front) {
	    p_last->cd_private = tmd;
	} else {
	    p_front = tmd;
	}
	p_last = tmd;
	if (scsi_target_thread_notified == 0) {
	    up(&scsi_thread_sleep_semaphore);
	    scsi_target_thread_notified++;
	}
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	break;
    }
    case QOUT_TMD_DONE:
    {
	tmd_cmd_t *tmd = arg;
	ini_t *nptr;

	spin_lock_irqsave(&scsi_target_lock, flags);
	bp = bus_from_tmd(tmd, 1);
	if (bp == NULL) {
	    printk("scsi_target: bogus QOUT_TMD_DONE(%p)\n", arg);
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    break;
	}
	nptr = ini_from_tmd(bp, tmd);
	SDprintk2("scsi_target: TMD_DONE from %s%d (0x%x)\n",
	    bp->h.r_name, bp->h.r_inst, tmd->cd_lflags);
	if (tmd->cd_hflags & CDFH_STSVALID) {
	    if ((tmd->cd_lflags & CDFL_SENTSTATUS) == 0) {
		if (tmd->cd_xfrlen) {
		    SDprintk("scsi_target: data done, now do status\n");
		    scsi_target_ldfree(tmd);
		    tmd->cd_xfrlen = 0;
		    spin_unlock_irqrestore(&scsi_target_lock, flags);
		    (*bp->h.r_action)(QIN_TMD_CONT, tmd);
		    break;
		}
		printk("scsi_target: already tried to send status\n");
	        scsi_target_ldfree(tmd);
		spin_unlock_irqrestore(&scsi_target_lock, flags);
		(*bp->h.r_action)(QIN_TMD_FIN, arg);
		break;
	    }
	}
	/*
	 * Was sense valid? Do we know whom we were talking to?
	 * If so, and we sent sense, remove the Sense Data that's
	 * current for this initiator.
	 */
	if (tmd->cd_hflags & CDFH_SNSVALID) {
	    if (nptr) {
		if (tmd->cd_lflags & CDFL_SENTSENSE) {
		    rem_sdata(nptr);
		}
	    }
	}

	/*
	 * Was this a REQUEST SENSE command? If so,
	 * remove any sense data for this initiator
	 * which we might have sent.
	 *
	 * XXX: Actually, we should remove it once
	 * XXX: we make it data we would actually
	 * XXX: send.
	 */
	if (tmd->cd_cdb[0] == REQUEST_SENSE) {
	    if (nptr) {
		rem_sdata(nptr);
	    }
	}

	scsi_target_ldfree(tmd);
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	(*bp->h.r_action)(QIN_TMD_FIN, arg);
	break;
    }
    case QOUT_TEVENT:
	spin_lock_irqsave(&scsi_target_lock, flags);
	bp = bus_from_tmd(arg, 1);
	if (bp == NULL) {
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    printk("scsi_target: bogus QOUT_TMD_EVENT (%p)\n", arg);
	    break;
	}
	SDprintk("scsi_target: TEVENT from %s%d\n",
	    bp->h.r_name, bp->h.r_inst);
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	break;
    case QOUT_TMSG:
	spin_lock_irqsave(&scsi_target_lock, flags);
	bp = bus_from_tmd(arg, 1);
	if (bp == NULL) {
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    printk("scsi_target: bogus QOUT_TMD_MSG (%p)\n", arg);
	    break;
	}
	printk("scsi_target: XXXX TMSG from %s%d\n",
	    bp->h.r_name, bp->h.r_inst);
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	break;
    case QOUT_HBA_UNREG:
	spin_lock_irqsave(&scsi_target_lock, flags);
	bp = bus_from_tmd(arg, 0);
	if (bp == NULL) {
	    spin_unlock_irqrestore(&scsi_target_lock, flags);
	    printk("scsi_target: bogus QOUT_HBA_UNREG (%p)\n", arg);
	    break;
	}
	printk("scsi_target: unregistering %s%d\n",
	    bp->h.r_name, bp->h.r_inst);
	memset(bp, 0, sizeof (*bp));
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	break;

    default:
	printk("scsi_target: action code %d (0x%x)?\n", action, action);
	break;
    }
}

static int
scsi_target_thread(void *arg)
{
    unsigned long flags;

    siginitsetinv(&current->blocked, 0);
    lock_kernel();
    daemonize();
    sprintf(current->comm, "scsi_target_thread");
    unlock_kernel();
    up(&scsi_thread_entry_exit_semaphore);
    SDprintk("scsi_target_thread starting\n");

    while (scsi_target_thread_exit == 0) {
	bus_t *bp;
	tmd_cmd_t *tp;

	SDprintk2("scsi_task_thread sleeping\n");
	down(&scsi_thread_sleep_semaphore);
	SDprintk2("scsi_task_thread running\n");

	spin_lock_irqsave(&scsi_target_lock, flags);
        scsi_target_thread_notified = 0;
	if ((tp = p_front) != NULL) {
		p_last = p_front = NULL;
	}
	spin_unlock_irqrestore(&scsi_target_lock, flags);
	while (tp) {
	    tmd_cmd_t *active;

	    active = tp;
	    tp = active->cd_private;
	    active->cd_private = NULL;
	    bp = bus_from_tmd(active, 1);
	    if (bp == NULL) {
		panic("Lost the Bus?");
	    }
	    scsi_target_start_cmd(active, bp);
	}
    }
    SDprintk("scsi_target_thread exiting\n");
    up(&scsi_thread_entry_exit_semaphore);
    return(0);
}

static int
scsi_target_endis(char *hba_name_unit, int channel, int target, int lun, int en)
{
    tmd_cmd_t ec;
    bus_t *bp;
/*
 * XXX: yes, there is a race condition here where the bus can
 * XXX: go away. But in order to solve it, we have to make the
 * XXX: bus structure stay around while we call into the HBA
 * XXX: anyway, so fooey,.
 */
    bp = bus_from_name(hba_name_unit);
    if (bp == NULL) {
	return (-ENXIO);
    }
    if (channel > 1 || channel < 0 || lun < 0 || lun >= MAX_LUN) {
	return (-EINVAL);
    }
    memset(&ec, 0, sizeof (ec));
    ec.cd_hba = bp->h.r_identity;
    ec.cd_bus = channel;
    ec.cd_iid = target;
    ec.cd_lun = lun;
    (*bp->h.r_action)(en? QIN_ENABLE : QIN_DISABLE, &ec);
    if (ec.cd_lflags & CDFL_ERROR) {
	return (ec.cd_error);
    } else {
	bp->luns[channel][lun] = (en != 0);
	return (0);
    }
}

#ifdef	MODULE
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
EXPORT_SYMBOL_NOVERS(scsi_target_handler);
MODULE_PARM(scsi_tdebug, "i");
#endif
int init_module(void)
{
    int r = scsi_target_init();
    if (r == 0) {
	for (r = N_SENSE_BUFS; r > 0; r--) {
	    sdata_t *t = kmalloc(sizeof (sdata_t), GFP_KERNEL);
	    if (t) {
		t->next = sdp;
		sdp = t;
	    }
	}
    }
    return (r);
}

void cleanup_module(void)
{
    int i;

    scsi_target_thread_exit = 1;
    up(&scsi_thread_sleep_semaphore);
    down(&scsi_thread_entry_exit_semaphore);
    for (i = 0; i < MAX_BUS; i++) {
	if (busses[i].h.r_action) {
	    int j;
	    tmd_cmd_t tmd;
	    tmd.cd_hba = busses[i].h.r_identity;
	    (*busses[i].h.r_action)(QIN_HBA_UNREG, &tmd);
	    busses[i].h.r_action = NULL;
	    for (j = 0; j < HASH_WIDTH; j++) {
		ini_t *nptr = busses[i].list[j];
		while (nptr) {
		    ini_t *next = nptr->ini_next;
		    kfree(nptr);
		    nptr = next;
		}
	    }
	}
    }
    while (sdp) {
	sdata_t *t = sdp->next;
	kfree(sdp);
	sdp = t;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
    if (st) {
	remove_proc_entry("scsi/scsi_target", 0);
    }
#endif
    if (misc_deregister(&scsi_target_dev)) {
	printk("scsi_target: warning- could not deregister\n");
    }
}
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
