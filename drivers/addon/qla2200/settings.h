
/****************************************************************************
              Please see revision.notes for revision history.
*****************************************************************************/

/*
 * Compile time Options:
 *     0 - Disable and 1 - Enable
 */
#define  LOOP_ID_FROM_ONE              0	/* loop ID start from 1 when P2P */
#define  QL2100_TARGET_MODE_SUPPORT    0	/* Target mode support */
#define  MEMORY_MAPPED_IO              0
#define  DEBUG_QLA2100_INTR            0
#define  USE_NVRAM_DEFAULTS	       0
#define  DEBUG_PRINT_NVRAM             0
#define  LOADING_RISC_ACTIVITY         0
#define  AUTO_ESCALATE_RESET           0	/* Automatically escalate resets */
#define  AUTO_ESCALATE_ABORT           0	/* Automatically escalate aborts */
#define  STOP_ON_ERROR                 0	/* Stop on aborts and resets  */
#define  STOP_ON_RESET                 0
#define  STOP_ON_ABORT                 0
#define  QLA2100_COMTRACE              0	/* One char tracing  */
#define  WATCH_THREADS_SIZ             0	/* watch size of pending queue */
#define  USE_PORTNAME                  1	/* option to use port names for targets */
#define  LUN_MASKING                   0
#define  USE_FLASH_DATABASE            0	/* Save persistent data to flash */
#define  QLA2100_PROFILE               1
#define  QLA_SCSI_VENDOR_DIR           0	/* Decode vendor specific opcodes for direction */
#define  DISABLE_REMOTE_MAILBOX	       0
#define  QLA2100_LIPTEST    	       0
#define  REQ_TRACE    		       1

#undef   TRACECODE		/* include tracing code in watchdog routines */
#define  CHECK_BINDING
#define  DUMP_INQ_DATA                 0	/* DEBUG_QLA2100 */

#define  DEBUG_QLA2100                 0	/* For Debug of qla2x00 */
#define  DEBUG_GET_FW_DUMP             0	/* also set DEBUG_QLA2100 and */
						/* use COM1 and capture it */
#define  NO_LONG_DELAYS		       0
/* do NOT enable NO_LONG_DELAYS as that makes the driver sleep with spinlocks 
   helt!
   */
#define  QL_TRACE_MEMORY	       0
/* The following WORD_FW_LOAD is defined in Makefile for ia-64 builds
   and can also be decommented here for Word by Word confirmation of
   RISC code download operation */
/* #define  WORD_FW_LOAD               0  */

#define MPIO_SUPPORT		       1	/* Multipath I/O */

/*
 * Driver version 
*/
#if DEBUG_QLA2100
#define QLA2100_VERSION      "5.31.RH1-debug"
#else
#define QLA2100_VERSION      "5.31.RH1"
#endif

/*************** 64 BIT PCI DMA ******************************************/
#define  FORCE_64BIT_PCI_DMA           0	
/* set to one for testing only  */
/* Applicable to 64 version of the Linux 2.4.x and above only            */
/* NVRAM bit nv->cntr_flags_1.enable_64bit_addressing should be used for */
/* administrator control of PCI DMA width size per system configuration  */
/*************************************************************************/

/*
* String arrays
*/
#define LINESIZE    256
#define MAXARGS      26

/*
* Include files
*/
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>


#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/slab.h>

#ifndef KERNEL_VERSION
#  define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#define  APIDEV        1

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

/*
 * We must always allow SHUTDOWN_SIGS.  Even if we are not a module,
 * the host drivers that we are using may be loaded as modules, and
 * when we unload these,  we need to ensure that the error handler thread
 * can be shut down.
 *
 * Note - when we unload a module, we send a SIGHUP.  We mustn't
 * enable SIGTERM, as this is how the init shuts things down when you
 * go to single-user mode.  For that matter, init also sends SIGKILL,
 * so we mustn't enable that one either.  We use SIGHUP instead.  Other
 * options would be SIGPWR, I suppose.
 */

#define SHUTDOWN_SIGS	(sigmask(SIGHUP))

#include "sd.h"
#include "scsi.h"
#include "hosts.h"

#ifdef FC_IP_SUPPORT
#include <linux/skbuff.h>
#include "qlcommon.h"
#endif
 
#include "exioct.h"
#include "qla2x00.h"
#if MPIO_SUPPORT
#include "qla_cfg.h"
#include "qla_gbl.h"
#include "qla_dbg.h"
#endif

#include "inioct.h"
#include "qla_gbl.h"
#include "qla_dbg.h"

#include "qlfo.h"
#include "qla_fo.h"
#include "qlfolimits.h"

