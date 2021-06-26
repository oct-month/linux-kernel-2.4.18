/* @(#)isp_pci.c 1.25 */
/*
 * Qlogic ISP Host Adapter PCI specific probe and attach routines
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

#include "isp_linux.h"
#if	defined(__powerpc__) || defined(__sparc__)
static int isp_pci_mapmem = 0xffffffff;
#else
static int isp_pci_mapmem = 0;
#endif
#if	defined(__sparc__)
#undef	ioremap_nocache
#define	ioremap_nocache	ioremap
#endif
static int isplinux_pci_init(struct Scsi_Host *);
static u_int16_t isp_pci_rd_reg(struct ispsoftc *, int);
static void isp_pci_wr_reg(struct ispsoftc *, int, u_int16_t);
#if !(defined(ISP_DISABLE_1080_SUPPORT) && defined(ISP_DISABLE_12160_SUPPORT))
static u_int16_t isp_pci_rd_reg_1080(struct ispsoftc *, int);
static void isp_pci_wr_reg_1080(struct ispsoftc *, int, u_int16_t);
#endif
static int
isp_pci_rd_isr(struct ispsoftc *, u_int16_t *, u_int16_t *, u_int16_t *);
#ifndef	ISP_DISABLE_2300_SUPPORT
static int
isp_pci_rd_isr_2300(struct ispsoftc *, u_int16_t *, u_int16_t *, u_int16_t *);
#endif
static int isp_pci_mbxdma(struct ispsoftc *);
static int
isp_pci_dmasetup(struct ispsoftc *, XS_T *, ispreq_t *, u_int16_t *, u_int16_t);

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
static void isp_pci_dmateardown(struct ispsoftc *, XS_T *, u_int16_t);
#else
#define	isp_pci_dmateardown	NULL
#endif

static void isp_pci_reset1(struct ispsoftc *);
static void isp_pci_dumpregs(struct ispsoftc *, const char *);

#ifndef	ISP_CODE_ORG
#define	ISP_CODE_ORG		0x1000
#endif

#ifndef	ISP_DISABLE_1020_SUPPORT
#include "asm_1040.h"
#define	ISP_1040_RISC_CODE	(u_int16_t *) isp_1040_risc_code
#else
#define	ISP_1040_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_1080_SUPPORT
#include "asm_1080.h"
#define	ISP_1080_RISC_CODE	(u_int16_t *) isp_1080_risc_code
#else
#define	ISP_1080_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_12160_SUPPORT
#include "asm_12160.h"
#define	ISP_12160_RISC_CODE	(u_int16_t *) isp_12160_risc_code
#else
#define	ISP_12160_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_2100_SUPPORT
#include "asm_2100.h"
#define	ISP_2100_RISC_CODE	(u_int16_t *) isp_2100_risc_code
#else
#define	ISP_2100_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_2200_SUPPORT
#include "asm_2200.h"
#define	ISP_2200_RISC_CODE	(u_int16_t *) isp_2200_risc_code
#else
#define	ISP_2200_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_2300_SUPPORT
#include "asm_2300.h"
#define	ISP_2300_RISC_CODE	(u_int16_t *) isp_2300_risc_code
#else
#define	ISP_2300_RISC_CODE	NULL
#endif

#ifndef	ISP_DISABLE_1020_SUPPORT
static struct ispmdvec mdvec = {
    isp_pci_rd_isr,
    isp_pci_rd_reg,
    isp_pci_wr_reg,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_1040_RISC_CODE,
    BIU_BURST_ENABLE|BIU_PCI_CONF1_FIFO_64
};
#endif

#ifndef	ISP_DISABLE_1080_SUPPORT
static struct ispmdvec mdvec_1080 = {
    isp_pci_rd_isr,
    isp_pci_rd_reg_1080,
    isp_pci_wr_reg_1080,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_1080_RISC_CODE,
    BIU_BURST_ENABLE|BIU_PCI_CONF1_FIFO_128
};
#endif

#ifndef	ISP_DISABLE_12160_SUPPORT
static struct ispmdvec mdvec_12160 = {
    isp_pci_rd_isr,
    isp_pci_rd_reg_1080,
    isp_pci_wr_reg_1080,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_12160_RISC_CODE,
    BIU_BURST_ENABLE|BIU_PCI_CONF1_FIFO_128
};
#endif

#ifndef	ISP_DISABLE_2100_SUPPORT
static struct ispmdvec mdvec_2100 = {
    isp_pci_rd_isr,
    isp_pci_rd_reg,
    isp_pci_wr_reg,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_2100_RISC_CODE
};
#endif

#ifndef	ISP_DISABLE_2200_SUPPORT
static struct ispmdvec mdvec_2200 = {
    isp_pci_rd_isr,
    isp_pci_rd_reg,
    isp_pci_wr_reg,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_2200_RISC_CODE
};
#endif

#ifndef	ISP_DISABLE_2300_SUPPORT
static struct ispmdvec mdvec_2300 = {
    isp_pci_rd_isr_2300,
    isp_pci_rd_reg,
    isp_pci_wr_reg,
    isp_pci_mbxdma,
    isp_pci_dmasetup,
    isp_pci_dmateardown,
    NULL,
    isp_pci_reset1,
    isp_pci_dumpregs,
    ISP_2300_RISC_CODE
};
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP1020
#define	PCI_DEVICE_ID_QLOGIC_ISP1020	0x1020
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP1020
#define	PCI_DEVICE_ID_QLOGIC_ISP1020	0x1020
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP1080
#define	PCI_DEVICE_ID_QLOGIC_ISP1080	0x1080
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP12160
#define	PCI_DEVICE_ID_QLOGIC_ISP12160	0x1216
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP1240
#define	PCI_DEVICE_ID_QLOGIC_ISP1240	0x1240
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP1280
#define	PCI_DEVICE_ID_QLOGIC_ISP1280	0x1280
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP2100
#define	PCI_DEVICE_ID_QLOGIC_ISP2100	0x2100
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP2200
#define	PCI_DEVICE_ID_QLOGIC_ISP2200	0x2200
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP2300
#define	PCI_DEVICE_ID_QLOGIC_ISP2300	0x2300
#endif

#ifndef	PCI_DEVICE_ID_QLOGIC_ISP2312
#define	PCI_DEVICE_ID_QLOGIC_ISP2312	0x2312
#endif

#define	PCI_DFLT_LTNCY	0x40
#define	PCI_DFLT_LNSZ	0x10
#define	PCI_CMD_ISP	\
 (PCI_COMMAND_MASTER|PCI_COMMAND_INVALIDATE|PCI_COMMAND_PARITY|PCI_COMMAND_SERR)

/*
 * Encapsulating softc... Order of elements is important. The tag
 * pci_isp must come first because of multiple structure punning
 * (Scsi_Host * == struct isp_pcisoftc * == struct ispsofct *).
 */
struct isp_pcisoftc {
    struct ispsoftc	pci_isp;
    struct pci_dev *	pci_dev;
    vm_offset_t		port;	/* I/O port address */
    vm_offset_t		paddr;	/* Physical Memory Address */
    vm_offset_t		vaddr;	/* Mapped Memory Address */
    vm_offset_t		poff[_NREG_BLKS];
    union pstore	params;
#ifdef	LINUX_ISP_TARGET_MODE
    tmd_cmd_t		rpool[NTGT_CMDS];
#endif
};

/*
 * Gratefully borrowed from Gerard Roudier's sym53c8xx driver
 */
static INLINE vm_offset_t
map_pci_mem(u_long base, u_long size)
{
    u_long page_base	= ((u_long) base) & PAGE_MASK;
    u_long page_offs	= ((u_long) base) - page_base;
    u_long map_size =  roundup(page_offs+size, PAGE_SIZE);
    u_long page_remapped = (u_long) ioremap_nocache(page_base, map_size);
    (void) map_size;
    return (vm_offset_t) (page_remapped ? (page_remapped + page_offs) : 0);
}

static INLINE
void unmap_pci_mem(vm_offset_t vaddr, u_long size)
{
    if (vaddr)
	iounmap((void *) (vaddr & PAGE_MASK));
}

static INLINE int 
map_isp_mem(struct isp_pcisoftc *isp_pci, u_short cmd, vm_offset_t mem_base)
{
    if (cmd & PCI_COMMAND_MEMORY) {
	isp_pci->paddr = __pa(mem_base);
	isp_pci->paddr &= PCI_BASE_ADDRESS_MEM_MASK;
	isp_pci->vaddr = map_pci_mem(isp_pci->paddr, 0xff);
	return (isp_pci->vaddr != (vm_offset_t) 0);
    }
    return (0);
}

static INLINE int 
map_isp_io(struct isp_pcisoftc *isp_pci, u_short cmd, vm_offset_t io_base)
{
    if ((cmd & PCI_COMMAND_IO) && (io_base & 3) == 1) {
   	isp_pci->port = io_base & PCI_BASE_ADDRESS_IO_MASK;
	if (check_region(isp_pci->port, 0xff)) {
	    return (0);
	}
	request_region(isp_pci->port, 0xff, "isp");
	return (1);
    }
    return (0);
}

#define	ISP_PCI_BUS	pcidev->bus->number
#define	ISP_PCI_DEVICE	pcidev->devfn
#define	ISEARCH_RESET	pcidev = NULL
#define	ISEARCH(x)	\
  (pcidev = pci_find_device(PCI_VENDOR_ID_QLOGIC, x, pcidev)) != NULL
#define	ISEARCH_NEXT
#define	ISTORE_ARGS		struct pci_dev *pcidev
#define	ISTORE_FNDARGS		ISTORE_ARGS
#define	ISTORE_FNCARGS		pcidev
#define	ISTORE_ISP_INFO(x)	(x)->pci_dev = pcidev

static INLINE struct isp_pcisoftc *
isplinux_pci_addhost(Scsi_Host_Template *tmpt, ISTORE_FNDARGS)
{
    struct Scsi_Host *host;
    struct ispsoftc *isp;
    struct isp_pcisoftc *pci_isp;

    host = scsi_register(tmpt, sizeof(struct isp_pcisoftc));
    if (host == NULL) {
	printk("isp_detect: scsi_register failed\n");
	return (NULL);
    }
    pci_isp = (struct isp_pcisoftc *) host->hostdata;
    if (pci_isp == NULL) {
	printk("isp_detect: cannot get softc out of scsi_register\n");
	return (NULL);
    }
    ISTORE_ISP_INFO(pci_isp);
    isp = (struct ispsoftc *) pci_isp;
    isp->isp_host = host;
    isp->isp_osinfo.storep = &pci_isp->params;
    if (isplinux_pci_init(host)) {
	scsi_unregister(host);
	return (NULL);
    }
    isp->isp_next = isplist;
    isplist = isp;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,4)
    scsi_set_pci_device(host, pci_isp->pci_dev);
#endif
    return (pci_isp);
}

int
isplinux_pci_detect(Scsi_Host_Template *tmpt)
{
    static const char *fmt =
	"ISP SCSI and Fibre Channel Host Adapter Driver\n"
	"      Linux Platform Version %d.%d\n"
	"      Common Core Code Version %d.%d\n"
	"      Built on %s, %s\n";
    int nfound = 0;
    struct isp_pcisoftc *pci_isp;
    ISTORE_ARGS;

    if (pci_present() == 0) {
	return (0);
    }

    printk(fmt, ISP_PLATFORM_VERSION_MAJOR, ISP_PLATFORM_VERSION_MINOR,
	ISP_CORE_VERSION_MAJOR, ISP_CORE_VERSION_MINOR, __DATE__ , __TIME__ );

#ifndef	ISP_DISABLE_1020_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP1020); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif

#ifndef	ISP_DISABLE_1080_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP1240); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP1080); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP1280); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif

#ifndef	ISP_DISABLE_12160_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP12160); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif

#ifndef	ISP_DISABLE_2100_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP2100); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif

#ifndef	ISP_DISABLE_2200_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP2200); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif

#ifndef	ISP_DISABLE_2300_SUPPORT
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP2300); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
    for (ISEARCH_RESET; ISEARCH(PCI_DEVICE_ID_QLOGIC_ISP2312); ISEARCH_NEXT) {
	pci_isp = isplinux_pci_addhost(tmpt, ISTORE_FNCARGS);
        if (pci_isp) {
		nfound++;
	}
    }
#endif
    return (nfound);
}

void
isplinux_pci_release(struct Scsi_Host *host)
{
    struct ispsoftc *isp = (struct ispsoftc *) host->hostdata;
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) host->hostdata;
    free_irq(host->irq, pcs);
    if (pcs->vaddr != 0) {
	unmap_pci_mem(pcs->vaddr, 0xff);
	pcs->vaddr = 0;
    } else {
	release_region(pcs->port, 0xff);
	pcs->port = 0;
    }
    kfree(isp->isp_xflist);
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    pci_free_consistent(pcs->pci_dev, RQUEST_QUEUE_LEN(isp) * QENTRY_LEN,
	isp->isp_rquest, isp->isp_rquest_dma);
    pci_free_consistent(pcs->pci_dev, RESULT_QUEUE_LEN(isp) * QENTRY_LEN,
	isp->isp_result, isp->isp_result_dma);
    if (IS_FC(isp)) {
	pci_free_consistent(pcs->pci_dev, ISP2100_SCRLEN,
	    FCPARAM(isp)->isp_scratch, FCPARAM(isp)->isp_scdma);
    }
#else
    RlsPages(isp->isp_rquest, IspOrder(RQUEST_QUEUE_LEN(isp)));
    RlsPages(isp->isp_result, IspOrder(RESULT_QUEUE_LEN(isp)));
    if (IS_FC(isp) && FCPARAM(isp)->isp_scratch) {
	RlsPages(FCPARAM(isp)->isp_scratch, 1);
    }
#endif
}

static int
isplinux_pci_init(struct Scsi_Host *host)
{
    static char *nomap = "cannot map either memory or I/O space";
    unsigned long io_base, mem_base;
    unsigned int irq, pci_cmd_isp = PCI_CMD_ISP;
    struct isp_pcisoftc *isp_pci;
    u_char rev, lnsz, timer;
    u_short vid, did, cmd;
    char loc[32];
    struct ispsoftc *isp;

    isp_pci = (struct isp_pcisoftc *) host->hostdata;
    isp = (struct ispsoftc *) isp_pci;
    sprintf(loc, "isp@<PCI%d,Slot%d,Func%d>", isp_pci->pci_dev->bus->number,
	PCI_SLOT(isp_pci->pci_dev->devfn), PCI_FUNC(isp_pci->pci_dev->devfn));
    if (PRDW(isp_pci, PCI_COMMAND, &cmd) ||
	PRDB(isp_pci, PCI_CACHE_LINE_SIZE, &lnsz) ||
	PRDB(isp_pci, PCI_LATENCY_TIMER, &timer) ||
	PRDB(isp_pci, PCI_CLASS_REVISION, &rev)) {
	printk("%s: error reading PCI configuration", loc);
	return (1);
    }
    vid = isp_pci->pci_dev->vendor;
    did = isp_pci->pci_dev->device;

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    io_base = pci_resource_start(isp_pci->pci_dev, 0);
    if (pci_resource_flags(isp_pci->pci_dev, 0) & PCI_BASE_ADDRESS_MEM_TYPE_64)
	irq = 2;
    else
	irq = 1;
    mem_base = pci_resource_start(isp_pci->pci_dev, irq);
    if (pci_resource_flags(isp_pci->pci_dev, irq) &
	PCI_BASE_ADDRESS_MEM_TYPE_64) {
#if	BITS_PER_LONG == 64
	mem_base |= pci_resource_start(isp_pci->pci_dev, irq+1) << 32;
#else
	isp_pci_mapmem &= ~(1 << isp->isp_unit);
#endif
    }
#else	/* LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) */
    io_base = isp_pci->pci_dev->base_address[0];
    mem_base = isp_pci->pci_dev->base_address[1];
    if (mem_base & PCI_BASE_ADDRESS_MEM_TYPE_64) {
#if	BITS_PER_LONG == 64
	mem_base |= isp_pci->pci_dev->base_address[2] << 32;
#else
	isp_pci_mapmem &= ~(1 << isp->isp_unit);
#endif
    }
#endif	/* LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) */
    irq = isp_pci->pci_dev->irq;

    if (vid != PCI_VENDOR_ID_QLOGIC) {
	printk("%s: 0x%04x is not QLogic's PCI Vendor ID\n", loc, vid);
	return (1);
    }

    isp_pci->poff[BIU_BLOCK >> _BLK_REG_SHFT] = BIU_REGS_OFF;
    isp_pci->poff[MBOX_BLOCK >> _BLK_REG_SHFT] = PCI_MBOX_REGS_OFF;
    isp_pci->poff[SXP_BLOCK >> _BLK_REG_SHFT] = PCI_SXP_REGS_OFF;
    isp_pci->poff[RISC_BLOCK >> _BLK_REG_SHFT] = PCI_RISC_REGS_OFF;
    isp_pci->poff[DMA_BLOCK >> _BLK_REG_SHFT] = DMA_REGS_OFF;
    switch (did) {
    case PCI_DEVICE_ID_QLOGIC_ISP1020:
	break;
    case PCI_DEVICE_ID_QLOGIC_ISP1080:
    case PCI_DEVICE_ID_QLOGIC_ISP1240:
    case PCI_DEVICE_ID_QLOGIC_ISP1280:
    case PCI_DEVICE_ID_QLOGIC_ISP12160:
	isp_pci->poff[DMA_BLOCK >> _BLK_REG_SHFT] = ISP1080_DMA_REGS_OFF;
	break;
    case PCI_DEVICE_ID_QLOGIC_ISP2200:
    case PCI_DEVICE_ID_QLOGIC_ISP2100:
	isp_pci->poff[MBOX_BLOCK >> _BLK_REG_SHFT] = PCI_MBOX_REGS2100_OFF;
	break;
    case PCI_DEVICE_ID_QLOGIC_ISP2300:
	pci_cmd_isp &= ~PCI_COMMAND_INVALIDATE;	/* per errata */
	isp_pci->poff[MBOX_BLOCK >> _BLK_REG_SHFT] = PCI_MBOX_REGS2300_OFF;
	break;
    case PCI_DEVICE_ID_QLOGIC_ISP2312:
	isp->isp_port = PCI_FUNC(isp_pci->pci_dev->devfn);
	isp_pci->poff[MBOX_BLOCK >> _BLK_REG_SHFT] = PCI_MBOX_REGS2300_OFF;
	break;
    default:
	printk("%s: Device ID 0x%04x is not a known Qlogic Device", loc, did);
	return (1);
    }

    /*
     * Bump unit seed- we're here, whether we complete the attachment or not.
     */
    isp->isp_unit = isp_unit_seed++;
    sprintf(isp->isp_name, "isp%d", isp->isp_unit);

    isp->isp_osinfo.device_id =
	((isp_pci->pci_dev->bus->number) << 16)		|
        (PCI_SLOT(isp_pci->pci_dev->devfn) << 8)	|
	(PCI_FUNC(isp_pci->pci_dev->devfn));

    if (isp_disable & (1 << isp->isp_unit)) {
	isp_prt(isp, ISP_LOGALL, "disabled at user request");
	return (1);
    }

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    if (pci_enable_device(isp_pci->pci_dev)) {
	printk("%s: fails to be PCI_ENABLEd\n", loc);
	return (1);
    }
    (void) PRDW(isp_pci, PCI_COMMAND, &cmd);
#endif

    if ((cmd & PCI_CMD_ISP) != pci_cmd_isp) {
	if (isp_debug & ISP_LOGINFO)
	    printk("%s: rewriting command register from 0x%x to 0x%x\n",
		loc, cmd, (cmd & ~PCI_CMD_ISP) | pci_cmd_isp);
	cmd &= ~PCI_CMD_ISP;
	cmd |= pci_cmd_isp;
	PWRW(isp_pci, PCI_COMMAND, cmd);
    }

    if (lnsz != PCI_DFLT_LNSZ) {
	if (isp_debug & ISP_LOGINFO)
	    printk("%s: rewriting cache line size from 0x%x to 0x%x\n",
		loc, lnsz, PCI_DFLT_LNSZ);
	lnsz = PCI_DFLT_LNSZ;
	PWRB(isp_pci, PCI_CACHE_LINE_SIZE, lnsz);
    }

#ifdef	__sparc__
    if (PRDB(isp_pci, PCI_MIN_GNT, &rev)) {
	printk("%s: unable to read min grant\n", loc);
	return (1);
    }
    if (rev) {
	rev = (rev << 3) & 0xff;
    }
    if (rev == 0) {
	rev = 64;
    }
    if (isp_debug & ISP_LOGINFO) {
	printk("%s: rewriting latency timer from 0x%x to 0x%x\n",
	    loc, timer, rev);
    }
    PWRB(isp_pci, PCI_LATENCY_TIMER, rev);
#else
    if (timer < PCI_DFLT_LTNCY) {
	if (isp_debug & ISP_LOGINFO)
	    printk("%s: rewriting latency timer from 0x%x to 0x%x\n",
		loc, timer, PCI_DFLT_LTNCY);
	timer = PCI_DFLT_LTNCY;
	PWRB(isp_pci, PCI_LATENCY_TIMER, timer);
    }
#endif

    if ((cmd & (PCI_COMMAND_MEMORY|PCI_COMMAND_IO)) == 0) {
#ifdef	__powerpc__
	if (io_base == 0 && mem_base == 0) {
	    printk("%s: you lose- no register access defined.\n", loc);
	    return (1);
	}
	if (io_base)
		cmd |= PCI_COMMAND_IO;
	if (mem_base)
		cmd |= PCI_COMMAND_MEMORY;
	PWRW(isp_pci, PCI_COMMAND, cmd);
#else
	printk("%s: you lose- no register access defined.\n", loc);
	return (1);
#endif
    }

    /*
     * Disable the ROM.
     */
    PWRL(isp_pci, PCI_ROM_ADDRESS, 0);

    /*
     * Set up stuff...
     */
    isp_pci->port = isp_pci->vaddr = 0;

    /*
     * If we prefer to map memory space over I/O, try that first.
     */
    if (isp_pci_mapmem & (1 << isp->isp_unit)) {
	if (map_isp_mem(isp_pci, cmd, mem_base) == 0) {
	    if (map_isp_io(isp_pci, cmd, io_base) == 0) {
		isp_prt(isp, ISP_LOGERR, nomap);
		return (1);
	    }
	}
    } else {
	if (map_isp_io(isp_pci, cmd, io_base) == 0) {
	    if (map_isp_mem(isp_pci, cmd, mem_base) == 0) {
		isp_prt(isp, ISP_LOGERR, nomap);
		return (1);
	    }
	}
    }
    if (isp_pci->vaddr) {
	isp_prt(isp, ISP_LOGCONFIG,
	    "mapped memory 0x%lx at 0x%lx\n", isp_pci->paddr, isp_pci->vaddr);
	host->io_port = isp_pci->paddr;
    } else {
        isp_prt(isp, ISP_LOGCONFIG,
	    "mapped I/O space at 0x%lx\n", isp_pci->port);
	host->io_port = isp_pci->port;
    }
    host->irq = 0;
    isp_pci->pci_isp.isp_revision = rev;
#ifndef	ISP_DISABLE_1020_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP1020) {
	isp_pci->pci_isp.isp_mdvec = &mdvec;
	isp_pci->pci_isp.isp_type = ISP_HA_SCSI_UNKNOWN;
    } 
#endif
#ifndef	ISP_DISABLE_1080_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP1080) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_1080;
	isp_pci->pci_isp.isp_type = ISP_HA_SCSI_1080;
    }
    if (did == PCI_DEVICE_ID_QLOGIC_ISP1240) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_1080;
	isp_pci->pci_isp.isp_type = ISP_HA_SCSI_1240;
	host->max_channel = 1;
    }
    if (did == PCI_DEVICE_ID_QLOGIC_ISP1280) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_1080;
	isp_pci->pci_isp.isp_type = ISP_HA_SCSI_1280;
	host->max_channel = 1;
    }
#endif
#ifndef	ISP_DISABLE_12160_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP12160) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_12160;
	isp_pci->pci_isp.isp_type = ISP_HA_SCSI_12160;
	host->max_channel = 1;
    }
#endif
#ifndef	ISP_DISABLE_2100_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP2100) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_2100;
	isp_pci->pci_isp.isp_type = ISP_HA_FC_2100;
    }
#endif
#ifndef	ISP_DISABLE_2200_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP2200) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_2200;
	isp_pci->pci_isp.isp_type = ISP_HA_FC_2200;
    }
#endif
#ifndef	ISP_DISABLE_2300_SUPPORT
    if (did == PCI_DEVICE_ID_QLOGIC_ISP2300) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_2300;
	isp_pci->pci_isp.isp_type = ISP_HA_FC_2300;
    }
    if (did == PCI_DEVICE_ID_QLOGIC_ISP2312) {
	isp_pci->pci_isp.isp_mdvec = &mdvec_2300;
	isp_pci->pci_isp.isp_type = ISP_HA_FC_2312;
    }
#endif

    if (request_irq(irq, isplinux_intr, SA_SHIRQ, isp->isp_name, isp_pci)) {
	isp_prt(isp, ISP_LOGERR, "could not snag irq %u (0x%x)", irq, irq);
	goto bad;
    }
    host->irq = irq;
    host->select_queue_depths = isplinux_sqd;
    isp->isp_param = &isp_pci->params;
#ifdef	LINUX_ISP_TARGET_MODE
    isp->isp_osinfo.pool = isp_pci->rpool;
#endif
    /*
     * At this point, we're committed to keeping this adapter around.
     */
    isplinux_common_init(isp);
    return (0);
bad:
    if (host->irq) {
	DISABLE_INTS(isp);
	free_irq(host->irq, isp_pci);
	host->irq = 0;
    }
    if (isp_pci->vaddr != 0) {
	unmap_pci_mem(isp_pci->vaddr, 0xff);
	isp_pci->vaddr = 0;
    } else {
	release_region(isp_pci->port, 0xff);
	isp_pci->port = 0;
    }
    return (1);
}

static INLINE u_int16_t
ispregrd(struct isp_pcisoftc *pcs, vm_offset_t offset)
{
    u_int16_t rv;
    if (pcs->vaddr) {
	offset += pcs->vaddr;
	rv = readw(offset);
    } else {
	offset += pcs->port;
	rv = inw(offset);
    }
    return (rv);
}

static INLINE void
ispregwr(struct isp_pcisoftc *pcs, vm_offset_t offset, u_int16_t val)
{
    if (pcs->vaddr) {
	offset += pcs->vaddr;
	writew(val, offset);
    } else {
	offset += pcs->port;
	outw(val, offset);
    }
    MEMORYBARRIER(isp, SYNC_REG, offset, 2);
}

static INLINE int
isp_pci_rd_debounced(struct isp_pcisoftc *pcs, vm_offset_t off, u_int16_t *rp)
{
    u_int16_t val0, val1;
    int i = 0;
    do {
	val0 = ispregrd(pcs, off);
	val1 = ispregrd(pcs, off);
    } while (val0 != val1 && ++i < 1000);
    if (val0 != val1) {
	return (1);
    }
    *rp = val0;
    return (0);
}

#define	IspVirt2Off(a, x)	\
	((a)->poff[((x) & _BLK_REG_MASK) >> _BLK_REG_SHFT] + ((x) & 0xff))

static int
isp_pci_rd_isr(struct ispsoftc *isp, u_int16_t *isrp,
    u_int16_t *semap, u_int16_t *mbp)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
    u_int16_t isr, sema;

    if (IS_2100(isp)) {
	if (isp_pci_rd_debounced(pcs, IspVirt2Off(pcs, BIU_ISR), &isr)) {
	    return (0);
        }
	if (isp_pci_rd_debounced(pcs, IspVirt2Off(pcs, BIU_SEMA), &sema)) {
	    return (0);
        }
    } else {
	isr = ispregrd(pcs, IspVirt2Off(pcs, BIU_ISR));
	sema = ispregrd(pcs, IspVirt2Off(pcs, BIU_SEMA));
    }
    isp_prt(isp, ISP_LOGDEBUG3, "ISR 0x%x SEMA 0x%x", isr, sema);
    isr &= INT_PENDING_MASK(isp);
    sema &= BIU_SEMA_LOCK;
    if (isr == 0 && sema == 0) {
	return (0);
    }
    *isrp = isr;
    if ((*semap = sema) != 0) {
	if (IS_2100(isp)) {
	    if (isp_pci_rd_debounced(pcs, IspVirt2Off(pcs, OUTMAILBOX0), mbp)) {
		return (0);
	    }
	} else {
	    *mbp = ispregrd(pcs, IspVirt2Off(pcs, OUTMAILBOX0));
	}
    }
    return (1);
}

#ifndef	ISP_DISABLE_2300_SUPPORT
static INLINE u_int32_t
ispregrd32(struct isp_pcisoftc *pcs, vm_offset_t offset)
{
    u_int32_t rv;
    if (pcs->vaddr) {
	offset += pcs->vaddr;
	rv = readl(offset);
    } else {
	offset += pcs->port;
	rv = inl(offset);
    }
    return (rv);
}

static int
isp_pci_rd_isr_2300(struct ispsoftc *isp, u_int16_t *isrp,
    u_int16_t *semap, u_int16_t *mbox0p)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
    u_int32_t r2hisr;

   /*
    * Avoid parity errors on the 2312.
    */
    if (!(ispregrd(pcs, IspVirt2Off(pcs, BIU_ISR)) & BIU2100_ISR_RISC_INT)) {
	*isrp = 0;
	return (0);
    }

    r2hisr = ispregrd32(pcs, IspVirt2Off(pcs, BIU_R2HSTSLO));
    isp_prt(isp, ISP_LOGDEBUG3, "RISC2HOST ISR 0x%x", r2hisr);
    if ((r2hisr & BIU_R2HST_INTR) == 0) {
	*isrp = 0;
	return (0);
    }
    switch (r2hisr & BIU_R2HST_ISTAT_MASK) {
    case ISPR2HST_ROM_MBX_OK:
    case ISPR2HST_ROM_MBX_FAIL:
    case ISPR2HST_MBX_OK:
    case ISPR2HST_MBX_FAIL:
    case ISPR2HST_ASYNC_EVENT:
    case ISPR2HST_RIO_16:
    case ISPR2HST_FPOST:
    case ISPR2HST_FPOST_CTIO:
	*isrp = r2hisr & 0xffff;
	*mbox0p = (r2hisr >> 16);
	*semap = 1;
	return (1);
    case ISPR2HST_RSPQ_UPDATE:
	*isrp = r2hisr & 0xffff;
	*mbox0p = 0;
	*semap = 0;
	return (1);
   default:
	return (0);
   }
}
#endif

static u_int16_t
isp_pci_rd_reg(struct ispsoftc *isp, int regoff)
{
    u_int16_t rv, oldconf = 0;
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;

    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK) {
	/*
	 * We will assume that someone has paused the RISC processor.
	 */
	oldconf = ispregrd(pcs, IspVirt2Off(pcs, BIU_CONF1));
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf | BIU_PCI_CONF1_SXP);
    }
    rv = ispregrd(pcs, IspVirt2Off(pcs, regoff));
    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK) {
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf);
    }
    return (rv);
}

static void
isp_pci_wr_reg(struct ispsoftc *isp, int regoff, u_int16_t val)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
    u_int16_t oldconf = 0;
    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK) {
	/*
	 * We will assume that someone has paused the RISC processor.
	 */
	oldconf = ispregrd(pcs, IspVirt2Off(pcs, BIU_CONF1));
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf | BIU_PCI_CONF1_SXP);
    }
    ispregwr(pcs, IspVirt2Off(pcs, regoff), val);
    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK) {
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf);
    }
}

#if !(defined(ISP_DISABLE_1080_SUPPORT) && defined(ISP_DISABLE_12160_SUPPORT))
static u_int16_t
isp_pci_rd_reg_1080(struct ispsoftc *isp, int regoff)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
    u_int16_t rv, oldconf = 0;

    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK ||
	(regoff & _BLK_REG_MASK) == (SXP_BLOCK|SXP_BANK1_SELECT)) {
	u_int16_t tmpconf;
	/*
	 * We will assume that someone has paused the RISC processor.
	 */
	oldconf = ispregrd(pcs,  IspVirt2Off(pcs, BIU_CONF1));
	tmpconf = oldconf & ~BIU_PCI1080_CONF1_DMA;
	if (IS_1280(isp)) {
	    if (regoff & SXP_BANK1_SELECT) {
		tmpconf |= BIU_PCI1080_CONF1_SXP0;
	    } else {
		tmpconf |= BIU_PCI1080_CONF1_SXP1;
	    }
	} else {
	    tmpconf |= BIU_PCI1080_CONF1_SXP0;
	}
	ispregwr(pcs,  IspVirt2Off(pcs, BIU_CONF1), tmpconf);
    } else if ((regoff & _BLK_REG_MASK) == DMA_BLOCK) {
	oldconf = ispregrd(pcs,  IspVirt2Off(pcs, BIU_CONF1));
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1),
	    oldconf | BIU_PCI1080_CONF1_DMA);
    }
    rv = ispregrd(pcs, IspVirt2Off(pcs, regoff));
    if (oldconf) {
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf);
    }
    return (rv);
}

static void
isp_pci_wr_reg_1080(struct ispsoftc *isp, int regoff, u_int16_t val)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
    u_int16_t oldconf = 0;

    if ((regoff & _BLK_REG_MASK) == SXP_BLOCK ||
	(regoff & _BLK_REG_MASK) == (SXP_BLOCK|SXP_BANK1_SELECT)) {
	u_int16_t tmpconf;
	/*
	 * We will assume that someone has paused the RISC processor.
	 */
	oldconf = ispregrd(pcs,  IspVirt2Off(pcs, BIU_CONF1));
	tmpconf = oldconf & ~BIU_PCI1080_CONF1_DMA;
	if (IS_1280(isp)) {
	    if (regoff & SXP_BANK1_SELECT) {
		tmpconf |= BIU_PCI1080_CONF1_SXP0;
	    } else {
		tmpconf |= BIU_PCI1080_CONF1_SXP1;
	    }
	} else {
	    tmpconf |= BIU_PCI1080_CONF1_SXP0;
	}
	ispregwr(pcs,  IspVirt2Off(pcs, BIU_CONF1), tmpconf);
    } else if ((regoff & _BLK_REG_MASK) == DMA_BLOCK) {
	oldconf = ispregrd(pcs,  IspVirt2Off(pcs, BIU_CONF1));
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1),
	    oldconf | BIU_PCI1080_CONF1_DMA);
    }
    ispregwr(pcs, IspVirt2Off(pcs, regoff), val);
    if (oldconf) {
	ispregwr(pcs, IspVirt2Off(pcs, BIU_CONF1), oldconf);
    }
}
#endif

static int
isp_pci_mbxdma(struct ispsoftc *isp)
{
    if (isp->isp_xflist == NULL) {
	size_t amt = isp->isp_maxcmds * sizeof (XS_T **);
	isp->isp_xflist = kmalloc(amt, GFP_KERNEL);
	if (isp->isp_xflist == NULL) {
	    isp_prt(isp, ISP_LOGERR, "unable to allocate xflist array");
	    return (1);
	}
	MEMZERO(isp->isp_xflist, amt);
    }
    if (isp->isp_rquest == NULL) {
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	dma_addr_t busaddr;
	isp->isp_rquest =
	    pci_alloc_consistent(pcs->pci_dev,
		RQUEST_QUEUE_LEN(isp) * QENTRY_LEN, &busaddr);
	if (isp->isp_rquest == NULL) {
	    isp_prt(isp, ISP_LOGERR, "unable to allocate request queue");
	    return (1);
	}
	isp->isp_rquest_dma = busaddr;
#else
	isp->isp_rquest = (caddr_t) GetPages(IspOrder(RQUEST_QUEUE_LEN(isp)));
	if (isp->isp_rquest == NULL) {
	    isp_prt(isp, ISP_LOGERR, "unable to allocate request queue");
	    return (1);
	}
	/*
	 * Map the Request queue.
	 */
	isp->isp_rquest_dma = virt_to_bus(isp->isp_rquest);
#endif
	if (isp->isp_rquest_dma & 0x3f) {
	    isp_prt(isp, ISP_LOGERR, "Request Queue not on 64 byte boundary");
	    return (1);
        }
	MEMZERO(isp->isp_rquest, ISP_QUEUE_SIZE(RQUEST_QUEUE_LEN(isp)));
    }

    if (isp->isp_result == NULL) {
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	dma_addr_t busaddr;
	isp->isp_result =
	    pci_alloc_consistent(pcs->pci_dev,
		RESULT_QUEUE_LEN(isp) * QENTRY_LEN, &busaddr);
	if (isp->isp_result == NULL) {
	    isp_prt(isp, ISP_LOGERR, "unable to allocate result queue");
	    return (1);
	}
	isp->isp_result_dma = busaddr;
#else
	isp->isp_result = (caddr_t) GetPages(IspOrder(RESULT_QUEUE_LEN(isp)));
	if (isp->isp_result == NULL) {
	    isp_prt(isp, ISP_LOGERR, "unable to allocate result queue");
	    free_pages((unsigned long)isp->isp_rquest,
		IspOrder(RQUEST_QUEUE_LEN(isp)));
	    return (1);
	}
	/*
	 * Map the result queue.
	 */
	isp->isp_result_dma = virt_to_bus(isp->isp_result);
#endif
	if (isp->isp_rquest_dma & 0x3f) {
	    isp_prt(isp, ISP_LOGERR, "Result Queue not on 64 byte boundary");
	    return (1);
        }
	MEMZERO(isp->isp_result, ISP_QUEUE_SIZE(RESULT_QUEUE_LEN(isp)));
    }

    if (IS_FC(isp)) {
	fcparam *fcp = isp->isp_param;
	if (fcp->isp_scratch == NULL) {
#if	LINUX_VERSION_CODE > KERNEL_VERSION(2,3,92)
	    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	    dma_addr_t busaddr;
	    fcp->isp_scratch =
		pci_alloc_consistent(pcs->pci_dev, ISP2100_SCRLEN, &busaddr);
	    if (fcp->isp_scratch == NULL) {
		isp_prt(isp, ISP_LOGERR, "unable to allocate scratch space");
		return (1);
	    }
	    fcp->isp_scdma = busaddr;
#else
	   /*
	    * Just get a page....
	    */
	    fcp->isp_scratch = (void *) GetPages(1);
	    if (fcp->isp_scratch == NULL) {
		isp_prt(isp, ISP_LOGERR, "unable to allocate scratch space");
		return (1);
	    }
	    fcp->isp_scdma = virt_to_bus((void *)fcp->isp_scratch);
#endif
	    MEMZERO(fcp->isp_scratch, ISP2100_SCRLEN);
	    if (isp->isp_rquest_dma & 0x7) {
		isp_prt(isp, ISP_LOGERR, "scratch space not 8 byte aligned");
		return (1);
	    }
	}
    }
    return (0);
}

#ifdef	LINUX_ISP_TARGET_MODE
/*
 * We need to handle DMA for target mode differently from initiator mode.
 * 
 * DMA mapping and construction and submission of CTIO Request Entries
 * and rendevous for completion are very tightly coupled because we start
 * out by knowing (per platform) how much data we have to move, but we
 * don't know, up front, how many DMA mapping segments will have to be used
 * cover that data, so we don't know how many CTIO Request Entries we
 * will end up using. Further, for performance reasons we may want to
 * (on the last CTIO for Fibre Channel), send status too (if all went well).
 *
 * The standard vector still goes through isp_pci_dmasetup, but the callback
 * for the DMA mapping routines comes here instead with the whole transfer
 * mapped and a pointer to a partially filled in already allocated request
 * queue entry. We finish the job.
 */
static int tdma_mk(struct ispsoftc *, tmd_cmd_t *, ct_entry_t *,
    u_int16_t *, u_int16_t);
static int tdma_mkfc(struct ispsoftc *, tmd_cmd_t *, ct2_entry_t *,
    u_int16_t *, u_int16_t);

#define	STATUS_WITH_DATA        1
    
static int
tdma_mk(struct ispsoftc *isp, tmd_cmd_t *tcmd, ct_entry_t *cto,
    u_int16_t *nxtip, u_int16_t optr)
{
    static const char ctx[] =
	"CTIO[%x] lun %d for iid%d flgs 0x%x sts 0x%x ssts 0x%x res %u %s";
    struct scatterlist *sg;
    ct_entry_t *qe;
    u_int8_t scsi_status;
    u_int16_t curi, nxti, handle;
    u_int32_t sflags;
    int32_t resid;
    int nth_ctio, nctios, send_status, nseg;


    curi = isp->isp_reqidx;
    qe = (ct_entry_t *) ISP_QUEUE_ENTRY(isp->isp_rquest, isp->isp_reqidx);

    cto->ct_xfrlen = 0;
    cto->ct_seg_count = 0;
    cto->ct_header.rqs_entry_count = 1;
    MEMZERO(cto->ct_dataseg, sizeof (cto->ct_dataseg));

    if (tcmd->cd_xfrlen == 0) {
	ISP_TDQE(isp, "tdma_mk[no data]", curi, cto);
	isp_prt(isp, ISP_LOGTDEBUG1, ctx, cto->ct_fwhandle, (int) tcmd->cd_lun,
	    (int) cto->ct_iid, cto->ct_flags, cto->ct_status,
	    cto->ct_scsi_status, cto->ct_resid, "<END>");
	isp_put_ctio(isp, cto, qe);
	return (CMD_QUEUED);
    }

    sg = tcmd->cd_data;
    nseg = 0;
    resid = (int32_t) tcmd->cd_xfrlen;
    while (resid > 0) {
	nseg++;
	resid -= sg->length;
	sg++;
    }
    sg = tcmd->cd_data;
#if	 LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	int new_seg_cnt;
	new_seg_cnt = pci_map_sg(pcs->pci_dev, sg, nseg,
	  (cto->ct_flags & CT_DATA_IN)? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
	if (new_seg_cnt == 0) {
	    isp_prt(isp, ISP_LOGWARN, "unable to dma map request");
	    cto->ct_resid = -ENOMEM;
	    return (CMD_COMPLETE);
	}
	if (new_seg_cnt != nseg) {
	    isp_prt(isp, ISP_LOGERR, "new seg cnt != old");
	    cto->ct_resid = -EINVAL;
	    return (CMD_COMPLETE);
	}
    }
#endif
    nctios = nseg / ISP_RQDSEG;
    if (nseg % ISP_RQDSEG) {
	nctios++;
    }

    /*
     * Save handle, and potentially any SCSI status, which
     * we'll reinsert on the last CTIO we're going to send.
     */
    handle = cto->ct_syshandle;
    cto->ct_syshandle = 0;
    cto->ct_header.rqs_seqno = 0;
    send_status = (cto->ct_flags & CT_SENDSTATUS) != 0;

    if (send_status) {
	sflags = cto->ct_flags & (CT_SENDSTATUS | CT_CCINCR);
	cto->ct_flags &= ~(CT_SENDSTATUS|CT_CCINCR);
	/*
	 * Preserve residual.
	 */
	resid = cto->ct_resid;

	/*
	 * Save actual SCSI status.
	 */
	scsi_status = cto->ct_scsi_status;

#ifndef	STATUS_WITH_DATA
	sflags |= CT_NO_DATA;
	/*
	 * We can't do a status at the same time as a data CTIO, so
	 * we need to synthesize an extra CTIO at this level.
	 */
	nctios++;
#endif
    } else {
	sflags = scsi_status = resid = 0;
    }

    cto->ct_resid = 0;
    cto->ct_scsi_status = 0;

    nxti = *nxtip;

    for (nth_ctio = 0; nth_ctio < nctios; nth_ctio++) {
	int seglim;

	seglim = nseg;
	if (seglim) {
	    int seg;

	    if (seglim > ISP_RQDSEG)
		seglim = ISP_RQDSEG;

	    for (seg = 0; seg < seglim; seg++, nseg--) {
		/*
		 * Unlike normal initiator commands, we don't do
		 * any swizzling here.
		 */
#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
		cto->ct_dataseg[seg].ds_base = virt_to_bus(sg->address);
#else
		cto->ct_dataseg[seg].ds_base = (u_int32_t) sg_dma_address(sg);
#endif
		cto->ct_dataseg[seg].ds_count = (u_int32_t) sg->length;
		cto->ct_xfrlen += sg->length;
		sg++;
	    }
	    cto->ct_seg_count = seg;
	} else {
	    /*
	     * This case should only happen when we're
	     * sending an extra CTIO with final status.
	     */
	    if (send_status == 0) {
		isp_prt(isp, ISP_LOGERR,
		    "tdma_mk ran out of segments, no status to send");
		return (CMD_EAGAIN);
	    }
	}

	/*
	 * At this point, the fields ct_lun, ct_iid, ct_tagval, ct_tagtype, and
	 * ct_timeout have been carried over unchanged from what our caller had
	 * set.
	 *
	 * The dataseg fields and the seg_count fields we just got through
	 * setting. The data direction we've preserved all along and only
	 * clear it if we're now sending status.
	 */

	if (nth_ctio == nctios - 1) {
	    /*
	     * We're the last in a sequence of CTIOs, so mark this
	     * CTIO and save the handle to the command such that when
	     * this CTIO completes we can free dma resources and
	     * do whatever else we need to do to finish the rest
	     * of the command.
	     */
	    cto->ct_syshandle = handle;
	    cto->ct_header.rqs_seqno = 1;

	    if (send_status) {
		cto->ct_scsi_status = scsi_status;
		cto->ct_flags |= sflags;
		cto->ct_resid = resid;
	    }
	    if (send_status) {
		isp_prt(isp, ISP_LOGTDEBUG1, ctx, 
		    cto->ct_fwhandle, (int) tcmd->cd_lun, (int) cto->ct_iid,
		    cto->ct_flags, cto->ct_status, cto->ct_scsi_status,
		    cto->ct_resid, "<END>");
	    } else {
		isp_prt(isp, ISP_LOGTDEBUG1, ctx, 
		    cto->ct_fwhandle, (int) tcmd->cd_lun, (int) cto->ct_iid,
		    cto->ct_flags, cto->ct_status, cto->ct_scsi_status,
		    cto->ct_resid, "<MID>");
	    }
	    isp_put_ctio(isp, cto, qe);
	    ISP_TDQE(isp, "last tdma_mk", curi, cto);
	    if (nctios > 1) {
		MEMORYBARRIER(isp, SYNC_REQUEST, curi, QENTRY_LEN);
	    }
	} else {
	    ct_entry_t *oqe = qe;

	    /*
	     * Make sure handle fields are clean
	     */
	    cto->ct_syshandle = 0;
	    cto->ct_header.rqs_seqno = 0;

	    isp_prt(isp, ISP_LOGTDEBUG1,
		"CTIO[%x] lun%d for ID%d ct_flags 0x%x",
		cto->ct_fwhandle, (int) tcmd->cd_lun,
		(int) cto->ct_iid, cto->ct_flags);

	    /*
	     * Get a new CTIO
	     */
	    qe = (ct_entry_t *) ISP_QUEUE_ENTRY(isp->isp_rquest, nxti);
	    nxti = ISP_NXT_QENTRY(nxti, RQUEST_QUEUE_LEN(isp));
	    if (nxti == optr) {
		isp_prt(isp, ISP_LOGERR, "queue overflow in tdma_mk");
		return (CMD_EAGAIN);
	    }

	   /*
	    * Now that we're done with the old CTIO,
	    * flush it out to the request queue.
	    */
	    ISP_TDQE(isp, "tdma_mk", curi, cto);
	    isp_put_ctio(isp, cto, oqe);
	    if (nth_ctio != 0) {
		MEMORYBARRIER(isp, SYNC_REQUEST, curi, QENTRY_LEN);
	    }
	    curi = ISP_NXT_QENTRY(curi, RQUEST_QUEUE_LEN(isp));

	    /*
	     * Reset some fields in the CTIO so we can reuse
	     * for the next one we'll flush to the request
	     * queue.
	     */
	    cto->ct_header.rqs_entry_type = RQSTYPE_CTIO;
	    cto->ct_header.rqs_entry_count = 1;
	    cto->ct_header.rqs_flags = 0;
	    cto->ct_status = 0;
	    cto->ct_scsi_status = 0;
	    cto->ct_xfrlen = 0;
	    cto->ct_resid = 0;
	    cto->ct_seg_count = 0;
	    MEMZERO(cto->ct_dataseg, sizeof (cto->ct_dataseg));
	}
    }
    *nxtip = nxti;
    return (CMD_QUEUED);
}

static int
tdma_mkfc(struct ispsoftc *isp, tmd_cmd_t *tcmd, ct2_entry_t *cto,
    u_int16_t *nxtip, u_int16_t optr)
{
    static const char ctx[] = 
	"CTIO2[%x] lun %d for iid %d flgs 0x%x sts 0x%x ssts 0x%x res %d %s";
    u_int8_t sense[QLTM_SENSELEN];
    struct scatterlist *sg;
    ct2_entry_t *qe;
    u_int16_t send_status, scsi_status, send_sense, handle;
    u_int16_t curi, nxti;
    int32_t resid;
    int nth_ctio, nctios, nseg;

    curi = isp->isp_reqidx;
    qe = (ct2_entry_t *) ISP_QUEUE_ENTRY(isp->isp_rquest, curi);

    if (tcmd->cd_xfrlen == 0) {
	if ((cto->ct_flags & CT2_FLAG_MMASK) != CT2_FLAG_MODE1) {
	    isp_prt(isp, ISP_LOGERR,
		"tdma_mkfc, a status CTIO2 without MODE1 set (0x%x)",
		cto->ct_flags);
	    cto->ct_resid = -EINVAL;
	    return (CMD_COMPLETE);
	}
	cto->ct_header.rqs_entry_count = 1;
	cto->ct_header.rqs_seqno = 1;

	/* ct_syshandle contains the synchronization handle set by caller */
	/*
	 * We preserve ct_lun, ct_iid, ct_rxid. We set the data movement
	 * flags to NO DATA and clear relative offset flags. We preserve
	 * ct_resid and the response area.
	 */
	cto->ct_flags |= CT2_NO_DATA;
	if (cto->ct_resid > 0)
	    cto->rsp.m1.ct_scsi_status |= CT2_DATA_UNDER;
	else if (cto->ct_resid < 0)
	    cto->rsp.m1.ct_scsi_status |= CT2_DATA_OVER;
	cto->ct_seg_count = 0;
	cto->ct_reloff = 0;
	isp_prt(isp, ISP_LOGTDEBUG1, ctx, cto->ct_rxid, (int) tcmd->cd_lun,
	    cto->ct_iid, cto->ct_flags, cto->ct_status,
	    cto->rsp.m1.ct_scsi_status, cto->ct_resid, "<END>");
	isp_put_ctio2(isp, cto, qe);
	ISP_TDQE(isp, "tdma_mkfc[no data]", curi, qe);
	return (CMD_QUEUED);
    }

    if ((cto->ct_flags & CT2_FLAG_MMASK) != CT2_FLAG_MODE0) {
	isp_prt(isp, ISP_LOGERR,
	    "tdma_mkfc, a data CTIO2 without MODE0 set (0x%x)", cto->ct_flags);
	cto->ct_resid = -EINVAL;
	return (CMD_COMPLETE);
    }

    sg = tcmd->cd_data;
    nseg = 0;
    resid = (int32_t) tcmd->cd_xfrlen;
    while (resid > 0) {
	nseg++;
	resid -= sg->length;
	sg++;
    }
    sg = tcmd->cd_data;
#if	 LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	int new_seg_cnt;
	new_seg_cnt = pci_map_sg(pcs->pci_dev, sg, nseg,
	  (cto->ct_flags & CT2_DATA_IN)? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
	if (new_seg_cnt == 0) {
	    isp_prt(isp, ISP_LOGWARN, "unable to dma map request");
	    cto->ct_resid = -ENOMEM;
	    return (CMD_COMPLETE);
	}
	if (new_seg_cnt != nseg) {
	    isp_prt(isp, ISP_LOGERR, "new seg cnt != old");
	    cto->ct_resid = -EINVAL;
	    return (CMD_COMPLETE);
	}
    }
#endif
    nctios = nseg / ISP_RQDSEG_T2;
    if (nseg % ISP_RQDSEG_T2) {
	nctios++;
    }

    /*
     * Save the handle, status, reloff, and residual. We'll reinsert the
     * handle into the last CTIO2 we're going to send, and reinsert status
     * and residual (and possibly sense data) if that's to be sent as well.
     *
     * We preserve ct_reloff and adjust it for each data CTIO2 we send past
     * the first one. This is needed so that the FCP DATA IUs being sent out
     * have the correct offset (they can arrive at the other end out of order).
     */

    handle = cto->ct_syshandle;
    cto->ct_syshandle = 0;
    send_status = (cto->ct_flags & CT2_SENDSTATUS) != 0;

    if (send_status) {
	cto->ct_flags &= ~(CT2_SENDSTATUS|CT2_CCINCR);

	/*
	 * Preserve residual.
	 */
	resid = cto->ct_resid;

	/*
	 * Save actual SCSI status. We'll reinsert the CT2_SNSLEN_VALID
	 * later if appropriate.
	 */
	scsi_status = cto->rsp.m0.ct_scsi_status & 0xff;
	send_sense = cto->rsp.m0.ct_scsi_status & CT2_SNSLEN_VALID;

	/*
	 * If we're sending status and have a CHECK CONDTION and
	 * have sense data,  we send one more CTIO2 with just the
	 * status and sense data. The upper layers have stashed
	 * the sense data in the dataseg structure for us.
	 */

	if ((scsi_status & 0xf) == SCSI_CHECK && send_sense) {
	    MEMCPY(sense, cto->rsp.m0.ct_dataseg, QLTM_SENSELEN);
	    nctios++;
	}
    } else {
	scsi_status = send_sense = resid = 0;
    }

    cto->ct_resid = 0;
    cto->rsp.m0.ct_scsi_status = 0;
    MEMZERO(&cto->rsp, sizeof (cto->rsp));

    nxti = *nxtip;

    for (nth_ctio = 0; nth_ctio < nctios; nth_ctio++) {
	u_int32_t oxfrlen;
	int seglim;

	seglim = nseg;
	if (seglim) {
	    int seg;

	    if (seglim > ISP_RQDSEG_T2)
		seglim = ISP_RQDSEG_T2;

	    for (seg = 0; seg < seglim; seg++, nseg--)  {
		/*
		 * Unlike normal initiator commands, we don't do
		 * any swizzling here.
		 */
#if	LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
		cto->rsp.m0.ct_dataseg[seg].ds_base = virt_to_bus(sg->address);
#else
		cto->rsp.m0.ct_dataseg[seg].ds_base =
		    (u_int32_t) sg_dma_address(sg);
#endif
		cto->rsp.m0.ct_dataseg[seg].ds_count = (u_int32_t) sg->length;
		cto->rsp.m0.ct_xfrlen += sg->length;
		sg++;
	    }
	    cto->ct_seg_count = seg;
	    oxfrlen = cto->rsp.m0.ct_xfrlen;
	} else {
	    /*
	     * This case should only happen when we're sending a synthesized
	     * MODE1 final status with sense data.
	     */
	    if (send_sense == 0) {
		isp_prt(isp, ISP_LOGERR,
		    "tdma_mkfc ran out of segments, no SENSE DATA");
		cto->ct_resid = -EINVAL;
		return (CMD_COMPLETE);
	    }
	    oxfrlen = 0;
	}

	/*
	 * At this point, the fields ct_lun, ct_iid, ct_rxid, ct_timeout
	 * have been carried over unchanged from what our caller had set.
	 *
	 * The field ct_reloff is either what the caller set, or what we've
	 * added to below.
	 *
	 * The dataseg fields and the seg_count fields we just got through
	 * setting. The data direction we've preserved all along and only
	 * clear it if we're sending a MODE1 status as the last CTIO.
	 *
	 */

	if (nth_ctio == nctios - 1) {
	    /*
	     * We're the last in a sequence of CTIO2s, so mark this
	     * CTIO2 and save the handle to the CCB such that when
	     * this CTIO2 completes we can free dma resources and
	     * do whatever else we need to do to finish the rest
	     * of the command.
	     */

	    cto->ct_syshandle = handle;
	    cto->ct_header.rqs_seqno = 1;

	    if (send_status) {
		/*
		 * Get 'real' residual and set flags based on it.
		 */
		cto->ct_resid = resid;
		if (send_sense) {
		    MEMCPY(cto->rsp.m1.ct_resp, sense, QLTM_SENSELEN);
		    cto->rsp.m1.ct_senselen = QLTM_SENSELEN;
		    scsi_status |= CT2_SNSLEN_VALID;
		    cto->rsp.m1.ct_scsi_status = scsi_status;
		    cto->ct_flags &= CT2_FLAG_MMASK;
		    cto->ct_flags |= CT2_FLAG_MODE1 | CT2_NO_DATA |
			CT2_SENDSTATUS | CT2_CCINCR;
		    if (cto->ct_resid > 0)
			cto->rsp.m1.ct_scsi_status |= CT2_DATA_UNDER;
		    else if (cto->ct_resid < 0)
			cto->rsp.m1.ct_scsi_status |= CT2_DATA_OVER;
		} else {
		    cto->rsp.m0.ct_scsi_status = scsi_status;
		    cto->ct_flags |= CT2_SENDSTATUS | CT2_CCINCR;
		    if (cto->ct_resid > 0)
			cto->rsp.m0.ct_scsi_status |= CT2_DATA_UNDER;
		    else if (cto->ct_resid < 0)
			cto->rsp.m0.ct_scsi_status |= CT2_DATA_OVER;
		}
	    }
	    isp_prt(isp, ISP_LOGTDEBUG1, ctx, cto->ct_rxid, (int) tcmd->cd_lun,
		cto->ct_iid, cto->ct_flags, cto->ct_status,
		cto->rsp.m1.ct_scsi_status, cto->ct_resid, "<END>");
	    isp_put_ctio2(isp, cto, qe);
	    ISP_TDQE(isp, "last tdma_mkfc", curi, cto);
	    if (nctios > 1) {
		MEMORYBARRIER(isp, SYNC_REQUEST, curi, QENTRY_LEN);
	    }
	} else {
	    ct2_entry_t *oqe = qe;

	    /*
	     * Make sure handle fields are clean
	     */
	    cto->ct_syshandle = 0;
	    cto->ct_header.rqs_seqno = 0;

	    isp_prt(isp, ISP_LOGTDEBUG1, ctx, cto->ct_rxid, (int) tcmd->cd_lun,
		cto->ct_iid, cto->ct_flags, cto->ct_status,
		cto->rsp.m1.ct_scsi_status, cto->ct_resid, "<MID>");
	    /*
	     * Get a new CTIO2 entry from the request queue.
	     */
	    qe = (ct2_entry_t *) ISP_QUEUE_ENTRY(isp->isp_rquest, nxti);
	    nxti = ISP_NXT_QENTRY(nxti, RQUEST_QUEUE_LEN(isp));
	    if (nxti == optr) {
		isp_prt(isp, ISP_LOGERR, "queue overflow in tdma_mkfc");
		return (CMD_EAGAIN);
	    }

	    /*
	     * Now that we're done with the old CTIO2,
	     * flush it out to the request queue.
	     */
	    ISP_TDQE(isp, "tdma_mkfc", curi, cto);
	    isp_put_ctio2(isp, cto, oqe);
	    if (nth_ctio != 0) {
		MEMORYBARRIER(isp, SYNC_REQUEST, curi, QENTRY_LEN);
	    }
	    curi = ISP_NXT_QENTRY(curi, RQUEST_QUEUE_LEN(isp));

	    /*
	     * Reset some fields in the CTIO2 so we can reuse
	     * for the next one we'll flush to the request
	     * queue.
	     */
	    cto->ct_header.rqs_entry_type = RQSTYPE_CTIO2;
	    cto->ct_header.rqs_entry_count = 1;
	    cto->ct_header.rqs_flags = 0;
	    cto->ct_status = 0;
	    cto->ct_resid = 0;
	    cto->ct_seg_count = 0;
	    /*
	     * Adjust the new relative offset by the amount which is
	     * recorded in the data segment of the old CTIO2 we just
	     * finished filling out.
	     */
	    cto->ct_reloff += oxfrlen;
	    MEMZERO(&cto->rsp, sizeof (cto->rsp));
	}
    }
    *nxtip = nxti;
    return (CMD_QUEUED);
}
#endif

static int
isp_pci_dmasetup(struct ispsoftc *isp, Scsi_Cmnd *Cmnd, ispreq_t *rq,
    u_int16_t *nxi, u_int16_t optr)
{
    struct scatterlist *sg;
    DMA_ADDR_T one_shot_addr;
    unsigned int one_shot_length;
    int segcnt, seg, ovseg, seglim;
    void *h;
    u_int16_t nxti;

#ifdef	LINUX_ISP_TARGET_MODE
    if (rq->req_header.rqs_entry_type == RQSTYPE_CTIO ||
        rq->req_header.rqs_entry_type == RQSTYPE_CTIO2) {
	int s;
	if (IS_SCSI(isp))
	    s = tdma_mk(isp, (tmd_cmd_t *)Cmnd, (ct_entry_t *)rq, nxi, optr);
	else
	    s = tdma_mkfc(isp, (tmd_cmd_t *)Cmnd, (ct2_entry_t *)rq, nxi, optr);
	return (s);
   }
#endif

    nxti = *nxi;
    h = (void *) ISP_QUEUE_ENTRY(isp->isp_rquest, isp->isp_reqidx);

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    if (Cmnd->sc_data_direction == SCSI_DATA_NONE ||
        Cmnd->request_bufflen == 0) {
	rq->req_seg_count = 1;
	goto mbxsync;
    }
#else
    if (Cmnd->request_bufflen == 0) {
	rq->req_seg_count = 1;
	goto mbxsync;
    }
#endif

    if (IS_FC(isp)) {
	if (rq->req_header.rqs_entry_type == RQSTYPE_T3RQS)
	    seglim = ISP_RQDSEG_T3;
	else
	    seglim = ISP_RQDSEG_T2;
	((ispreqt2_t *)rq)->req_totalcnt = Cmnd->request_bufflen;
	/*
	 * Linux doesn't make it easy to tell which direction
	 * the data is expected to go, and you really need to
	 * know this for FC. We'll have to assume that some
	 * of these commands that might be used for writes
	 * our outbounds and all else are inbound.
	 */
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	if (Cmnd->sc_data_direction == SCSI_DATA_WRITE) {
	    ((ispreqt2_t *)rq)->req_flags |= REQFLAG_DATA_OUT;
	} else if (Cmnd->sc_data_direction == SCSI_DATA_READ) {
	    ((ispreqt2_t *)rq)->req_flags |= REQFLAG_DATA_IN;
	} else if (Cmnd->sc_data_direction != SCSI_DATA_UNKNOWN) {
	    isp_prt(isp, ISP_LOGERR,
		"bogus direction (%x) for %d byte request (opcode 0x%x)",
	        Cmnd->sc_data_direction, Cmnd->request_bufflen, Cmnd->cmnd[0]);
	    XS_SETERR(Cmnd, HBA_BOTCH);
	    return (CMD_COMPLETE);
	} else
#endif
	switch (Cmnd->cmnd[0]) {
	case FORMAT_UNIT:
	case WRITE_6:
	case MODE_SELECT:
	case SEND_DIAGNOSTIC:
	case WRITE_10:
	case WRITE_BUFFER:
	case WRITE_LONG:
	case WRITE_SAME:
	case MODE_SELECT_10:
	case WRITE_12:
	case WRITE_VERIFY_12:
	case SEND_VOLUME_TAG:
	    ((ispreqt2_t *)rq)->req_flags |= REQFLAG_DATA_OUT;
	    break;
	default:
	    ((ispreqt2_t *)rq)->req_flags |= REQFLAG_DATA_IN;
	}
    } else {
	if (Cmnd->cmd_len > 12)
	    seglim = 0;
	else
	    seglim = ISP_RQDSEG;
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	if (Cmnd->sc_data_direction == SCSI_DATA_WRITE) {
	    rq->req_flags |= REQFLAG_DATA_OUT;
	} else if (Cmnd->sc_data_direction == SCSI_DATA_READ) {
	    rq->req_flags |= REQFLAG_DATA_IN;
	} else if (Cmnd->sc_data_direction != SCSI_DATA_UNKNOWN) {
	    isp_prt(isp, ISP_LOGERR,
		"bogus direction (%x) for %d byte request (opcode 0x%x)",
	        Cmnd->sc_data_direction, Cmnd->request_bufflen, Cmnd->cmnd[0]);
	    XS_SETERR(Cmnd, HBA_BOTCH);
	    return (CMD_COMPLETE);
	} else
#endif
	rq->req_flags |=  REQFLAG_DATA_OUT | REQFLAG_DATA_IN;
    }

    one_shot_addr = (DMA_ADDR_T) 0;
    one_shot_length = 0;
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    if ((segcnt = Cmnd->use_sg) == 0) {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	segcnt = 1;
	sg = NULL;
	one_shot_length = Cmnd->request_bufflen;
	one_shot_addr = pci_map_single(pcs->pci_dev,
	    Cmnd->request_buffer, Cmnd->request_bufflen,
	    scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
	QLA_HANDLE(Cmnd) = (DMA_HTYPE_T) one_shot_addr;
    } else {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;
	sg = (struct scatterlist *) Cmnd->request_buffer;
	segcnt = pci_map_sg(pcs->pci_dev, sg, Cmnd->use_sg,
	    scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
    }
#else
    if ((segcnt = Cmnd->use_sg) == 0) {
	segcnt = 1;
	sg = NULL;
	one_shot_length = Cmnd->request_bufflen;
	one_shot_addr = virt_to_bus(Cmnd->request_buffer);
    } else {
	sg = (struct scatterlist *) Cmnd->request_buffer;
    }
#endif
    if (segcnt == 0) {
	isp_prt(isp, ISP_LOGWARN, "unable to dma map request");
	XS_SETERR(Cmnd, HBA_BOTCH);
	return (CMD_EAGAIN);
    }

    for (seg = 0, rq->req_seg_count = 0;
	 seg < segcnt && rq->req_seg_count < seglim;
	 seg++, rq->req_seg_count++) {
	DMA_ADDR_T addr;
	unsigned int length;

	if (sg) {
		length = QLA_SG_C(sg);
		addr = QLA_SG_A(sg);
		sg++;
	} else {
		length = one_shot_length;
		addr = one_shot_addr;
	}
#if	defined(CONFIG_HIGHMEM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,18)
	if (addr > 0xffffffff) {
		panic("Aieee! Highmem!");
	}
#endif

	
	if (rq->req_header.rqs_entry_type == RQSTYPE_T3RQS) {
	    ispreqt3_t *rq3 = (ispreqt3_t *)rq;
	    rq3->req_dataseg[rq3->req_seg_count].ds_count = length;
	    rq3->req_dataseg[rq3->req_seg_count].ds_base = addr;
#if	defined(CONFIG_HIGHMEM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,18)
	    rq3->req_dataseg[rq3->req_seg_count].ds_basehi = addr >> 32;
#endif
	} else if (rq->req_header.rqs_entry_type == RQSTYPE_T2RQS) {
	    ispreqt2_t *rq2 = (ispreqt2_t *)rq;
	    rq2->req_dataseg[rq2->req_seg_count].ds_count = length;
	    rq2->req_dataseg[rq2->req_seg_count].ds_base = addr;
	} else {
	    rq->req_dataseg[rq->req_seg_count].ds_count = length;
	    rq->req_dataseg[rq->req_seg_count].ds_base = addr;
	}
	isp_prt(isp, ISP_LOGDEBUG1, "seg0[%d]%llx:%u from %p", seg,
	    (long long)addr, length, sg? sg->address : Cmnd->request_buffer);
    }

    if (seg == segcnt) {
	goto mbxsync;
    }

    do {
	int lim;
	u_int16_t curip;
	ispcontreq_t local, *crq = &local, *qep;

	curip = nxti;
	qep = (ispcontreq_t *) ISP_QUEUE_ENTRY(isp->isp_rquest, curip);
	nxti = ISP_NXT_QENTRY((curip), RQUEST_QUEUE_LEN(isp));
	if (nxti == optr) {
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	    isp_pci_dmateardown(isp, Cmnd, 0);
#endif
	    isp_prt(isp, ISP_LOGDEBUG0, "out of space for continuations");
	    XS_SETERR(Cmnd, HBA_BOTCH);
	    return (CMD_EAGAIN);
	}
	rq->req_header.rqs_entry_count++;
	MEMZERO((void *)crq, sizeof (*crq));
	crq->req_header.rqs_entry_count = 1;
	if (rq->req_header.rqs_entry_type == RQSTYPE_T3RQS) {
	    lim = ISP_CDSEG64;
	    crq->req_header.rqs_entry_type = RQSTYPE_A64_CONT;
	} else {
	    lim = ISP_CDSEG;
	    crq->req_header.rqs_entry_type = RQSTYPE_DATASEG;
	}

	for (ovseg = 0; seg < segcnt && ovseg < lim;
	     rq->req_seg_count++, seg++, ovseg++, sg++) {
	    if (sg->length == 0) {
		panic("zero length s-g element at line %d", __LINE__);
	    }
#if	defined(CONFIG_HIGHMEM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,18)
	    if (QLA_SG_A(sg) > 0xffffffff) {
		panic("Aieee! Highmem!");
	    }
#endif
	    if (rq->req_header.rqs_entry_type == RQSTYPE_T3RQS) {
		    ispcontreq64_t *xrq = (ispcontreq64_t *) crq;
		    xrq->req_dataseg[ovseg].ds_count = QLA_SG_C(sg);
		    xrq->req_dataseg[ovseg].ds_base = QLA_SG_A(sg);
#if	defined(CONFIG_HIGHMEM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,18)
		    xrq->req_dataseg[ovseg].ds_basehi = QLA_SG_A(sg) >> 32;
#endif
	    } else {
		    crq->req_dataseg[ovseg].ds_count = QLA_SG_C(sg);
		    crq->req_dataseg[ovseg].ds_base = QLA_SG_A(sg);
	    }
	    isp_prt(isp, ISP_LOGDEBUG1, "seg%d[%d]%llx:%u from %p",
		rq->req_header.rqs_entry_count-1, ovseg,
	    	(unsigned long long) QLA_SG_A(sg), QLA_SG_C(sg), sg->address);
	}
	MEMORYBARRIER(isp, SYNC_REQUEST, curip, QENTRY_LEN);
	isp_put_cont_req(isp, crq, qep);
    } while (seg < segcnt);
mbxsync:
    if (rq->req_header.rqs_entry_type == RQSTYPE_T3RQS) {
	isp_put_request_t3(isp, (ispreqt3_t *) rq, (ispreqt3_t *) h);
    } else if (rq->req_header.rqs_entry_type == RQSTYPE_T2RQS) {
	isp_put_request_t2(isp, (ispreqt2_t *) rq, (ispreqt2_t *) h);
    } else {
	isp_put_request(isp, (ispreq_t *) rq, (ispreq_t *) h);
    }
    *nxi = nxti;
    return (CMD_QUEUED);
}

#if	LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
static void
isp_pci_dmateardown(struct ispsoftc *isp, Scsi_Cmnd *Cmnd, u_int16_t handle)
{
#ifdef	LINUX_ISP_TARGET_MODE
    if (Cmnd->sc_magic != SCSI_CMND_MAGIC) {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *)isp;
	tmd_cmd_t *tcmd = (tmd_cmd_t *) Cmnd;
	struct scatterlist *sg = tcmd->cd_data;
	int nseg = 0;

	while (sg->address) {
		nseg++;
		sg++;
	}
        pci_unmap_sg(pcs->pci_dev, tcmd->cd_data, nseg,
	    (tcmd->cd_hflags & CDFH_DATA_IN)? PCI_DMA_TODEVICE :
	    PCI_DMA_FROMDEVICE);
    } else
#endif
    if (Cmnd->sc_data_direction != SCSI_DATA_NONE) {
	struct isp_pcisoftc *pcs = (struct isp_pcisoftc *)isp;
	if (Cmnd->use_sg) {
	    pci_unmap_sg(pcs->pci_dev, (struct scatterlist *)Cmnd->buffer,
		Cmnd->use_sg, scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
	} else if (Cmnd->request_bufflen) {
	    DMA_ADDR_T handle = (DMA_ADDR_T) QLA_HANDLE(Cmnd);
	    pci_unmap_single(pcs->pci_dev, handle, Cmnd->request_bufflen,
		scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
	}
    }
}
#endif

static void
isp_pci_reset1(struct ispsoftc *isp)
{
    isp_pci_wr_reg(isp, HCCR, PCI_HCCR_CMD_BIOS);
    ENABLE_INTS(isp);
    isp->mbintsok = 1;
}

static void
isp_pci_dumpregs(struct ispsoftc *isp, const char *msg)
{
    struct isp_pcisoftc *pcs = (struct isp_pcisoftc *) isp;    
    u_int16_t csr;

    pci_read_config_word(pcs->pci_dev, PCI_COMMAND, &csr);
    printk("%s: ", isp->isp_name);
    if (msg)
	printk("%s\n", msg);
    if (IS_SCSI(isp))
	printk("    biu_conf1=%x", ISP_READ(isp, BIU_CONF1));
    else
	printk("    biu_csr=%x", ISP_READ(isp, BIU2100_CSR));
    printk(" biu_icr=%x biu_isr=%x biu_sema=%x ", ISP_READ(isp, BIU_ICR),
	   ISP_READ(isp, BIU_ISR), ISP_READ(isp, BIU_SEMA));
    printk("risc_hccr=%x\n", ISP_READ(isp, HCCR));
    if (IS_SCSI(isp)) {
	ISP_WRITE(isp, HCCR, HCCR_CMD_PAUSE);
	printk("    cdma_conf=%x cdma_sts=%x cdma_fifostat=%x\n",
	       ISP_READ(isp, CDMA_CONF), ISP_READ(isp, CDMA_STATUS),
	       ISP_READ(isp, CDMA_FIFO_STS));
	printk("    ddma_conf=%x ddma_sts=%x ddma_fifostat=%x\n",
	       ISP_READ(isp, DDMA_CONF), ISP_READ(isp, DDMA_STATUS),
	       ISP_READ(isp, DDMA_FIFO_STS));
	printk("    sxp_int=%x sxp_gross=%x sxp(scsi_ctrl)=%x\n",
	       ISP_READ(isp, SXP_INTERRUPT),
	       ISP_READ(isp, SXP_GROSS_ERR),
	       ISP_READ(isp, SXP_PINS_CTRL));
	ISP_WRITE(isp, HCCR, HCCR_CMD_RELEASE);
    }
    printk("    mbox regs: %x %x %x %x %x\n",
	   ISP_READ(isp, OUTMAILBOX0), ISP_READ(isp, OUTMAILBOX1),
	   ISP_READ(isp, OUTMAILBOX2), ISP_READ(isp, OUTMAILBOX3),
	   ISP_READ(isp, OUTMAILBOX4));
    printk("    PCI Status Command/Status=%x\n", csr);
}

#ifdef	MODULE
MODULE_PARM(isp_pci_mapmem, "i");
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
