/*
 * iSCSI driver for Linux
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi@cisco.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi.c,v 1.58 2002/02/20 20:15:58 smferris Exp $ 
 *
 */

/* there's got to be a better way to wait for child processes created by kernel_thread */
static int errno = 0;
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/timer.h>


#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
# include <asm/semaphore.h>
#else
# include <asm/spinlock.h>
#endif
#include <asm/uaccess.h>
#include <scsi/sg.h>


#include <sd.h>
#include <scsi.h>
#include <hosts.h>

#ifdef DEBUG
# define DEBUG_ERROR  1
# define DEBUG_TRACE  1
# define DEBUG_INIT   1
# define DEBUG_QUEUE  1
# define DEBUG_FLOW   1
# define DEBUG_ALLOC  1
# define DEBUG_EH     1
# define DEBUG_SMP    1
#else
# define DEBUG_ERROR  1
# define DEBUG_TRACE  0
# define DEBUG_INIT   0
# define DEBUG_QUEUE  0
# define DEBUG_FLOW   0
# define DEBUG_ALLOC  0
# define DEBUG_EH     0
# define DEBUG_SMP    0
#endif


#define TEST_ABORTS 0
#define ABORT_FREQUENCY 2000
#define ABORT_COUNT 4

/* requires TEST_ABORTS 1 */
#define TEST_DEVICE_RESETS 0
#define DEVICE_RESET_FREQUENCY 1
/* note: any count greater than 1 will cause scsi_unjam_host to eventually do a bus reset as well */
#define DEVICE_RESET_COUNT 3

/* requires TEST_DEVICE_RESETS 1 */
#define TEST_BUS_RESETS 0
#define BUS_RESET_FREQUENCY 1
#define BUS_RESET_COUNT 2

/* requires TEST_BUS_RESETS 1 */
#define TEST_HOST_RESETS 0

/* periodically fake unit attention sense data to test bugs in Linux */
#define FAKE_DEFERRED_ERRORS 0
#define FAKE_DEFERRED_ERROR_FREQUENCY 100

#include "iscsi-common.h"
#include "iscsi-protocol.h"
#include "iscsi-login.h"
#include "iscsi-ioctl.h"
#include "iscsi-trace.h"
#include "iscsi.h"
#include "version.h"

/*
 *  IMPORTANT NOTE: to prevent deadlock, when holding multiple locks,
 *  the following locking order must be followed at all times:
 *
 *  hba_list_lock           - access to collection of HBA instances
 *  session->task_lock      - access to a session's collections of tasks
 *  hba->free_task_lock     - for task alloc/free from the HBA's task pool
 *  io_request_lock         - mid-layer acquires before calling queuecommand, eh_*, 
 *                                we must acquire before done() callback
 *  hba->session_lock       - access to an HBA's collection of sessions   
 *  session->scsi_cmnd_lock - access to a session's list of Scsi_Cmnds
 *  iscsi_trace_lock        - for the (mostly unmaintained) tracing code
 *
 * 
 *  The locking order is somewhat counter-intuitive.  The queue()
 *  function may get called by a bottom-half handler for the SCSI
 *  midlayer, which means it may be called after any interrupt occurs,
 *  while another kernel thread is suspended due to the interrupt.
 *  Since this may be one of our threads which is holding a spinlock,
 *  to prevent deadlocks the spinlocks used by the queue() function must
 *  be last in the locking order.  Also, the bottom-half handler must somehow 
 *  be locally disabled when holding any lock that might be used by queue(), 
 *  to prevent the lock holder being suspended by an interrupt, and then 
 *  the queue() function called (which would deadlock).  While 2.4 kernels
 *  have a spin_lock_bh() function, we don't use it, because spin_unlock_bh()
 *  may immediately run bottom-halves, and the driver sometimes would have
 *  needed to call spin_unlock_bh() will interrupts were off and the 
 *  io_request_lock was already held, which could cause deadlocks.  Instead,
 *  the driver always uses spin_lock_irqsave.
 *
 *  Also, since any interrupt may try to acquire the io_request_lock, we 
 *  want the io_request_lock as late in the lock order as possible, since
 *  interrupts must be disabled when holding any lock that follows the 
 *  io_request_lock in the locking order.  The locks needed in queue()
 *  follow the io_request_lock so that interrupts may call the queue()
 *  entry point.  The eh_*_handlers all release the io_request_lock, since
 *  they all may invoke the scheduler, and that can't be done with a spinlock 
 *  held.  Likewise, since scheduling in an interrupt will panic the kernel,
 *  all of the eh_*_handlers may fail if called from interrupt context.
 *
 *  As of 1-2-2002, various threads may be in the following lock states
 *  (ignoring the trace_lock, since the tracing code is largely unmaintained):
 *
 *  queue: (interrupts off) io_request_lock 
 *         (interrupts off) io_request_lock, hba->session_lock 
 *         (interrupts off) io_request_lock, hba->session_lock, session->scsi_cmnd_lock 
 *
 *  tx: none
 *      (an interrupt acquires) io_request_lock 
 *      hba->free_task_lock,
 *      hba->free_task_lock, (an interrupt acquires) io_request_lock 
 *      session->task_lock, 
 *      session->task_lock, (an interrupt acquires) io_request_lock 
 *      session->task_lock, (interrupts off) session->scsi_cmnd_lock 
 *      (interrupts off) session->scsi_cmnd_lock 
 *
 *  rx: none
 *      (an interrupt acquires) io_request_lock 
 *      session->task_lock
 *      session->task_lock, (an interrupt acquires) io_request_lock 
 *      session->task_lock, (interrupts off) io_request_lock 
 *      hba->free_task_lock
 *      hba->free_task_lock, (an interrupt acquires) io_request_lock 
 *      (interrupts off) session->scsi_cmnd_lock 
 *      session->task_lock, (interrupts off) session->scsi_cmnd_lock 
 *
 *  timer: none
 *         hba_list_lock
 *         hba_list_lock, (an interrupt acquires) io_request_lock
 *         hba_list_lock, (interrupts off) hba->session_lock 
 *         hba_list_lock, (interrupts off) hba->session_lock, io_request_lock
 *
 *  ioctl: none
 *         (an interrupt acquires) io_request_lock 
 *         hba_list_lock
 *         hba_list_lock, (an interrupt acquires) io_request_lock
 *         (interrupts off) hba->session_lock 
 *         session->task_lock
 *         session->task_lock, (an interrupt acquires) io_request_lock
 *         session->task_lock, (interrupts off) session->scsi_cmnd_lock
 *         session->task_lock, (interrupts off) io_request_lock 
 *        (interrupts off) session->scsi_cmnd_lock 
 *         
 *  eh_*_handler: (interrupts off) io_request_lock
 *                none
 *                (an interrupt acquires) io_request_lock
 *                (interrupts off) session->scsi_cmnd_lock
 *                session->task_lock
 *                session->task_lock, (an interrupt acquires) io_request_lock
 *
 *  This driver assumes the eh_*_handler functions can safely release
 *  the io_request_lock and locally enable interrupts, which is true
 *  on 2.4 kernels, but unclear on 2.2 kernels.
 *
 *  The eh_*_handler functions may fail if called from interrupt context,
 *  since they typically need to block and wait for a response from the
 *  target, and scheduling in interrupt context would panic the kernel.
 *
 *  The driver assumes that calling the following kernel primitives may invoke the
 *  scheduler and preempt the caller, and thus no spinlocks can be held when they
 *  are called, nor can interrupts or bottom-half handlers be disabled:
 *
 *  sock_sendmsg
 *  sock_recvmsg
 *  kmalloc
 *  schedule_timeout  (duh)
 *  kernel_thread
 *  waitpid
 *
 *  The following kernel primitives probably don't schedule, but the driver
 *  could handle it even if they did:
 *
 *  signal_pending
 *  get_ds
 *  get_fs
 *  set_fs
 *  fget
 *  fput
 *  
 *  The driver assumes that calling the following kernel primitives WILL NOT invoke the
 *  scheduler, and thus cannot cause a preemption.  If this assumption is violated,
 *  the driver will break badly:
 *
 *  wake_up
 *  kill_proc
 *  printk
 *  kfree
 * 
 *  The following driver functions may invoke the scheduler, and must not be
 *  called while holding any spinlock:
 *
 *  iscsi_sendmsg
 *  iscsi_recvmsg
 *  alloc_task
 *  cold_target_reset
 *  warm_target_reset
 */

MODULE_AUTHOR("Cisco Systems, Inc.");
MODULE_DESCRIPTION("iSCSI Driver");
MODULE_LICENSE("GPL");
#ifndef UINT32_MAX
# define UINT32_MAX 0xFFFFFFFFU
#endif

/* useful 2.4-ism */
#ifndef set_current_state
# define set_current_state(state_value) do { current->state = state_value; mb(); } while(0)
#endif


/* determine if a particular signal is pending or not */
# if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
#  define SIGNAL_IS_PENDING(SIG) sigismember(&current->pending.signal, (SIG))
# else
#  define SIGNAL_IS_PENDING(SIG) sigismember(&current->signal, (SIG))
# endif


#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
typedef unsigned long cpu_flags_t;
#else
typedef unsigned int cpu_flags_t;
#endif

/* we'd prefer to do all the locking ourselves, but the SCSI mid-layer 
 * tends to call us with the io_request_lock held, and requires that we
 * get the lock before calling a SCSI command's done() callback.
 * This is supposed to be removed in lk 2.5, so make it conditional at compile-time.
 */
#define MIDLAYER_USES_IO_REQUEST_LOCK

#ifdef MIDLAYER_USES_IO_REQUEST_LOCK
/* for releasing the lock when we don't want it, but have it */
# define RELEASE_IO_REQUEST_LOCK  spin_unlock_irq(&io_request_lock)
# define REACQUIRE_IO_REQUEST_LOCK spin_lock_irq(&io_request_lock)
/* for getting the lock when we need it to call done(), but don't have it */
# define DECLARE_IO_REQUEST_FLAGS cpu_flags_t io_request_flags_
# define LOCK_IO_REQUEST_LOCK spin_lock_irqsave(&io_request_lock, io_request_flags_);
# define UNLOCK_IO_REQUEST_LOCK spin_unlock_irqrestore(&io_request_lock, io_request_flags_);
#else
# define RELEASE_IO_REQUEST_LOCK
# define REACQUIRE_IO_REQUEST_LOCK
# define DECLARE_IO_REQUEST_FLAGS 
# define LOCK_IO_REQUEST_LOCK
# define UNLOCK_IO_REQUEST_LOCK
#endif

/* we need to ensure the SCSI midlayer won't call the queuecommand()
 * entry point from a bottom-half handler while a thread holding locks
 * that queuecommand() will need to acquire is suspended by an interrupt.
 * we don't use spin_lock_bh() on 2.4 kernels, because spin_unlock_bh()
 * will run bottom-half handlers, which is bad if interrupts are turned off
 * and the io_request_lock is held, since the SCSI bottom-half handler will
 * try to acquire the io_request_lock again and deadlock.
 */
#define DECLARE_NOQUEUE_FLAGS cpu_flags_t noqueue_flags_
#define SPIN_LOCK_NOQUEUE(lock) spin_lock_irqsave((lock), noqueue_flags_)
#define SPIN_UNLOCK_NOQUEUE(lock) spin_unlock_irqrestore((lock), noqueue_flags_)


/* Scsi_cmnd->result */
#define DRIVER_BYTE(byte)   ((byte) << 24)
#define HOST_BYTE(byte)     ((byte) << 16) /* HBA codes */
#define MSG_BYTE(byte)      ((byte) << 8)
#define STATUS_BYTE(byte)   ((byte))  /* SCSI status */

/* extract parts of the sense data from an (unsigned char *) to the beginning of sense data */
#define SENSE_KEY(sensebuf) ((sensebuf)[2] & 0x0F)
#define ASC(sensebuf)       ((sensebuf)[12])
#define ASCQ(sensebuf)       ((sensebuf)[13])

static int ctl_open(struct inode *inode, struct file *file);
static int ctl_close(struct inode *inode, struct file *file);
static int ctl_ioctl(struct inode *inode,
                      struct file *file,
                      unsigned int cmd,
                      unsigned long arg );

static int control_major;
static const char *control_name = "iscsictl";

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
static struct file_operations control_fops = {
    owner: THIS_MODULE,
    ioctl: ctl_ioctl,    /* ioctl */
    open: ctl_open,      /* open */
    release: ctl_close,  /* release */
};
#else
static struct file_operations control_fops = {
    NULL,                   /* lseek */
    NULL,                   /* read */
    NULL,                   /* write */
    NULL,                   /* readdir */
    NULL,                   /* poll */
    ctl_ioctl,              /* ioctl */
    NULL,                   /* mmap */
    ctl_open,               /* open */
    NULL,                   /* flush */
    ctl_close,              /* release */
};
#endif

spinlock_t iscsi_hba_list_lock = SPIN_LOCK_UNLOCKED;
static iscsi_hba_t *iscsi_hba_list = NULL;

static unsigned int init_module_complete = 0;
static volatile int iscsi_timer_running = 0;
static volatile pid_t iscsi_timer_pid = 0;

volatile unsigned int iscsi_log_settings = LOG_SET(ISCSI_LOG_ERR);

#if DEBUG_TRACE
spinlock_t iscsi_trace_lock = SPIN_LOCK_UNLOCKED;
static iscsi_trace_entry_t trace_table[ISCSI_TRACE_COUNT];
static int trace_index=0;

# define ISCSI_TRACE(P_TYPE, P_CMND, P_TASK, P_DATA1, P_DATA2) \
           iscsi_fill_trace((P_TYPE), (P_CMND), (P_TASK), (P_DATA1), (P_DATA2))
#else
# define ISCSI_TRACE(P_TYPE, P_CMND, P_TASK, P_DATA1, P_DATA2) 
#endif

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,3,27) )
/* note change modeled per linux2.4 drivers/scsi/ips.c */
struct proc_dir_entry proc_dir_iscsi = {
# ifdef PROC_SCSI_ISCSI
    PROC_SCSI_ISCSI,
# else
    PROC_SCSI_NOT_PRESENT,
# endif
    5,
    "iscsi",
    S_IFDIR|S_IRUGO|S_IXUGO,
    2
};
#endif

/* become a daemon kernel thread.  Some kernels provide this functionality
 * already, and some even do it correctly
 */
void iscsi_daemonize(void)
{
    struct task_struct *this_task = current;
    
# if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,10) )
    /* use the kernel's daemonize */
    daemonize();

    /* Reparent to init */
    reparent_to_init();

    /* increase priority like the md driver does for it's kernel threads */
    wmb();

# elif ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) )
    /* use the kernel's daemonize */
    daemonize();

    /* We'd like to reparent to init, but don't have a function to do it, and
     * symbols like child_reaper aren't exported to modules
     */ 
    
    wmb();
    
# else
    /* 2.2.18 and later has daemonize(), but it's not always correct, so we do it ourselves. */
    struct fs_struct *fs;

    lock_kernel();
    
    /*
     * If we were started as result of loading a module, close all of the
     * user space pages.  We don't need them, and if we didn't close them
     * they would be locked into memory.
     */
    exit_mm(this_task);
    
    this_task->session = 1;
    this_task->pgrp = 1;
    this_task->tty = NULL;
    
    /* Become as one with the init task */
    exit_files(this_task);
    this_task->files = init_task.files;
    atomic_inc(&this_task->files->count);
    
    exit_fs(this_task);       /* this_task->fs->count--; */
    fs = init_task.fs;
    this_task->fs = fs;
    atomic_inc(&fs->count);                                                 

    /* We'd like to reparent to init, but don't have a function to do it, and
     * symbols like child_reaper aren't exported to modules.
     */ 

    /* increase priority like the md driver does for it's kernel threads */
    this_task->policy = SCHED_OTHER;
    this_task->priority = 40;
    wmb();
    
    unlock_kernel();
# endif
}

/* drop an iscsi session */
void iscsi_drop_session(iscsi_session_t *session)
{
    pid_t pid;
              
    DEBUG_INIT4("iSCSI: iscsi_drop_session %p, rx %d, tx %d at %lu\n", 
                session, session->rx_pid, session->tx_pid, jiffies);

    clear_bit(SESSION_ESTABLISHED, &session->control_bits);

    if ((pid = session->tx_pid))
        kill_proc(pid, SIGHUP, 1);
    if ((pid = session->rx_pid))
        kill_proc(pid, SIGHUP, 1);
}

void iscsi_terminate_session(iscsi_session_t *session)
{
    pid_t pid;
              
    DEBUG_INIT4("iSCSI: iscsi_terminate_session %p, rx %d, tx %d at %lu\n", 
                session, session->rx_pid, session->tx_pid, jiffies);

    set_bit(SESSION_TERMINATING, &session->control_bits);
    clear_bit(SESSION_ESTABLISHED, &session->control_bits);

    if ((pid = session->tx_pid))
        kill_proc(pid, SIGKILL, 1);
    if ((pid = session->rx_pid))
        kill_proc(pid, SIGKILL, 1);
}

/* if a signal is pending, deal with it, and return 1.
 * Otherwise, return 0.
 */
static int iscsi_handle_signals(iscsi_session_t *session)
{
    pid_t pid;
    int ret = 0;
    
    /* if we got SIGHUP, try to establish a replacement session.
     * if we got SIGKILL, terminate this session.
     */
    if (signal_pending(current)) {
        spin_lock_irq(&current->sigmask_lock);

        /* iscsi_drop_session and iscsi_terminate_session signal both
         * threads, but someone logged in as root may not.  So, we
         * make sure whichever process gets signalled first propagates
         * the signal when it looks like only one thread got
         * signalled.
         */

        /* on SIGKILL, terminate the session */
        if (SIGNAL_IS_PENDING(SIGKILL)) {
            if (!test_and_set_bit(SESSION_TERMINATING, &session->control_bits)) {
                if ((pid = session->tx_pid) && (pid != current->pid)) {
                    printk("iSCSI: rx thread %d received SIGKILL, killing tx thread %d\n", current->pid, pid);
                    kill_proc(pid, SIGKILL, 1);
                }
                if ((pid = session->rx_pid) && (pid != current->pid)) {
                    printk("iSCSI: tx thread %d received SIGKILL, killing rx thread %d\n", current->pid, pid);
                    kill_proc(pid, SIGKILL, 1);
                }
            }
            ret = 1;
        }
        /* on SIGHUP, drop the session, and try to establish a replacement session */
        if (SIGNAL_IS_PENDING(SIGHUP)) {
            if (test_and_clear_bit(SESSION_ESTABLISHED, &session->control_bits)) {
                if ((pid = session->tx_pid) && (pid != current->pid)) {
                    printk("iSCSI: rx thread %d received SIGHUP, signaling tx thread %d\n", current->pid, pid);
                    kill_proc(pid, SIGHUP, 1);
                }
                if ((pid = session->rx_pid) && (pid != current->pid)) {
                    printk("iSCSI: tx thread %d received SIGHUP, signaling rx thread %d\n", current->pid, pid);
                    kill_proc(pid, SIGHUP, 1);
                }
            }
            ret = 1;
        }
        /* we don't care about any other signals */
        flush_signals(current);
        spin_unlock_irq(&current->sigmask_lock);
    }

    return ret;
}


/* wake up the tx_thread without ever losing the wakeup event */
static void wake_tx_thread(int control_bit, iscsi_session_t *session)
{
    /* tell the tx thread what to do when it wakes up. */
    set_bit(control_bit, &session->control_bits);
    
    /* We make a condition variable out of a wait queue and atomic test&clear.
     * May get spurious wake-ups, but no wakeups will be lost.
     * this is cv_signal().  wait_event_interruptible is cv_wait().
     */
    set_bit(TX_WAKE, &session->control_bits);
    wake_up(&session->tx_wait_q);
}


/* compare against 2^31 */
#define SNA32_CHECK 2147483648UL

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
static int sna_lt(uint32_t n1, uint32_t n2)
{
    return ((n1 != n2) && 
            (((n1 < n2) && ((n2 - n1) < SNA32_CHECK)) || ((n1 > n2) && ((n2 - n1) < SNA32_CHECK))));
}

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
static int sna_lte(uint32_t n1, uint32_t n2)
{
    return ((n1 == n2) ||
            (((n1 < n2) && ((n2 - n1) < SNA32_CHECK)) || ((n1 > n2) && ((n2 - n1) < SNA32_CHECK))));
}

/* difference isn't really a defined operation in SNA, but we'd like it so that
 * we can determine how many commands can be queued to a session.
 */
static int cmdsn_window_size(uint32_t expected, uint32_t max)
{
    if ((expected <= max) && ((max - expected) < SNA32_CHECK)) {
        return (max - expected + 1);
    }
    else if ((expected > max) && ((expected - max) < SNA32_CHECK)) {
        /* window wraps around */
        return ((UINT32_MAX - expected) + 1 + max + 1);
    }
    else {
        /* window closed, or numbers bogus */
        return 0;
    }
}

/* remember old peak cmdsn window size, and report the largest */
static int max_tasks_for_session(iscsi_session_t *session)
{
    if (session->ExpCmdSn == session->MaxCmdSn + 1)
        /* if the window is closed, report nothing, regardless of what it was in the past */
        return 0;
    else if (session->last_peak_window_size < session->current_peak_window_size)
        /* window increasing, so report the current peak size */
        return MIN(session->current_peak_window_size, ISCSI_CMDS_PER_LUN * session->num_luns);
    else
        /* window decreasing.  report the previous peak size, in case it's
         * a temporary decrease caused by the commands we're sending.
         * we want to keep the right number of commands queued in the driver,
         * ready to go as soon as they can.
         */
        return MIN(session->last_peak_window_size, ISCSI_CMDS_PER_LUN * session->num_luns);
}

/* possibly update the ExpCmdSN and MaxCmdSN, and peak window sizes */
static void updateSN(iscsi_session_t *session, UINT32 expcmdsn, UINT32 maxcmdsn) 
{
    int window_size;

    /* standard specifies this check for when to update expected and max sequence numbers */
    if (!sna_lt(maxcmdsn, expcmdsn - 1)) {
        if ((expcmdsn != session->ExpCmdSn) && !sna_lt(expcmdsn, session->ExpCmdSn)) {
            session->ExpCmdSn = expcmdsn;
        }
        if ((maxcmdsn != session->MaxCmdSn) && !sna_lt(maxcmdsn, session->MaxCmdSn)) {
            
            session->MaxCmdSn = maxcmdsn;

            /* look for the peak window size */
            window_size = cmdsn_window_size(expcmdsn, maxcmdsn);
            if (window_size > session->current_peak_window_size)
                session->current_peak_window_size = window_size;

            /* age peak window size info */
            if (time_before(session->window_peak_check + (15 * HZ), jiffies)) {
                session->last_peak_window_size = session->current_peak_window_size;
                session->current_peak_window_size = window_size;
                session->window_peak_check = jiffies;
            }

            /* memory barrier for all of that */
            mb();

            /* wake the tx thread to try sending more commands */
            wake_tx_thread(TX_SCSI_COMMAND, session);
        }

        /* record whether or not the command window for this session has closed,
         * so that we can ping the target periodically to ensure we eventually
         * find out that the window has re-opened.  
         */
        if (maxcmdsn == expcmdsn - 1) {
            session->current_peak_window_size = 0;
            set_bit(SESSION_WINDOW_CLOSED, &session->control_bits);
        }
        else
            clear_bit(SESSION_WINDOW_CLOSED, &session->control_bits);

        DEBUG_FLOW3("iSCSI: session %p - ExpCmdSN %u, MaxCmdSN %u\n", 
                    session, session->ExpCmdSn, session->MaxCmdSn);
    }
}

/* add a session from an HBA's collection of sessions.
 * caller must hold the HBA's session lock.
 */
static int add_session(iscsi_hba_t *hba, iscsi_session_t *session)
{
    iscsi_session_t *prior, *next;

    prior = NULL;
    next = hba->session_list_head;
    while (next && (next->channel < session->channel)) {
        prior = next;
        next = prior->next;
    }
    while (next && (next->channel == session->channel) && (next->target_id < session->target_id)) {
        prior = next;
        next = prior->next;
    }
    
    /* same Linux SCSI address? */
    if (next && (next->channel == session->channel) && (next->target_id == session->target_id)) {
        if (strcmp(next->TargetName, session->TargetName) == 0) {
            /* already have a session running to this target */
            printk("iSCSI: session to %s already exists\n", session->TargetName);
        }
        else {
            printk("iSCSI: error - TargetName %s cannot claim bus %d id %d, already in use by %s\n", 
                   session->TargetName, session->iscsi_bus, next->target_id, next->TargetName);
        }
        return 0;
    }
    else {
        /* insert the session into the list */
        if ((session->next = next))
            next->prev = session;
        else
            hba->session_list_tail = session;

        if ((session->prev = prior))
            prior->next = session;
        else
            hba->session_list_head = session;

        session->hba = hba;
        mb();

        DEBUG_INIT2("iSCSI: added session %p to hba %p\n", session, hba);
        
        return 1;
    }
}

/* remove a session from an HBA's collection of sessions.
 * caller must hold the HBA's session lock.
 */
static int remove_session(iscsi_hba_t *hba, iscsi_session_t *session)
{
    if (session->hba && (hba != session->hba)) {
        printk("iSCSI: tried to remove session %p from hba %p, but session->hba is %p\n",
               session, hba, session->hba);
        return 0;
    }

    /* remove the session from the HBA */
    if (session == hba->session_list_head) {
        if ((hba->session_list_head = session->next))
            hba->session_list_head->prev = NULL;
        else
            hba->session_list_tail = NULL;
    }
    else if (session == hba->session_list_tail) {
        hba->session_list_tail = session->prev;
        hba->session_list_tail->next = NULL;
    }
    else {
        /* we should always be in the middle, 
         * but check pointers to make sure we don't crash the kernel 
         * if the function is called for a session not on the hba.
         */
        if (session->next && session->prev) {
            session->next->prev = session->prev;
            session->prev->next = session->next;
        }
        else {
            DEBUG_ERR2("iSCSI: failed to remove session %p from hba %p\n",
                       session, hba);
            return 0;
        }
    }
    session->prev = NULL;
    session->next = NULL;

    return 1;
}

static iscsi_session_t *find_session_for_cmnd(Scsi_Cmnd *sc)
{
    iscsi_session_t *session = NULL;
    iscsi_hba_t *hba;
    DECLARE_NOQUEUE_FLAGS;

    if (!sc->host)
        return NULL;

    if (!sc->host->hostdata)
        return NULL;

    hba = (iscsi_hba_t *)sc->host->hostdata;

    /* find the session for this command */
    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    session = hba->session_list_head;
    while (session && (session->channel != sc->channel || session->target_id != sc->target))
        session = session->next;
    if (session)
        atomic_inc(&session->refcount); /* caller must use drop_reference when it's done with the session */
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    return session;
}

/* decrement the session refcount, and remove it and free it if the refcount hit zero */
static void drop_reference(iscsi_session_t *session)
{
    iscsi_hba_t *hba = session->hba;
    DECLARE_NOQUEUE_FLAGS;

    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    if (atomic_dec_and_test(&session->refcount)) {
        if (remove_session(hba, session)) {
            DEBUG_INIT1("iSCSI: terminated and deleted session %p\n", session);
            memset(session, 0, sizeof(*session));
            kfree(session);
        }
        else {
            printk("iSCSI: bug - failed to remove unreferenced session %p\n", session);
        }
    }
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
}


/* must hold the task_lock to call this */
static iscsi_task_t *find_task(iscsi_task_collection_t *collection, uint32_t itt)
{
    iscsi_task_t *task = collection->head;
    
    while (task) {
        if (task->itt == itt) {
            DEBUG_FLOW3("iSCSI: found itt %u, task %p, refcount %d\n", itt, task, atomic_read(&task->refcount));
            return task;
        }
        task = task->next;
    }

    return NULL;
}

#if 0
/* don't actually use this at the moment */
/* must hold the task_lock to call this */
static iscsi_task_t *find_mgmt_task(iscsi_task_collection_t *collection, uint32_t mgmt_itt)
{
    iscsi_task_t *task = collection->head;
    
    while (task) {
        if (task->mgmt_itt == mgmt_itt) {
            DEBUG_FLOW2("iSCSI: found mgmt_itt %u, task %p\n", mgmt_itt, task);
            return task;
        }
        task = task->next;
    }

    return NULL;
}
#endif

#if 0
/* don't actually need this at the moment */
/* must hold the task_lock to call this */
static iscsi_task_t *find_task_for_cmnd(iscsi_task_collection_t *collection, Scsi_Cmnd *sc)
{
    iscsi_task_t *task = collection->head;
    
    while (task) {
        if (task->scsi_cmnd == sc) {
            DEBUG_FLOW3("iSCSI: found itt %u, task %p for cmnd %p\n", task->itt, task, sc);
            return task;
        }
        task = task->next;
    }

    return NULL;
}
#endif

/* add a task to the collection.  Must hold the task_lock to do this. */
static void add_task(iscsi_task_collection_t *collection, iscsi_task_t *task)
{
    if (task->prev || task->next)
        printk("iSCSI: bug - adding task %p, prev %p, next %p, to collection %p\n",
               task, task->prev, task->next, collection);

    if (collection->head) {
        task->next = NULL;
        task->prev = collection->tail;
        collection->tail->next = task;
        collection->tail = task;
    }
    else {
        task->prev = task->next = NULL;
        collection->head = collection->tail = task;
    }
}

#define first_task(collection_ptr) ((collection_ptr)->head)
#define next_task(collection_ptr, task_ptr) ((task_ptr)->next)
#define order_next_task(collection_ptr, task_ptr) ((task_ptr)->order_next)

/* must hold the task_lock when calling this */
static iscsi_task_t *pop_task(iscsi_task_collection_t *collection)
{
    iscsi_task_t *task = NULL;

    if ((task = collection->head)) {
        /* pop the head */
        if ((collection->head = task->next))
            collection->head->prev = NULL;
        else
            collection->tail = NULL;

        /* and return it */
        task->prev = NULL;
        task->next = NULL;
        
        return task;
    }

    return NULL;
}

/* must hold the task_lock when calling this */
static void push_task(iscsi_task_collection_t *collection, iscsi_task_t *task)
{
    if (task) {
        task->prev = NULL;
        task->next = collection->head;
        if (collection->head) {
            collection->head->prev = task;
            collection->head = task;
        }
        else {
            collection->head = collection->tail = task;
        }
    }
}

static void unlink_task(iscsi_task_collection_t *collection, iscsi_task_t *task)
{
    /* unlink the task from the collection */
    if (task == collection->head) {
        if ((collection->head = task->next))
            collection->head->prev = NULL;
        else
            collection->tail = NULL;
    }
    else if (task == collection->tail) {
        collection->tail = task->prev;
        collection->tail->next = NULL;
    }
    else {
        task->next->prev = task->prev;
        task->prev->next = task->next;
    }
    task->next = NULL;
    task->prev = NULL;
}

/* if the task for the itt is found in the collection, remove it, and return it.
 * otherwise, return NULL.  Must hold the task_lock to call this.
 */
static iscsi_task_t *remove_task(iscsi_task_collection_t *collection, uint32_t itt)
{
    iscsi_task_t *task = NULL;
    iscsi_task_t *search = collection->head;

    while (search) {
        if (search->itt == itt) {
            task = search;
            unlink_task(collection, task);
            return task;
        }
        search = search->next;
    }

    return NULL;
}

/* if the task for the mgmt_itt is found in the collection, remove it, and return it.
 * otherwise, return NULL.  Must hold the task_lock to call this.
 */
static iscsi_task_t *remove_mgmt_task(iscsi_task_collection_t *collection, uint32_t mgmt_itt)
{
    iscsi_task_t *task = NULL;
    iscsi_task_t *search = collection->head;

    while (search) {
        if (search->mgmt_itt == mgmt_itt) {
            task = search;
            unlink_task(collection, task);
            return task;
        }
        search = search->next;
    }

    return NULL;
}

/* if the task for the itt is found in the collection, remove it, and return it.
 * otherwise, return NULL.  Must hold the task_lock to call this.
 */
static iscsi_task_t *remove_task_for_cmnd(iscsi_task_collection_t *collection, Scsi_Cmnd *sc)
{
    iscsi_task_t *task = NULL;
    iscsi_task_t *search = collection->head;

    while (search) {
        if (search->scsi_cmnd == sc) {
            task = search;
            unlink_task(collection, task);
            return task;
        }
        search = search->next;
    }

    return NULL;
}

/* 
 * remove all tasks with the specified LUN.  Must hold the task_lock to call this.
 */
static void remove_tasks_for_lun(iscsi_task_collection_t *collection, int lun)
{
    iscsi_task_t *search = collection->head;
    iscsi_task_t *next = NULL;

    while (search) {
        next = search->next;
        if (search->scsi_cmnd && search->scsi_cmnd->lun == lun)
            unlink_task(collection, search);
        
        search = next;
    }
}


/* must be called with no locks held, since it may sleep, and acquires
 * locks on it's own.
 */
static iscsi_task_t *alloc_task(iscsi_session_t *session)
{
    iscsi_task_t *task = NULL;
    iscsi_hba_t *hba = session->hba;

    if (!hba) {
        printk("iSCSI: alloc_task - session %p has NULL HBA\n", session);
        return NULL;
    }

    /* try to get one from the HBA's free task collection */
    spin_lock(&hba->free_task_lock);
    if ((task = pop_task(&hba->free_tasks))) {
        atomic_dec(&hba->num_free_tasks);
        atomic_inc(&hba->num_used_tasks);
        hba->min_free_tasks = MIN(hba->min_free_tasks, atomic_read(&hba->num_free_tasks));
    }
    else {
        hba->min_free_tasks = 0;
    }
    spin_unlock(&hba->free_task_lock);

    /* otherwise, try to dynamically allocate a task */
    if (!task) {
        if ((task = kmalloc(sizeof(iscsi_task_t), GFP_ATOMIC))) {
            atomic_inc(&hba->num_used_tasks);
            DEBUG_ALLOC6("iSCSI: kmalloc task %p (active %u, used %u, free %u) for session %p to %s\n",
                         task, atomic_read(&session->num_active_tasks), 
                         atomic_read(&hba->num_used_tasks), atomic_read(&hba->num_free_tasks),
                         session, session->log_name);
        }
    }

    if (task) {
        memset(task, 0, sizeof(iscsi_task_t) );
        task->itt = RSVD_TASK_TAG;
        task->ttt = RSVD_TASK_TAG;
        task->mgmt_itt = RSVD_TASK_TAG;
        task->next = task->prev = NULL;
        task->order_next = task->order_prev = NULL;
        task->session = session;
        wmb();
    }
    else {
        set_bit(SESSION_TASK_ALLOC_FAILED, &session->control_bits);
    }
    return task;
}


static void free_task( iscsi_task_t *task )
{
    iscsi_session_t *session = task->session;
    iscsi_hba_t *hba;
    
    if (! task) {
        DEBUG_ERR0("iSCSI: free_task couldn't free NULL task\n");
        return;
    }
    if (! session) {
        DEBUG_ERR1("iSCSI: free_task couldn't find session for task %p\n", task);
        return;
    }
    hba = session->hba;
    if (!hba) {
        DEBUG_ERR1("iSCSI: free_task couldn't find HBA for task %p\n", task);
        return;
    }

    if (task->next || task->prev || task->order_next || task->order_prev) {
        /* this is a memory leak, which is better than memory corruption */
        printk("iSCSI: bug - tried to free task %p with prev %p, next %p, order_prev %p, order_next %p\n",
               task, task->prev, task->next, task->order_prev, task->order_next);
        return;
    }

    DEBUG_QUEUE4("iSCSI: free_task %p, itt %u, session %p, %u currently free\n", 
                 task, task->itt, task->session, atomic_read(&hba->num_free_tasks));

    /* zero out the task settings */
    task->scsi_cmnd = NULL;
    task->session = NULL;
    task->itt = RSVD_TASK_TAG;
    task->mgmt_itt = RSVD_TASK_TAG;
    task->next = task->prev = NULL;
    task->order_next = task->order_prev = NULL;
    atomic_set(&task->refcount, 0);
    
    /* put the task on the session's free list */
    spin_lock(&hba->free_task_lock);
    atomic_inc(&hba->num_free_tasks);
    atomic_dec(&hba->num_used_tasks);
    add_task(&hba->free_tasks, task);
    spin_unlock(&hba->free_task_lock);

    /* If an alloc call has failed, we need to wake up the TX thread
     * now that a task is available, since there are no guarantees
     * that anything else will wake it up.
     */
    if (test_and_clear_bit(SESSION_TASK_ALLOC_FAILED, &session->control_bits))
        wake_tx_thread(TX_SCSI_COMMAND, session);
}


/* As long as the tx thread is the only caller, no locking
 * is required.  If any other thread also needs to call this,
 * then all callers must be changed to agree on some locking
 * protocol.  Currently, some but not all caller's are holding
 * the session->task_lock.
 */
static inline uint32_t allocate_itt(iscsi_session_t *session)
{
    uint32_t itt = 0;

    if (session) {
        itt = session->itt++;
        /* iSCSI reserves 0xFFFFFFFF, this driver reserves 0 */
        if (session->itt == RSVD_TASK_TAG)
            session->itt = 1;
    }
    return itt;
}


/* Caller must hold the session's task_lock.  Associating a task with
 * a session causes it to be completed on a session drop or target
 * reset, along with all other session tasks, in the order they were
 * added to the session.  Preserving the ordering is required by the
 * Linux SCSI architecture.  Tasks that should not be completed to the
 * Linux SCSI layer (because the eh_abort_handler has or will return
 * SUCCESS for it) get removed from the session, though they may still
 * be in various task collections so that PDUs relating to them can be
 * sent or received.
 */
static void add_session_task(iscsi_session_t *session, iscsi_task_t *task)
{
    if (atomic_read(&session->num_active_tasks) == 0) {
        /* session going from idle to active, pretend we just
         * received something, so that the idle period before this doesn't
         * cause an immediate timeout.
         */
        session->last_rx = jiffies;
    }
    atomic_inc(&session->num_active_tasks);

    /* set task info */
    task->session = session;
    task->itt = allocate_itt(session);

    DEBUG_QUEUE5("iSCSI: task %p allocated itt %u for command %p, session %p to %s\n", 
                 task, task->itt, task->scsi_cmnd, session, session->log_name);

    /* add it to the session task ordering list */
    if (session->arrival_order.head) {
        task->order_prev = session->arrival_order.tail;
        task->order_next = NULL;
        session->arrival_order.tail->order_next = task;
        session->arrival_order.tail = task;
    }
    else {
        task->order_prev = NULL;
        task->order_next = NULL;
        session->arrival_order.head = session->arrival_order.tail = task;
    }

    DEBUG_FLOW4("iSCSI: task %p, itt %u, added to session %p to %s\n", task, task->itt, session, session->log_name);
}

static int remove_session_task(iscsi_session_t *session, iscsi_task_t *task)
{
    /* remove the task from the session's arrival_order collection */
    if (task == session->arrival_order.head) {
        if ((session->arrival_order.head = task->order_next))
            session->arrival_order.head->order_prev = NULL;
        else
            session->arrival_order.tail = NULL;
    }
    else if (task == session->arrival_order.tail) {
        session->arrival_order.tail = task->order_prev;
        session->arrival_order.tail->order_next = NULL;
    }
    else {
        /* we should always be in the middle, 
         * but check pointers to make sure we don't crash the kernel 
         * if the function is called for a task not in the session.
         */
        if (task->order_next && task->order_prev) {
            task->order_next->order_prev = task->order_prev;
            task->order_prev->order_next = task->order_next;
        }
        else {
            DEBUG_ERR4("iSCSI: failed to remove itt %u, task %p from session %p to %s\n", 
                       task->itt, task, session, session->log_name);
            return 0;
        }
    }
    task->order_prev = NULL;
    task->order_next = NULL;
    atomic_dec(&session->num_active_tasks);

    return 1;
}

/* 
 * move all tasks in the session for the specified LUN into the collection.
 */
static void move_session_tasks_for_lun(iscsi_task_collection_t *collection, iscsi_session_t *session, int lun)
{
    iscsi_task_t *search = session->arrival_order.head;

    while (search) {
        iscsi_task_t *next = search->order_next;

        if (search->scsi_cmnd && search->scsi_cmnd->lun == lun) {
            remove_session_task(session, search);
            add_task(collection, search);
        }
        search = next;
    }
}

/* 
 * remove cmnds for the specified LUN that are in the session's cmnd queue, 
 * or the forced abort queue, and return a list of them.
 */
static Scsi_Cmnd *remove_session_cmnds_for_lun(iscsi_session_t *session, int lun)
{
    Scsi_Cmnd *cmnd = NULL;
    Scsi_Cmnd *prior = NULL;
    Scsi_Cmnd *head = NULL, *tail = NULL;

    /* handle any commands we hid from the tx thread */

    while (session->ignored_cmnd_head && (session->ignored_cmnd_head->lun == lun)) {
        /* move the head */
        cmnd = session->ignored_cmnd_head;
        session->ignored_cmnd_head = (Scsi_Cmnd *)cmnd->host_scribble;
        if (session->ignored_cmnd_head == NULL)
            session->ignored_cmnd_tail = NULL;
        atomic_dec(&session->num_ignored_cmnds);
        if (head) {
            tail->host_scribble = (unsigned char *)cmnd;
            tail = cmnd;
        }
        else {
            cmnd->host_scribble = NULL;
            head = tail = cmnd;
        }
    }
    
    /* we're either out of cmnds, or the head is for a different LUN */
    prior = session->ignored_cmnd_head;
    while (prior && (cmnd = (Scsi_Cmnd *)prior->host_scribble)) {
        if (cmnd->lun == lun) {
            /* splice out cmnd and move it */
            prior->host_scribble = cmnd->host_scribble;
            if (session->ignored_cmnd_tail == cmnd)
                session->ignored_cmnd_tail = prior;
            atomic_dec(&session->num_ignored_cmnds);
            if (head) {
                tail->host_scribble = (unsigned char *)cmnd;
                tail = cmnd;
            }
            else {
                cmnd->host_scribble = NULL;
                head = tail = cmnd;
            }
        }
        else {
            prior = cmnd;
        }
    }

    /* handle cmnds queued for the tx thread to send */

    while (session->scsi_cmnd_head && (session->scsi_cmnd_head->lun == lun)) {
        /* move the head */
        cmnd = session->scsi_cmnd_head;
        session->scsi_cmnd_head = (Scsi_Cmnd *)cmnd->host_scribble;
        if (session->scsi_cmnd_head == NULL)
            session->scsi_cmnd_tail = NULL;
        atomic_dec(&session->num_cmnds);
        if (head) {
            tail->host_scribble = (unsigned char *)cmnd;
            tail = cmnd;
        }
        else {
            cmnd->host_scribble = NULL;
            head = tail = cmnd;
        }
    }
    
    /* we're either out of cmnds, or the head is for a different LUN */
    prior = session->scsi_cmnd_head;

    while (prior && (cmnd = (Scsi_Cmnd *)prior->host_scribble)) {
        if (cmnd->lun == lun) {
            /* splice out cmnd and move it */
            prior->host_scribble = cmnd->host_scribble;
            if (session->scsi_cmnd_tail == cmnd)
                session->scsi_cmnd_tail = prior;
            atomic_dec(&session->num_cmnds);
            if (head) {
                tail->host_scribble = (unsigned char *)cmnd;
                tail = cmnd;
            }
            else {
                cmnd->host_scribble = NULL;
                head = tail = cmnd;
            }
        }
        else {
            prior = cmnd;
        }
    }

    return head;
}


/* decode common network errno values into more useful strings.
 * strerror would be nice right about now.
 */
static char *iscsi_strerror(int errno)
{
    switch (errno) {
        case EIO:
            return "I/O error";
        case EINTR:
            return "Interrupted system call";
        case ENXIO:
            return "No such device or address";
        case EFAULT:
            return "Bad address";
        case EBUSY:
            return "Device or resource busy";
        case EINVAL:
            return "Invalid argument";
        case EPIPE:
            return "Broken pipe";
        case ENONET:
            return "Machine is not on the network";
        case ECOMM:
            return "Communication error on send";
        case EPROTO:
            return "Protocol error";
        case ENOTUNIQ:
            return "Name not unique on network";
        case ENOTSOCK:
            return "Socket operation on non-socket";
        case ENETDOWN:
            return "Network is down";
        case ENETUNREACH:
            return "Network is unreachable";
        case ENETRESET:
            return "Network dropped connection because of reset";
        case ECONNABORTED:
            return "Software caused connection abort";
        case ECONNRESET:
            return "Connection reset by peer";
        case ESHUTDOWN:
            return "Cannot send after shutdown";
        case ETIMEDOUT:
            return "Connection timed out";
        case ECONNREFUSED:
            return "Connection refused";
        case EHOSTDOWN:
            return "Host is down";
        case EHOSTUNREACH:
            return "No route to host";
        default:
            return "";
    }
}


static int iscsi_recvmsg( iscsi_session_t *session, struct msghdr *msg, int len )
{
    int rc = 0;
    mm_segment_t oldfs;

    if (session->socket) {
        oldfs = get_fs();
        set_fs( get_ds() );

        /* Try to avoid memory allocation deadlocks by using GFP_ATOMIC. */
        session->socket->sk->allocation = GFP_ATOMIC;
            
        rc = sock_recvmsg( session->socket, msg, len, MSG_WAITALL);
        if (rc > 0) {
            session->last_rx = jiffies;
            mb();
        }
    }
    
    set_fs( oldfs );

    return rc;
}

static int iscsi_sendmsg( iscsi_session_t *session, struct msghdr *msg, int len )
{
    int rc = 0;
    mm_segment_t oldfs;

    if (session->socket) {
        oldfs = get_fs();
        set_fs( get_ds() );
        
        /* Try to avoid resource acquisition deadlocks by using GFP_ATOMIC. */
        session->socket->sk->allocation = GFP_ATOMIC;

        /* FIXME: ought to loop handling short writes, unless a signal occurs */
        rc = sock_sendmsg(session->socket, msg, len);
        
        set_fs( oldfs );
    }
        
    return rc;
}


/* create and connect a new socket for this session */
static int iscsi_connect(iscsi_session_t *session) 
{
    mm_segment_t oldfs;
    struct socket           *socket;
    struct sockaddr_in      addr;
    int window_size;
    int arg = 1, arglen = 0;
    int rc = 0, ret = 0;

    if (session->socket) {
        printk("iSCSI: session %p already has socket %p\n", session, session->socket);
        return 1;
    }

    oldfs = get_fs();
    set_fs( get_ds() );

    if (sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &socket) < 0) {
        printk("iSCSI: failed to create socket\n");
        set_fs(oldfs);
        return 0;
    }

    /* no delay in sending */
    if (socket->ops->setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&arg, sizeof(arg)) < 0) {
        printk("iSCSI: failed to setsockopt TCP_NODELAY\n");
        goto done;
    }

    /* try to ensure a reasonably sized TCP window */
    arglen = sizeof(window_size);
    if (sock_getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&window_size, &arglen) >= 0) {
        DEBUG_FLOW1("iSCSI: TCP recv window size %u\n", window_size);
        
        if (session->tcp_window_size && (window_size < session->tcp_window_size)) {
            window_size = session->tcp_window_size;
            if (sock_setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&window_size, sizeof(window_size)) < 0) {
                printk("iSCSI: failed to set TCP recv window size to %u\n", window_size);
            }
            else if (sock_getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&window_size, &arglen) >= 0) {
                DEBUG_FLOW2("iSCSI: set TCP recv window size to %u, actually got %u\n", session->tcp_window_size, window_size);
            }
        }
    }
    else {
        printk("iSCSI: getsockopt RCVBUF %p failed\n", socket);
    }
    if (sock_getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&window_size, &arglen) >= 0) {
        DEBUG_FLOW1("iSCSI: TCP send window size %u\n", window_size);
        
        if (session->tcp_window_size && (window_size < session->tcp_window_size)) {
            window_size = session->tcp_window_size;
            if (sock_setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&window_size, sizeof(window_size)) < 0) {
                printk("iSCSI: failed to set TCP send window size to %u\n", window_size);
            }
            else if (sock_getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&window_size, &arglen) >= 0) {
                DEBUG_FLOW2("iSCSI: set TCP send window size to %u, actually got %u\n", session->tcp_window_size, window_size);
            }
        }
    }
    else {
        printk("iSCSI: getsockopt SNDBUF %p failed\n", socket);
    }

    /* connect to the target */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->port);
    if (session->address_length == 4) {
        memcpy(&addr.sin_addr.s_addr, session->ip_address, MIN(sizeof(addr.sin_addr.s_addr), session->address_length));
    }
    else {
        /* FIXME: IPv6 */
        printk("iSCSI: unable to handle IPv6 address\n");
        goto done;
    }
    rc = socket->ops->connect(socket, (struct sockaddr *)&addr, sizeof(addr), 0);

    if (signal_pending(current))
        goto done;

    if (rc < 0) {
        char *error = iscsi_strerror(-rc);
        if (error && error[0] != '\0') {
            printk("iSCSI: session %p to %s failed to connect, rc %d, %s\n", session, session->log_name, rc, error);
        }
        else {
            printk("iSCSI: session %p to %s failed to connect, rc %d\n", session, session->log_name, rc);
        }
    }
    else {
        if (LOG_ENABLED(ISCSI_LOG_LOGIN))
            printk("iSCSI: session %p to %s connected at %lu\n", session, session->log_name, jiffies);
        ret = 1;
    }

 done:
    if (ret) {
        /* save the socket pointer for later */
        session->socket = socket;
        mb();
    }
    else {
        /* close the socket */
        sock_release(socket);
    }
    set_fs(oldfs);
    return ret;
}

static void iscsi_disconnect(iscsi_session_t *session) 
{
    if (session->socket) {
        sock_release(session->socket);
        session->socket = NULL;
        mb();
    }
}


int iscsi_send_login_pdu(iscsi_session_t *session, struct IscsiLoginHdr *pdu, int max_pdu_length)
{
    struct msghdr msg;
    struct iovec iov;
    int rc;
    int pdu_length = sizeof(*pdu) + ntoh24(pdu->dlength);

    /* add any padding needed */
    if (pdu_length % PAD_WORD_LEN) {
        int pad = 0;
        char *data = ((char *)pdu) + pdu_length;

        pad = PAD_WORD_LEN - (pdu_length % PAD_WORD_LEN);
        if (pdu_length + pad > max_pdu_length) {
            printk("iSCSI: session %p failing to send login pdu %p, no room for padding\n", pdu, session);
            return 0;
        }
        DEBUG_FLOW3("iSCSI: session %p adding %d pad bytes on login pdu %p\n", session, pad, pdu);
        for (; pad; pad--) {
            *data++ = 0;
            pdu_length++;
        }
    }

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = pdu;
    iov.iov_len = pdu_length;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /* set a timer, though we shouldn't really need one */
    session->login_phase_timer = jiffies + (session->login_timeout * HZ);
    mb();

    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
        char *text = (char *)(pdu + 1);
        char *end = text + ntoh24(pdu->dlength);

        /* show the phases and tbit */
        printk("iSCSI: session %p sending login pdu with current phase %d, next %d, tbit %d, dlength %d at %lu, timeout at %lu (%d seconds)\n",
               session, pdu->curr, pdu->next, pdu->tbit, ntoh24(pdu->dlength), jiffies, session->login_phase_timer, session->login_timeout);

        /* show all the text that we're sending */
        while (text < end) {
            printk("iSCSI: session %p login text: %s\n", session, text);
            text += strlen(text);
            while ((text < end) && (*text == '\0'))
                text++;
        }
    }

    rc = iscsi_sendmsg(session, &msg, pdu_length);

    /* clear the timer */
    session->login_phase_timer = 0;
    mb();

    if (rc != pdu_length) {
        char *error;
        if ((rc < 0) && (error = iscsi_strerror(-rc)) && (error[0] != '\0'))
            printk("iSCSI: session %p failed to send login PDU, rc %d, %s\n", session, rc, iscsi_strerror(-rc));
        else
            printk("iSCSI: session %p failed to send login PDU, rc %d\n", session, rc);

        return 0;
    }

    DEBUG_INIT5("iSCSI: session %p sent login pdu %p at %lu, length %d, dlength %d\n", 
                session, pdu, jiffies, pdu_length, ntoh24(pdu->dlength));

    return 1;
}

/* try to read an entire login PDU into the buffer, timing out after timeout seconds */
int iscsi_recv_login_pdu(iscsi_session_t *session, struct IscsiLoginRspHdr *pdu, int max_pdu_length, int timeout)
{
    struct msghdr msg;
    struct iovec iov;
    int rc = 0;
    int pdu_length;
    int ret = 0;

    if (max_pdu_length < sizeof(*pdu)) {
        printk("iSCSI: session %p, pdu %p max_pdu_length %d is too small to recv a login header\n", 
               session, pdu, max_pdu_length);
        return 0;
    }

    /* set the timer to implement the timeout requested */
    if (timeout)
        session->login_phase_timer = jiffies + (timeout * HZ);
    else
        session->login_phase_timer = 0;
    mb();
    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
        printk("iSCSI: session %p trying to recv login pdu at %lu, timeout at %lu (%d seconds)\n", 
               session, jiffies, session->login_phase_timer, timeout);
    }

    /* read the PDU header */
    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)pdu;
    iov.iov_len = sizeof(*pdu);
    memset( &msg, 0, sizeof(struct msghdr) );
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    rc = iscsi_recvmsg(session, &msg, sizeof(*pdu));

    if (signal_pending(current)) {
        printk("iSCSI: session %p recv_login_pdu timed out at %lu\n", session, jiffies);
        goto done;
    }
    
    if (rc != sizeof(*pdu)) {
        if (rc < 0) {
            char *error = iscsi_strerror(-rc);
            if (error && error[0] != '\0') {
                printk("iSCSI: session %p recv_login_pdu failed to recv %d login PDU bytes, rc %d, %s\n", 
                       session, iov.iov_len, rc, iscsi_strerror(-rc));
            }
            else {
                printk("iSCSI: session %p recv_login_pdu failed to recv %d login PDU bytes, rc %d\n", 
                       session, iov.iov_len, rc);
            }
        }
        else if (rc == 0) {
            printk("iSCSI: session %p recv_login_pdu: connection closed\n", session);
        }
        else {
            /* short reads should be impossible unless a signal occured,
             * which we already checked for.
             */
            printk("iSCSI: bug - session %p recv_login_pdu, short read %d of %d\n", session, rc, sizeof(*pdu));
        }
        goto done;
    }

    pdu_length = ntoh24(pdu->dlength);
    if (pdu_length) {
        char *nul = (char *)(pdu + 1);

        /* check for buffer overflow */
        if (pdu_length > (max_pdu_length - sizeof(*pdu))) {
            printk("iSCSI: session %p recv_login_pdu can't read %d bytes of login PDU data, only %d bytes of buffer available\n",
                   session, pdu_length, (max_pdu_length - sizeof(*pdu)));
            goto done;
        }

        /* handle PDU padding */
        if (pdu_length % PAD_WORD_LEN) {
            int pad = PAD_WORD_LEN - (pdu_length % PAD_WORD_LEN);
            pdu_length += pad;
        }

        /* make sure data + pad + NUL fits in the buffer */
        if (pdu_length + sizeof(*pdu) + 1 >= max_pdu_length) {
            printk("iSCSI: session %p recv_login_pdu failing, PDU size %d would overflow buffer size %d\n", 
                   session, pdu_length + sizeof(*pdu) + 1, max_pdu_length);
            goto done;
        }


        /* read the PDU's text data payload */
        memset(&iov, 0, sizeof(iov));
        iov.iov_base = (void *)(pdu + 1);
        iov.iov_len = max_pdu_length - sizeof(*pdu);
        memset( &msg, 0, sizeof(struct msghdr) );
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        rc = iscsi_recvmsg(session, &msg, pdu_length);

        /* ensure NUL termination of the text */
        nul += pdu_length;
        *nul = '\0';
        
        if (signal_pending(current)) {
            printk("iSCSI: session %p recv_login_pdu timed out at %lu\n", session, jiffies);
            goto done;
        }
        
        if (rc != pdu_length) {
            if (rc < 0) {
                char *error = iscsi_strerror(-rc);
                if (error && error[0] != '\0') {
                    printk("iSCSI: session %p recv_login_pdu failed to recv %d login data PDU bytes, rc %d, %s\n", 
                           session, pdu_length, rc, iscsi_strerror(-rc));
                }
                else {
                    printk("iSCSI: session %p recv_login_pdu failed to recv %d login data PDU bytes, rc %d\n", 
                           session, pdu_length, rc);
                }
            }
            else if (rc == 0) {
                printk("iSCSI: session %p recv_login_pdu: connection closed\n", session);
            }
            else {
                /* short reads should be impossible unless a signal occured,
                 * which we already checked for.
                 */
                printk("iSCSI: bug - session %p recv_login_pdu, short read %d of %d\n", session, rc, pdu_length);
            }
            goto done;
        }
    }

    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
        char *text = (char *)(pdu + 1);
        char *end = text + ntoh24(pdu->dlength);

        /* show the phases and tbit */
        printk("iSCSI: session %p received login pdu response at %lu with current phase %d, next %d, tbit %d, dlength %d\n",
               session, jiffies, pdu->curr, pdu->next, pdu->tbit, ntoh24(pdu->dlength));

        /* show all the text that we're sending */
        while (text < end) {
            printk("iSCSI: session %p login resp text: %s\n", session, text);
            text += strlen(text);
            while ((text < end) && (*text == '\0'))
                text++;
        }
    }
    
    ret = 1;
    
 done:    
    /* clear the timer */
    session->login_phase_timer = 0;
    mb();
    iscsi_handle_signals(session);

    return ret;
}


#if DEBUG_TRACE
static void
iscsi_fill_trace(unsigned char type, Scsi_Cmnd *sc, iscsi_task_t *task, unsigned long data1, unsigned long data2)
{
    iscsi_trace_entry_t *te;
    DECLARE_NOQUEUE_FLAGS;

    SPIN_LOCK_NOQUEUE(&iscsi_trace_lock);

    te = &trace_table[trace_index];
    trace_index++;
    if ( trace_index >= ISCSI_TRACE_COUNT ) {
        trace_index = 0;
    }
    memset(te, 0x0, sizeof(*te));

    te->type = type;
    if (sc) {
        te->cmd = sc->cmnd[0];
        te->host = sc->host->host_no;
        te->channel = sc->channel;
        te->target = sc->target;
        te->lun = sc->lun;
    }
    if (task) {
        te->itt = task->itt;
    }
    te->data1 = data1;
    te->data2 = data2;
    te->jiffies = jiffies;

    SPIN_UNLOCK_NOQUEUE(&iscsi_trace_lock);
}
#endif

/* FIXME: update for 16 byte CDBs, such as:
   lock unlock cache 16
   pre-fetch 16
   read 16
   rebuild 16
   regenerate 16
   synchronize cache 16
   verify 16
   write 16
   write and verify 16
   write same 16
   xdwrite extended 16

   Then increase ISCSI_MAX_CMD_LEN to 16 in iscsi.h.
*/
/* FIXME: for that matter, check the existing list for correctness */
static int
iscsi_set_direction(iscsi_task_t *task)
{
    if (task && task->scsi_cmnd)
        switch (task->scsi_cmnd->cmnd[0]) {
            case TEST_UNIT_READY:
            case START_STOP:
            case REZERO_UNIT:
            case WRITE_FILEMARKS:
            case SPACE:
            case ERASE:
            case ALLOW_MEDIUM_REMOVAL:
                /* just control commands */
                set_bit(ISCSI_TASK_CONTROL, &task->flags);
                return ISCSI_TASK_CONTROL;
            case WRITE_6:           case WRITE_10:          case WRITE_12: 
            case 0x8a: /* WRITE_16 */        case 0x8e: /* write and verify 16 */
            case 0x93: /* write same 16 */
            case WRITE_LONG:        case WRITE_SAME:        case WRITE_BUFFER:
            case WRITE_VERIFY:      case WRITE_VERIFY_12:
            case COMPARE:           case COPY:              case COPY_VERIFY:
            case SEARCH_EQUAL:      case SEARCH_HIGH:       case SEARCH_LOW:
            case SEARCH_EQUAL_12:   case SEARCH_HIGH_12:    case SEARCH_LOW_12:
            case FORMAT_UNIT:       case REASSIGN_BLOCKS:   case RESERVE:
            case MODE_SELECT:       case MODE_SELECT_10:    case LOG_SELECT:
            case SEND_DIAGNOSTIC:   case CHANGE_DEFINITION: case UPDATE_BLOCK:
            case SET_WINDOW:        case MEDIUM_SCAN:       case SEND_VOLUME_TAG:
            case WRITE_LONG_2:
                set_bit(ISCSI_TASK_WRITE, &task->flags);
                return ISCSI_TASK_WRITE;
        default:
            set_bit(ISCSI_TASK_READ, &task->flags);
            return ISCSI_TASK_READ;
    }
    
    return -1;
}

/* tagged queueing */
static void
iscsi_set_tag( Scsi_Cmnd *cmd, struct IscsiScsiCmdHdr *hdr )
{
    if ( cmd->device->tagged_supported ) {
        switch( cmd->tag ) {
            case HEAD_OF_QUEUE_TAG:
                hdr->flags.attr = ISCSI_ATTR_HEAD_OF_QUEUE;
                break;
            case ORDERED_QUEUE_TAG:
                hdr->flags.attr = ISCSI_ATTR_ORDERED;
                break;
            default:
                hdr->flags.attr = ISCSI_ATTR_SIMPLE;
                break;
        }
    }
    else
        hdr->flags.attr = ISCSI_ATTR_UNTAGGED;
}

void print_cmnd(Scsi_Cmnd *sc)
{
    printk("iSCSI: Scsi_Cmnd %p to (%u %u %u %u), Cmd 0x%x\n"
           "       done %p, scsi_done %p, host_scribble %p\n"
           "       reqbuf %p, req_len %u\n"
           "       buffer %p, bufflen %u\n"
           "       use_sg %u, old_use_sg %u, sglist_len %u\n"
           "       owner 0x%x, state  0x%x, eh_state 0x%x\n"
           "       cmd_len %u, old_cmd_len %u, abort_reason 0x%x\n",
           sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0],
           sc->done, sc->scsi_done, sc->host_scribble, 
           sc->request_buffer, sc->request_bufflen, sc->buffer, sc->bufflen,
           sc->use_sg, sc->old_use_sg, sc->sglist_len,
           sc->owner, sc->state, sc->eh_state, 
           sc->cmd_len, sc->old_cmd_len, sc->abort_reason);

    if (sc->cmd_len >= 12)
        printk("iSCSI: cdb %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
               sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
               sc->cmnd[4], sc->cmnd[5], sc->cmnd[6], sc->cmnd[7],
               sc->cmnd[8], sc->cmnd[9], sc->cmnd[10], sc->cmnd[11]);
    else if (sc->cmd_len >= 8)
        printk("iSCSI: cdb %02x%02x%02x%02x %02x%02x%02x%02x\n",
               sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
               sc->cmnd[4], sc->cmnd[5], sc->cmnd[6], sc->cmnd[7]);
    else if (sc->cmd_len >= 6)
        printk("iSCSI: cdb %02x%02x%02x%02x %02x%02x\n",
               sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
               sc->cmnd[4], sc->cmnd[5]);
    else if (sc->cmd_len >= 4)
        printk("iSCSI: cdb %02x%02x%02x%02x\n",
               sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3]);
    else if (sc->cmd_len >= 2)
        printk("iSCSI: cdb %02x%02x\n", sc->cmnd[0], sc->cmnd[1]);

    if (sc->use_sg && sc->request_buffer) {
        struct scatterlist *sglist = (struct scatterlist *)sc->request_buffer;
        int i;
        
        for (i = 0; i < sc->use_sg; i++) {
            printk("iSCSI: sglist %p[%02d] = addr %p, len %u\n", 
                   (struct scatterlist *)sc->request_buffer, i, sglist->address, sglist->length);
            sglist++;
        }
    }
}

#ifdef DEBUG
/* caller must hold the session's scsi_cmnd_lock */
static void print_session_cmnds(iscsi_session_t *session)
{
    Scsi_Cmnd *search = session->scsi_cmnd_head;
    printk("iSCSI: session %p to %s unsent cmnd queue: head %p, tail %p, num %u\n", 
           session, session->log_name, session->scsi_cmnd_head, session->scsi_cmnd_tail,
           atomic_read(&session->num_cmnds));
    while (search) {
        printk("iSCSI: session %p u cmnd %p: state %4x, eh_state %4x, scribble %p, Cmd 0x%x to (%u %u %u %u)\n",
               session, search, search->state, search->eh_state, search->host_scribble,
               search->cmnd[0], search->host->host_no, search->channel, search->target, search->lun);
        search = (Scsi_Cmnd *)search->host_scribble;
    }
    printk("iSCSI: session %p to %s ignored cmnd queue: head %p, tail %p, num %u\n", 
           session, session->log_name, session->ignored_cmnd_head, session->ignored_cmnd_tail,
           atomic_read(&session->num_ignored_cmnds));
    search = session->ignored_cmnd_head;
    while (search) {
        printk("iSCSI: session %p i cmnd %p: state %4x, eh_state %4x, scribble %p, Cmd 0x%x to (%u %u %u %u)\n",
               session, search, search->state, search->eh_state, search->host_scribble,
               search->cmnd[0], search->host->host_no, search->channel, search->target, search->lun);
        search = (Scsi_Cmnd *)search->host_scribble;
    }
}

/* caller must hold the session's task_lock */
static void print_session_tasks(iscsi_session_t *session)
{
    iscsi_task_t *task = NULL;
    Scsi_Cmnd *cmnd = NULL;

    printk("iSCSI: session %p to %s task queue: head %p, tail %p, num %u\n", 
           session, session->log_name, session->arrival_order.head, session->arrival_order.tail,  
           atomic_read(&session->num_active_tasks));

    task = session->arrival_order.head;
    while (task) {
        if ((cmnd = task->scsi_cmnd)) 
            printk("iSCSI: session %p task %p itt %u with cmnd %p: state %4x, eh_state %4x, scribble %p, Cmd 0x%x to (%u %u %u %u)\n",
                   session, task, task->itt, cmnd, cmnd->state, cmnd->eh_state, cmnd->host_scribble,
                   cmnd->cmnd[0], cmnd->host->host_no, cmnd->channel, cmnd->target, cmnd->lun);
        else
            printk("iSCSI: session %p task %p itt %u for NULL cmnd\n", session, task, task->itt);
        
        task = task->order_next;
    }
}

#endif

/* the Scsi_Cmnd's request_bufflen doesn't always match the actual amount of data
 * to be read or written.  Try to compensate by decoding the cdb.
 */
static unsigned int expected_data_length(Scsi_Cmnd *sc)
{
    switch (sc->cmnd[0]) {
        case INQUIRY:
        case REQUEST_SENSE:
            return sc->cmnd[4];
        default:
            return sc->request_bufflen;
    }
}

static void
iscsi_xmit_data(iscsi_task_t *task, uint32_t ttt, uint32_t data_offset, uint32_t data_length)
{
    struct msghdr msg;
    struct IscsiDataHdr stdh;
    Scsi_Cmnd *sc = NULL;
    iscsi_session_t *session = task->session;
    struct scatterlist *sglist = NULL;
    int wlen, rc, index, iovn;
    uint32_t segOffset=0;
    int i, remain, xfrlen, segSN = 0;
    int bytes_to_fill, bytes_from_segment;

    /* make sure we have data to send */
    sc = task->scsi_cmnd;
    if (!sc) {
        printk("iSCSI: xmit_data task %p, cmnd NULL, ttt %u, offset %u, length %u\n", 
               task, ttt, data_offset, data_length);
        return;
    }
    if ((sc->request_bufflen == 0) || (sc->request_buffer == NULL)) {
        printk("iSCSI: xmit_data for itt %u, task %p, sc %p, dlength %u, expected %u, no data in buffer\n"
               "       request_buffer %p len %u, buffer %p len %u\n",
               task->itt, task, sc, data_length, expected_data_length(sc),
               sc->request_buffer, sc->request_bufflen, sc->buffer, sc->bufflen);
        print_cmnd(sc);
        return;
    }
    if ((data_length == 0) || (expected_data_length(sc) == 0)) {
        printk("iSCSI: xmit_data for itt %u, task %p, data length %u, expected %u\n"
               "       request_buffer %p len %u, buffer %p len %u\n",
               task->itt, task, data_length, expected_data_length(sc),
               sc->request_buffer, sc->request_bufflen, sc->buffer, sc->bufflen);
        print_cmnd(sc);
        return;
    }
    remain = data_length;
    
    memset( &stdh, 0, sizeof(stdh) );
    stdh.opcode = ISCSI_OP_SCSI_DATA;
    stdh.itt = htonl(task->itt);
    stdh.ttt = ttt;
    stdh.offset = htonl(data_offset);
    
    session->TxIov[0].iov_base = &stdh;
    session->TxIov[0].iov_len  = sizeof(stdh);

#if DEBUG_FLOW
    if (LOG_ENABLED(ISCSI_LOG_FLOW))
        printk("iSCSI: xmit_data for itt %u, task %p, credit %d @ %u\n"
               "       request_buffer %p len %u, buffer %p len %u\n",
               task->itt, task, remain, data_offset,
               sc->request_buffer, sc->request_bufflen, sc->buffer, sc->bufflen);
#endif
    
    /* Find the segment and offset within the segment to start writing from.  */
    index = -1;
    if ( sc->use_sg ) {
        sglist = (struct scatterlist *)sc->request_buffer;
        segOffset = data_offset;
        for (i = 0; i < sc->use_sg; i++) {
            if (segOffset < sglist->length) {
                index = i;
                break;
            }
            segOffset -= sglist->length;
            sglist++;
        }

        /* FIXME: we seem to be getting commands that indicate scatter-gather, 
         * but have all zeroes in the sglist.
         */
        if (index < 0) {
            sglist = (struct scatterlist *)sc->request_buffer;
            printk("iSCSI: xmit_data for itt %u couldn't find offset %u in sglist %p, sc %p, bufflen %u, use_sg %u\n",
                   task->itt, data_offset, sglist, sc, sc->request_bufflen, sc->use_sg);
            print_cmnd(sc);
            return;
        }

        DEBUG_FLOW4("iSCSI: index %d, sglist %p length %d, segOffset %d\n", index, sglist, sglist->length, segOffset);
    }

    ISCSI_TRACE(ISCSI_TRACE_TxData, sc, task, data_offset, data_length);
    
    /* Our starting point is now segOffset within segment index.
     * Start sending the data.
     */
    while (!signal_pending(current) && remain) {
        stdh.datasn = htonl(segSN++);
        stdh.offset = htonl(data_offset);
        stdh.expstatsn = htonl(session->ExpStatSn);
        
        if (session->DataPDULength && (remain > session->DataPDULength)) {
            /* enforce a DataPDULength limit */
            bytes_to_fill = session->DataPDULength;
        } 
        else {
            bytes_to_fill = remain;
            stdh.final = 1;
        }
        
        DEBUG_FLOW4("iSCSI: remain %d, bytes_to_fill %d, sc->use_sg %u, dataPDUlength %d\n", 
                    remain, bytes_to_fill, sc->use_sg, session->DataPDULength);

        if ( sc->use_sg ) {
            iovn = 1;
            
            xfrlen = 0;
            /* while there is more data and we want to send more data */
            while (bytes_to_fill > 0) {
                if (index >= sc->use_sg) {
                    printk("iSCSI: xmit_data index %d, sc->use_sg %d, out of buffer\n", index, sc->use_sg);
                    return;
                }
                if (signal_pending(current)) {
                    DEBUG_FLOW0("iSCSI: signal pending, returning from xmit_data\n");
                    return;
                }

                bytes_from_segment = sglist->length - segOffset;
                if ( bytes_from_segment > bytes_to_fill ) {
                    /* last data for this PDU */
                    xfrlen += bytes_to_fill;
                    session->TxIov[iovn].iov_base = sglist->address + segOffset;
                    session->TxIov[iovn].iov_len  = bytes_to_fill;
                    iovn++;
                    segOffset += bytes_to_fill;
                    DEBUG_FLOW3("iSCSI: index %d, xfrlen %d, to_fill %d, last segment\n", 
                                index, xfrlen, bytes_to_fill);
                    break;
                } 
                else {
                    /* need all of this segment, and more from the next */
                    xfrlen += bytes_from_segment;
                    session->TxIov[iovn].iov_base = sglist->address + segOffset;
                    session->TxIov[iovn].iov_len  = bytes_from_segment;
                    bytes_to_fill -= bytes_from_segment;
                    iovn++;
                    index++;
                    sglist++;
                    segOffset = 0;
                }
#ifdef DEBUG
                DEBUG_FLOW5("iSCSI: index %d, xfrlen %d, to_fill %d, from_segment %d, sglist %p\n", 
                            index, xfrlen, bytes_to_fill, bytes_from_segment, sglist);
#endif
            }
            
            if (xfrlen <= 0) {
                printk("iSCSI: Error xmit_data picked xfrlen of 0, index %d, sc->use_sg %d, bytes_to_fill %d\n",
                       index, sc->use_sg, bytes_to_fill);
                iscsi_drop_session(session);
                return;
            }
        } 
        else {
            /* no scatter-gather */
            if ((sc->request_buffer + data_offset + bytes_to_fill) <= (sc->request_buffer + sc->request_bufflen)) {
                /* send all the data */
                session->TxIov[1].iov_base = sc->request_buffer + data_offset;
                session->TxIov[1].iov_len = xfrlen = bytes_to_fill;
                iovn = 2;
            }
            else if ((sc->request_buffer + data_offset) < (sc->request_buffer + sc->request_bufflen)) {
                /* send some data, but can't send all requested */
                xfrlen = sc->request_bufflen - data_offset;
                printk("iSCSI: xmit_data ran out of data, buffer %p len %u but offset %d length %d, sending final %d bytes\n",
                       sc->request_buffer, sc->request_bufflen, data_offset, bytes_to_fill, xfrlen);
                session->TxIov[1].iov_base = sc->request_buffer + data_offset;
                session->TxIov[1].iov_len = xfrlen;
                iovn = 2;
                stdh.final = 1;
                remain = xfrlen;
            }
            else {
                /* can't send any data */
                printk("iSCSI: xmit_data ran out of data, buffer %p len %u but offset %d length %d, sending no more data\n",
                       sc->request_buffer, sc->request_bufflen, data_offset, bytes_to_fill);
                return;
            }
        }

        hton24(stdh.dlength, xfrlen);
        
        memset( &msg, 0, sizeof(msg) );
        msg.msg_iov = &session->TxIov[0];
        msg.msg_iovlen = iovn;
        /* msg.msg_flags = MSG_DONTWAIT; */

        ISCSI_TRACE(ISCSI_TRACE_TxDataPDU, sc, task, data_offset, xfrlen);

        /* FIXME: can we really hold the largest possible size we might need to send in an int? */
        wlen = sizeof(stdh) + xfrlen;
        rc = iscsi_sendmsg( session, &msg, wlen );
        if ( rc != wlen ) {
            printk("iSCSI: session %p xmit_data failed to send %d bytes, rc %d\n", session, wlen, rc);
            iscsi_drop_session(session);
            return;
        }
        
        remain -= xfrlen;

        DEBUG_FLOW5("iSCSI: xmit_data sent %d @ %u for itt %u, remaining %d, final %d\n",
                    xfrlen, data_offset, task->itt, remain, stdh.final);

        data_offset += xfrlen;
    }
}

static int iscsi_xmit_task(iscsi_task_t *task)
{
    struct msghdr msg;
    struct iovec iov;
    struct IscsiScsiCmdHdr stsch;
    int rc, wlen, direction;
    iscsi_session_t *session = task->session;
    Scsi_Cmnd *sc = task->scsi_cmnd;

    if (!task) {
        printk("iSCSI: xmit_task NULL\n");
        return 0;
    }

    if (!sc) {
        printk("iSCSI: xmit_task %p, cmnd NULL\n", task);
        return 0;
    }
    
#if DEBUG_FLOW
    if (LOG_ENABLED(ISCSI_LOG_FLOW))
        printk("iSCSI: xmit_task %p, itt %u to (%u %u %u %u), Cmd 0x%x, cmd_len %u, bufflen %u\n",  
               task, task->itt, sc->host->host_no, sc->channel, sc->target,sc->lun, sc->cmnd[0], 
               sc->cmd_len, sc->request_bufflen);
#endif

    wlen = sizeof(stsch);
    memset( &stsch, 0, sizeof(stsch) );

    if ((direction = iscsi_set_direction(task)) < 0) {
        printk("iSCSI: xmit_task - cmnd %p is an unsupported SCSI command, 0x%x\n", sc, sc->cmnd[0]);
        return 0;
    }
    else if ( direction == ISCSI_TASK_READ ) {
        /* read */
        stsch.flags.read_data = 1;
        stsch.data_length = htonl(expected_data_length(sc));
    }
    else if (direction == ISCSI_TASK_WRITE) {
        /* write */
        stsch.flags.write_data = 1;                                        
        stsch.data_length = htonl(expected_data_length(sc));
    }

    iscsi_set_tag( sc, &stsch );
    stsch.opcode = ISCSI_OP_SCSI_CMD;
    stsch.itt = htonl(task->itt);
    task->cmdsn = session->CmdSn;
    stsch.cmdsn = htonl(session->CmdSn);
    stsch.expstatsn = htonl(session->ExpStatSn);

    /* set the F-bit unless unsolicited Data-out PDUs will be sent */
    if (stsch.flags.write_data && !session->InitialR2T && 
        sc->request_buffer && sc->request_bufflen && expected_data_length(sc))
        stsch.flags.final = 0;
    else {
        stsch.flags.final = 1;
    }

    /* single level LUN format puts LUN in byte 1, 0 everywhere else */
    stsch.lun[1] = sc->lun;

    memcpy( stsch.scb, sc->cmnd, sc->cmd_len );

    iov.iov_base = &stsch;
    iov.iov_len = sizeof(stsch);
    memset( &msg, 0, sizeof(struct msghdr) );
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ISCSI_TRACE(ISCSI_TRACE_TxCmd, sc, task, session->CmdSn, ntohl(stsch.data_length));

    /* FIXME: possibly send ImmediateData along with the cmd PDU */

    /* FIXME: use MSG_MORE to keep write cmds in the same TCP segment as the unsolicited data? */
    rc = iscsi_sendmsg( session, &msg, wlen );
    if ( rc != wlen ) {
        printk("iSCSI: session %p xmit_task sendmsg %d failed, error %d\n", session, wlen, rc);
        iscsi_drop_session(session);
        return 0;
    }

    session->CmdSn++;
    
    if (test_bit(ISCSI_TASK_WRITE, &task->flags) && (session->InitialR2T == 0)) {
        /* send unsolicited data PDUs */
        uint32_t length;
        
        if (session->FirstBurstSize)
            task->data_length = length = MIN(session->FirstBurstSize, expected_data_length(sc));
        else
            task->data_length = length = expected_data_length(sc);
        
        iscsi_xmit_data(task, RSVD_TASK_TAG, 0U, length);
    }

    return 1;
}


static void iscsi_xmit_queued_cmnds( iscsi_session_t *session)
{
    Scsi_Cmnd *sc;
    iscsi_task_t *task;
    DECLARE_NOQUEUE_FLAGS;

    if (!session) {
        DEBUG_ERR0("iSCSI: can't xmit queued commands, no session\n");
        return;
    }

#if DEBUG_QUEUE
    if (signal_pending(current)) {
        DEBUG_QUEUE2("iSCSI: can't start tasks now, signal pending for session %p to %s\n",
                     session, session->log_name);
        return;
    }

    if (!sna_lte(session->CmdSn, session->MaxCmdSn)) {
        DEBUG_QUEUE6("iSCSI: can't start %u tasks now, ExpCmdSN %u, CmdSn %u, MaxCmdSN %u, session %p to %s\n",
                     atomic_read(&session->num_cmnds), 
                     session->ExpCmdSn, session->CmdSn, session->MaxCmdSn, 
                     session, session->log_name);
        return;
    }

    if (test_bit(SESSION_RESETTING, &session->control_bits)) {
        DEBUG_QUEUE2("iSCSI: resetting session %p to %s, can't start tasks now\n",
                    session, session->log_name);
        return;
    }

    if (atomic_read(&session->num_cmnds) == 0) {
        DEBUG_QUEUE2("iSCSI: no SCSI cmnds queued for session %p to %s\n", session, session->log_name);
        return;
    }

    DEBUG_QUEUE4("iSCSI: xmit_queued_cmnds, MaxCmdSN %u, session %p to %s, cpu%d\n", 
                 session->MaxCmdSn, session, session->log_name, smp_processor_id());
#endif

    while ((atomic_read(&session->num_cmnds) > 0) &&
           !signal_pending(current) && 
           sna_lte(session->CmdSn, session->MaxCmdSn) && 
           !test_bit(SESSION_RESETTING, &session->control_bits)) {

        /* allocate a task, without holding any locks, so that the
         * allocation function may block if necessary.  
         */
        task = alloc_task(session);
        if (!task) {
            DEBUG_ERR2("iSCSI: couldn't allocate task for session %p to %s\n",
                       session, session->log_name);
            /* to prevent a stall of the driver, free_task must wakeup
             * the tx thread later.
             */
            return;
        }

        /* add the task to the session before removing the Scsi_Cmnd
         * from the queue, to ensure the error handlers can always
         * find either the command or the task.  Need to hold both
         * locks to atomically convert a cmnd to a task, and must
         * respect the lock order.  
         */
        spin_lock(&session->task_lock);

        /* try to grab one SCSI command off the session's command queue */
        SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
        if ((sc = session->scsi_cmnd_head) == NULL) {
            /* this should never happen if session->num_cmnds is accurate */
            printk("iSCSI: bug - no SCSI cmnds queued at %lu for session %p, num_cmnds %u, head %p, tail %p\n", 
                   jiffies, session, atomic_read(&session->num_cmnds), session->scsi_cmnd_head, session->scsi_cmnd_tail);
            atomic_set(&session->num_cmnds, 0);
            SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
            spin_unlock(&session->task_lock);
            free_task(task);
            return;
        }

        /* FIXME: handle cmnds queued after a session drop, which we need to allow to timeout and be aborted */
        if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
            /* pretend we sent the task, and let it time out and abort.
             * this can happen after a session drop
             */
            printk("iSCSI: xmit_queued_cmnds is trying to send cmnd %p, 0x%x to (%u %u %u %u), but session %p is not established\n",
                   sc, sc->cmnd[0], sc->host->host_no, sc->channel, sc->target, sc->lun, session);
        }
        atomic_dec(&session->num_cmnds);

        task->scsi_cmnd = sc;
        atomic_inc(&task->refcount);
        add_session_task(session, task);
        add_task(&session->rx_tasks, task);

        /* remove Cmnd from queue */
        session->scsi_cmnd_head = (Scsi_Cmnd *)sc->host_scribble;
        sc->host_scribble = NULL;
        if (session->scsi_cmnd_head == NULL)
            session->scsi_cmnd_tail = NULL;
        
#if (DEBUG_SMP > 0) || (DEBUG_QUEUE > 0)
        if (LOG_ENABLED(ISCSI_LOG_SMP) || LOG_ENABLED(ISCSI_LOG_QUEUE))
            printk("iSCSI: cmnd %p became task %p at %lu for session %p, num_cmnds %u, head %p, tail %p\n", 
                   sc, task, jiffies, session, atomic_read(&session->num_cmnds), session->scsi_cmnd_head, session->scsi_cmnd_tail);
#endif
        
        SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
        spin_unlock(&session->task_lock);

        DEBUG_FLOW4("iSCSI: sending itt %u on session %p as CmdSN %u, MaxCmdSn %u\n",
                     task->itt, session, session->CmdSn, session->MaxCmdSn);

        if (iscsi_xmit_task(task)) {
            DEBUG_FLOW4("iSCSI: sent itt %u, task %p, cmnd %p, refcount %d\n", task->itt, task, sc, atomic_read(&task->refcount));
            atomic_dec(&task->refcount);
            DEBUG_FLOW3("iSCSI: after sending itt %u, task %p now has refcount %d\n", task->itt, task, atomic_read(&task->refcount));
        }
        else {
            printk("iSCSI: failed to send itt %u, task %p, cmnd %p, returning to queue\n", 
                   task->itt, task, sc);
            
            spin_lock(&session->task_lock);

            /* put it back on the head of the Scsi_Cmnd queue, we couldn't send it */
            SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
            if ( session->scsi_cmnd_head ) {
                sc->host_scribble = (unsigned char *)session->scsi_cmnd_head;
                session->scsi_cmnd_head = sc;
            }
            else {
                sc->host_scribble = NULL;
                session->scsi_cmnd_head = session->scsi_cmnd_tail = sc;
            }
            atomic_inc(&session->num_cmnds);
            SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

            /* get rid of the task */
            remove_task(&session->rx_tasks, task->itt);
            if (!remove_session_task(session, task)) {
                printk("iSCSI: bug - couldn't xmit task %p, or remove it from session %p to %s\n",
                       task, session, session->log_name);
                task = NULL;
            }
            spin_unlock(&session->task_lock);

            /* if we removed it from the session, free it */
            if (task)
                free_task(task);

            /* don't try to send any more right now */
            return;
        }
    }
}

static void iscsi_xmit_r2t_data(iscsi_session_t *session)
{
    iscsi_task_t *task;
    uint32_t itt;
    uint32_t ttt;
    uint32_t offset;
    uint32_t length;

    spin_lock(&session->task_lock);
    while ((task = pop_task(&session->tx_tasks))) {
        itt = task->itt;
        /* save the values that get set when we receive an R2T before putting the task in rx_tasks */
        ttt = task->ttt;
        offset = task->data_offset;
        length = task->data_length;
        atomic_inc(&task->refcount);
        add_task(&session->rx_tasks, task);
        spin_unlock(&session->task_lock);
        
        /* if we can't send any unsolicited data, try to start
         * commands as soon as possible, so that we can
         * overlap the R2T latency with the time it takes to
         * send data for commands already issued.  Command
         * PDUs are small, so this increases throughput
         * without significantly increasing the completion
         * time of commands already issued.
         */
        if (session->InitialR2T && !session->ImmediateData) {
            DEBUG_FLOW1("iSCSI: checking for new commands before sending R2T data to %s\n", 
                        session->log_name);
            iscsi_xmit_queued_cmnds(session);
        }
        
        /* send the requested data */
        DEBUG_FLOW6("iSCSI: sending R2T data (%u @ %u) for itt %u, ttt %u, task %p to %s\n", 
                    length, offset, itt, ntohl(ttt), task, session->log_name);
        iscsi_xmit_data(task, ttt, offset, length);

        atomic_dec(&task->refcount);
        
        if (signal_pending(current))
            return;

        /* relock before checking loop condition */
        spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}


/* send a reply to a nop that requested one */
static void
iscsi_xmit_nop_reply(iscsi_session_t *session, iscsi_nop_info_t *nop_info)
{
    struct IscsiNopOutHdr stnoh;
    struct msghdr msg;
    struct iovec iov[3];
    int rc;
    int pad[4];
    int length = sizeof(stnoh);

    memset( &stnoh, 0, sizeof(stnoh) );
    stnoh.opcode = ISCSI_OP_NOOP_OUT;
    stnoh.opcode |= ISCSI_OP_IMMEDIATE;
    stnoh.itt  = RSVD_TASK_TAG;
    stnoh.ttt  = nop_info->ttt;
    memcpy(stnoh.lun, nop_info->lun, sizeof(stnoh.lun));
    hton24(stnoh.dlength, nop_info->dlength);
    stnoh.cmdsn  = htonl(session->CmdSn); /* don't increment after immediate cmds */
    stnoh.expstatsn = htonl(session->ExpStatSn);

    iov[0].iov_base = &stnoh;
    iov[0].iov_len = sizeof(stnoh);
    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov = iov;
    if (nop_info->dlength) {
        /* PDU header */
        /* data */
        iov[1].iov_base = nop_info->data;
        iov[1].iov_len = nop_info->dlength;
        /* pad */
        if (nop_info->dlength % PAD_WORD_LEN) {
            memset(&pad, 0, sizeof(pad));
            msg.msg_iovlen = 3;
            iov[2].iov_base = &pad;
            iov[2].iov_len = PAD_WORD_LEN - (nop_info->dlength % PAD_WORD_LEN);
            length = sizeof(stnoh) + nop_info->dlength + iov[2].iov_len;
        }
        else {
            msg.msg_iovlen = 2;
            length = sizeof(stnoh) + nop_info->dlength;
        }
    }
    else {
        /* just a PDU header */
        msg.msg_iovlen = 1;
        length = sizeof(stnoh);
    }
    
    rc = iscsi_sendmsg( session, &msg, length);
    if ( rc != length ) {
        DEBUG_ERR2("iSCSI: xmit_nop %d failed, rc %d\n", length, rc);
        iscsi_drop_session(session);
    }
    
    ISCSI_TRACE( ISCSI_TRACE_TxNopReply, NULL, NULL, nop_info->ttt, nop_info->dlength);
}

/* send replies for NopIns that requested them */
static void
iscsi_xmit_nop_replys(iscsi_session_t *session)
{
    iscsi_nop_info_t *nop_info;

    /* these aren't really tasks, but it's not worth having a separate lock for them */
    spin_lock(&session->task_lock);
    while ((nop_info = session->nop_reply_head)) {
        session->nop_reply_head = nop_info->next;
        if (!session->nop_reply_head)
            session->nop_reply_tail = NULL;
        spin_unlock(&session->task_lock);

        iscsi_xmit_nop_reply(session, nop_info);
        kfree(nop_info);
        DEBUG_ALLOC1("iSCSI: kfree nop_info %p after sending nop reply\n", nop_info);

        if (signal_pending(current))
            return;
        
        /* relock before checking loop condition */
        spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}

static int
iscsi_xmit_abort(iscsi_task_t *task, uint32_t mgmt_itt)
{
    struct msghdr msg;
    struct iovec iov;
    int rc;
    iscsi_session_t *session;
    struct IscsiScsiTaskMgtHdr ststmh;
    Scsi_Cmnd *sc = task->scsi_cmnd;

    session = task->session;
    if ( ! session ) {
        printk("iSCSI: no session for task %p, command %p, can't abort\n", task, sc);
        return 0;
    }

    memset( &ststmh, 0, sizeof(ststmh) );
    ststmh.opcode = ISCSI_OP_SCSI_TASK_MGT_MSG;
    /* Flag it as an Immediate CMD */
    ststmh.opcode |= ISCSI_OP_IMMEDIATE;

    /* allocate an itt.  The reply will only have the rtt
     * if the task was not found, so we need to be able to find
     * the task being aborted based on the abort's itt.
     */
    task->mgmt_itt = mgmt_itt;
    ststmh.itt = htonl(task->mgmt_itt);
    ststmh.rtt = htonl(task->itt);

    ststmh.lun[1] = sc->lun;
    ststmh.function = ISCSI_TM_FUNC_ABORT_TASK;
    ststmh.cmdsn = htonl(session->CmdSn);   /* CmdSN not incremented after imm cmd */
    ststmh.expstatsn = htonl(session->ExpStatSn);

    iov.iov_base = &ststmh;
    iov.iov_len = sizeof(ststmh);
    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ISCSI_TRACE(ISCSI_TRACE_TxAbort, sc, task, task->mgmt_itt, 0);

    printk("iSCSI: sending abort itt %u for task %p, rtt %u\n",
           ntohl(ststmh.itt), task, task->itt);

    /* indicate that we're done with the task */
    atomic_dec(&task->refcount);
    task = NULL;
    sc = NULL;

    /* send the abort */
    rc = iscsi_sendmsg( session, &msg, sizeof(ststmh) );
    if ( rc != sizeof(ststmh) ) {
        DEBUG_ERR3("iSCSI: xmit_abort failed, itt %u, rtt %u, rc %d\n", 
                   ntohl(ststmh.itt), ntohl(ststmh.rtt), rc);
        iscsi_drop_session(session);
        return 0;
    }

    return 1;
}

/* send aborts for every task that needs one */
static void iscsi_xmit_aborts(iscsi_session_t *session) 
{
    iscsi_task_t *task;
    uint32_t mgmt_itt;

    spin_lock(&session->task_lock);
    while ((task = pop_task(&session->tx_abort_tasks))) {
        atomic_inc(&task->refcount);
        add_task(&session->rx_abort_tasks, task);
        mgmt_itt = allocate_itt(session);
        spin_unlock(&session->task_lock);
    
        iscsi_xmit_abort(task, mgmt_itt);
        /* xmit_abort decrements the task refcount when it's done with it */

        if (signal_pending(current))
            return;

        /* relock before checking loop condition */
        spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}

static int
iscsi_xmit_reset(iscsi_session_t *session, uint8_t reset_type, iscsi_task_t *task, uint32_t mgmt_itt)
{
    struct msghdr msg;
    struct iovec iov;
    int rc;
    struct IscsiScsiTaskMgtHdr ststmh;
    Scsi_Cmnd *sc = NULL;

    memset( &ststmh, 0, sizeof(ststmh) );
    ststmh.opcode = ISCSI_OP_SCSI_TASK_MGT_MSG;
    /* Flag it as an Immediate CMD */
    ststmh.opcode |= ISCSI_OP_IMMEDIATE;

    /* record the itt */
    if (task)
        task->mgmt_itt = mgmt_itt;
    if (reset_type == ISCSI_TM_FUNC_TARGET_WARM_RESET)
        session->warm_reset_itt = mgmt_itt;
    if (reset_type == ISCSI_TM_FUNC_TARGET_COLD_RESET)
        session->cold_reset_itt = mgmt_itt;

    ststmh.itt = htonl(mgmt_itt); 
    ststmh.function = reset_type;
    ststmh.cmdsn = htonl(session->CmdSn);   /* CmdSN not incremented after imm cmd */
    ststmh.expstatsn = htonl(session->ExpStatSn);

    switch (reset_type) {
        case ISCSI_TM_FUNC_LOGICAL_UNIT_RESET:
            /* need a LUN for this */
            if (task && task->scsi_cmnd) {
                sc = task->scsi_cmnd;
                ststmh.lun[1] = sc->lun;
                printk("iSCSI: sending logical unit reset to (%u %u %u %u), itt %u\n", 
                       sc->host->host_no, sc->channel, sc->target, sc->lun, ntohl(ststmh.itt));
            }
            else {
                printk("iSCSI: failed to send logical unit reset, no SCSI command\n");
                return 0;
            }
            break;
        case ISCSI_TM_FUNC_TARGET_WARM_RESET:
            printk("iSCSI: sending target warm reset for session %p to %s, itt %u\n", 
                   session, session->log_name, ntohl(ststmh.itt));
            break;
        case ISCSI_TM_FUNC_TARGET_COLD_RESET:
            printk("iSCSI: sending target cold reset for session %p to %s, itt %u\n", 
                   session, session->log_name, ntohl(ststmh.itt));
            break;
        default:
            printk("iSCSI: unknown reset type %u for session %p to %s\n", 
                   reset_type, session, session->log_name);
            return 0;
            break;
    }

    iov.iov_base = &ststmh;
    iov.iov_len = sizeof(ststmh);
    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ISCSI_TRACE(ISCSI_TRACE_TxReset, sc, task, ntohl(ststmh.itt), reset_type);

    /* send the reset */
    rc = iscsi_sendmsg( session, &msg, sizeof(ststmh) );
    if ( rc != sizeof(ststmh) ) {
        DEBUG_ERR1("iSCSI: xmit_reset failed, rc %d\n", rc);
        iscsi_drop_session(session);
        return 0;
    }
    
    return 1;
}

static void iscsi_xmit_lun_resets(iscsi_session_t *session)
{
    iscsi_task_t *task;
    uint32_t mgmt_itt;

    spin_lock(&session->task_lock);
    while ((task = pop_task(&session->tx_lun_reset_tasks))) {
        atomic_inc(&task->refcount);
        add_task(&session->rx_lun_reset_tasks, task);
        mgmt_itt = allocate_itt(session);
        spin_unlock(&session->task_lock);

        iscsi_xmit_reset(session, ISCSI_TM_FUNC_LOGICAL_UNIT_RESET, task, mgmt_itt);
        atomic_dec(&task->refcount);

        if (signal_pending(current))
            return;

        /* relock before checking loop condition */
        spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}


static void
iscsi_xmit_ping(iscsi_session_t *session, uint32_t itt)
{
    struct IscsiNopOutHdr stph;
    struct msghdr msg;
    struct iovec iov;
    int rc;
    
    memset( &stph, 0, sizeof(stph) );
    stph.opcode = ISCSI_OP_NOOP_OUT;
    stph.opcode |= ISCSI_OP_IMMEDIATE;
    stph.poll = 1;  /* draft 8 always wants this bit set now */
    stph.itt  = htonl(itt); /* draft 8 reply request */
    stph.ttt  = RSVD_TASK_TAG;
    stph.cmdsn = htonl(session->CmdSn);
    stph.expstatsn = htonl(session->ExpStatSn);

    iov.iov_base = &stph;
    iov.iov_len = sizeof(stph);
    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    ISCSI_TRACE(ISCSI_TRACE_TxPing, NULL, NULL, ntohl(stph.itt), 0);

    rc = iscsi_sendmsg( session, &msg, sizeof(stph) );
    if ( rc != sizeof(stph) ) {
        DEBUG_ERR1("iSCSI: XmitPing error %d\n",rc);
        iscsi_drop_session(session);
    }
}


/* the writer thread */
static int iscsi_tx_thread( void *vtaskp )
{
    iscsi_session_t *session;

    if ( ! vtaskp ) {
        DEBUG_ERR0("iSCSI: tx thread task parameter NULL\n");
        return 0;
    }

    session = (iscsi_session_t *)vtaskp;
    atomic_inc(&session->refcount);

    /* become a daemon kernel thread, and abandon any user space resources */
    sprintf(current->comm,"iscsi-tx");
    iscsi_daemonize(); 
    session->tx_pid = current->pid;
    current->flags |= PF_MEMALLOC;
    mb();

    /* Block all signals except SIGHUP and SIGKILL */
    spin_lock_irq(&current->sigmask_lock);
    siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGHUP));
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);                                                            

    
    DEBUG_INIT3("iSCSI: tx thread %d for session %p starting cpu%d\n", current->pid, session, smp_processor_id());

    while (!test_bit(SESSION_TERMINATING, &session->control_bits)) {

        DEBUG_INIT3("iSCSI: tx thread %d for session %p waiting for new session to be established at %lu\n",
                    current->pid, session, jiffies);

        /* wait for a session to be established */
        while (!test_bit(SESSION_ESTABLISHED, &session->control_bits) || (session->socket == NULL)) {
            /* tell the rx thread that we're blocked, and that it can
             * safely call iscsi_sendmsg now as part of the Login
             * phase, since we're guaranteed not to be doing any IO
             * until the session is up.  We don't use a semaphore
             * because the counts might get off if we receive signals,
             * and loop again.  This is really just an on/off setting.  
             */
            set_bit(TX_THREAD_BLOCKED, &session->control_bits);
            wake_up(&session->tx_blocked_wait_q);
            
            /* wait for the rx thread to tell us the session is up.
             * We could use a semaphore, but we want to be able to
             * wakeup both the tx thread an ioctl call when a session
             * first comes up, so we use a condition variable
             * equivalent.  
             */
            DEBUG_INIT3("iSCSI: tx thread %d blocking on session %p at %lu\n", current->pid, session, jiffies);
            wait_event_interruptible(session->login_wait_q, test_bit(SESSION_ESTABLISHED, &session->control_bits));

            if (iscsi_handle_signals(session)) {
                printk("iSCSI: tx thread %d signalled at %lu while waiting for session %p\n", current->pid, jiffies, session);
            }
            
            if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
                /* we're all done */
                goto ThreadExit;
            }
        }

        /* we're up and running with a new session */
        clear_bit(TX_THREAD_BLOCKED, &session->control_bits);
        DEBUG_INIT4("iSCSI: tx thread %d for session %p starting to process new session with socket %p at %lu\n",
                    current->pid, session, session->socket, jiffies);

        /* make sure we start sending commands again */
        set_bit(TX_SCSI_COMMAND, &session->control_bits);
        set_bit(TX_WAKE, &session->control_bits);
        
        /* process tx requests for this session, until the session drops */
        while (!signal_pending(current)) {
            
            DEBUG_FLOW3("iSCSI: tx thread %d for session %p waiting at %lu\n", session->tx_pid, session, jiffies);
            wait_event_interruptible(session->tx_wait_q, test_and_clear_bit(TX_WAKE, &session->control_bits));
            
            DEBUG_FLOW3("iSCSI: tx thread %d for session %p is awake at %lu\n", session->tx_pid, session, jiffies);

            if (signal_pending(current)) break;
            
            /* See if we should send a ping (Nop with reply requested) */
            if (test_and_clear_bit(TX_PING, &session->control_bits)) {
                uint32_t itt;

                DEBUG_FLOW1("iSCSI: sending Nop/poll on session %p\n", session);
                /* may need locking someday.  see allocate_itt comment */
                itt = allocate_itt(session);
                iscsi_xmit_ping(session, itt);
            }

            if (signal_pending(current)) break;
            
            /* See if we should send one or more Nops (replies requested by the target) */
            if (test_and_clear_bit(TX_NOP_REPLY, &session->control_bits)) {
                DEBUG_FLOW1("iSCSI: sending Nop replies on session %p\n", session);
                iscsi_xmit_nop_replys(session);
            }

            if (signal_pending(current)) break;
            
            /* See if we should abort any tasks */
            if (test_and_clear_bit(TX_ABORT, &session->control_bits)) {
                DEBUG_FLOW1("iSCSI: sending aborts on session %p\n", session);
                iscsi_xmit_aborts(session);
            }

            if (signal_pending(current)) break;
            
            /* See if we should reset any LUs */
            if (test_and_clear_bit(TX_LUN_RESET, &session->control_bits)) {
                DEBUG_FLOW1("iSCSI: sending logical unit resets on session %p\n", session);
                iscsi_xmit_lun_resets(session);
            }

            if (signal_pending(current)) break;
            
            /* See if we should warm reset the target */
            if (test_and_clear_bit(TX_WARM_TARGET_RESET, &session->control_bits)) {
                uint32_t itt;
                
                DEBUG_FLOW1("iSCSI: sending target warm reset to %s\n", session->log_name);
                /* may need locking someday.  see allocate_itt comment */
                itt = allocate_itt(session);
                iscsi_xmit_reset(session, ISCSI_TM_FUNC_TARGET_WARM_RESET, NULL, itt);
            }

            if (signal_pending(current)) break;

            /* See if we should cold reset the target */
            if (test_and_clear_bit(TX_COLD_TARGET_RESET, &session->control_bits)) {
                uint32_t itt;
                
                DEBUG_FLOW1("iSCSI: sending target cold reset to %s\n", session->log_name);
                /* may need locking someday.  see allocate_itt comment */
                itt = allocate_itt(session);
                iscsi_xmit_reset(session, ISCSI_TM_FUNC_TARGET_COLD_RESET, NULL, itt);
            }

            if (signal_pending(current)) break;
            
            /* See if we need to send more data after receiving an R2T */
            if (test_and_clear_bit(TX_R2T_DATA, &session->control_bits)) {
                /* NOTE: this may call iscsi_xmit_queued_cmnds under some conditions */
                iscsi_xmit_r2t_data(session);
            }

            if (signal_pending(current)) break;
            
            /* New SCSI command received, or MaxCmdSN incremented, or task freed */
            if (test_and_clear_bit(TX_SCSI_COMMAND, &session->control_bits)) {
                /* if possible, issue new commands */
                iscsi_xmit_queued_cmnds(session);
            }
        }

        /* handle any signals that may have occured */
        iscsi_handle_signals(session);
    }

 ThreadExit:        
    DEBUG_INIT2("iSCSI: tx thread %d for session %p exiting\n", session->tx_pid, session);
    
    /* the rx thread may be waiting for the tx thread to block.  make it happy */
    set_bit(TX_THREAD_BLOCKED, &session->control_bits);
    wake_up(&session->tx_blocked_wait_q);

    /* we're done */
    set_current_state(TASK_RUNNING);
    session->tx_pid = 0;
    mb();
    drop_reference(session);
    
    return 0;
}

/* 
 * complete a task in the session's completing queue, and return a pointer to it,
 * or NULL if the task could not be completed.
 */
static iscsi_task_t *complete_task(iscsi_session_t *session, uint32_t itt)
{
    iscsi_task_t *task;
    unsigned long last_log = 0;
    unsigned long interval = (HZ / 10) ? (HZ / 10) : 10;
    int refcount;
    DECLARE_IO_REQUEST_FLAGS;

    while (!signal_pending(current)) {
        DEBUG_QUEUE1("iSCSI: attempting to complete itt %u\n", itt);

        spin_lock(&session->task_lock);

        if ((task = remove_task(&session->completing_tasks, itt))) {
            Scsi_Cmnd *sc = task->scsi_cmnd;

            if (sc == NULL) {
                spin_unlock(&session->task_lock);
                DEBUG_QUEUE2("iSCSI: can't complete itt %u, task %p, no SCSI cmnd\n",
                             itt, task);
                return NULL;
            }
            
            if (test_bit(ISCSI_TASK_ABORTING, &task->flags)) {
                spin_unlock(&session->task_lock);
                DEBUG_QUEUE4("iSCSI: can't complete itt %u, task %p, cmnd %p, aborting with mgmt_itt %u\n",
                             itt, task, sc, task->mgmt_itt);
                return NULL;
            }

            /* it's possible the tx thread is using the task right now.
             * the task's refcount can't increase while it's in the completing
             * collection, so wait for the refcount to hit zero, or the task
             * to leave the completing collection, whichever happens first.
             */
            if ((refcount = atomic_read(&task->refcount)) == 0) {
                /* this is the expected case */
#if DEBUG_EH
                if (LOG_ENABLED(ISCSI_LOG_EH) && (sc->cmnd[0] == TEST_UNIT_READY)) {
                    printk("iSCSI: completing TUR at %lu, itt %u, task %p, command %p, (%u %u %u %u), Cmd 0x%x, result 0x%x\n", 
                           jiffies, itt, task, sc, 
                           sc->host->host_no, sc->channel, sc->target, sc->lun, 
                           sc->cmnd[0], sc->result);
                }
                else
#endif
                {
#if DEBUG_QUEUE
                    if (LOG_ENABLED(ISCSI_LOG_QUEUE))
                        printk("iSCSI: completing itt %u, task %p, command %p, (%u %u %u %u), Cmd 0x%x, result 0x%x\n", 
                               itt, task, sc, 
                               sc->host->host_no, sc->channel, sc->target, sc->lun, 
                               sc->cmnd[0], sc->result);
#endif
                }

                /* remove the task from the session, to ensure a
                 * session drop won't try to complete the task again.
                 */
                if (remove_session_task(session, task)) {
                    DEBUG_QUEUE4("iSCSI: removed itt %u, task %p from session %p to %s\n", 
                                 task->itt, task, session, session->log_name);
                }

                /* this task no longer has a Scsi_Cmnd associated with it */
                task->scsi_cmnd = NULL;

                ISCSI_TRACE(ISCSI_TRACE_CmdDone, sc, task, sc->result, 0);

                /* tell the SCSI midlayer that the command is done */
                LOCK_IO_REQUEST_LOCK;
                if (sc->scsi_done)
                    sc->scsi_done(sc);
                UNLOCK_IO_REQUEST_LOCK;

                spin_unlock(&session->task_lock);
#if DEBUG_SMP || DEBUG_QUEUE
                if (LOG_ENABLED(ISCSI_LOG_SMP) || LOG_ENABLED(ISCSI_LOG_QUEUE))
                    printk("iSCSI: completed itt %u, task %p, command %p, (%u %u %u %u), Cmd 0x%x, result 0x%x\n", 
                           itt, task, sc, 
                           sc->host->host_no, sc->channel, sc->target, sc->lun, 
                           sc->cmnd[0], sc->result);
#endif

                
                return task;
            }
            else {
                /* task is still in use, can't complete it yet.  Since
                 * this only happens when a command is aborted by the
                 * target unexpectedly, this error case can be slow.
                 * Just keep polling for the refcount to hit zero.  If
                 * the tx thread is blocked while using a task, the
                 * timer thread will eventually send a signal to both
                 * the rx thread and tx thread, so this loop will
                 * terminate one way or another.  
                 */
                if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                    DEBUG_QUEUE4("iSCSI: waiting to complete itt %u, task %p, cmnd %p, refcount %d\n", itt, task, sc, refcount);
                }

                push_task(&session->completing_tasks, task);
                spin_unlock(&session->task_lock);

                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(interval);
            }
        }
        else {
            /* an abort removed it from the completing collection. */
            DEBUG_QUEUE1("iSCSI: can't complete itt %u, task no longer completing\n", itt);
            spin_unlock(&session->task_lock);
            return NULL;
        }
    }

    return NULL;
}


static void iscsi_recv_nop(iscsi_session_t *session, struct IscsiNopInHdr *stnih, unsigned char *data)
{
    int dlength = ntoh24(stnih->dlength);

    DEBUG_FLOW2("iSCSI: recv_nop for session %p from %s\n", session, session->log_name);
    ISCSI_TRACE( ISCSI_TRACE_RxNop, NULL, NULL, ntohl(stnih->itt), stnih->ttt);

    session->ExpStatSn = ntohl(stnih->statsn) + 1;
    updateSN(session, ntohl(stnih->expcmdsn), ntohl(stnih->maxcmdsn));

    /* check the ttt to decide whether to reply with a Nop-out */
    if (stnih->ttt != RSVD_TASK_TAG) {
        iscsi_nop_info_t *nop_info = kmalloc(sizeof(iscsi_nop_info_t) + dlength, GFP_ATOMIC);

        if (nop_info) {
            DEBUG_ALLOC2("iSCSI: kmalloc nop_info %p, %u bytes\n", nop_info, sizeof(iscsi_nop_info_t) + dlength);
            nop_info->next = NULL;
            nop_info->ttt = stnih->ttt;
            memcpy(nop_info->lun, stnih->lun, sizeof(nop_info->lun));
            nop_info->dlength = dlength;
            if (dlength)
                memcpy(nop_info->data, data, dlength);
            spin_lock(&session->task_lock);
            if (session->nop_reply_head) {
                session->nop_reply_tail->next = nop_info;
                session->nop_reply_tail = nop_info;
            }
            else {
                session->nop_reply_head = session->nop_reply_tail = nop_info;
            }
            spin_unlock(&session->task_lock);
            printk("iSCSI: queued nop reply for ttt %u, dlength %d\n", ntohl(stnih->ttt), dlength);
            wake_tx_thread(TX_NOP_REPLY, session);
        }
        else {
            printk("iSCSI: couldn't queue nop reply for ttt %u\n", ntohl(stnih->ttt));
        }
    }
}

static void
iscsi_recv_cmd(iscsi_session_t *session, struct IscsiScsiRspHdr *stsrh, unsigned char *xbuf )
{
    iscsi_task_t *task;
    Scsi_Cmnd *sc;
    unsigned int senselen = 0;
    unsigned int expected;
    uint32_t itt = ntohl(stsrh->itt);

    updateSN(session, ntohl(stsrh->expcmdsn), ntohl(stsrh->maxcmdsn));

    /* find the task for the itt we received */
    spin_lock(&session->task_lock);
    if ((task = remove_task(&session->rx_tasks, itt))) {
        /* task was waiting for this command response */
        DEBUG_QUEUE3("iSCSI: recv_cmd - rx_tasks has itt %u, task %p, refcount %d\n", itt, task, atomic_read(&task->refcount));
        atomic_inc(&task->refcount);
        add_task(&session->completing_tasks, task);
    }
    else if ((task = remove_task(&session->tx_tasks, itt))) {
        /* target aborted the command for some reason, even
         * though we're trying to send it more data.  
         */
        DEBUG_QUEUE3("iSCSI: recv_cmd - tx_tasks has itt %u, task %p, refcount %d\n", itt, task, atomic_read(&task->refcount));
        atomic_inc(&task->refcount);
        add_task(&session->completing_tasks, task);
    }
#ifdef DEBUG_INIT
    else if ((task = find_task(&session->rx_abort_tasks, itt))) {
        /* just leave it waiting for an abort response */
        spin_unlock(&session->task_lock);
        DEBUG_INIT3("iSCSI: recv_cmd - ignoring cmd response for itt %u, task %p, command %p, waiting for abort response\n", 
                    itt, task, task->scsi_cmnd);
        return;
    }
    else if ((task = find_task(&session->tx_abort_tasks, itt))) {
        spin_unlock(&session->task_lock);
        DEBUG_INIT3("iSCSI: recv_cmd ignoring cmd response for itt %u, task %p, command %p, abort queued\n",
                    itt, task, task->scsi_cmnd);
        return;
    }
#endif
    spin_unlock(&session->task_lock);

    if (!task) {
        DEBUG_INIT1("iSCSI: recv_cmd - response for itt %u, but no such task\n", itt);
        return;
    }

    sc = task->scsi_cmnd;

    DEBUG_FLOW6("iSCSI: recv_cmd - itt %u, task %p, cmnd %p, Cmd 0x%x, cmd_len %d, rsp dlength %d\n",
                itt, task, sc, sc->cmnd[0], sc->cmd_len, ntoh24(stsrh->dlength));

    if (stsrh->response) {
        if (LOG_ENABLED(ISCSI_LOG_INIT))
            printk("iSCSI: recv_cmd %p, iSCSI response 0x%x, SCSI status 0x%x\n", sc, stsrh->response, stsrh->cmd_status);
        sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stsrh->cmd_status);  
    } 
    else {
        sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(stsrh->cmd_status);
    }
    ISCSI_TRACE( ISCSI_TRACE_RxCmdStatus, sc, task, stsrh->cmd_status, stsrh->response);

    /* handle sense data */
    if (stsrh->cmd_status && (ntoh24(stsrh->dlength) > 1)) {
        /* Sense data format per draft-08, 3.4.6.  2-byte sense length, then sense data, then iSCSI response data */
        senselen = (xbuf[0] << 8) | xbuf[1];
        if (senselen > (ntoh24(stsrh->dlength) - 2))
            senselen = (ntoh24(stsrh->dlength) - 2);
        xbuf += 2;
        
        /* copy sense data to the Scsi_Cmnd */
        memcpy(sc->sense_buffer, xbuf, MIN(senselen, sizeof(sc->sense_buffer)));
        
        /* if sense data logging is enabled, or it's an
         * unexpected unit attention, which Linux doesn't
         * handle well, log the sense data.  
         */
        if ((LOG_ENABLED(ISCSI_LOG_SENSE)) || 
            ((SENSE_KEY(xbuf) == UNIT_ATTENTION) && (test_bit(SESSION_RESETTING, &session->control_bits) == 0)))
        {
            if (senselen >= 26) {
                printk("iSCSI: recv_cmd %p, Cmd 0x%x, status 0x%x, senselen %d, "
                       "key %02x, ASC/ASCQ %02X/%02X, task %p to (%u %u %u %u), %s\n"
                       "       Sense %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x " 
                       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
                       sc, sc->cmnd[0], stsrh->cmd_status, senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf),
                       task, sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name,
                       xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                       xbuf[4],xbuf[5],xbuf[6],xbuf[7],
                       xbuf[8],xbuf[9],xbuf[10],xbuf[11],
                       xbuf[12],xbuf[13],xbuf[14],xbuf[15],
                       xbuf[16],xbuf[17],xbuf[18],xbuf[19],
                       xbuf[20],xbuf[21],xbuf[22],xbuf[23],
                       xbuf[24], xbuf[25]);
            }
            else if ( senselen >= 18) {
                printk("iSCSI: recv_cmd %p, Cmd 0x%x, status 0x%x, senselen %d, "
                       "key %02x, ASC/ASCQ %02X/%02X, task %p to (%u %u %u %u), %s\n"
                       "       Sense %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
                       sc, sc->cmnd[0], stsrh->cmd_status, senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf),
                       task, sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name,
                       xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                       xbuf[4],xbuf[5],xbuf[6],xbuf[7],
                       xbuf[8],xbuf[9],xbuf[10],xbuf[11],
                       xbuf[12],xbuf[13],xbuf[14],xbuf[15],
                       xbuf[16],xbuf[17]);
            } 
            else {
                printk("iSCSI: recv_cmd %p, Cmd 0x%x, status 0x%x, senselen %d, key %02x, task %p to (%u %u %u %u), %s\n"
                       "       Sense %02x%02x%02x%02x %02x%02x%02x%02x\n",
                       sc, sc->cmnd[0], stsrh->cmd_status, senselen, SENSE_KEY(xbuf),
                       task, sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name,
                       xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                       xbuf[4],xbuf[5],xbuf[6],xbuf[7]);
            }
        }
    }
#if FAKE_DEFERRED_ERRORS
    else if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc && (senselen == 0) &&
             (sc->cmnd[0] != TEST_UNIT_READY) &&
             (stsrh->cmd_status == 0) &&
             (task->cmdsn >= FAKE_DEFERRED_ERROR_FREQUENCY) &&
             ((task->cmdsn % FAKE_DEFERRED_ERROR_FREQUENCY) == 0))
    {
        printk("iSCSI: faking deferred error sense on itt %u, CmdSN %u, task %p, sc %p, Cmd 0x%x to (%u %u %u %u)\n",
               itt, task->cmdsn, task, sc, sc->cmnd[0], sc->host->host_no, sc->channel, sc->target, sc->lun);

        /* fake a deferred error check condition for this command, indicating a target reset
         * Sense: 71000600 00000012 00000000 29030000 0000fe07 14000000 0100
         */
        sc->sense_buffer[0] = 0x71;
        sc->sense_buffer[2] = UNIT_ATTENTION;
        sc->sense_buffer[7] = 0x12;
        sc->sense_buffer[12] = 0x29;
        sc->sense_buffer[13] = 0x03;
        sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(0x02);
    }
#endif


    if (sc->cmnd[0] == INQUIRY) {
        unsigned char *data = NULL;
        
        if (sc->use_sg) {
            struct scatterlist *sg = (struct scatterlist *)sc->request_buffer;
            data = sg->address;
        }
        else
            data = sc->request_buffer;
    
        if (data && (*data == 0x7F)) {
            /* Possibly log about 0x7F responses to INQUIRY, which
             * indicate a device that isn't actually present or
             * responding.  Since this is useful in debugging LUNs
             * that should but don't appear, always compile it in, but
             * since it can log a lot of expected failures if we're
             * probing 32 LUNs per target, only do it if the user asks
             * for it.
             */
            if (LOG_ENABLED(ISCSI_LOG_INIT) || LOG_ENABLED(ISCSI_LOG_SENSE))
                printk("iSCSI: recv_cmd %p, 0x7F INQUIRY response (%u bytes total) from (%u %u %u %u), %s\n",
                       sc, task->rxdata, sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
            if (test_and_clear_bit(sc->lun, session->lun_bitmap)) {
                /* there's not really a useable LUN */
                session->num_luns--;
                mb();
            }
        }
    }

    expected = expected_data_length(sc);
    if (stsrh->flags.overflow || stsrh->flags.underflow || 
        ((test_bit(ISCSI_TASK_READ, &task->flags)) && (task->rxdata != expected))) 
    {
        if (LOG_ENABLED(ISCSI_LOG_INIT) || (senselen && (SENSE_KEY(xbuf) == UNIT_ATTENTION))) {
            /* for debugging, always log this for UNIT ATTENTION */
            printk("iSCSI: task %p itt %u to (%u %u %u %u), Cmd 0x%x, %s, received %u, residual %u, expected %u\n",
                   task, task->itt, sc->cmnd[0], sc->host->host_no, sc->channel, sc->target, sc->lun,
                   stsrh->flags.overflow ? "overflow" : "underflow",
                   task->rxdata, ntohl(stsrh->residual_count), expected);
        }
            
        if ( stsrh->flags.underflow ) {
            ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, ntohl(stsrh->residual_count), expected);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,19)
            sc->resid = ntohl(stsrh->residual_count);
#else
            if ( task->rxdata < sc->underflow ) {
                sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stsrh->cmd_status);
            }
#endif
        } 
        else if (stsrh->flags.overflow) {
            /* FIXME: not sure how to tell the SCSI layer of an overflow, so just give it an error */
            ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, ntohl(stsrh->residual_count), expected);
            sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stsrh->cmd_status);
        }
        else {
            /* All the read data did not arrive */
            ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, task->rxdata, expected);
            DEBUG_ERR4("iSCSI: task %p, cmnd %p received only %d of %d bytes\n",
                       task, sc, task->rxdata, expected);
            sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stsrh->cmd_status);
        }
    }

    ISCSI_TRACE( ISCSI_TRACE_RxCmd, sc, task, task->rxdata, expected);

    if ( sc->cmnd[0] == TEST_UNIT_READY ) {
        /* FIXME: this assumes the midlayer sends a TUR when probing LUNs, 
         * and that the target follows the recommendations for TEST UNIT READY responses
         * in SPC-3 section 7.25 
         */
        if (sc->lun >= ISCSI_MAX_LUN) {
            printk("iSCSI: LUN too high, (%u %u %u %u), target %s\n",
                   sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
        }
        else if (stsrh->cmd_status == 0 || 
                 (stsrh->cmd_status==0x2 && (senselen >= 3) && (SENSE_KEY(xbuf) != NOT_READY) && (SENSE_KEY(xbuf) != ILLEGAL_REQUEST))) {
            /* when a LUN becomes ready */
            if ( test_and_set_bit(sc->lun, session->lun_bitmap)) {
                DEBUG_FLOW5("iSCSI: unit still ready, (%u %u %u %u), target %s\n",
                            sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
            }
            else {
                printk("iSCSI: unit ready, (%u %u %u %u), target %s\n",
                       sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
                session->num_luns++;
                mb();
            }
        }
        else {
            if (test_and_clear_bit(sc->lun, session->lun_bitmap)) {
                /* was ready, but isn't anymore */
                printk("iSCSI: unit no longer ready, (%u %u %u %u), target %s\n",
                       sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
                session->num_luns--;
                mb();
            }
            else {
                DEBUG_FLOW5("iSCSI: unit not ready, (%u %u %u %u), target %s\n",
                            sc->host->host_no, sc->channel, sc->target, sc->lun, session->log_name);
            }
        }
    }

#if TEST_ABORTS
    if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc &&
        (sc->cmnd[0] != TEST_UNIT_READY) &&
        (stsrh->cmd_status == 0) &&
        (task->cmdsn >= ABORT_FREQUENCY) &&
        ((task->cmdsn % ABORT_FREQUENCY) >= 0) && ((task->cmdsn % ABORT_FREQUENCY) < ABORT_COUNT)) 
    {
        /* don't complete this command, so that we can test the error handling
         * code.
         */
        spin_lock(&session->task_lock);
        if ((task = remove_task(&session->completing_tasks, itt))) {
            add_task(&session->rx_tasks, task);
            printk("iSCSI: ignoring completion of itt %u, CmdSN %u, task %p, sc %p, Cmd 0x%x to (%u %u %u %u)\n",
                   itt, task->cmdsn, task, sc, sc->cmnd[0], sc->host->host_no, sc->channel, sc->target, sc->lun);
        }
        spin_unlock(&session->task_lock);
        atomic_dec(&task->refcount);
        return;
    }
#endif


    /* we're done using the command and task */
    atomic_dec(&task->refcount);
    task = NULL;
    sc = NULL;

    /* now that we're done with it, try to complete it.  This may fail
     * if an abort for this command comes in while we're not holding
     * certain locks.  That's ok.  
     */
    DEBUG_FLOW1("iSCSI: recv_cmd attempting to complete itt %u\n", itt);
    if ((task = complete_task(session, itt))) {
        free_task(task);
    }
}

static void
iscsi_recv_r2t(iscsi_session_t *session, struct IscsiRttHdr *strh )
{
    iscsi_task_t *task = NULL;
    uint32_t itt = ntohl(strh->itt);
    uint32_t offset = 0, length = 0;
    uint32_t ttt = RSVD_TASK_TAG;

    updateSN(session, ntohl(strh->expcmdsn), ntohl(strh->maxcmdsn));

    spin_lock(&session->task_lock);
    if ((task = remove_task(&session->rx_tasks, itt))) {
        task->ttt = ttt = strh->ttt;
        task->data_length = length = ntohl(strh->data_length);
        task->data_offset = offset = ntohl(strh->data_offset);
        add_task(&session->tx_tasks, task);
        ISCSI_TRACE(ISCSI_TRACE_R2T, task->scsi_cmnd, task, offset, length);
    }
    spin_unlock(&session->task_lock);

    if (task) {
        DEBUG_FLOW4("iSCSI: R2T for task %p itt %u, %u bytes @ offset %u\n",
                    task, ntohl(strh->itt), ntohl(strh->data_length), ntohl(strh->data_offset));
        
        /* wake up the Tx thread for this connection */
        wake_tx_thread(TX_R2T_DATA, session);
    }
    else {
        /* the task wasn't waiting for an R2T, aborting or already received an R2T */
        DEBUG_ERR3("iSCSI: ignoring R2T for itt %u, %u bytes @ offset %u\n",
                   ntohl(strh->itt), ntohl(strh->data_length), ntohl(strh->data_offset));
    }
}


static void
iscsi_recv_data( iscsi_session_t *session, struct IscsiDataRspHdr *stdrh )
{
    iscsi_task_t *task = NULL;
    Scsi_Cmnd *sc = NULL;
    struct scatterlist *sglist = NULL;
    int dlength, remaining, rc;
    uint32_t offset;
    unsigned int segNum = 0, iovn = 0, pad = 0;
    unsigned int relative_offset = 0;
    struct msghdr msg;
    struct iovec iov;
    uint32_t itt = ntohl(stdrh->itt);
    
    updateSN(session, ntohl(stdrh->expcmdsn), ntohl(stdrh->maxcmdsn));

    dlength = ntoh24( stdrh->dlength );
    offset = ntohl( stdrh->offset );
    /* Compute padding bytes that follow the data */
    pad = dlength % PAD_WORD_LEN;
    if (pad) {
        pad = PAD_WORD_LEN - pad;
    }

    spin_lock(&session->task_lock);
    if ((task = find_task(&session->rx_tasks, itt))) {
        /* receive the data, and leave it in rx_tasks for more data or a cmd response */
        atomic_inc(&task->refcount);
        sc = task->scsi_cmnd;
    }
    else if ((task = find_task(&session->rx_abort_tasks, itt))) {
        /* discard the data */
        DEBUG_ERR1("iSCSI: recv_data - task aborting, itt %u, discarding received data\n", ntohl(stdrh->itt));
        task = NULL;
    }
    else if ((task = find_task(&session->tx_abort_tasks, itt))) {
        /* discard the data */
        DEBUG_ERR1("iSCSI: recv_data - task will abort, itt %u, discarding received data\n", ntohl(stdrh->itt));
        task = NULL;
    }
    else {
        DEBUG_ERR1("iSCSI: recv_data - no task for itt %u, discarding received data\n", ntohl(stdrh->itt));
        task = NULL;
    }
    spin_unlock(&session->task_lock);

    if (task) {
        DEBUG_FLOW5("iSCSI: recv_data itt %u, task %p, datasn %u, offset %u, dlength %u\n", 
                    itt, task, ntohl(stdrh->datasn), offset, dlength);
    }
    
    /* configure for receiving the data */
    relative_offset = offset;

    /* make sure all the data fits in the buffer */
    if (sc && ((offset + dlength) > sc->request_bufflen)) {
        printk("iSCSI: recv_data for itt %u, task %p, cmnd %p, bufflen %u, discarding data for offset %u len %u\n", 
               itt, task, sc, sc->request_bufflen, offset, dlength);
        atomic_dec(&task->refcount);
        task = NULL;
        sc = NULL;
    }

    if (sc && sc->use_sg) {
        /* scatter-gather */
        
        sglist = (struct scatterlist *)sc->request_buffer;
        
        /* We need to find the sg segment where this data should go.
         * We look at the offset and dlength to find it (amg)
         */
        for (segNum = 0; segNum < sc->use_sg; segNum++) {
            if (relative_offset < sglist->length ) {
                /* found it */
                break;
            } 
            else {
                relative_offset -= sglist->length;
                sglist++;
            }
        }
        
        if (segNum < sc->use_sg) {
            /* offset is within our buffer */
            remaining = dlength;
            iovn = 0;
            
            /* may start at an offset into the first segment */
            session->RxIov[0].iov_base = sglist->address + relative_offset;
            session->RxIov[0].iov_len  = MIN(remaining, sglist->length  - relative_offset);
            remaining -= session->RxIov[0].iov_len;
            DEBUG_FLOW6("iSCSI: recv_data itt %u, iov[ 0] = sg[%2d], %u of %u at offset %u, remaining %u\n", 
                        itt, sglist - (struct scatterlist *)sc->request_buffer, 
                        session->RxIov[0].iov_len, sglist->length, relative_offset, remaining);
            sglist++;
            segNum++;
            iovn++;

            /* always start at the beginning of any more segments */
            while ((remaining > 0) && (segNum < sc->use_sg)) {
                session->RxIov[iovn].iov_base = sglist->address;
                session->RxIov[iovn].iov_len  = MIN(remaining, sglist->length);
                remaining -= session->RxIov[iovn].iov_len;
                DEBUG_FLOW6("iSCSI: recv_data itt %u, iov[%2d] = sg[%2d], %u of %u bytes, remaining %u\n", 
                            itt, iovn, sglist - (struct scatterlist *)sc->request_buffer, 
                            session->RxIov[iovn].iov_len, sglist->length, remaining);
                sglist++;
                segNum++;
                iovn++;
            }
            if (remaining != 0) {
                /* we ran out of buffer space with more data remaining.
                 * this should never happen if the Scsi_Cmnd's bufflen
                 * matches the combined length of the sglist segments.
                 */
                DEBUG_ERR5("iSCSI: recv_data for cmnd %p, bufflen %u, offset %u len %u, remaining data %u, discarding all data\n", 
                           sc, sc->request_bufflen, offset, dlength, remaining);
            }
            if (pad) {
                session->RxIov[iovn].iov_base = session->RxBuf;
                session->RxIov[iovn].iov_len  = pad;
                DEBUG_FLOW5("iSCSI: recv_data itt %u, iov[%2d] = sg[%2d], %u of %u, pad data\n", 
                            itt, iovn, sglist - (struct scatterlist *)sc->request_buffer,
                            session->RxIov[iovn].iov_len, sglist->length);
                iovn++;
            }
        }
        else {
            DEBUG_ERR4("iSCSI: recv_data for cmnd %p, bufflen %u, failed to find offset %u len %u, discarding data\n", 
                       sc, sc->request_bufflen, offset, dlength);
            atomic_dec(&task->refcount);
            task = NULL;
            sc = NULL;
        }
    } 
    else if (sc) {
        /* no scatter-gather, just read it into the buffer */
        session->RxIov[0].iov_base = sc->request_buffer + offset;
        session->RxIov[0].iov_len  = dlength;
        if (pad) {
            session->RxIov[1].iov_base = session->RxBuf;
            session->RxIov[1].iov_len  = pad;
            iovn = 2;
        }
        else
            iovn = 1;
    }

    /* just throw away the PDU */
    if (!sc) {
        int bytes_read = 0;

        while (!signal_pending(current) && (bytes_read < dlength + pad)) {
            int num_bytes = MIN(dlength + pad - bytes_read, sizeof(session->RxBuf));
            iov.iov_base = session->RxBuf;
            iov.iov_len = sizeof(session->RxBuf);;
            memset( &msg, 0, sizeof(struct msghdr) );
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            rc = iscsi_recvmsg( session, &msg, num_bytes);
            if ( rc <= 0) {
                DEBUG_ERR2("iSCSI: recv_data failed to recv %d data PDU bytes, rc %d\n", num_bytes, rc);
                iscsi_drop_session(session);
            }
            bytes_read += rc;
        }
        return;
    }

    /* accept the data */
    memset( &msg, 0, sizeof(struct msghdr) );
    msg.msg_iov = session->RxIov;
    msg.msg_iovlen = iovn;

    DEBUG_FLOW3("iSCSI: recv_data itt %u calling recvmsg %d bytes, iovn %u\n", itt, dlength + pad, iovn);
    rc = iscsi_recvmsg( session, &msg, dlength + pad);
    if ( rc == dlength + pad) {
        task->rxdata += dlength;
    }
    else if ( rc == dlength) {
        task->rxdata += dlength;
        /* FIXME: until iscsi_recvmsg handles the retries, allow the pad
         * to come in separately.
         */
        iov.iov_base = session->RxBuf;
        iov.iov_len = pad;
        memset( &msg, 0, sizeof(struct msghdr) );
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        rc = iscsi_recvmsg( session, &msg, pad );
        if ( rc != pad ) {
            DEBUG_ERR2("iSCSI: recv_data failed to recv %d pad bytes, rc %d\n", pad, rc);
            atomic_dec(&task->refcount);
            iscsi_drop_session(session);
            return;
        }
    }
    else {
        DEBUG_ERR2("iSCSI: recv_data failed to recv %d data PDU bytes, rc %d\n", dlength, rc);
        atomic_dec(&task->refcount);
        iscsi_drop_session(session);
        return;
    }
    
    ISCSI_TRACE(ISCSI_TRACE_RxData, sc, task, offset, dlength);

    if (stdrh->status_present) {
        unsigned int expected = expected_data_length(sc);

        /* we got status, meaning the command completed in a way that
         * doesn't give us any sense data, and the command must be
         * completed now, since we won't get a command response PDU.
         */
        /* FIXME: check the StatSN */
        DEBUG_FLOW4("iSCSI: Data-in with status 0x%x for itt %u, task %p, sc %p\n", 
                    stdrh->cmd_status, ntohl(stdrh->itt), task, task->scsi_cmnd);
        ISCSI_TRACE( ISCSI_TRACE_RxDataCmdStatus, sc, task, stdrh->cmd_status, 0);
        sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(stdrh->cmd_status);
        
        if (stdrh->overflow || stdrh->underflow || 
            ((test_bit(ISCSI_TASK_READ, &task->flags)) && (task->rxdata != expected))) 
        {
            if (LOG_ENABLED(ISCSI_LOG_INIT)) {
                printk("iSCSI: task %p, Cmd 0x%x, %s, received %u, residual %u, expected %u\n",
                       task, sc->cmnd[0], stdrh->overflow ? "overflow" : "underflow",
                       task->rxdata, ntohl(stdrh->residual_count), expected);
            }
            
            if ( stdrh->underflow ) {
                ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, ntohl(stdrh->residual_count), expected);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,19)
                sc->resid = ntohl(stdrh->residual_count);
#else
                if ( task->rxdata < sc->underflow ) {
                    sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stdrh->cmd_status);
                }
#endif
            } 
            else if (stdrh->overflow) {
                /* FIXME: not sure how to tell the SCSI layer of an overflow, so just give it an error */
                ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, ntohl(stdrh->residual_count), expected);
                sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stdrh->cmd_status);
            }
            else {
                /* All the read data did not arrive */
                ISCSI_TRACE(ISCSI_TRACE_RxCmdFlow, sc, task, task->rxdata, expected);
                DEBUG_ERR4("iSCSI: task %p, cmnd %p received only %d of %d bytes\n",
                           task, sc, task->rxdata, expected);
                sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stdrh->cmd_status);
            }
        }
        
#if TEST_ABORTS
        if (test_bit(SESSION_ESTABLISHED, &session->control_bits) &&
            (sc->cmnd[0] != TEST_UNIT_READY) &&
            (stdrh->cmd_status == 0) &&
            (task->cmdsn >= ABORT_FREQUENCY) &&
            ((task->cmdsn % ABORT_FREQUENCY) >= 0) && ((task->cmdsn % ABORT_FREQUENCY) <= ABORT_COUNT))
        {
            /* don't complete this command, so that we can test the error handling
             * code.
             */
            printk("iSCSI: ignoring completion of itt %u, CmdSN %u, task %p, sc %p, Cmd 0x%x to (%u %u %u %u)\n", 
                   itt, task->cmdsn, task, sc, sc->cmnd[0], sc->host->host_no, sc->channel, sc->target, sc->lun);
            atomic_dec(&task->refcount);
            return;
        }
#endif

        /* done using the command and task */
        atomic_dec(&task->refcount);

        /* try to complete the task */
        spin_lock(&session->task_lock);
        if ((task = remove_task(&session->rx_tasks, itt))) {
            add_task(&session->completing_tasks, task);
            spin_unlock(&session->task_lock);

            if ((task = complete_task(session, itt))) {
                free_task(task);
            }
            else {
                DEBUG_FLOW1("iSCSI: Data-in with status for itt %u, but task couldn't be completed\n", 
                            ntohl(stdrh->itt));
            }
        }
        else {
            /* task was probably aborted */
            spin_unlock(&session->task_lock);
            DEBUG_FLOW1("iSCSI: Data-in with status for itt %u, but task aborted\n", ntohl(stdrh->itt));
        }
    }
    else {
        /* done modifying the command and task */
        atomic_dec(&task->refcount);
    }
}

static void iscsi_recv_mgmt_rsp(iscsi_session_t *session, struct IscsiScsiTaskMgtRspHdr *ststmrh )
{
    iscsi_task_t *task = NULL;
    uint32_t mgmt_itt = ntohl(ststmrh->itt);

    updateSN(session, ntohl(ststmrh->expcmdsn), ntohl(ststmrh->maxcmdsn));

    spin_lock(&session->task_lock);
    if (mgmt_itt == session->warm_reset_itt) {
        session->warm_reset_itt = RSVD_TASK_TAG;

        if ( ststmrh->response ) {
            printk("iSCSI: warm reset rejected (0x%x) for itt %u, rtt %u, session %p to %s\n", 
                   ststmrh->response, ntohl(ststmrh->itt), ntohl(ststmrh->rtt), session, session->log_name);
        }
        else
        {
            printk("iSCSI: warm reset success for itt %u, rtt %u, session %p to %s\n",
                   ntohl(ststmrh->itt), ntohl(ststmrh->rtt), session, session->log_name);
            set_bit(SESSION_RESET, &session->control_bits);
        }
        /* make sure we start sending SCSI commands again */
        clear_bit(SESSION_RESETTING, &session->control_bits);
        wake_tx_thread(TX_SCSI_COMMAND, session);
        ISCSI_TRACE(ISCSI_TRACE_RxReset, NULL, task, mgmt_itt, ststmrh->response);
    }
    else if ((task = remove_mgmt_task(&session->rx_abort_tasks, mgmt_itt))) {
        /* We don't really care if the target actually aborted the
         * command, we just want it to be done so that we can free the
         * task before returning from eh_abort.
         */
        add_task(&session->aborted_tasks, task);
        if ( ststmrh->response ) {
            printk("iSCSI: abort rejected (0x%x) for itt %u, rtt %u, task %p, cmnd %p, Cmd 0x%x\n", 
                   ststmrh->response, ntohl(ststmrh->itt), ntohl(ststmrh->rtt), task, task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
        }
        else {
            printk("iSCSI: abort successful for itt %u, task %p, cmnd %p, Cmd 0x%x\n",
                   ntohl(ststmrh->itt), task, task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
        }
        ISCSI_TRACE(ISCSI_TRACE_RxAbort, NULL, task, mgmt_itt, ststmrh->response);
    }        
    else if ((task = remove_mgmt_task(&session->rx_lun_reset_tasks, mgmt_itt))) {
        if ( ststmrh->response ) {
            printk("iSCSI: LUN reset rejected (0x%x) for itt %u, rtt %u, task %p, cmnd %p, Cmd 0x%x\n", 
                   ststmrh->response, ntohl(ststmrh->itt), ntohl(ststmrh->rtt), task, task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
            /* this is kind of a kludge, but we have the space in the task, and adding
             * another task collection to the session seemed pointless.
             */
            set_bit(ISCSI_TASK_RESET_FAILED, &task->flags);
        }
        else {
            printk("iSCSI: LUN reset successful for itt %u, task %p, cmnd %p, Cmd 0x%x\n",
                   ntohl(ststmrh->itt), task, task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
        }
        add_task(&session->lun_reset_tasks, task);
        ISCSI_TRACE(ISCSI_TRACE_RxReset, NULL, task, mgmt_itt, ststmrh->response);
    }
    else {
        printk("iSCSI: mgmt response 0x%x for unknown itt %u, rtt %u\n", 
               ststmrh->response, ntohl(ststmrh->itt), ntohl(ststmrh->rtt));
    }
    spin_unlock(&session->task_lock);
}


static void iscsi_recv_async_event(iscsi_session_t *session, struct IscsiAsyncEvtHdr *staeh, unsigned char *xbuf)
{
    unsigned int senselen = ntoh24(staeh->dlength);

    updateSN(session, ntohl(staeh->expcmdsn), ntohl(staeh->maxcmdsn));

    ISCSI_TRACE(ISCSI_TRACE_RxAsyncEvent, NULL, NULL, staeh->async_event, staeh->async_vcode);

    switch (staeh->async_event) {
        case ASYNC_EVENT_SCSI_EVENT:
            /* no way to pass this up to the SCSI layer, since there is no command associated with it */
            if (LOG_ENABLED(ISCSI_LOG_SENSE)) {
                if (senselen >= 26) {
                    printk("iSCSI: SCSI Async event, senselen %d, key %02x, ASC/ASCQ %02X/%02X, session %p to %s\n"
                           "       Sense %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x " 
                           "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
                           senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf), session, session->log_name,
                           xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                           xbuf[4],xbuf[5],xbuf[6],xbuf[7],
                           xbuf[8],xbuf[9],xbuf[10],xbuf[11],
                           xbuf[12],xbuf[13],xbuf[14],xbuf[15],
                           xbuf[16],xbuf[17],xbuf[18],xbuf[19],
                           xbuf[20],xbuf[21],xbuf[22],xbuf[23],
                           xbuf[24], xbuf[25]);
                }
                else if ( senselen >= 18) {
                    printk("iSCSI: SCSI Async event, senselen %d, key %02x, ASC/ASCQ %02X/%02X, session %p to %s\n"
                           "       Sense %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
                           senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf), session, session->log_name,
                           xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                           xbuf[4],xbuf[5],xbuf[6],xbuf[7],
                           xbuf[8],xbuf[9],xbuf[10],xbuf[11],
                           xbuf[12],xbuf[13],xbuf[14],xbuf[15],
                           xbuf[16],xbuf[17]);
                } 
                else if ( senselen >= 14) {
                    printk("iSCSI: SCSI Async event, senselen %d, key %02x, ASC/ASCQ %02X/%02X, session %p to %s\n"
                           "       Sense %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
                           senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf), session, session->log_name,
                           xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                           xbuf[4],xbuf[5],xbuf[6],xbuf[7],
                           xbuf[8],xbuf[9],xbuf[10],xbuf[11],
                           xbuf[12],xbuf[13]);
                } 
                else {
                    printk("iSCSI: SCSI Async event, senselen %d, key %02x, session %p to %s\n"
                           "       Sense %02x%02x%02x%02x %02x%02x%02x%02x\n",
                           senselen, SENSE_KEY(xbuf), session, session->log_name,
                           xbuf[0],xbuf[1],xbuf[2],xbuf[3],
                           xbuf[4],xbuf[5],xbuf[6],xbuf[7]);
                }
            }
            break;
        case ASYNC_EVENT_REQUEST_LOGOUT:
            DEBUG_ERR2("iSCSI: target requests logout within %u seconds for session to %s\n", 
                       ntohs(staeh->param3), session->log_name);
            /* FIXME: this is really a request to drop a connection, not the whole session,
             * but we currently only have one connection per session, so there's no difference
             * at the moment.
             */
            session->logout_deadline = jiffies + (ntohs(staeh->param3) * HZ);
            set_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits);
            mb();
            /* FIXME: pass this up to the daemon?  try to unmount
             * filesystems? decide what to do, then do it, and make
             * sure we'll get woken up to send the target a logout.
             * The spec confusingly says initiators MUST send a
             * logout, and then goes on to define what targets should
             * do if the initiator doesn't send a logout.
             */
            break;
        case ASYNC_EVENT_DROPPING_CONNECTION:
            DEBUG_ERR4("iSCSI: target dropping connection %u for session to %s, reconnect min %u max %u\n", 
                       ntohs(staeh->param1), session->log_name, ntohs(staeh->param2), ntohs(staeh->param3));
            session->min_reconnect_time = jiffies + (ntohs(staeh->param2) * HZ);
            /* FIXME: obey the min reconnect time */
            break;
        case ASYNC_EVENT_DROPPING_ALL_CONNECTIONS:
            DEBUG_ERR3("iSCSI: target dropping all connections for session to %s, reconnect min %u max %u\n", 
                       session->log_name, ntohs(staeh->param2), ntohs(staeh->param3));
            session->min_reconnect_time = jiffies + (ntohs(staeh->param2) * HZ);
            /* FIXME: obey the min reconnect time */
            break;
        case ASYNC_EVENT_VENDOR_SPECIFIC:
            DEBUG_ERR2("iSCSI: vendor-specific async event, vcode 0x%x, received on session to %s\n",
                       staeh->async_vcode, session->log_name);
            break;
        default:
            printk("iSCSI: unknown async event 0x%x received on session to %s\n",
                   staeh->async_event, session->log_name);
            break;
    }
}

/* wait for the tx thread to block or exit, ignoring signals.
 * the rx thread needs to know that the tx thread is not running before
 * it can safely close the socket and start a new login phase on a new socket,
 * Also, tasks still in use by the tx thread can't safely be completed on
 * a session drop.
 */
static int wait_for_tx_blocked(iscsi_session_t *session)
{
    while (session->tx_pid) {
        DEBUG_INIT2("iSCSI: thread %d waiting for tx thread %d to block\n",
                    current->pid, session->tx_pid);

        wait_event_interruptible(session->tx_blocked_wait_q, 
                                 test_bit(TX_THREAD_BLOCKED, &session->control_bits));
        
        if (iscsi_handle_signals(session)) {
            DEBUG_INIT3("iSCSI: wait_for_tx_blocked signalled at %lu while waiting for session %p tx %d\n", 
                        jiffies, session, session->tx_pid);
        }
        /* if the session is terminating, the tx thread will exit, waking us up in the process 
         * we don't want to return until the tx thread is blocked, since there's not much
         * the rx thread can do until the tx thread is guaranteed not to be doing anything.
         */
        if (test_bit(TX_THREAD_BLOCKED, &session->control_bits)) {
            DEBUG_INIT2("iSCSI: rx thread %d found tx thread %d blocked\n",
                        current->pid, session->tx_pid);
            return 1;
        }
    }

    /* dead and blocked are fairly similar, really */
    DEBUG_INIT2("iSCSI: rx thread %d found tx thread %d exited\n",
                current->pid, session->tx_pid);
    return 1;
}


/* Wait for a session to be established.  
 * Returns 1 if the session is established, zero if the timeout expires
 * or the session is terminating/has already terminated.
 */
static int wait_for_session(iscsi_session_t *session, int replacement_timeout)
{
    int ret = 0;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
    wait_queue_t waitq;
#else
    struct wait_queue waitq;
#endif

    if (test_bit(SESSION_ESTABLISHED, &session->control_bits))
        return 1;

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
        printk("iSCSI: wait_for_session %p failed, session terminating\n", session);
        return 0;
    }

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
    init_waitqueue_entry(&waitq, current);
#else
    waitq.task = current;
    mb();
#endif

    add_wait_queue(&session->login_wait_q, &waitq);

    DEBUG_INIT2("iSCSI: pid %d waiting for session %p\n", current->pid, session);

    for (;;) {
        set_current_state(TASK_INTERRUPTIBLE);

        if (test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
            ret = 1;
            break;
        }

        if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
            ret = 0;
            break;
        }

        if (!signal_pending(current)) {
            if (replacement_timeout && session->replacement_timeout) {
                unsigned long timeout;
                long sleep_jiffies = 0;

                if (session->session_drop_time)
                    timeout = session->session_drop_time + (HZ * session->replacement_timeout);
                else
                    timeout = jiffies + (HZ * session->replacement_timeout);

                if (time_before_eq(timeout, jiffies)) {
                    printk("iSCSI: pid %d timed out in wait_for_session %p\n", current->pid, session);
                    ret = 0;
                    break;
                }

                /* handle wrap-around */
                if (jiffies < timeout)
                    sleep_jiffies = timeout - jiffies;
                else
                    sleep_jiffies = ULONG_MAX - jiffies + timeout;
                
                schedule_timeout(sleep_jiffies);
            }
            else
                schedule();

            continue;					
        }							
        ret = 0;
        break;							
    }

    set_current_state(TASK_RUNNING);
    remove_wait_queue(&session->login_wait_q, &waitq);

    if (ret == 0)
        printk("iSCSI: wait_for_session %p failed\n", session);
    return ret;
}

static int iscsi_establish_session(iscsi_session_t *session)
{
    int ret = 0;

    if (LOG_ENABLED(ISCSI_LOG_LOGIN) || LOG_ENABLED(ISCSI_LOG_INIT))
        printk("iSCSI: trying to establish session %p to %s, rx %d, tx %d at %lu\n",
               session, session->TargetName, session->rx_pid, session->tx_pid, jiffies);
    else
        printk("iSCSI: trying to establish session %p to %s\n", session, session->TargetName);

    /* set a timer on the connect */
    session->login_phase_timer = jiffies + (session->login_timeout * HZ);
    mb();
    if (LOG_ENABLED(ISCSI_LOG_LOGIN))
        printk("iSCSI: session %p attempting to connect at %lu, timeout at %lu (%d seconds)\n",
               session, jiffies, session->login_phase_timer, session->login_timeout);

    if (!iscsi_connect(session)) {
        if (signal_pending(current))
            printk("iSCSI: session %p connect timed out at %lu\n", session, jiffies);
        else
            printk("iSCSI: session %p connect failed at %lu\n", session, jiffies);
        
        goto done;
    }

    /* clear the connect timer */
    session->login_phase_timer = 0;
    mb();
    iscsi_handle_signals(session);
    
    /* try to make sure other timeouts don't go off as soon as the session is established */
    session->last_rx = jiffies;
    session->last_ping = jiffies - 1;
    mb();

    session->type = ISCSI_SESSION_TYPE_NORMAL;

    /* use the session's RxBuf for a login PDU buffer, since it is
     * currently unused.  We can't afford to dynamically allocate
     * memory right now, since it's possible we're reconnecting, and
     * the VM system is already blocked trying to write dirty pages to
     * the iSCSI device we're trying to reconnect.  The session's
     * RxBuf was sized to allow us to find an appropriate alignment
     * for the Login header, plus the maximum size of a Login PDU,
     * plus any padding mandated by the iSCSI protocol, plus 1 extra
     * byte to ensure NUL termination of the Login response.
     */
    if (!iscsi_login(session, session->RxBuf, sizeof(session->RxBuf))) {
        printk("iSCSI: session %p login failed at %lu, rx %d, tx %d\n", 
               session, jiffies, session->rx_pid, session->tx_pid);
        iscsi_disconnect(session);
        goto done;
    }

    /* logged in, get the new session ready */
    ret = 1;
    session->generation++;
    session->last_rx = jiffies;
    session->last_ping = jiffies - 1;
    session->last_window_check = jiffies;
    session->last_peak_window_size = 0;
    session->last_kill = 0;
    session->current_peak_window_size = max_tasks_for_session(session);
    session->window_peak_check = jiffies;
    session->warm_reset_itt = RSVD_TASK_TAG;
    session->cold_reset_itt = RSVD_TASK_TAG;
    session->nop_reply_head = session->nop_reply_tail = NULL;
    session->session_drop_time = 0; /* used to detect sessions that aren't coming back up */
    session->login_phase_timer = 0;
    mb();
    
    /* announce it */
    if (session->TargetAlias[0] != '\0')
        printk("iSCSI: session %p #%lu to %s, alias %s, entering full-feature phase\n",
               session, session->generation, session->TargetName, session->TargetAlias);
    else
        printk("iSCSI: session %p #%lu to %s entering full-feature phase\n",
               session, session->generation, session->TargetName);

    if (LOG_ENABLED(ISCSI_LOG_INIT) || LOG_ENABLED(ISCSI_LOG_EH)) {
        printk("iSCSI: session %p #%lu established at %lu, %u unsent cmnds, %u ignored cmnds, %u tasks, bits 0x%08x\n",
               session, session->generation, jiffies, atomic_read(&session->num_cmnds), atomic_read(&session->num_ignored_cmnds), 
               atomic_read(&session->num_active_tasks), session->control_bits);
    }
    
    /* wake up everyone waiting for the session to be established */
    set_bit(SESSION_ESTABLISHED, &session->control_bits);
    wake_up(&session->login_wait_q);
    
    /* make sure we start sending commands again */
    wake_tx_thread(TX_SCSI_COMMAND, session);

 done:
    /* clear any timer that may have been left running */
    session->login_phase_timer = 0;
    mb();
    /* cleanup after a possible timeout expiration */
    if (iscsi_handle_signals(session)) {
        printk("iSCSI: signal received while establishing session %p\n", session);
        return 0;
    }

    return ret;
}

/* complete all of the cmnds for this session with the specified result code */
static void complete_all_cmnds(iscsi_session_t *session, int result)
{
    Scsi_Cmnd *sc, *unsent_cmnds = NULL;
    iscsi_task_t *task = NULL, *head = NULL;
    unsigned int num_cmnds;
    unsigned int num_tasks;
    DECLARE_IO_REQUEST_FLAGS;
    DECLARE_NOQUEUE_FLAGS;

    /* Grab all tasks and unsent cmnds, and then complete them all in
     * the proper order.  We grab the commands before we abort
     * anything, in case the done() function tries to queue up retries
     * while we're aborting.  We don't want to abort anything that
     * arrives because of done() being called, we just want to abort
     * everything currently in the driver.  We do need to grab and
     * abort even the unsent cmnds, to ensure that dependent SCSI
     * commands won't get re-ordered in cases where the upcoming
     * completion re-queues a command behind the unsent cmnds currently
     * queued.
     *
     * We keep holding the task lock so that any eh_*_handlers will
     * block until we're finished completing commands.
     */

    spin_lock(&session->task_lock);
    /* grab all the sent commands (tasks) for this connection */
    num_tasks = atomic_read(&session->num_active_tasks);
    head = session->arrival_order.head;
    session->arrival_order.head = session->arrival_order.tail = NULL;
    atomic_set(&session->num_active_tasks, 0);
    /* clear out the task collections */
    session->rx_tasks.head = session->rx_tasks.tail = NULL;
    session->tx_tasks.head = session->tx_tasks.tail = NULL;
    session->completing_tasks.head = session->completing_tasks.tail = NULL;
    session->rx_abort_tasks.head = session->rx_abort_tasks.tail = NULL;
    session->tx_abort_tasks.head = session->tx_abort_tasks.tail = NULL;
    session->aborted_tasks.head = session->aborted_tasks.tail = NULL;
    session->tx_lun_reset_tasks.head = session->tx_lun_reset_tasks.tail = NULL;
    session->rx_lun_reset_tasks.head = session->rx_lun_reset_tasks.tail = NULL;
    session->lun_reset_tasks.head = session->lun_reset_tasks.tail = NULL;
    session->warm_reset_itt = RSVD_TASK_TAG;
    session->cold_reset_itt = RSVD_TASK_TAG;

    /* grab the ignored and unsent cmnds */
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
    if ((unsent_cmnds = session->ignored_cmnd_head))
        session->ignored_cmnd_tail->host_scribble = (unsigned char *)session->scsi_cmnd_head;
    else
        unsent_cmnds = session->scsi_cmnd_head;
    session->scsi_cmnd_head = session->scsi_cmnd_tail = NULL;
    session->ignored_cmnd_head = session->ignored_cmnd_tail = NULL;
    num_cmnds = atomic_read(&session->num_cmnds) + atomic_read(&session->num_ignored_cmnds);
    atomic_set(&session->num_cmnds, 0);
    atomic_set(&session->num_ignored_cmnds, 0);
    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
    
    LOCK_IO_REQUEST_LOCK;
    while ((task = head)) {
        head = task->order_next;

        if (atomic_read(&task->refcount) == 0) {
            //            DEBUG_EH3("iSCSI: aborting sent task %p, command %p at %lu\n", task, task->scsi_cmnd, jiffies);
            task->next = task->prev = task->order_next = task->order_prev = NULL;
            sc = task->scsi_cmnd;
            task->scsi_cmnd = NULL;
            
            /* free the task memory back to the kernel */
            free_task(task);

            if (sc && sc->scsi_done) {
                sc->host_scribble = NULL;
                sc->result = result;
                ISCSI_TRACE(ISCSI_TRACE_TaskAborted, sc, task, result, 0);
                sc->scsi_done(sc);
                //  DEBUG_EH3("iSCSI: aborted sent task %p, command %p at %lu\n", task, sc, jiffies);
            }
        }
        else {
            /* This should never happen, which is good, since we don't really 
             * have any good options here.  Leak the task memory, and fail to 
             * complete the cmnd, which may leave apps blocked forever in the kernel.
             */
            printk("iSCSI: can't abort sent task %p, refcount %u, command %p\n", 
                   task, atomic_read(&task->refcount), task->scsi_cmnd);
        }
    }
    
    while ((sc = unsent_cmnds)) {
        unsent_cmnds = (Scsi_Cmnd *)sc->host_scribble;
        //        DEBUG_EH2("iSCSI: aborting unsent SCSI command %p at %lu\n", sc, jiffies);
        if ( sc->scsi_done) {
            sc->host_scribble = NULL;
            sc->result = result;
            ISCSI_TRACE(ISCSI_TRACE_CmndAborted, sc, NULL, result, 0);
            sc->scsi_done(sc);
            //            DEBUG_EH2("iSCSI: aborted unsent SCSI command %p at %lu\n", sc, jiffies);
        }
        else {
            printk("iSCSI: unsent command %p already aborting\n", sc);
        }
    }
    
    UNLOCK_IO_REQUEST_LOCK;
    spin_unlock(&session->task_lock);

    printk("iSCSI: completed %u tasks and %u cmnds for session %p\n", 
           num_tasks, num_cmnds, session);
}

static int iscsi_rx_thread(void *vtaskp)
{
    iscsi_session_t *session;
    iscsi_hba_t *hba;
    int rc = -EPIPE, xlen;
    struct msghdr msg;
    struct iovec iov;
    struct IscsiHdr sth;
    unsigned char *rxbuf;
    long login_delay = 0;
    int pad;

    if ( ! vtaskp ) {
        DEBUG_ERR0("iSCSI: rx thread task parameter NULL\n");
        return 0;
    }

    session = (iscsi_session_t *)vtaskp;
    atomic_inc(&session->refcount);
    hba = session->hba;

    /* become a daemon kernel thread, and abandon any user space resources */
    sprintf(current->comm,"iscsi-rx");
    iscsi_daemonize(); 
    session->rx_pid = current->pid;
    current->flags |= PF_MEMALLOC;
    mb();

    /* Block all signals except SIGHUP and SIGKILL */
    spin_lock_irq(&current->sigmask_lock);
    siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGHUP));
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);                                                            

    DEBUG_INIT3("iSCSI: rx thread %d for session %p, cpu%d\n", current->pid, session, smp_processor_id());

    while (!test_bit(SESSION_TERMINATING, &session->control_bits)) {
        unsigned long login_failures = 0;
        
        /* we need a session for the rx and tx threads to use */
        while (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
            if (login_delay) {
                printk("iSCSI: session %p to %s waiting %ld seconds before next login attempt\n",
                       session, session->TargetName, login_delay);
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(login_delay * HZ);
            }

            /* ensure we can write to the socket without interference */
            DEBUG_INIT3("iSCSI: rx thread %d waiting for tx blocked for session %p at %lu\n", 
                        current->pid, session, jiffies);
            wait_for_tx_blocked(session);
            if (test_bit(SESSION_TERMINATING, &session->control_bits))
                goto ThreadExit;
            
            /* now that the tx thread is idle, it's safe to clean up the old session, if there was one */
            iscsi_disconnect(session);
            clear_bit(SESSION_TASK_ALLOC_FAILED, &session->control_bits);
            clear_bit(SESSION_TIMED_OUT, &session->control_bits);
            clear_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits);
            clear_bit(SESSION_WINDOW_CLOSED, &session->control_bits);
            
            /* try to get a new session */
            if (iscsi_establish_session(session))
                login_failures = 0;
            else
                login_failures++;

            /* slowly back off the frequency of login attempts */
            if (login_failures < 10)
                login_delay = 1;  /* 10 seconds at 1 sec each */
            else if (login_failures < 20)
                login_delay = 2;  /* 20 seconds at 2 sec each */
            else if (login_failures < 26)
                login_delay = 5;  /* 30 seconds at 5 sec each */
            else if (login_failures < 34)
                login_delay = 15; /* 60 seconds at 15 sec each */
            else
                login_delay = 60; /* after 2 minutes, try once a minute */
        }

        DEBUG_INIT3("iSCSI: rx thread %d established session %p at %lu\n", current->pid, session, jiffies);
        
        /* handle rx for this session */
        while (!signal_pending(current)) {
            /* check for anything to read on socket */
            iov.iov_base = &sth;
            iov.iov_len = sizeof(sth);
            memset( &msg, 0, sizeof(struct msghdr) );
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            
            DEBUG_FLOW2("iSCSI: rx thread %d for session %p waiting to receive\n", session->rx_pid, session);
            
            rc = iscsi_recvmsg( session, &msg, sizeof(sth) );
            if (signal_pending(current)) {
                DEBUG_FLOW2("iSCSI: rx thread %d for session %p received signal\n", session->rx_pid, session);
                goto EndSession;
            }
            if ( rc == sizeof(sth) ) {
                if (sth.hlength) {
                    /* FIXME: read any additional header segments.
                     * For now, drop the session if one is received, since we can't handle them.
                     */
                    printk("iSCSI: additional header segments not supported by this driver version.\n");
                    iscsi_drop_session(session);
                    goto EndSession;
                }
                
                /* received something */
                xlen = ntoh24( sth.dlength );
                
                /* If there are padding bytes, read them as well */
                pad = xlen % PAD_WORD_LEN;
                if (pad) {
                    pad = PAD_WORD_LEN - pad;
                    xlen += pad;
                }

                DEBUG_FLOW5("iSCSI: rx PDU, opcode %x, len %d on session %p by pid %u at %lu\n", 
                            sth.opcode, xlen, session, current->pid, jiffies);

                if ( xlen && (sth.opcode != ISCSI_OP_SCSI_DATA_RSP) ) {
                    /* unless it's a data PDU, read the whole PDU into memory beforehand */
                    if ( xlen > ISCSI_RXCTRL_SIZE ) {
                        DEBUG_ERR2("iSCSI: PDU data length too large, opcode %x, dlen %d\n", sth.opcode, xlen);
                        iscsi_drop_session(session);
                        goto EndSession;
                    }
                    rxbuf = session->RxBuf;
                    iov.iov_base = rxbuf;
                    iov.iov_len = xlen;
                    memset( &msg, 0, sizeof(struct msghdr) );
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1;
                    rc = iscsi_recvmsg( session, &msg, xlen );
                    if ( rc != xlen ) {
                        DEBUG_ERR3("iSCSI: PDU opcode %x, recvmsg %d failed, %d\n", sth.opcode, xlen, rc);
                        iscsi_drop_session(session);
                        goto EndSession;
                    }
                } 
                else {
                    rxbuf = NULL;
                }
                
                switch (sth.opcode) {
                    case ISCSI_OP_NOOP_IN:
                        iscsi_recv_nop( session, (struct IscsiNopInHdr *)&sth, rxbuf);
                        break;
                    case ISCSI_OP_SCSI_RSP:
                        session->ExpStatSn = ntohl(((struct IscsiScsiRspHdr *)&sth)->statsn)+1;
                        mb();
                        iscsi_recv_cmd(session, (struct IscsiScsiRspHdr *)&sth, rxbuf );
                        break;
                    case ISCSI_OP_SCSI_TASK_MGT_RSP:
                        session->ExpStatSn = ntohl(((struct IscsiScsiTaskMgtRspHdr *)&sth)->statsn)+1;
                        mb();
                        iscsi_recv_mgmt_rsp(session, (struct IscsiScsiTaskMgtRspHdr *)&sth );
                        break;
                    case ISCSI_OP_RTT_RSP:
                        iscsi_recv_r2t(session, (struct IscsiRttHdr *)&sth );
                        break;
                    case ISCSI_OP_SCSI_DATA_RSP:
                        iscsi_recv_data( session, (struct IscsiDataRspHdr *)&sth );
                        break;
                    case ISCSI_OP_ASYNC_EVENT:
                        session->ExpStatSn = ntohl(((struct IscsiAsyncEvtHdr *)&sth)->statsn)+1;
                        mb();
                        iscsi_recv_async_event(session, (struct IscsiAsyncEvtHdr *)&sth, rxbuf);
                        break;
                    case ISCSI_OP_REJECT_MSG:
                        DEBUG_ERR0("iSCSI: target rejected PDU, dropping session\n");
                        iscsi_drop_session(session);
                        goto EndSession;
                    default:
                        DEBUG_ERR1("iSCSI: received unexpected opcode 0x%x, dropping session\n", sth.opcode);
                        iscsi_drop_session(session);
                        goto EndSession;
                }
            } 
            else {
                if ( rc != -EAGAIN ) {
                    if (rc == 0) {
                        printk( "iSCSI: session %p closed by target %s\n", session, session->log_name);
                    }
                    else if (rc == -ECONNRESET) {
                        printk( "iSCSI: session %p to %s received connection reset\n", session, session->log_name);
                    } 
                    else if ( rc == -ERESTARTSYS ) {
                        printk( "iSCSI: session %p to %s received signal\n", session, session->log_name);
                    } 
                    else {
                        printk("iSCSI: unexpected read status %d on session %p to %s\n", rc, session, session->log_name);
                    }
                    iscsi_drop_session(session);
                    goto EndSession;
                }
            }
        }

    EndSession:
        DEBUG_INIT3("iSCSI: rx thread %d noticed session %p going down at %lu\n", current->pid, session, jiffies);

        /* handle any signals that may have occured, which may kill the tx thread */
        iscsi_handle_signals(session);

        /* we need to wait for the tx thread to block before trying to complete commands,
         * since it may be using a task at the moment, which means we can't complete it yet.
         * even if the session is terminating, we must wait for the tx thread.
         */
        wait_for_tx_blocked(session);

        /* complete cmnds as appropriate */
        if (session->cold_reset_itt != RSVD_TASK_TAG) {
            printk("iSCSI: session %p cold target reset, completing SCSI commands with DID_RESET\n", session);
            /* linux SCSI layer wants the commands back after a reset */
            complete_all_cmnds(session, HOST_BYTE(DID_RESET));
            if (test_bit(SESSION_RESETTING, &session->control_bits)) {
                /* inform the eh_*_reset functions that the reset is completed */
                set_bit(SESSION_RESET, &session->control_bits);
                clear_bit(SESSION_RESETTING, &session->control_bits);
            }
        }
        else if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
            /* no point in retrying commands, complete them all back trying to provoke
             * an error rather than a retry.
             */
            printk("iSCSI: session %p terminating, aborting SCSI commands with DID_NO_CONNECT\n", session);
            complete_all_cmnds(session, HOST_BYTE(DID_NO_CONNECT));
        }
        else {
            DECLARE_NOQUEUE_FLAGS;

            /* We want to make sure we eventually get aborts for any
             * cmnds which are currently in the driver for this
             * session.  If we leave cmnds queued up unsent, they will
             * get sent when the session is reestablished.  There's a
             * race condition there, in that they may get marked as
             * timed out while the session is down, get sent and
             * completed after the session is reestablished, but then
             * get aborted by the SCSI error handler (which has
             * finally woken up).  In this case, we'd fail the abort,
             * since we no longer know anything about the command.  We
             * never want to fail an abort, since that causes reset
             * attempts, and Linux has too many bugs in it's reset
             * code.  Instead, we try to ensure that if any aborts
             * will occur, aborts will succeed for all tasks already
             * sent, and for all cmnds that get queued before the
             * error handler blocks the host and starts calling our
             * eh_abort entry point.
             */
            spin_lock(&session->task_lock);
            SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

            if (atomic_read(&session->num_active_tasks) ||
                atomic_read(&session->num_cmnds) || 
                atomic_read(&session->num_ignored_cmnds))
            {
                /* The session has dropped, so we can't possibly get a
                 * response from the target for any outstanding task.
                 * Thus, they are all guaranteed to remain in the
                 * driver and get aborted, and we don't need to do
                 * anything further with them.  
                 */
    
                /* any commands already queued must not be sent, to avoid races
                 * between command completion and the SCSI midlayer's error handler
                 * thread issuing aborts.  Move them to a Scsi_Cmnd queue that
                 * the tx thread will ignore.
                 */
                if (session->scsi_cmnd_head) {
                    if (session->ignored_cmnd_head) {
                        /* append to the existing ignored cmnds */
                        session->ignored_cmnd_tail->host_scribble = (unsigned char *)session->scsi_cmnd_head;
                        session->ignored_cmnd_tail = session->scsi_cmnd_tail;
                        atomic_add(atomic_read(&session->num_cmnds), &session->num_ignored_cmnds);
                    }
                    else {
                        /* nothing ignored yet, just move everything over */
                        session->ignored_cmnd_head = session->scsi_cmnd_head;
                        session->ignored_cmnd_tail = session->scsi_cmnd_tail;
                        atomic_set(&session->num_ignored_cmnds, atomic_read(&session->num_cmnds));
                    }
                    /* no longer anything queued to be sent */
                    session->scsi_cmnd_head = NULL;
                    session->scsi_cmnd_tail = NULL;
                    atomic_set(&session->num_cmnds, 0);
                }
                    
                /* make sure any cmnds queued from now til whenever
                 * error recovery starts will not actually get sent
                 * (see iscsi_queue).  
                 */
                set_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);

                printk("iSCSI: session %p to %s dropped at %lu, forcing error recovery of %u tasks and %u cmnds\n",
                       session, session->log_name, jiffies,
                       atomic_read(&session->num_active_tasks), atomic_read(&session->num_ignored_cmnds));
            }
            else {
                /* if there are no cmnds or tasks, don't force error
                 * recovery, to avoid the pathological case where a
                 * session drops, gets marked FORCE_ERROR_RECOVERY,
                 * gets reestablished, remains idle for hours, and
                 * then the first command queued to it hours later is
                 * forced through error recovery.  Instead,
                 * iscsi_queue is smart enough to force error recovery
                 * if a cmnd is queued while the session isn't
                 * established.  We don't need to force error recovery
                 * on idle sessions.  
                 */
                printk("iSCSI: session %p to %s dropped at %lu with no tasks or cmnds queued\n",
                       session, session->log_name, jiffies);
            }
#ifdef DEBUG
            /* to understand why an abort would fail later, we need to know what's in the driver now */
            print_session_cmnds(session);
            print_session_tasks(session);
#endif
            SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
            spin_unlock(&session->task_lock);
        }
        
        /* free any nop replies still queued */
        spin_lock(&session->task_lock);
        while (session->nop_reply_head) {
            iscsi_nop_info_t *nop_info = session->nop_reply_head;
            session->nop_reply_head = nop_info->next;
            DEBUG_ALLOC1("iSCSI: kfree nop_info %p\n", nop_info);
            kfree(nop_info);
        }
        session->nop_reply_tail = NULL;
        spin_unlock(&session->task_lock);

        /* record the time the session went down */
        session->session_drop_time = jiffies ? jiffies : 1;
    }

 ThreadExit:
    DEBUG_INIT2("iSCSI: rx thread %d for session %p exiting\n", session->rx_pid, session);
    /* indicate that we're already going down, so that we don't get killed */
    session->rx_pid = 0;
    mb();

    /* cleanup the socket */
    if (session->socket) {
        /* wait for the tx thread to exit */
        DEBUG_INIT2("iSCSI: rx thread %d waiting for tx thread %d to exit\n",
                    current->pid, session->tx_pid);
        while (session->tx_pid) {
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
        }

        /* drop the connection */
        iscsi_disconnect(session);
    }

    set_bit(SESSION_TERMINATED, &session->control_bits);
    drop_reference(session);

    return 0;
}

static int
iscsi_session( iscsi_session_t *session, iscsi_session_ioctl_t *ioctld )
{
    iscsi_hba_t *hba;
    int hba_number;
    int channel_number;
    pid_t rx_pid, tx_pid;
    int ret = 1;
    DECLARE_NOQUEUE_FLAGS;
    
    DEBUG_INIT2("iSCSI: ioctl at %lu for session to %s\n", jiffies, ioctld->TargetName);

    /* find the HBA that has the requested iSCSI bus */
    hba_number = ioctld->iscsi_bus / ISCSI_MAX_CHANNELS_PER_HBA;
    channel_number = ioctld->iscsi_bus % ISCSI_MAX_CHANNELS_PER_HBA;

    spin_lock(&iscsi_hba_list_lock);
    hba = iscsi_hba_list;
    while (hba && (hba_number-- > 0)) {
        hba = hba->next;
    }
    spin_unlock(&iscsi_hba_list_lock);

    if (!hba) {
        printk("iSCSI: couldn't find HBA with iSCSI bus %d\n", ioctld->iscsi_bus);
        return -EINVAL;
    }
    if (!hba->active) {
        printk("iSCSI: HBA %p is not active, can't add session\n", hba);
        return -EINVAL;
    }
    if (!hba->host) {
        printk("iSCSI: HBA %p has no host, can't add session\n", hba);
        return -EINVAL;
    }

    /* initialize the session structure */
    session->socket = NULL;
    
    spin_lock_init( &session->scsi_cmnd_lock);
    session->scsi_cmnd_head = session->scsi_cmnd_tail = NULL;
    atomic_set(&session->num_cmnds, 0);
    session->ignored_cmnd_head = session->ignored_cmnd_tail = NULL;
    atomic_set(&session->num_ignored_cmnds, 0);
    
    spin_lock_init( &session->task_lock);
    session->arrival_order.head = session->arrival_order.tail = NULL;
    session->rx_tasks.head = session->rx_tasks.tail = NULL;
    session->tx_tasks.head = session->tx_tasks.tail = NULL;
    session->completing_tasks.head = session->completing_tasks.tail = NULL;
    session->rx_abort_tasks.head = session->rx_abort_tasks.tail = NULL;
    session->tx_abort_tasks.head = session->tx_abort_tasks.tail = NULL;
    session->aborted_tasks.head = session->aborted_tasks.tail = NULL;
    session->tx_lun_reset_tasks.head = session->tx_lun_reset_tasks.tail = NULL;
    session->rx_lun_reset_tasks.head = session->rx_lun_reset_tasks.tail = NULL;
    session->lun_reset_tasks.head = session->lun_reset_tasks.tail = NULL;
    atomic_set(&session->num_active_tasks, 0);
    
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
    init_waitqueue_head(&session->tx_wait_q);
    init_waitqueue_head(&session->tx_blocked_wait_q);
    init_waitqueue_head(&session->login_wait_q);
#else
    session->tx_wait_q = NULL;
    session->tx_blocked_wait_q = NULL;
    session->login_wait_q = NULL;
#endif
    
    /* copy the IP address and port for the /proc entries */
    session->address_length = ioctld->address_length;
    memcpy(session->ip_address, ioctld->ip_address, ioctld->address_length);
    session->port = ioctld->port;
    session->tcp_window_size = ioctld->tcp_window_size;
    
    session->iscsi_bus = ioctld->iscsi_bus;
    session->host_no = hba->host->host_no;
    session->channel = ioctld->iscsi_bus % ISCSI_MAX_CHANNELS_PER_HBA;
    session->target_id = ioctld->target_id;
    session->generation = 0;
    session->itt = 1;

    /* copy the iSCSI params */
    iscsi_strncpy(session->InitiatorName, ioctld->InitiatorName, sizeof(session->InitiatorName));
    session->InitiatorName[sizeof(session->InitiatorName)-1] = '\0';
    iscsi_strncpy(session->InitiatorAlias, ioctld->InitiatorAlias, sizeof(session->InitiatorAlias));
    session->InitiatorAlias[sizeof(session->InitiatorAlias)-1] = '\0';
    session->isid = ioctld->isid;
    iscsi_strncpy(session->TargetName, ioctld->TargetName, sizeof(session->TargetName));
    session->TargetName[sizeof(session->TargetName)-1] = '\0';
    session->log_name = session->TargetName;

    /* iSCSI operational params */
    session->desired_InitialR2T = ioctld->InitialR2T;
    session->desired_DataPDULength = ioctld->DataPDULength; 
    session->desired_FirstBurstSize= ioctld->FirstBurstSize;
    session->desired_MaxBurstSize= ioctld->MaxBurstSize; 
    session->desired_ImmediateData = ioctld->ImmediateData;

    /* timeouts */
    session->login_timeout = ioctld->login_timeout;
    session->auth_timeout = ioctld->auth_timeout;
    session->active_timeout = ioctld->active_timeout;
    session->idle_timeout = ioctld->idle_timeout;
    session->ping_timeout = ioctld->ping_timeout; 
    session->abort_timeout = MAX(1, ioctld->abort_timeout);
    session->reset_timeout = MAX(1, ioctld->reset_timeout);
    session->replacement_timeout = MAX(60, ioctld->replacement_timeout);

    /* in case the session never comes up */
    session->session_drop_time = jiffies;

    if (ioctld->authenticate) {
        session->auth_client = kmalloc(sizeof(*session->auth_client), GFP_KERNEL);
        if (!session->auth_client) {
            printk("iSCSI: couldn't allocate authentication structure for session %p\n", session);
            return -ENOMEM;
        }        

        /* save the username and password */
        strncpy(session->username, ioctld->username, sizeof(session->username));
        session->password_length = strlen(ioctld->password);
        memcpy(session->password, ioctld->password, session->password_length);
    }

    /* attach the session to the HBA */
    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    if (!add_session(hba, session)) {
        /* couldn't add the session, tell the caller it failed in way that won't cause a retry */
        printk("iSCSI: couldn't add session %p, %s\n", session, session->TargetName);
        SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
        if (session->auth_client) {
            kfree(session->auth_client);
            session->auth_client = NULL;
        }
        /* clear the structure, since it may have contained a password */
        memset(session, 0, sizeof(*session));
        kfree(session);
        return 0;
    }
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    /* start a tx thread */
    DEBUG_INIT2("iSCSI: session %p about to start tx and rx threads at %lu\n", 
                session, jiffies);
    tx_pid = kernel_thread(iscsi_tx_thread, (void *)session, CLONE_VM| CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
    DEBUG_INIT3("iSCSI: session %p started tx thread %u at %lu\n", session, tx_pid, jiffies);
    
    /* start an rx thread */
    rx_pid = kernel_thread(iscsi_rx_thread, (void *)session, CLONE_VM| CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
    DEBUG_INIT3("iSCSI: session %p started rx thread %u at %lu\n", session, rx_pid, jiffies);
    
    /* wait for the threads to start */
    while ((session->tx_pid == 0) || (session->tx_pid == 0)) {
        schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
    }

    /* wait for the session login to complete before returning */
    wait_for_session(session, FALSE);
    if (signal_pending(current)) {
        iscsi_terminate_session(session);
    }
    if (test_bit(SESSION_TERMINATING, &session->control_bits))
        ret = 0;

    drop_reference(session);

    /* session is up, return success */
    return ret;
}


/* do timer processing for one session, and return the length of time
 * (in jiffies) til this session needs to be checked again.
 */
static unsigned long check_session_timeouts(iscsi_session_t *session)
{
    unsigned long timeout;
    unsigned long session_timeout = 0;

    if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
        /* check login phase timeouts */
        if (session->login_phase_timer) {
            session_timeout = session->login_phase_timer;
            if (time_before_eq(session_timeout, jiffies)) {
                printk("iSCSI: login phase for session %p (rx %d, tx %d) timed out at %lu, timeout was set for %lu\n", 
                       session, session->rx_pid, session->tx_pid, jiffies, session_timeout);
                set_bit(SESSION_TIMED_OUT, &session->control_bits);
                session->login_phase_timer = 0;
                mb();
                session_timeout = 0;
                iscsi_drop_session(session);
            }
        }
    }
    else {
        /* check full-feature phase timeouts. */
        if (test_bit(SESSION_WINDOW_CLOSED, &session->control_bits) &&
            time_before_eq(session->last_window_check + HZ, jiffies)) {
            /* command window closed, ping once a second to ensure we find out
             * when it re-opens.  Target should send us an update when it does,
             * but we're not very trusting of target correctness.
             */
            session->last_window_check = jiffies;
            printk("iSCSI: session %p command window closed, ExpCmdSN %u, MaxCmdSN %u\n", 
                   session, session->ExpCmdSn, session->MaxCmdSn);
            
            /* request a window update from the target with Nops */
            wake_tx_thread(TX_PING, session);
        }
        
        if (atomic_read(&session->num_active_tasks))
            timeout = session->active_timeout;
        else
            timeout = session->idle_timeout;
        
        if (timeout) {
            if (session->ping_timeout && 
                time_before_eq(session->last_rx + (timeout * HZ) + (session->ping_timeout * HZ), jiffies)) {
                /* should have received something by now, kill the connection */
                if ((session->last_kill == 0) || time_before_eq(session->last_kill + HZ, jiffies)) {
                    
                    session->last_kill = jiffies;
                    
                    printk("iSCSI: %lu second timeout expired for session %p, rx %lu, ping %lu, now %lu\n", 
                           timeout + session->ping_timeout, session, session->last_rx, session->last_ping, jiffies);
                    
                    iscsi_drop_session(session);
                    
                    set_bit(SESSION_TIMED_OUT, &session->control_bits);
                    session_timeout = jiffies + HZ;
                }
                else
                    session_timeout = 0;
            }
            else if (time_before_eq(session->last_rx + (timeout * HZ), jiffies)) {
                
                if (time_before_eq(session->last_ping, session->last_rx)) {
                    /* send a ping to try to provoke some traffic */
                    DEBUG_INIT4("iSCSI: timer queuing ping for session %p, rx %lu, ping %lu, now %lu\n", 
                                session, session->last_rx, session->last_ping, jiffies);
                    session->last_ping = jiffies - 1;
                    
                    wake_tx_thread(TX_PING, session);
                }
                session_timeout = session->last_rx + (timeout * HZ) + (session->ping_timeout * HZ);
            }
            else {
                if (atomic_read(&session->num_active_tasks)) {
                    session_timeout = session->last_rx + (session->active_timeout * HZ);
                }
                else {
                    unsigned long active_timeout, idle_timeout;
                    
                    /* session is idle, but may become active without the timer being notified,
                     * so use smaller of (now + active_timeout, last_rx + idle_timeout)
                     */
                    idle_timeout = session->last_rx + (session->idle_timeout * HZ);
                    active_timeout = jiffies + (session->active_timeout * HZ);
                    if (time_before_eq(idle_timeout, active_timeout)) {
                        session_timeout = idle_timeout;
                    }
                    else {
                        session_timeout = active_timeout;
                    }
                }
            }
        }
    }
    
    return session_timeout;
}

/*
 *  FIXME: it'd probably be cleaner to move the timeout logic to the rx thread.
 *         The only danger is if the rx thread somehow blocks indefinately.
 *         Doing timeouts here makes sure the timeouts get checked, at the
 *         cost of having this code constantly loop.
 */
static int iscsi_timer_thread(void *vtaskp) { 
    iscsi_session_t *session;
    iscsi_hba_t *hba;

    DEBUG_INIT1("iSCSI: timer starting at %lu\n", jiffies);

    /* become a child of init, and abandon any user space resources */
    sprintf(current->comm, "iscsi-timer");
    iscsi_daemonize(); 

    iscsi_timer_pid = current->pid;
    mb();
    printk("iSCSI: timer thread is pid %d\n", iscsi_timer_pid);

    /* Block all signals except SIGKILL */
    spin_lock_irq(&current->sigmask_lock);
    siginitsetinv(&current->blocked, sigmask(SIGKILL));
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);                                                            

    /* wait for the module to initialize */
    while (test_bit(0, &init_module_complete) == 0) {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
        if (signal_pending(current)) {
            iscsi_timer_running = 0;
            mb();
            return 0;
        }
    }

    DEBUG_INIT1("iSCSI: timer waiting for HBA at %lu\n", jiffies);
    while (!signal_pending(current)) {
        spin_lock(&iscsi_hba_list_lock);
        hba = iscsi_hba_list;
        spin_unlock(&iscsi_hba_list_lock);

        if (hba)
            break;

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
    }

    DEBUG_INIT1("iSCSI: timer looping over HBAs at %lu\n", jiffies);

    while (!signal_pending(current)) {
        unsigned long next_timeout = jiffies + (5 * HZ);
#if (ISCSI_MIN_CANQUEUE != ISCSI_MAX_CANQUEUE)
        int can_queue = 0;
#endif

        spin_lock(&iscsi_hba_list_lock);
        hba = iscsi_hba_list;
        while (hba) {
            DECLARE_NOQUEUE_FLAGS;

            SPIN_LOCK_NOQUEUE(&hba->session_lock);
            session = hba->session_list_head;
            while (session) {
                unsigned long session_timeout = 0;

#if DEBUG_ALLOC
                if (LOG_ENABLED(ISCSI_LOG_ALLOC))
                    printk("iSCSI: session %p, rx %5u, tx %5u, %u luns, %3u u, %3u i, %3u t, bits 0x%08x at %lu\n",
                           session, session->rx_pid, session->tx_pid, session->num_luns, 
                           atomic_read(&session->num_cmnds), atomic_read(&session->num_ignored_cmnds),
                           atomic_read(&session->num_active_tasks), 
                           session->control_bits, jiffies);
#endif                    
                
#if (ISCSI_MIN_CANQUEUE != ISCSI_MAX_CANQUEUE)
                if (!sna_lt(session->MaxCmdSn, session->CmdSn)) {
                    /* record how many more commands we can send on this session */
                    can_queue += max_tasks_for_session(session);
                }
#endif
                if (session->rx_pid) {
                    session_timeout = check_session_timeouts(session);
                }
                else {
                    /* FIXME: if a session has dropped, and not been replaced after a few minutes,
                     * try to speed up the the process of getting apps to give up and Linux to
                     * take the device offline by failing all the commands queued for the session.
                     * the tricky part is that the locking order is io_request_lock, and then hba->session_lock,
                     * so either we need to acquire the io_request_lock even though we may not need it,
                     * or we need to queue up dropped sessions somewhere to process later.  Queueing
                     * up the commands themselves for completeion later needs to ensure the cmnds
                     * don't get accidently reordered if the session comes up, and then drops again.
                     * We can't have 2 threads doing mass completions and maintain ordering.
                     */
                }

                /* find the earliest timeout that might occur, so that we know how long to sleep */
                if (session_timeout && time_before_eq(session_timeout, jiffies)) 
                    printk("iSCSI: ignoring session timeout %lu at %lu, last rx %lu, for session %p\n", 
                           session_timeout, jiffies, session->last_rx, session);
                else if (session_timeout && time_before(session_timeout, next_timeout))
                    next_timeout = session_timeout;

                session = session->next;
            }
            SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

#if (ISCSI_MIN_CANQUEUE != ISCSI_MAX_CANQUEUE)
            /* dynamically adjust the number of commands the HBA will accept, based
             * on each session's CmdSN window.
             */
            if (can_queue > ISCSI_MAX_CANQUEUE) {
                /* to avoid exhausting system resources, clamp the maximum number of commands
                 * the driver will accept.  This hopefully fixes the stalls seen when sessions drop
                 * and the daemon can't get a new session up because it's blocked on something.
                 */
                hba->host->can_queue = ISCSI_MAX_CANQUEUE;
                mb();
            }
            else if (can_queue > (NR_REQUEST / 2)) {
                /* we run into problems if we exhaust every struct request in the system.
                 * limit ourselves to half of the number of requests, so that other reads and
                 * writes can occur to non-iSCSI devices.  Linux 2.2 tends to reserve the top 1/3 
                 * of the requests for reads, so we could use at most 2/3 of the requests
                 * if we want to allow writes to non-iSCSI devices.  1/2 seems like a reasonable
                 * compromise.  Linux 2.4 uses separate request queues per device.
                 */
                hba->host->can_queue = NR_REQUEST / 2;
                mb();
            }
            else if (can_queue > ISCSI_MIN_CANQUEUE) {
                hba->host->can_queue = can_queue;
                mb();
            }
            else {
                hba->host->can_queue = ISCSI_MIN_CANQUEUE;
                mb();
            }
#endif
#ifdef DEBUG_ALLOC
            DEBUG_ALLOC5("iSCSI: timer - host %d can_queue %d, used %u, free %u, at %lu\n", 
                         hba->host->host_no, hba->host->can_queue, 
                         atomic_read(&hba->num_used_tasks), atomic_read(&hba->num_free_tasks),
                         jiffies);
#endif
            
            /* check every 3 minutes to see if we should free tasks from the HBA's pool back to the kernel */
            if (time_before_eq(hba->last_kfree_check + (3 * 60 * HZ), jiffies)) {
                spin_lock(&hba->free_task_lock);
                DEBUG_ALLOC2("iSCSI: checking free tasks at %lu, min_free %u\n", jiffies, hba->min_free_tasks);
                hba->last_kfree_check = jiffies;
                /* always keep some tasks pre-allocated */
                while (hba->min_free_tasks > ISCSI_PREALLOCATED_TASKS) {
                    iscsi_task_t *task;
                    
                    if ((task = pop_task(&hba->free_tasks))) {
                        atomic_dec(&hba->num_free_tasks);
                        hba->min_free_tasks--;
                        kfree(task);
                        //    DEBUG_ALLOC1("iSCSI: kfree task %p\n", task);
                    }
                    else {
                        printk("iSCSI: bug - min_free_tasks %u, free_tasks %u, but couldn't pop a task\n",
                               hba->min_free_tasks, atomic_read(&hba->num_free_tasks));
                        atomic_set(&hba->num_free_tasks, 0);
                    }
                }
                hba->min_free_tasks = atomic_read(&hba->num_free_tasks);
                spin_unlock(&hba->free_task_lock);
            }

            hba = hba->next;
        }
        spin_unlock(&iscsi_hba_list_lock);

        /* sleep for a while */
        if (time_before(jiffies, next_timeout)) {
            unsigned long sleep;

            /* sleep til the next time a timeout might occur, and handle jiffies wrapping */
            if (next_timeout < jiffies)
                sleep = (ULONG_MAX - jiffies + next_timeout);
            else
                sleep = (next_timeout - jiffies);
            DEBUG_FLOW4("iSCSI: timer sleeping for %lu jiffies, now %lu, next %lu, HZ %u\n", 
                        sleep, jiffies, next_timeout, HZ);

            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(sleep);
            if (signal_pending(current))
                goto finished;
        }
        else {
            /* this should never happen, but make sure we block for at least a little while
             * if it does somehow, otherwise it'll lock up the machine and be impossible
             * to debug what went wrong.
             */
            DEBUG_FLOW3("iSCSI: timer forced to sleep, now %lu, next %lu, HZ %u\n", 
                        jiffies, next_timeout, HZ);
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(1);
            if (signal_pending(current))
                goto finished;
        }
    }

 finished:
    /* timer finished */
    DEBUG_INIT1("iSCSI: timer leaving kernel at %lu\n", jiffies);
    
    set_current_state(TASK_RUNNING);

    iscsi_timer_running = 0;
    iscsi_timer_pid = 0;
    mb();
    
    return 0;
}

int
iscsi_detect( Scsi_Host_Template *sht )
{
    struct Scsi_Host *sh;
    iscsi_hba_t *hba;
    int num_tasks;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
    sht->proc_dir = &proc_dir_iscsi;
#else
    sht->proc_name = "iscsi";
#endif

    sh = scsi_register( sht, sizeof(iscsi_hba_t) );
    if (!sh ) {
        printk("iSCSI: Unable to register controller\n");
        return 0;
    }

    sh->max_id = ISCSI_MAX_TARGETS;
    sh->max_lun = ISCSI_MAX_LUN;
    sh->max_channel = ISCSI_MAX_CHANNELS_PER_HBA - 1; /* convert from count to index */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,0)
    /* indicate the maximum CDB length we can accept */
    sh->max_cmd_len = ISCSI_MAX_CMD_LEN;
#endif

    hba = (iscsi_hba_t *)sh->hostdata;
    memset( hba, 0, sizeof(iscsi_hba_t) );

    hba->next = NULL;
    hba->host = sh;

    /* list of sessions on this HBA */
    spin_lock_init(&hba->session_lock);
    hba->session_list_head = NULL;
    hba->session_list_tail = NULL;
    atomic_set(&hba->num_sessions, 0);

    /* pool of free iscsi tasks */
    spin_lock_init(&hba->free_task_lock);
    hba->free_tasks.head = NULL;
    hba->free_tasks.tail = NULL;
    for (num_tasks = 0; num_tasks < ISCSI_PREALLOCATED_TASKS; num_tasks++) {
        iscsi_task_t *task = (iscsi_task_t *)kmalloc(sizeof(*task), GFP_KERNEL);
        memset(task, 0x0, sizeof(*task));
        add_task(&hba->free_tasks, task);
    }
    atomic_set(&hba->num_free_tasks, num_tasks);
    atomic_set(&hba->num_used_tasks, 0);
    hba->min_free_tasks = 0;
    hba->last_kfree_check = jiffies;

    hba->active = 1;

    /* for now, there's just one iSCSI HBA */
    mb();
    iscsi_hba_list = hba;
    mb();
    printk("iSCSI: detected HBA %p, host #%d\n", hba, sh->host_no);
    return 1;
}

/* cleanup before unloading the module */
int iscsi_release(struct Scsi_Host *sh)
{
    iscsi_hba_t *hba;
    iscsi_session_t *session;
    iscsi_task_t *task;
    pid_t pid;
    DECLARE_NOQUEUE_FLAGS;

    hba = (iscsi_hba_t *)sh->hostdata;
    if ( ! hba ) {
        return FALSE;
    }

    printk("iSCSI: release HBA %p, host #%d\n", hba, hba->host->host_no);
    
    /* remove all sessions on this HBA */
    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    session = hba->session_list_head;
    while (session) {
        DEBUG_INIT2("iSCSI: host #%u terminating session %p\n", sh->host_no, session);
        iscsi_terminate_session(session);
        session = session->next;
    }
    
    /* wait for sessions to drop */
    while ((session = hba->session_list_head)) {
        if (atomic_read(&session->refcount)) {
            /* can't sleep with the lock held */
            SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

            /* give up if we got a signal */
            if (signal_pending(current)) {
                printk("iSCSI: host #%u failed to terminate session %p\n",
                       sh->host_no, session);
                return FALSE;
            }

            /* try to get the tx thread to wakeup and suicide */
            wake_tx_thread(SESSION_TERMINATING, session);

            /* wait a bit for it to die */
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout((HZ / 10) ? (HZ / 10) : 1);

            SPIN_LOCK_NOQUEUE(&hba->session_lock);
        }
        else {
            /* session dropped */
            printk("iSCSI: host #%u terminated session %p\n", sh->host_no, session);
            hba->session_list_head = session->next;
            kfree(session);
        }
    }
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    /* kill the timer */
    if ((pid = iscsi_timer_pid)) {
        printk("iSCSI: killing timer pid %d\n", pid);
        kill_proc(pid, SIGKILL, 1);

        /* wait for it to die */
        while (iscsi_timer_running || iscsi_timer_pid) {
            schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
        }
    }

    /* free the tasks */
#if DEBUG_ALLOC
    if (atomic_read(&hba->num_free_tasks))
        printk("iSCSI: freeing kernel memory for %u tasks\n", atomic_read(&hba->num_free_tasks));
#endif
        
    spin_lock(&hba->free_task_lock);
    while ((task = pop_task(&hba->free_tasks))) {
        /* DEBUG_ALLOC1("iSCSI: freeing kernel memory for task %p\n", task); */
        kfree(task);
    }
    spin_unlock(&hba->free_task_lock);

    /* remove from the iSCSI HBA list */
    spin_lock(&iscsi_hba_list_lock);
    if (hba == iscsi_hba_list) {
        iscsi_hba_list = iscsi_hba_list->next;
    }
    else {
        iscsi_hba_t *prior = iscsi_hba_list;

        while (prior && prior->next != hba)
            prior = prior->next;
        if (prior && prior->next == hba)
            prior->next = hba->next;
    }
    spin_unlock(&iscsi_hba_list_lock);

    memset( hba, 0, sizeof(iscsi_hba_t) );
    scsi_unregister( sh );

    return FAILED;
}

/* remove a Scsi_Cmnd from a singly linked list joined by the host_scribble pointers. */
static int remove_cmnd(Scsi_Cmnd *sc, Scsi_Cmnd **head, Scsi_Cmnd **tail)
{
    if (!sc || !head || !tail) {
        printk("iSCSI: bug - remove_cmnd %p, head %p, tail %p\n", sc, head, tail);
        return 0;
    }

    if (sc == *head) {
        /* it's the head, remove it */
        *head = (Scsi_Cmnd *)sc->host_scribble; /* next */
        if (*head == NULL)
            *tail = NULL;
        sc->host_scribble = NULL;
        return 1;
    }
    else if (*head) {
        Scsi_Cmnd *prior, *next;
        
        /* try find the command prior to sc */
        prior = *head;
        next = (Scsi_Cmnd *)prior->host_scribble;
        while (next && (next != sc)) {
            prior = next;
            next = (Scsi_Cmnd *)prior->host_scribble; /* next command */
        }
        if (prior && (next == sc)) {
            /* remove the command */
            prior->host_scribble = sc->host_scribble;
            if (*tail == sc)
                *tail = prior;
            sc->host_scribble = NULL;
            return 1;
        }
    }

    return 0;
}

/*
 * The abort handlers will wait for a session to be established, so
 * that we can actually send aborts, and we don't progress through the
 * error recovery functions while we can't do anything useful.  If we
 * don't have a session, we don't want to be doing resets, because:
 *
 * 1) the SCSI layer doesn't handle LUN resets of multiple commands
 * correctly in any Linux kernel available as of 2/10/2002, so we
 * almost always go on to a target reset, since sessions rarely have
 * only one command outstanding when the session drops (they'll
 * typically either have 0 or more than 1).
 *
 * 2) target resets tend to cause the next command after a reset to
 * return sense data with a deferred error "device reset".  For
 * kernels 2.2.16-2.2.20, 2.4.1, and 2.4.2, if this happens on the TUR
 * following the reset, the device will get marked offline
 * inappropriately, due to code in scsi_error.c mapping NEEDS_RETRY to
 * FAILED for no apparent reason.  For kernels with that mapping
 * removed, the sense data causes the SCSI layer to try to partially
 * complete the sectors of an IO request that have been finished, and
 * retry the rest.  However, sometimes an IO error gets sent to the
 * application.
 *
 * In short, the Linux SCSI layer is sufficiently broken that we'd
 * prefer to just pretend all the commands eventually got aborted,
 * which will hopefully retry everything without reporting errors to
 * applications, since they often can't handle errors in any useful
 * way.  We use a timeout to notice cases where to session doesn't
 * appear to be coming back up, so that we do eventually exit the
 * error recovery no matter what.  Since the error recovery process
 * blocks all commands to the iSCSI HBA, no other iSCSI targets can do
 * IO will any target is in error recovery.  The timeout ensures that
 * eventually the targets with sessions will start getting commands
 * again.  
 */
int
iscsi_eh_abort( Scsi_Cmnd *sc )
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_task_t *task = NULL;
    iscsi_session_t *session = NULL;
    iscsi_task_t *aborted_task = NULL;
    int refcount, ret = FAILED;
    unsigned long timeout;
    unsigned long interval = (HZ / 10) ? (HZ / 10) : 10;
    unsigned long last_log = 0;
    unsigned long current_gen;
    DECLARE_NOQUEUE_FLAGS;

    if ( ! sc ) {
        return FAILED;
    }
    host = sc->host;
    if (! host) {
        printk("iSCSI: no host for SCSI command %p\n", sc);
        return FAILED;
    }
    hba = (iscsi_hba_t *)host->hostdata;
    if (!hba) {
        printk("iSCSI: no iSCSI HBA associated with SCSI command %p\n", sc);
        return FAILED;
    }

    /* find the appropriate session for the command */
    session = find_session_for_cmnd(sc);
    if (!session) {
        printk("iSCSI: can't abort cmnd %p, no session for command\n", sc);
        return FAILED;
    }

    RELEASE_IO_REQUEST_LOCK;

    printk("iSCSI: eh_abort at %lu for command %p to (%u %u %u %u), Cmd 0x%x, session %p, rx %u, tx %u, host_failed %u\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], 
           session, session->rx_pid, session->tx_pid, sc->host->host_failed);

    /* try to wait til we have a session, since even if it's an unsent
     * command that doesn't require a session itself, the next thing
     * after a successful abort is a TUR, which requires a session to
     * succeed, and we really want it succeed, to avoid the later
     * stages of error recovery, which Linux doesn't handle very well.
     */
    if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
        if (in_interrupt()) {
            printk("iSCSI: eh_abort failed, in interrupt with no session at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
                   jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
            ret = FAILED;
            goto relock;
        }
        printk("iSCSI: eh_abort %p by pid %d waiting for session %p to be established at %lu\n", 
               sc, current->pid, session, jiffies);

        if (!wait_for_session(session, TRUE)) {
            printk("iSCSI: eh_abort failed waiting for session %p at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
                   session, jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
            ret = FAILED;
            goto relock;
        }

        printk("iSCSI: eh_abort %p by pid %d wait ended at %lu, session %p established\n", 
               sc, current->pid, jiffies, session);
    }

    /* we're in error recovery with a session established, mission
     * accomplished.  ensure this bit is cleared so that the TUR
     * command queued if we return success will actually get sent.
     */
    clear_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);

    /* check if the cmnd is unsent, or was deliberately held so that we could abort it. */
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
    if (remove_cmnd(sc, &session->scsi_cmnd_head, &session->scsi_cmnd_tail)) {
        atomic_dec(&session->num_cmnds);
        printk("iSCSI: aborted unsent command %p to (%u %u %u %u), Cmd 0x%x\n",
               sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
        ret = SUCCESS;
        goto done;
    }
    else if (remove_cmnd(sc, &session->ignored_cmnd_head, &session->ignored_cmnd_tail)) {
        atomic_dec(&session->num_ignored_cmnds);
        printk("iSCSI: aborted command %p to (%u %u %u %u), Cmd 0x%x\n",
               sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
        ret = SUCCESS;
        goto done;
    }
    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    if (in_interrupt()) {
        printk("iSCSI: eh_abort failed, in interrupt at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        ret = FAILED;
        goto relock;
    }

    /* 
     * check tasks (commands already sent to the target) 
     */
    spin_lock(&session->task_lock);
    current_gen = session->generation;
    if ((task = remove_task_for_cmnd(&session->rx_tasks, sc))) {
        /* it's waiting for data, an R2T, or a command response */
        task->mgmt_itt = 0;
        atomic_inc(&task->refcount);
        add_task(&session->tx_abort_tasks, task);
        set_bit(ISCSI_TASK_ABORTING, &task->flags);
        aborted_task = NULL;
    }
    else if ((task = remove_task_for_cmnd(&session->tx_tasks, sc))) {
        /* it's received an R2T, and is queued to have data sent */
        task->mgmt_itt = 0;
        atomic_inc(&task->refcount);
        add_task(&session->tx_abort_tasks, task);
        set_bit(ISCSI_TASK_ABORTING, &task->flags);
        aborted_task = NULL;
    }
    else if ((task = remove_task_for_cmnd(&session->completing_tasks, sc))) {
        /* already received command completion, no point in sending an abort */
        task->mgmt_itt = 0;
        atomic_inc(&task->refcount);
        set_bit(ISCSI_TASK_ABORTING, &task->flags);
        aborted_task = task;
        remove_session_task(session, task);
        DEBUG_INIT3("iSCSI: eh_abort - no need to send abort for itt %u, task %p, cmnd %p completed\n",
                    task->itt, task, sc);
    }
    spin_unlock(&session->task_lock);
    
    if (task) {
        if (aborted_task == NULL) {
            
            printk("iSCSI: aborting itt %u, task %p, command %p to (%u %u %u %u), Cmd 0x%x\n",
                   task->itt, task, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
            wake_tx_thread(TX_ABORT, session);
            
            /* wait for an abort response for the abort we just queued up
             * to be sent.  it's possible the target will never respond,
             * so after a configurable timeout, just wait for the task's
             * refcount to hit zero, and then free the task.  It's
             * possible the refcount won't hit zero, if the tx thread or
             * rx thread is blocked trying to read from or write to the
             * command's buffer, but in that case the timer thread will
             * eventually kill the session, and we can return as soon as
             * the session starts going down.  
             */
            timeout = jiffies + (session->abort_timeout * HZ);
            /* until the session drops or the timeout expires */
            while (test_bit(SESSION_ESTABLISHED, &session->control_bits) && 
                   (session->generation == current_gen) && 
                   time_before(jiffies, timeout)) 
            {
                spin_lock(&session->task_lock);
                if ((aborted_task = remove_task_for_cmnd(&session->aborted_tasks, sc))) {
#if TEST_DEVICE_RESETS
                    /* ignore some successful aborts, to test the other error handlers */
                    if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc &&
                        (sc->cmnd[0] != TEST_UNIT_READY) &&
                        (aborted_task->cmdsn >= (DEVICE_RESET_FREQUENCY * ABORT_FREQUENCY)) && 
                        ((aborted_task->cmdsn % (DEVICE_RESET_FREQUENCY * ABORT_FREQUENCY)) >= 0) && 
                        ((aborted_task->cmdsn % (DEVICE_RESET_FREQUENCY * ABORT_FREQUENCY)) < DEVICE_RESET_COUNT))
                    {
                            
                        printk("iSCSI: ignoring successful abort of command %p, task %p, (%u %u %u %u), Cmd 0x%x\n",
                               sc, aborted_task, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
                        /* put the task back in rx_tasks, so that we can find it in the other eh_*_handlers */
                        add_task(&session->rx_tasks, aborted_task);
                        atomic_dec(&aborted_task->refcount);
                        spin_unlock(&session->task_lock);
                        ret = FAILED;
                        goto done;
                    }
#endif
                    remove_session_task(session, task);
                }
                spin_unlock(&session->task_lock);

                if (aborted_task) {
                    printk("iSCSI: abort confirmed for itt %u, task %p\n", aborted_task->itt, aborted_task);
                    break;
                }
                
                /* wait a bit and check again */
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(interval);
            }

            /* if we didn't receive an abort by now, quit waiting for it */
            if (!aborted_task) {
                spin_lock(&session->task_lock);
                if ((aborted_task = remove_task_for_cmnd(&session->rx_abort_tasks, sc))) {
                    remove_session_task(session, aborted_task);
                    printk("iSCSI: failed to recv abort response for itt %u, task %p, cmnd %p\n",
                           aborted_task->itt, aborted_task, sc);
                }
                else if ((aborted_task = remove_task_for_cmnd(&session->tx_abort_tasks, sc))) {
                    remove_session_task(session, aborted_task);
                    printk("iSCSI: failed to send abort for itt %u, task %p, cmnd %p\n",
                           aborted_task->itt, aborted_task, sc);
                }
                spin_unlock(&session->task_lock);
                ret = FAILED;
                atomic_dec(&task->refcount);
                goto done;
            }
        }

        while (aborted_task) {
            /* We've removed the task from the session, and now just
             * need to wait for the task refcount to be 1 (just us),
             * before we free it.  This should happen eventually, since
             * if nothing else the timer thread will kill the tx and
             * rx threads, and they'll quit using the task then.  
             */
            if ((refcount = atomic_read(&aborted_task->refcount)) <= 1) {
                printk("iSCSI: aborted command %p, task %p, (%u %u %u %u), Cmd 0x%x\n",
                       sc, aborted_task, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
                atomic_dec(&aborted_task->refcount);
                free_task(aborted_task);
                ret = SUCCESS;
                goto done;
            }
            
            /* log once per second indicating that eh_abort is waiting */
            if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                last_log = jiffies;
                DEBUG_ERR3("iSCSI: eh_abort waiting for task %p, command %p, refcount %u before returning\n",
                           aborted_task, sc, refcount);
            }

            /* wait a bit and check again */
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(interval);
        }
    }
    else {

        ret = FAILED;
        printk("iSCSI: eh_abort couldn't find task for sc %p to (%u %u %u %u), %u unsent cmnds, %u ignored cmnds, tasks: active %u, used %u, free %u\n", 
               sc, sc->host->host_no, sc->channel, sc->target, sc->lun, 
               atomic_read(&session->num_cmnds), atomic_read(&session->num_ignored_cmnds), 
               atomic_read(&session->num_active_tasks), atomic_read(&hba->num_used_tasks), atomic_read(&hba->num_free_tasks));

#ifdef DEBUG
        {
            /* print the session's queued Cmnds */
            SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
            print_session_cmnds(session);
            SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
            
            /* print the session's tasks */
            spin_lock(&session->task_lock);
            print_session_tasks(session);
            spin_unlock(&session->task_lock);
        }
#endif
    }

 done:
    if (ret != SUCCESS) {
        if (task) {
            printk("iSCSI: abort failed for command %p, task %p, (%u %u %u %u), Cmd 0x%x\n",
                   sc, task, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        }
        else {
            printk("iSCSI: abort failed for command %p, task not found, (%u %u %u %u), Cmd 0x%x\n",
                   sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        }
    }
 relock:
    drop_reference(session);
    REACQUIRE_IO_REQUEST_LOCK;
    return ret;
}


/* trigger a warm reset */
int warm_reset_target(iscsi_session_t *session)
{
    unsigned long timeout = jiffies + (session->reset_timeout * HZ);
    unsigned long interval = (HZ / 10) ? (HZ / 10) : 10;
    unsigned long last_log = 0;
    
    printk("iSCSI: warm target reset starting at %lu, timeout at %lu (%d seconds), interval %lu, HZ %u\n",
           jiffies, timeout, session->reset_timeout, interval, HZ);

    if (in_interrupt()) {
        printk("iSCSI: warm_reset_target in interrupt, failing\n");
        return 0;
    }

    /* make sure there is only one oustanding reset, 
     * and prevent queued commands from being sent.
     */
    if (test_and_set_bit(SESSION_RESETTING, &session->control_bits)) {
        /* already resetting */
        printk("iSCSI: session %p to %s already resetting, reset_target failed\n",
               session, session->log_name);
        return 0;
    }
    
    /* queue up a reset for the target */
    wake_tx_thread(TX_WARM_TARGET_RESET, session);

    /* wait for it to get sent */
    while (time_before(jiffies, timeout) && test_bit(TX_WARM_TARGET_RESET, &session->control_bits)) {
        /* log once per second indicating that we're waiting */
        if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
            last_log = jiffies;
            printk("iSCSI: waiting for target reset to be sent for session %p to %s\n", 
                   session, session->log_name);
        }
        /* wait for a bit */
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(interval);
    }
    
    /* now wait for the reset response */
    last_log = 0;
    while (time_before(jiffies, timeout) && 
           test_bit(SESSION_RESETTING, &session->control_bits) &&
           !test_bit(SESSION_RESET, &session->control_bits)) {
        
        /* log once per second indicating that we're waiting */
        if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
            last_log = jiffies;
            printk("iSCSI: waiting for target reset to occur for session %p to %s\n", session, session->log_name);
        }
        /* wait for a while and check again */
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(interval);
    }
    
    last_log = 0;
    if (test_and_clear_bit(SESSION_RESET, &session->control_bits)) {
        iscsi_task_t *task;
        int nonzero_refcount = 1;

        printk("iSCSI: aborting commands after warm target reset for session %p to %s\n", session, session->log_name);
        
        /* make sure the refcount of all tasks is zero before completing them.
         * if it doesn't happen before the timeout expires, try killing the session.
         * to force the refcount to zero.
         */
        while (nonzero_refcount) {
            spin_lock(&session->task_lock);
            nonzero_refcount = 0;
            task = first_task(&session->arrival_order);
            while (!nonzero_refcount && task) {
                if (time_before(timeout, jiffies)) {
                    printk("iSCSI: reset timeout expired while waiting for task %p refcount %u, killing session\n",
                           task, atomic_read(&task->refcount));
                    /* try killing the session */
                    iscsi_drop_session(session);
                    /* and waiting a bit longer */
                    timeout = jiffies + (session->reset_timeout * HZ);
                    break;
                }
                if (atomic_read(&task->refcount) == 0) {
                    task = order_next_task(&session->arrival_order, task);
                }
                else {
                    nonzero_refcount = 1;
                }
            }
            spin_unlock(&session->task_lock);
             
            if (nonzero_refcount) {
                /* log once per second indicating that we're waiting */
                if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                    last_log = jiffies;
                    printk("iSCSI: warm reset waiting for task %p refcount %u to reach 0\n", 
                           task, atomic_read(&task->refcount));
                }
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(interval);
            }
        }
        
        /* complete everything after the reset */
        printk("iSCSI: warm target reset, completing SCSI commands for session %p to %s with DID_RESET\n", 
               session, session->log_name);
        complete_all_cmnds(session, HOST_BYTE(DID_RESET));

        printk("iSCSI: warm target reset succeeded at %lu for session %p to %s\n", 
               jiffies, session, session->log_name);
        return 1;
    }

    if (time_before(jiffies, timeout))
        printk("iSCSI: warm target reset failed at %lu for session %p to %s\n", jiffies, session, session->log_name);
    else
        printk("iSCSI: warm target reset timed out at %lu (%d seconds) for session %p to %s\n", 
               jiffies, session->reset_timeout, session, session->log_name);

    /* give up */
    clear_bit(TX_WARM_TARGET_RESET, &session->control_bits);
    if (test_and_clear_bit(SESSION_RESETTING, &session->control_bits)) {
        /* since sending tasks is disabled while resetting, make sure we
         * start sending them again when we stop trying to reset.
         */
        wake_tx_thread(TX_SCSI_COMMAND, session);
    }
    clear_bit(SESSION_RESET, &session->control_bits);
    return 0;
}

/* trigger a cold reset */
int cold_reset_target(iscsi_session_t *session)
{
    unsigned long timeout = jiffies + (session->reset_timeout * HZ);
    unsigned long interval = (HZ / 10) ? (HZ / 10) : 10;
    unsigned long last_log = 0;
    unsigned long current_gen = session->generation;

    printk("iSCSI: cold target reset starting at %lu, timeout at %lu (%d seconds), interval %lu, HZ %u\n",
           jiffies, timeout, session->reset_timeout, interval, HZ);

    if (in_interrupt()) {
        printk("iSCSI: cold_reset_target in interrupt, failing\n");
        return 0;
    }

    /* make sure there is only one oustanding reset, 
     * and prevent queued commands from being sent.
     */
    if (test_and_set_bit(SESSION_RESETTING, &session->control_bits)) {
        /* already resetting */
        printk("iSCSI: session %p to %s already resetting, reset_target failed\n",
               session, session->log_name);
        return 0;
    }

    /* queue up a reset for the target */
    wake_tx_thread(TX_COLD_TARGET_RESET, session);

    /* wait for it to get sent */
    while (time_before(jiffies, timeout) && test_bit(TX_COLD_TARGET_RESET, &session->control_bits)) {
        /* log once per second indicating that we're waiting */
        if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
            last_log = jiffies;
            printk("iSCSI: waiting for cold target reset to be sent for session %p to %s\n", 
                   session, session->log_name);
        }
        /* wait for a bit */
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(interval);
    }
    
    /* now wait for the reset to actually occur. */
    last_log = 0;
    while (time_before(jiffies, timeout) && 
           test_bit(SESSION_RESETTING, &session->control_bits) &&
           !test_bit(SESSION_RESET, &session->control_bits)) {
        
        /* log once per second indicating that we're waiting */
        if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
            last_log = jiffies;
            printk("iSCSI: waiting %lu jiffies from %lu for cold target reset to occur for session %p to %s\n", 
                   interval, jiffies, session, session->log_name);
        }
        /* wait for a while and check again */
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(interval);
    }
    
    if (test_and_clear_bit(SESSION_RESET, &session->control_bits)) {
        /* make sure we start sending SCSI commands again */
        clear_bit(SESSION_RESETTING, &session->control_bits);
        wake_tx_thread(TX_SCSI_COMMAND, session);
        /* the commands have already completed with DID_RESET when the session dropped */
        printk("iSCSI: cold target reset succeeded at %lu for session %p to %s\n", 
               jiffies, session, session->log_name);

        /* Try to wait for another session to start before returning.  This
         * helps when we're doing error recovery, since once the handler returns,
         * the SCSI layer will queue a TUR with a 10 second timeout, and it must
         * complete sucessfully.  Since it may take longer than 10 seconds to get
         * another session up, the TUR would timeout unsent.  To avoid that, we
         * try to make sure we have another session before returning.
         */
        last_log = 0;
        while (time_before(jiffies, timeout) && 
               ((current_gen == session->generation) || !test_bit(SESSION_ESTABLISHED, &session->control_bits))) {
            /* log once per second indicating that we're waiting */
            if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                last_log = jiffies;
                printk("iSCSI: waiting for another session to %s after cold target reset, sleeping %lu at %lu\n",
                       session->log_name, interval, jiffies);
            }
            /* wait for a while and check again */
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(interval);
        }
        if ((current_gen == session->generation) || !test_bit(SESSION_ESTABLISHED, &session->control_bits))
            printk("iSCSI: timed out waiting for another session to %s after cold target reset\n",
                   session->log_name);
        return 1;
    }

    if (time_before(jiffies, timeout))
        printk("iSCSI: cold target reset failed at %lu for session %p to %s\n", jiffies, session, session->log_name);
    else
        printk("iSCSI: cold target reset timed out at %lu (%d seconds) for session %p to %s\n", 
               jiffies, session->reset_timeout, session, session->log_name);

    /* give up */
    clear_bit(TX_COLD_TARGET_RESET, &session->control_bits);
    if (test_and_clear_bit(SESSION_RESETTING, &session->control_bits)) {
        /* since sending tasks is disabled while resetting, make sure we
         * start sending them again when we stop trying to reset.
         */
        wake_tx_thread(TX_SCSI_COMMAND, session);
    }
    clear_bit(SESSION_RESET, &session->control_bits);
    return 0;
}

/*
 * All the docs say we're supposed to reset the device and complete
 * all commands for it back to the SCSI layer.  However, the SCSI
 * layer doesn't actually count how many commands are completed back
 * to it after a device reset, but rather just assumes only 1 command,
 * with a comment saying it should be fixed to handle the case where
 * there are multiple commands.  
 *
 * If there are multiple commands, the SCSI layer will blindly
 * continue on to the next stage of error recovery, even if we
 * complete all the failed commands back to it after a device reset.
 * Hopefully the Linux SCSI layer will be fixed to handle this
 * corectly someday.  In the meantime, we do the right thing here, and
 * make sure the other reset handlers can deal with the case where
 * they get called with a command that has already been completed back
 * to the SCSI layer by a device reset.
 *   
 */
int
iscsi_eh_device_reset( Scsi_Cmnd *sc )
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_session_t *session = NULL;
    iscsi_task_t *task = NULL;
    unsigned long timeout;
    unsigned long interval = (HZ / 10) ? (HZ / 10) : 10;
    unsigned long last_log = 0;
    unsigned long current_gen;

    if ( ! sc ) {
        printk("iSCSI: device reset, no SCSI command\n");
        return FAILED;
    }
    host = sc->host;
    if (! host) {
        printk("iSCSI: device reset, no host for SCSI command %p\n", sc);
        return FAILED;
    }
    hba = (iscsi_hba_t *)host->hostdata;
    if (!hba) {
        printk("iSCSI: device reset, no iSCSI HBA associated with SCSI command %p\n", sc);
        return FAILED;
    }

    /* find the appropriate session for the command */
    session = find_session_for_cmnd(sc);
    if (!session) {
        printk("iSCSI: can't reset device for cmnd %p, no session\n", sc);
        return FAILED;
    }

    printk("iSCSI: eh_device_reset at %lu for command %p to (%u %u %u %u), Cmd 0x%x, session %p, rx %u, tx %u, host_failed %u\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], 
           session, session->rx_pid, session->tx_pid, sc->host->host_failed);

    if (in_interrupt()) {
        printk("iSCSI: eh_device_reset in interrupt at %lu for command %p for (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        return FAILED;
    }
    
    RELEASE_IO_REQUEST_LOCK;

    if (!wait_for_session(session, TRUE)) {
        printk("iSCSI: eh_device_reset failed waiting for session %p at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
               session, jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        REACQUIRE_IO_REQUEST_LOCK;
        return FAILED;
    }

    /* we're in error recovery with a session established, mission
     * accomplished.  ensure this bit is cleared so that the TUR
     * command queued if we return success will actually get sent.
     */
    clear_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);

    /* see if we have a task for this command */
    spin_lock(&session->task_lock);
    current_gen = session->generation;
    if ((task = remove_task_for_cmnd(&session->aborted_tasks, sc))) {
        /* had an abort sent and confirmed already */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    else if ((task = remove_task_for_cmnd(&session->rx_abort_tasks, sc))) {
        /* had an abort sent already */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    else if ((task = remove_task_for_cmnd(&session->tx_abort_tasks, sc))) {
        /* waiting to have an abort sent */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    else if ((task = remove_task_for_cmnd(&session->rx_tasks, sc))) {
        /* it's waiting for data, an R2T, or a command response */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    else if ((task = remove_task_for_cmnd(&session->tx_tasks, sc))) {
        /* it's received an R2T, and is queued to have data sent */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    else if ((task = remove_task_for_cmnd(&session->completing_tasks, sc))) {
        /* already received command completion */
        add_task(&session->tx_lun_reset_tasks, task);
    }
    spin_unlock(&session->task_lock);

    if (!task) {
        /* couldn't find one, go allocate one, which may sleep, 
         * which is why we had to drop the spinlock.  Don't add the task
         * to the session, since we don't want to complete this task
         * back to the SCSI layer later.
         */
        if ((task = alloc_task(session))) {
            DEBUG_ALLOC1("iSCSI: kmalloc task %p for LUN reset\n", task);
            task->scsi_cmnd = sc;
            spin_lock(&session->task_lock);
            task->itt = 0; /* fake itt for the fake task */
            add_task(&session->tx_lun_reset_tasks, task);
            spin_unlock(&session->task_lock);
        }
    }

    if (task) {
        iscsi_task_collection_t tasks_for_lun;
        Scsi_Cmnd *unsent_cmnds = NULL;
        iscsi_task_t *reset = NULL;
        uint32_t itt = task->itt;

        tasks_for_lun.head = tasks_for_lun.tail = NULL;
        timeout = jiffies + (session->reset_timeout * HZ);

        printk("iSCSI: eh_device_reset (%u %u %u %u) starting at %lu for itt %u, task %p, "
               "timeout at %lu (%d seconds), %u tasks, %u cmnds\n",
               sc->host->host_no, sc->channel, sc->target, sc->lun, jiffies, task->itt, task, 
               timeout, session->reset_timeout, atomic_read(&session->num_active_tasks), atomic_read(&session->num_cmnds));
        
        wake_tx_thread(TX_LUN_RESET, session);

        /* wait for a response to the LUN reset */
        while (time_before(jiffies, timeout)) {
            spin_lock(&session->task_lock);
            if ((reset = remove_task(&session->lun_reset_tasks, itt))) {
                DECLARE_NOQUEUE_FLAGS;

                if (test_and_clear_bit(ISCSI_TASK_RESET_FAILED, &reset->flags)) {
                    /* the LUN reset attempt failed */
                    printk("iSCSI: LUN reset (%u %u %u %u) rejected\n",
                           sc->host->host_no, sc->channel, sc->target, sc->lun);
                    if (reset->itt == 0) {
                        /* free the task we allocated just for this */
                        free_task(reset);
                    }
                    else {
                        /* make sure the task can be found somewhere */
                        add_task(&session->rx_tasks, reset);
                    }
                    spin_unlock(&session->task_lock);
                    reset = NULL;
                    break;
                }

#if TEST_BUS_RESET
                if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc &&
                    (sc->cmnd[0] != TEST_UNIT_READY) &&
                    (reset->cmdsn >= (BUS_RESET_FREQUENCY * LUN_RESET_FREQUENCY * ABORT_FREQUENCY)) &&
                    ((reset->cmdsn % (BUS_RESET_FREQUENCY * LUN_RESET_FREQUENCY * ABORT_FREQUENCY)) >= 0) &&
                    ((reset->cmdsn % (BUS_RESET_FREQUENCY * LUN_RESET_FREQUENCY * ABORT_FREQUENCY)) < BUS_RESET_COUNT))                {
                    printk("iSCSI: ignoring successful LUN reset, task %p, (%u %u %u %u), Cmd 0x%x\n",
                           sc, reset, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
                    if (reset->itt == 0) {
                        /* free the task we allocated just for this */
                        free_task(reset);
                    }
                    else {
                        /* make sure the task can be found somewhere */
                        add_task(&session->rx_tasks, reset);
                    }
                    spin_unlock(&session->task_lock);
                    reset = NULL;
                    break;
                }
#endif
                
                printk("iSCSI: LUN reset (%u %u %u %u) confirmed for itt %u, task %p, session %p, tasks %u, cmnds %u\n", 
                       sc->host->host_no, sc->channel, sc->target, sc->lun, reset->itt, reset, session,
                       atomic_read(&session->num_cmnds), atomic_read(&session->num_active_tasks));
                remove_tasks_for_lun(&session->rx_tasks, sc->lun);
                remove_tasks_for_lun(&session->tx_tasks, sc->lun);
                remove_tasks_for_lun(&session->completing_tasks, sc->lun);
                remove_tasks_for_lun(&session->rx_abort_tasks, sc->lun);
                remove_tasks_for_lun(&session->tx_abort_tasks, sc->lun);
                remove_tasks_for_lun(&session->aborted_tasks, sc->lun);
                remove_tasks_for_lun(&session->rx_lun_reset_tasks, sc->lun);
                remove_tasks_for_lun(&session->tx_lun_reset_tasks, sc->lun);
                remove_tasks_for_lun(&session->lun_reset_tasks, sc->lun);
#if DEBUG_EH
                if (LOG_ENABLED(ISCSI_LOG_EH))
                    printk("iSCSI: LUN reset grabbing tasks, arrival head %p, tail %p, num %u\n", 
                           session->arrival_order.head, session->arrival_order.tail, atomic_read(&session->num_active_tasks));
#endif
                move_session_tasks_for_lun(&tasks_for_lun, session, sc->lun);
#if DEBUG_EH
                if (LOG_ENABLED(ISCSI_LOG_EH))
                    printk("iSCSI: LUN reset done grabbing tasks, arrival head %p, tail %p, num %u, lun tasks %p, %p\n", 
                           session->arrival_order.head, session->arrival_order.tail, atomic_read(&session->num_active_tasks),
                           tasks_for_lun.head, tasks_for_lun.tail);
#endif                
                /* grab all unsent cmnds for this LUN as well */
                SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
                unsent_cmnds = remove_session_cmnds_for_lun(session, sc->lun);
                SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

                /* we're now committed to completing these tasks and commands,
                 * and returning SUCCESS.
                 */
            }
            spin_unlock(&session->task_lock);
            
            if (reset) /* reset confirmed */
                break;

            if (session->generation != current_gen) {
                /* give up if the session has dropped, we'll never get a reply */
                break;
            }

            /* log once per second indicating that we're waiting */
            if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                last_log = jiffies;
                printk("iSCSI: waiting for LUN reset response for task %p\n", task);
            }
            
            /* wait a bit and check again */
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(interval);
        }
        last_log = 0;

        if (reset) {
            unsigned int num_tasks = 0, num_cmnds = 0;
            
            printk("iSCSI: LUN reset (%u %u %u %u) succeeded, completing tasks and cmnds\n",
                   sc->host->host_no, sc->channel, sc->target, sc->lun);
            
            /* wait for all the task refcounts to hit zero */
            task = first_task(&tasks_for_lun);
            while (task) {
                if (time_before(timeout, jiffies)) {
                    printk("iSCSI: reset timeout expired while waiting for task %p refcount %u, killing session\n",
                           task, atomic_read(&task->refcount));
                    /* try killing the session */
                    iscsi_drop_session(session);
                    /* and waiting a bit longer */
                    timeout = jiffies + (10 * HZ);
                }
                if (atomic_read(&task->refcount) == 0) {
                    task = next_task(&tasks_for_lun, task);
                }
                else {
                    /* log once per second indicating that we're waiting */
                    if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
                        last_log = jiffies;
                        printk("iSCSI: lun %d reset waiting for task %p refcount %u to reach 0\n", 
                               sc->lun, task, atomic_read(&task->refcount));
                    }
                    set_current_state(TASK_INTERRUPTIBLE);
                    schedule_timeout(interval);
                }
            }
            
            /* if we've made it here, it's safe to complete everything and return */
            REACQUIRE_IO_REQUEST_LOCK;
            
            /* complete all the tasks for this LUN */
            while ((task = pop_task(&tasks_for_lun))) {
                if ((task->itt != 0) && task->scsi_cmnd && task->scsi_cmnd->scsi_done) {
                    Scsi_Cmnd *sc = task->scsi_cmnd;

                    num_tasks++;
                    DEBUG_EH3("iSCSI: LUN reset completing sent task %p, command %p at %lu\n", 
                              task, sc, jiffies);
                    sc->result = HOST_BYTE(DID_RESET);
                    sc->scsi_done(sc);
                    task->scsi_cmnd = NULL;
                }
                free_task(task);
            }
            
            /* complete all unsent cmnds for this LUN */
            while (unsent_cmnds) {
                Scsi_Cmnd *cmnd = unsent_cmnds;
                unsent_cmnds = (Scsi_Cmnd *)cmnd->host_scribble;
                cmnd->host_scribble = NULL;
                if (cmnd->scsi_done) {
                    num_cmnds++;
                    DEBUG_EH2("iSCSI: LUN reset completing unsent cmnd %p at %lu\n", cmnd, jiffies);
                    cmnd->result = HOST_BYTE(DID_RESET);
                    cmnd->scsi_done(cmnd);
                }
            }
            
            printk("iSCSI: LUN reset (%u %u %u %u) completed %u tasks and %u cmnds\n",
                   sc->host->host_no, sc->channel, sc->target, sc->lun, num_tasks, num_cmnds);
            
            printk("iSCSI: device reset succeeded at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
                   jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);

            drop_reference(session);
            return SUCCESS;
        }
        else {
            /* if we didn't receive a response by now, quit waiting for it */
            spin_lock(&session->task_lock);
            if ((reset = remove_task(&session->rx_lun_reset_tasks, itt))) {
                remove_session_task(session, reset);
                printk("iSCSI: failed to recv lun reset response for itt %u, task %p\n",
                       reset->itt, reset);
            }
            else if ((reset = remove_task_for_cmnd(&session->tx_lun_reset_tasks, sc))) {
                remove_session_task(session, reset);
                printk("iSCSI: failed to send lun reset for itt %u, task %p\n",
                       reset->itt, reset);
            }
            spin_unlock(&session->task_lock);

            /* if we allocated the task earlier, free it now that we're giving up */
            if (reset && (reset->itt == 0)) {
                free_task(reset);
                reset = NULL;
            }
        }

        if (time_before(timeout, jiffies))
            printk("iSCSI: LUN reset (%u %u %u %u) timed out\n",
                   sc->host->host_no, sc->channel, sc->target, sc->lun);
        else
            printk("iSCSI: LUN reset (%u %u %u %u) failed\n",
                   sc->host->host_no, sc->channel, sc->target, sc->lun);
    }
    else {
        /* if we somehow failed to find or allocate a task, just do a target reset. */
        printk("iSCSI: eh_device_reset at %lu about to reset target for command %p for (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        if (warm_reset_target(session)) {
            printk("iSCSI: device reset succeeded at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
                   jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
            drop_reference(session);
            REACQUIRE_IO_REQUEST_LOCK;
            return SUCCESS;
        }
    }

    printk("iSCSI: device reset failed at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
    drop_reference(session);
    REACQUIRE_IO_REQUEST_LOCK;
    return FAILED;
}

/* NOTE: due to bugs in the linux SCSI layer (scsi_unjam_host), it's
 * possible for this handler to be called even if the device_reset
 * handler completed all the failed commands back to the SCSI layer
 * with DID_RESET and returned SUCCESS.  To compensate for this, we
 * must ensure that this reset handler doesn't actually care whether
 * the command is still in the driver.  Just find the session
 * associated with the command, and reset it.  
 */
int iscsi_eh_bus_reset( Scsi_Cmnd *sc ) 
{ 
    struct Scsi_Host *host = NULL; 
    iscsi_hba_t *hba = NULL; 
    iscsi_session_t *session;

    if ( ! sc ) {
        return FAILED;
    }
    host = sc->host;
    if (! host) {
        printk("iSCSI: bus reset, no host for SCSI command %p\n", sc);
        return FAILED;
    }
    hba = (iscsi_hba_t *)host->hostdata;
    if (!hba) {
        printk("iSCSI: bus reset, no iSCSI HBA associated with SCSI command %p\n", sc);
        return FAILED;
    }

    /* find the appropriate session for the command */
    session = find_session_for_cmnd(sc);
    if (!session) {
        printk("iSCSI: can't reset device for cmnd %p, no session\n", sc);
        return FAILED;
    }

    printk("iSCSI: eh_bus_reset about to reset target at %lu for command %p to (%u %u %u %u), Cmd 0x%x, "
           "session %p, rx %u, tx %u host_failed %u\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], 
           session, session->rx_pid, session->tx_pid, sc->host->host_failed);
    
#if TEST_HOST_RESETS
    /* just always fail it. occasionally failing it is too much work to code */
    printk("iSCSI: ignoring attempt to bus reset, command %p to (%u %u %u %u), Cmd 0x%x, host_failed %u\n",
           sc, reset, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], sc->host->host_failed);
    drop_reference(session);
    return FAILED;
#endif

    if (in_interrupt()) {
        printk("iSCSI: eh_bus_reset failing, in interrupt at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        return FAILED;
    }

    RELEASE_IO_REQUEST_LOCK;

    if (!wait_for_session(session, TRUE)) {
        printk("iSCSI: eh_bus_reset failed waiting for session %p at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
               session, jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        REACQUIRE_IO_REQUEST_LOCK;
        return FAILED;
    }

    /* we're in error recovery with a session established, mission
     * accomplished.  ensure this bit is cleared so that the TUR
     * command queued if we return success will actually get sent.
     */
    clear_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);

    if (warm_reset_target(session)) {
        printk("iSCSI: bus reset succeeded at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        REACQUIRE_IO_REQUEST_LOCK;
        return SUCCESS;
    }

    printk("iSCSI: bus reset failed at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
    drop_reference(session);
    REACQUIRE_IO_REQUEST_LOCK;
    return FAILED;
}



int
iscsi_eh_host_reset( Scsi_Cmnd *sc )
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_session_t *session;

    if ( ! sc ) {
        return FAILED;
    }
    host = sc->host;
    if (! host) {
        printk("iSCSI: host reset, no host for SCSI command %p\n", sc);
        return FAILED;
    }
    hba = (iscsi_hba_t *)host->hostdata;
    if (!hba) {
        printk("iSCSI: host reset, no iSCSI HBA associated with SCSI command %p\n", sc);
        return FAILED;
    }

    /* find the appropriate session for the command */
    session = find_session_for_cmnd(sc);
    if (!session) {
        printk("iSCSI: can't reset device for cmnd %p, no session\n", sc);
        return FAILED;
    }

    printk("iSCSI: eh_host_reset about to reset target at %lu for command %p for (%u %u %u %u), Cmd 0x%x, "
           "session %p, rx %u, tx %u, host_failed %u\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], 
           session, session->rx_pid, session->tx_pid, sc->host->host_failed);

    if (in_interrupt()) {
        printk("iSCSI: eh_host_reset failing, in interrupt at %lu for command %p to (%u %u %u %u), Cmd 0x%x, host_failed %u\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], sc->host->host_failed);
        drop_reference(session);
        return FAILED;
    }

    RELEASE_IO_REQUEST_LOCK;

    if (!wait_for_session(session, TRUE)) {
        printk("iSCSI: eh_host_reset failed waiting for session %p at %lu for command %p to (%u %u %u %u), Cmd 0x%x\n",
               session, jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        REACQUIRE_IO_REQUEST_LOCK;
        return FAILED;
    }

    /* we're in error recovery with a session established, mission
     * accomplished.  ensure this bit is cleared so that the TUR
     * command queued if we return success will actually get sent.
     */
    clear_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);

    /* ok, warm reset must not have worked, try a cold reset */
    if (cold_reset_target(session)) {
        printk("iSCSI: host reset succeeded at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
               jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        drop_reference(session);
        REACQUIRE_IO_REQUEST_LOCK;
        return SUCCESS;
    }

    printk("iSCSI: host reset failed at %lu for command %p, (%u %u %u %u), Cmd 0x%x\n",
           jiffies, sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
    drop_reference(session);
    REACQUIRE_IO_REQUEST_LOCK;
    return FAILED;
}

int
iscsi_queue( Scsi_Cmnd *sc, void (*done)(Scsi_Cmnd *) )
{
    iscsi_hba_t *hba;
    iscsi_session_t *session = NULL;
    struct Scsi_Host *host;
    DECLARE_NOQUEUE_FLAGS;
        
    host = sc->host;
    if (host == NULL) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: queuecommand but no Scsi_Host\n");
        sc->result = HOST_BYTE(DID_NO_CONNECT);
        done(sc);
        return 0;
    }

    hba = (iscsi_hba_t *)sc->host->hostdata;
    if ( (!hba) || (!hba->active) ) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: queuecommand but no HBA\n");
        sc->result = HOST_BYTE(DID_NO_CONNECT);
        done(sc);
        return 0;
    }

    if ( ! iscsi_timer_running ) {
        /* iSCSI coming up or going down, fail the command */
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        DEBUG_QUEUE6("iSCSI: no timer, failing to queue %p to (%u %u %u %u), Cmd 0x%x\n",
                     sc, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
    
        sc->result = HOST_BYTE(DID_NO_CONNECT);
        done(sc);
        return 0;
    }
    
    if (sc->target >= ISCSI_MAX_TARGETS) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: invalid target id %u, failing to queue %p to (%u %u %u %u), Cmd 0x%x\n",
               sc->target, sc, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        sc->result = HOST_BYTE(DID_NO_CONNECT);
        done(sc);
        return 0;
    }
    if (sc->lun >= ISCSI_MAX_LUN) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: invalid LUN %u, failing to queue %p to (%u %u %u %u), Cmd 0x%x\n",
               sc->lun, sc, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        sc->result = HOST_BYTE(DID_NO_CONNECT );
        done(sc);
        return 0;
    }
    /* CDBs larger than 16 bytes require additional header segments, not yet implemented */
    if (sc->cmd_len > ISCSI_MAX_CMD_LEN) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: cmd_len %u too large, failing to queue %p to (%u %u %u %u), Cmd 0x%x\n",
               sc->cmd_len, sc, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        sc->result = HOST_BYTE(DID_NO_CONNECT );
        done(sc);
        return 0;
    }
    /* make sure our SG_TABLESIZE limit was respected */
    if (sc->use_sg > ISCSI_MAX_SG) {
        ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
        printk("iSCSI: use_sg %u too large, failing to queue %p to (%u %u %u %u), Cmd 0x%x\n",
               sc->use_sg, sc, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0]);
        sc->result = HOST_BYTE(DID_NO_CONNECT );
        done(sc);
        return 0;
    }

#if DEBUG_EH
    if (LOG_ENABLED(ISCSI_LOG_EH) && (sc->cmnd[0] == TEST_UNIT_READY)) {
        printk("iSCSI: queueing TUR %p at %lu to (%u %u %u %u), Cmd 0x%x, cpu%d\n",
               sc, jiffies, hba->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0], smp_processor_id());
    }
    else
#endif
    {
#if DEBUG_QUEUE
        if (LOG_ENABLED(ISCSI_LOG_QUEUE))
            printk("iSCSI: queueing %p to (%u %u %u %u) at %lu, Cmd 0x%x, cpu%d\n",
                   sc, hba->host->host_no, sc->channel, sc->target, sc->lun, jiffies, sc->cmnd[0], smp_processor_id());
#endif
    }
    
    if (hba) {
        /* dispatch directly to the session's queue here, or fail.
         * this works since we keep iscsi_session_t structures around
         * even after a session drops, and re-use them if we get a new
         * session from the daemon for the same target.
         */
        SPIN_LOCK_NOQUEUE(&hba->session_lock);

        /* find the right session */
        session = hba->session_list_head;
        while (session) {
            if ((session->channel == sc->channel) && (session->target_id == sc->target)) {

                /* refuse any new commands while a session is terminating */
                if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
                    printk("iSCSI: session %p terminating, failing to queue %p to (%u %u %u %u), Cmd 0x%x to %s\n",
                           session, sc, 
                           session->host_no, sc->channel, sc->target, sc->lun, 
                           sc->cmnd[0], session->log_name);
                    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
                    ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
                    sc->result = HOST_BYTE(DID_NO_CONNECT);
                    done(sc);
                    return 0;
                }


                spin_lock(&session->scsi_cmnd_lock);
#ifdef DEBUG
                /* make sure the command hasn't already been queued */
                {
                    Scsi_Cmnd *search = session->scsi_cmnd_head;
                    while (search) {
                        if (search == sc) {
                            printk("iSCSI: bug - cmnd %p, state %x, eh_state %x, scribble %p is already queued to session %p\n",
                                   sc, sc->state, sc->eh_state, sc->host_scribble, session);
                            print_session_cmnds(session);
                            spin_unlock(&session->scsi_cmnd_lock);
                            SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
                            ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
                            return 1;
                        }
                        search = (Scsi_Cmnd *)search->host_scribble;
                    }
                }
#endif

                /* make sure we can complete it properly later */
                sc->scsi_done = done;
                sc->result = 0;

                sc->host_scribble = NULL;
                if (test_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits)) {
                    /* make sure this command aborts, by hiding it from the tx thread */
                    if (session->ignored_cmnd_head) {
                        /* append at the tail */
                        session->ignored_cmnd_tail->host_scribble = (unsigned char *)sc;
                        session->ignored_cmnd_tail = sc;
                    }
                    else {
                        /* make it the head */
                        session->ignored_cmnd_head = session->ignored_cmnd_tail = sc;
                    }
                    atomic_inc(&session->num_ignored_cmnds);
                }
                else if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
                    /* it's possible though not certain that this command would timeout
                     * before the session could be established.  To avoid races with
                     * the error recovery thread (where we send and complete commands
                     * which we'll later get an abort for), force this command and any
                     * that follow to get aborted.
                     */
                    set_bit(SESSION_FORCE_ERROR_RECOVERY, &session->control_bits);
                    /* make sure this command aborts, by hiding it from the tx thread */
                    if (session->ignored_cmnd_head) {
                        /* append at the tail */
                        session->ignored_cmnd_tail->host_scribble = (unsigned char *)sc;
                        session->ignored_cmnd_tail = sc;
                    }
                    else {
                        /* make it the head */
                        session->ignored_cmnd_head = session->ignored_cmnd_tail = sc;
                    }
                    atomic_inc(&session->num_ignored_cmnds);
                    printk("iSCSI: queuing cmnd %p to ignored queue and forcing error recovery, session %p is not established at %lu\n",
                           sc, session, jiffies);
                }
                else {
                    /* add it to the session's command queue so the tx thread will send it */
                    if (session->scsi_cmnd_head) {
                        /* append at the tail */
                        session->scsi_cmnd_tail->host_scribble = (unsigned char *)sc;
                        session->scsi_cmnd_tail = sc;
                    }
                    else {
                        /* make it the head */
                        session->scsi_cmnd_head = session->scsi_cmnd_tail = sc;
                    }
                    atomic_inc(&session->num_cmnds);
                }
#if DEBUG_EH
                if (LOG_ENABLED(ISCSI_LOG_EH) && (sc->cmnd[0] == TEST_UNIT_READY)) {
                    printk("iSCSI: queued TUR %p at %lu to session %p\n", sc, jiffies, session);
                }
                else
#endif
                {
#if DEBUG_SMP || DEBUG_QUEUE
                    if (LOG_ENABLED(ISCSI_LOG_SMP) || LOG_ENABLED(ISCSI_LOG_QUEUE))
                        printk("iSCSI: queued %p to session %p at %lu, %u cmnds, head %p, tail %p\n", 
                               sc, session, jiffies, atomic_read(&session->num_cmnds), 
                               session->scsi_cmnd_head, session->scsi_cmnd_tail);
#endif
                }
                spin_unlock(&session->scsi_cmnd_lock);
                    
                /* unlock, wake the tx thread, and return */
                SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
                ISCSI_TRACE(ISCSI_TRACE_Qd, sc, NULL, sc->retries, sc->timeout_per_command);
                wake_tx_thread(TX_SCSI_COMMAND, session);
                return 0;
            }
            
            session = session->next;
        }
        SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
    }

    /* it's normal for there to be no session while the module is initializing. */
    if (test_bit(0, &init_module_complete)) {
        printk("iSCSI: queuecommand %p failed to find a session for HBA %p, host %d channel %d id %d lun %d\n",
               sc, hba, hba->host->host_no, sc->channel, sc->target, sc->lun);
    }

    /* Complete with an error.  A comment says we shouldn't, but
     * it's actually ok to call done() as long as we're using
     * the eh_ style error handlers.
     */
    ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
    DEBUG_QUEUE1("iSCSI: queuecommand completing %p with DID_NO_CONNECT\n", sc);
    sc->result = HOST_BYTE(DID_NO_CONNECT);

    done(sc);

    return 0;
}

static int iscsi_shutdown(void)
{
    iscsi_hba_t *hba;
    iscsi_session_t *session;
    pid_t pid;
    DECLARE_NOQUEUE_FLAGS;
    int num_sessions = 0;
    
    /* terminate every session */
    printk("iSCSI: kill sessions\n");
    do {
        num_sessions = 0;

        spin_lock(&iscsi_hba_list_lock);
        for (hba = iscsi_hba_list; hba; hba = hba->next) {
            SPIN_LOCK_NOQUEUE(&hba->session_lock);
            for (session = hba->session_list_head; session; session = session->next) {
                if (!test_bit(SESSION_TERMINATED, &session->control_bits)) {
                    num_sessions++;
                    set_bit(SESSION_TERMINATING, &session->control_bits);
                    if ((session->last_kill == 0) || time_before_eq(session->last_kill + HZ, jiffies)) {
                        session->last_kill = jiffies;
                        iscsi_terminate_session(session);
                    }
                }
            }
            SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
        }
        spin_unlock(&iscsi_hba_list_lock);

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout((HZ / 10) ? (HZ / 10) : 1);

        if (signal_pending(current))
            return 0;
        
    } while (num_sessions);
    
    /* kill the timer */
    if ((pid = iscsi_timer_pid)) {
        printk("iSCSI: killing timer %d\n", pid);
        kill_proc(pid, SIGKILL, 1);
    }
    printk("iSCSI: shutdown waiting for timer to terminate\n");
    while (test_bit(0, &iscsi_timer_running)) {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout((HZ / 10) ? (HZ / 10) : 1);
        if (signal_pending(current))
            return 0;
    }

    printk("iSCSI: driver shutdown complete\n");

    return 1;
}

int
iscsi_biosparam( Disk *disk, kdev_t dev, int geom[] )
{
    DEBUG_INIT1("iSCSI: BiosParam, capacity %d\n",disk->capacity);
    geom[0] = 64;                                   /* heads */
    geom[1] = 32;                                   /* sectors */
    geom[2] = disk->capacity / (64*32);     /* cylinders */
    return 1;
}

const char *
iscsi_info( struct Scsi_Host *sh )
{
    iscsi_hba_t *hba;
    static char buffer[256];
    char *bp;

    DEBUG_INIT0("iSCSI: Info\n");
    hba = (iscsi_hba_t *)sh->hostdata;
    if ( ! hba ) {
        return NULL;
    }

    bp = &buffer[0];
    memset( bp, 0, sizeof(buffer) );
#if defined(DRIVER_INTERNAL_VERSION) && (DRIVER_INTERNAL_VERSION > 0)        
    sprintf(bp, "iSCSI (%d.%d.%d.%d)", 
                DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION);
#else
    sprintf(bp, "iSCSI (%d.%d.%d)", 
                DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION);
#endif

    return bp;
}

static char *show_session_luns(iscsi_session_t *session, char *bp, int hostno)
{
    /* if we've already found LUNs, show them all */
    int lfound = 0;
    int l;

    /* FIXME: how much can we write to the buffer? */

    for (l=0; l<ISCSI_MAX_LUN; l++) {
        if (test_bit(l, session->lun_bitmap)) {
            if (session->address_length == 4) {
                bp += sprintf(bp, "  %3.1u   0 %3.1u %3.1u    %u.%u.%u.%u %5d  %s\n", 
                              hostno, session->target_id, l, 
                              session->ip_address[0], session->ip_address[1], session->ip_address[2], session->ip_address[3],
                              session->port, session->TargetName);
            }
            else if (session->address_length == 16) {
                bp += sprintf(bp, "  %3.1u   0 %3.1u %3.1u    %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x  %5d  %s\n", 
                              hostno, session->target_id, l, 
                              session->ip_address[0], session->ip_address[1], session->ip_address[2], session->ip_address[3],
                              session->ip_address[4], session->ip_address[5], session->ip_address[6], session->ip_address[7],
                              session->ip_address[8], session->ip_address[9], session->ip_address[10], session->ip_address[11],
                              session->ip_address[12], session->ip_address[13], session->ip_address[14], session->ip_address[15],
                              session->port, session->TargetName);
            }
            lfound += 1;
        }
    }
    /* if we haven't found any LUNs, use ??? for a LUN number */
    if ( ! lfound ) {
        if (session->address_length == 4) {
            bp += sprintf(bp, "  %3.1u   0 %3.1u   ?    %u.%u.%u.%u %5d  %s\n", 
                          hostno, session->target_id, 
                          session->ip_address[0], session->ip_address[1], session->ip_address[2], session->ip_address[3],
                          session->port, session->TargetName);
        }
        else if (session->address_length == 16) {
            bp += sprintf(bp, "  %3.1u   0 %3.1u   ?    %02x:%02x:%02x:%02x%02x:%02x:%02x:%02x%02x:%02x:%02x:%02x%02x:%02x:%02x:%02x  %5d  %s\n", 
                          hostno, session->target_id, 
                          session->ip_address[0], session->ip_address[1], session->ip_address[2], session->ip_address[3],
                          session->ip_address[4], session->ip_address[5], session->ip_address[6], session->ip_address[7],
                          session->ip_address[8], session->ip_address[9], session->ip_address[10], session->ip_address[11],
                          session->ip_address[12], session->ip_address[13], session->ip_address[14], session->ip_address[15],
                          session->port, session->TargetName);
        }
    }

    return bp;
}

#define is_digit(c)	((c) >= '0' && (c) <= '9')
#define digit_to_bin(c)	((c) - '0')
#define is_space(c)	((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')

/* returns number of bytes matched */
static int find_keyword(char *start, char *end, char *key)
{
    char *ptr = start;
    int key_len = strlen(key);

    /* skip whitespace */
    while ((ptr < end) && is_space(*ptr))
        ptr++;

    /* compare */
    if (((end - ptr) == key_len) && !memcmp(key, ptr, key_len)) {
        return (ptr - start) + key_len;
    }
    else if (((end - ptr) > key_len) && !memcmp(key, ptr, key_len) && is_space(ptr[key_len])) {
        return (ptr - start) + key_len;
    }
    else {
        return 0;
    }
}

/*
 * *buffer: I/O buffer
 * **start: for user reads, driver can report where valid data starts in the buffer
 * offset: current offset into a /proc/scsi/iscsi/[0-9]* file
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * write: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written to a
 * /proc/scsi/iscsi/[0-9]* file.
 */

int iscsi_proc_info( char *buffer,
                     char **start,
                     off_t offset,
                     int length,
                     int hostno,
                     int write)
{
    char *bp = buffer;
    iscsi_hba_t *hba;
    iscsi_session_t *session;
    DECLARE_NOQUEUE_FLAGS;

    if (!buffer)
        return -EINVAL;

    if (write) {
        int cmd_len;
        char *end = buffer + length;

        if ((cmd_len = find_keyword(bp, end, "log"))) {
            unsigned int log_setting = 0;
            
            bp += cmd_len;
            
            if ((cmd_len = find_keyword(bp, end, "all")) != 0) {
                iscsi_log_settings = 0xFFFFFFFF;
                printk("iSCSI: all logging enabled\n");
            }
            else if ((cmd_len = find_keyword(bp, end, "none")) != 0) {
                iscsi_log_settings = 0;
                printk("iSCSI: all logging disabled\n");
            }
            else if ((cmd_len = find_keyword(bp, end, "sense")) != 0) {
                log_setting = ISCSI_LOG_SENSE;
                bp += cmd_len;
                
                if ((cmd_len = find_keyword(bp, end, "always")) != 0) {
                    iscsi_log_settings |= LOG_SET(log_setting);
                    printk("iSCSI: log sense always\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "on")) != 0) {
                    iscsi_log_settings |= LOG_SET(log_setting);
                    printk("iSCSI: log sense yes\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "yes")) != 0) {
                    iscsi_log_settings |= LOG_SET(log_setting);
                    printk("iSCSI: log sense yes\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "1")) != 0) {
                    iscsi_log_settings |= LOG_SET(log_setting);
                    printk("iSCSI: log sense 1\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "minimal")) != 0) {
                    iscsi_log_settings &= ~LOG_SET(log_setting);
                    printk("iSCSI: log sense off\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "off")) != 0) {
                    iscsi_log_settings &= ~LOG_SET(log_setting);
                    printk("iSCSI: log sense off\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "no")) != 0) {
                    iscsi_log_settings &= ~LOG_SET(log_setting);
                    printk("iSCSI: log sense no\n");
                }
                else if ((cmd_len = find_keyword(bp, end, "0")) != 0) {
                    iscsi_log_settings &= ~LOG_SET(log_setting);
                    printk("iSCSI: log sense 0\n");
                }
            }
            else {
                if ((cmd_len = find_keyword(bp, end, "login")) != 0) {
                    log_setting = ISCSI_LOG_LOGIN;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "init")) != 0) {
                    log_setting = ISCSI_LOG_INIT;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "queue")) != 0) {
                    log_setting = ISCSI_LOG_QUEUE;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "alloc")) != 0) {
                    log_setting = ISCSI_LOG_ALLOC;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "flow")) != 0) {
                    log_setting = ISCSI_LOG_FLOW;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "error")) != 0) {
                    log_setting = ISCSI_LOG_ERR;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "eh")) != 0) {
                    log_setting = ISCSI_LOG_EH;
                    bp += cmd_len;
                }
                else if ((cmd_len = find_keyword(bp, end, "smp")) != 0) {
                    log_setting = ISCSI_LOG_SMP;
                    bp += cmd_len;
                }
                
                if (log_setting) {
                    if ((cmd_len = find_keyword(bp, end, "on")) != 0) {
                        iscsi_log_settings |= LOG_SET(log_setting);
                    }
                    else if ((cmd_len = find_keyword(bp, end, "yes")) != 0) {
                        iscsi_log_settings |= LOG_SET(log_setting);
                    }
                    else if ((cmd_len = find_keyword(bp, end, "1")) != 0) {
                        iscsi_log_settings |= LOG_SET(log_setting);
                    }
                    else if ((cmd_len = find_keyword(bp, end, "off")) != 0) {
                        iscsi_log_settings &= ~LOG_SET(log_setting);
                    }
                    else if ((cmd_len = find_keyword(bp, end, "no")) != 0) {
                        iscsi_log_settings &= ~LOG_SET(log_setting);
                    }
                    else if ((cmd_len = find_keyword(bp, end, "0")) != 0) {
                        iscsi_log_settings &= ~LOG_SET(log_setting);
                    }
                }
            }

            printk("iSCSI: log settings %8x\n", iscsi_log_settings);
            mb();
        }
        return length;
    }
    else {
        /* it's a read */
        bp += sprintf(bp,"# SCSI:              iSCSI:\n");
        bp += sprintf(bp,"# Hst Chn Tgt Lun    IP address     Port  Target\n");
        
        /* find the HBA corresponding to hostno */
        spin_lock(&iscsi_hba_list_lock);
        hba = iscsi_hba_list;
        while (hba && hba->host->host_no != hostno)
            hba = hba->next;
        spin_unlock(&iscsi_hba_list_lock);
        
        if (!hba) {
            printk("iSCSI: couldn't find iSCSI HBA #%d\n", hostno);
            return 0;
        }
        
        /* show every session on the HBA */
        SPIN_LOCK_NOQUEUE(&hba->session_lock);
        session = hba->session_list_head;
        while (session) {
            bp = show_session_luns(session, bp, hostno);
            session = session->next;
        }
        SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

        /* tell the caller about the output */
        /* FIXME: handle buffer overflow */
        *start = buffer + offset;
        
        if ( (bp-buffer) < offset ) {
            return 0;
        }
        
        if ( (bp-buffer-offset) < length ) {
            return (bp-buffer-offset);
        }
        
        return length;
    }
}

/*
 * We cannot include scsi_module.c because the daemon has not got a connection
 * up yet.
 */
static int
ctl_open( struct inode *inode, struct file *file )
{
    MOD_INC_USE_COUNT;
    return 0;
}

static int
ctl_close( struct inode *inode, struct file *file )
{
    MOD_DEC_USE_COUNT;
    return 0;
}

static int
ctl_ioctl( struct inode *inode,
           struct file *file,
           unsigned int cmd,
           unsigned long arg)
{
    int rc;

    if ( cmd == ISCSI_ESTABLISH_SESSION ) {
        iscsi_session_t *session;
        iscsi_session_ioctl_t *ioctld = kmalloc(sizeof(*ioctld), GFP_KERNEL);
        
        if (!ioctld) {
            printk("iSCSI: couldn't allocate space for session ioctl data\n");
            return -ENOMEM;
        }
        if ( copy_from_user(ioctld, (void *)arg, sizeof(*ioctld)) ) {
            printk("iSCSI: Cannot copy session ioctl data\n");
            return -EFAULT;
        }
        if (ioctld->ioctl_size != sizeof(iscsi_session_ioctl_t)) {
            printk("iSCSI: ioctl size %u incorrect, expecting %u\n", ioctld->ioctl_size, sizeof(*ioctld));
            return -EFAULT;
        }
        if (ioctld->ioctl_version != LINUX_SESSION_IOCTL_VERSION) {
            printk("iSCSI: ioctl version %u incorrect, expecting %u\n", ioctld->ioctl_version, LINUX_SESSION_IOCTL_VERSION);
            return -EFAULT;
        }

        /* allocate a session structure */
        /* FIXME: at this point, the session structure and it's
         * receive buffer is so big that we might as well directly
         * allocate memory pages for it.  The slab allocator will work for
         * sizes up to around 128K, but it will force a power-of-two
         * size to be allocated, which may be larger than we really
         * need, and in any case, we don't get much of a benefit from
         * using a slab allocator for this.
         */
        session = (iscsi_session_t *)kmalloc(sizeof(*session), GFP_KERNEL);
        if ( ! session ) {
            DEBUG_ERR0("iSCSI: Cannot allocate new session entry\n");
            return -ENOMEM;
        }
        memset(session, 0, sizeof(*session));
        atomic_set(&session->refcount, 1);
        DEBUG_INIT2("iSCSI: allocated session %p at %lu\n", session, jiffies);

        /* unless we already have one, start a timer thread */
        if (!test_and_set_bit(0, &iscsi_timer_running)) {
            printk("iSCSI: starting timer thread at %lu\n", jiffies);
            kernel_thread(iscsi_timer_thread, NULL, 0);
        }
        
        rc = iscsi_session(session, ioctld);
        if (rc < 0) {
            /* on an error, we're the only ones referencing the session */
            memset(session, 0, sizeof(*session));
            kfree(session);
        }
        kfree(ioctld);

        /* FIXME: we may need to free the session and it's authClient here,
         * if we failed to establish a session.
         */

        return rc;
    }
    else if ( cmd == ISCSI_SHUTDOWN ) {
        return iscsi_shutdown();
    }
    else if ( cmd == ISCSI_GETTRACE ) {
#if DEBUG_TRACE
        iscsi_trace_dump_t dump;
        iscsi_trace_dump_t *user_dump;
        DECLARE_NOQUEUE_FLAGS;
        
        user_dump = (iscsi_trace_dump_t *)arg;
        if (copy_from_user(&dump, user_dump, sizeof(dump))) {
            printk("iSCSI: trace copy_from_user %p, %p, %u failed\n", 
                   &dump, user_dump, sizeof(dump));
            return -EFAULT;
        }

        if (dump.dump_ioctl_size != sizeof(iscsi_trace_dump_t)) {
            printk("iSCSI: trace dump ioctl size is %u, but caller uses %u\n",
                   sizeof(iscsi_trace_dump_t), dump.dump_ioctl_size);
            return -EINVAL;
        }

        if (dump.dump_version != TRACE_DUMP_VERSION) {
            printk("iSCSI: trace dump version is %u, but caller uses %u\n",
                   TRACE_DUMP_VERSION, dump.dump_version);
            return -EINVAL;
        }

        if (dump.trace_entry_size != sizeof(iscsi_trace_entry_t)) {
            printk("iSCSI: trace dump ioctl size is %u, but caller uses %u\n",
                   sizeof(iscsi_trace_dump_t), dump.dump_ioctl_size);
            return -EINVAL;
        }

        if (dump.num_entries < ISCSI_TRACE_COUNT) {
            /* tell the caller to use a bigger buffer */
            dump.num_entries = ISCSI_TRACE_COUNT;
            if (copy_to_user(user_dump, &dump, sizeof(dump)))
                return -EFAULT;
            else
                return -E2BIG;
        }

        /* the caller is responsible for zeroing the buffer before the ioctl, so
         * if the caller asks for too many entries, it should be able to tell which
         * ones actually have data.
         */

        /* only send what we've got */
        dump.num_entries = ISCSI_TRACE_COUNT;
        if (copy_to_user(user_dump, &dump, sizeof(dump))) {
            printk("iSCSI: trace copy_to_user %p, %p, %u failed\n", 
                   user_dump, &dump, sizeof(dump));
            return -EFAULT;
        }
        
        SPIN_LOCK_NOQUEUE(&iscsi_trace_lock);
        /* FIXME: copy_to_user may sleep, but we're holding a spin_lock */
        if (copy_to_user(user_dump->trace, &trace_table[0], dump.num_entries * sizeof(iscsi_trace_entry_t))) {
            printk("iSCSI: trace copy_to_user %p, %p, %u failed\n", 
                   user_dump->trace, &trace_table[0], dump.num_entries);
            SPIN_UNLOCK_NOQUEUE(&iscsi_trace_lock);
            return -EFAULT;
        }
        printk("iSCSI: copied %d trace entries to %p\n", dump.num_entries, user_dump->trace);
        SPIN_UNLOCK_NOQUEUE(&iscsi_trace_lock);
        return trace_index;
#else
        printk("iSCSI: iSCSI kernel module does not implement tracing\n");
        return -ENXIO;
#endif
    }

    return -EINVAL;
}

Scsi_Host_Template iscsi_driver_template = ISCSI_HOST_TEMPLATE;

int
init_module( void )
{
    DEBUG_INIT0("iSCSI init module\n");

#if defined(DRIVER_INTERNAL_VERSION) && (DRIVER_INTERNAL_VERSION > 0)
    /* show internal version number */
    printk("iSCSI version %d.%d.%d.%d (%s)\n",
           DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION,
           ISCSI_DATE);
#else
    /* released version number (INTERNAL == 0) */
    printk("iSCSI version %d.%d.%d (%s)\n",
           DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION,
           ISCSI_DATE);
#endif

#if defined(BUILD_STR)
    /* show a build string if we have one */
    if (NULL != BUILD_STR) {
        printk("iSCSI %s\n", (char *)BUILD_STR);
    }
#endif

    control_major = register_chrdev( 0, control_name, &control_fops );
    if ( control_major < 0 ) {
        printk("iSCSI failed to register the control device\n");
        return control_major;
    }
    printk("iSCSI control device major number %d\n", control_major);

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
    iscsi_driver_template.module = THIS_MODULE;
#else
    iscsi_driver_template.module = &__this_module;
#endif
    scsi_register_module( MODULE_SCSI_HA, &iscsi_driver_template );
    if ( ! iscsi_driver_template.present ) {
        DEBUG_ERR0("iSCSI scsi_register not present\n");
        scsi_unregister_module( MODULE_SCSI_HA, &iscsi_driver_template );
    }

    DEBUG_INIT0("iSCSI init module complete\n");
    set_bit(0, &init_module_complete);
    return 0;
}

void
cleanup_module( void )
{
    int rc;

    DEBUG_INIT0("iSCSI cleanup module\n");
    if ( control_major > 0 ) {
        rc = unregister_chrdev( control_major, control_name );
        if ( rc ) {
            DEBUG_ERR0("iSCSI Error trying to unregister control device\n");
        } 
        else {
            control_major = 0;
        }
    }

    if ( iscsi_driver_template.present ) {
        DEBUG_INIT0("iSCSI SCSI present\n");
        scsi_unregister_module( MODULE_SCSI_HA, &iscsi_driver_template );
        iscsi_driver_template.present = 0;
    }

    DEBUG_INIT0("iSCSI cleanup module complete\n");
    return;
}


/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
