#ifndef ISCSI_H_
#define ISCSI_H_

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
 * $Id: iscsi.h,v 1.27 2002/02/16 05:00:17 smferris Exp $ 
 *
 * iscsi.h
 *
 *    include for iSCSI kernel module
 * 
 */

#include "iscsiAuthClient.h"

#define ISCSI_MAX_CHANNELS_PER_HBA 1

#ifndef MIN
# define MIN(x, y)               ((x) < (y)) ? (x) : (y)
#endif

#ifndef MAX
# define MAX(x, y)               ((x) > (y)) ? (x) : (y)
#endif


#define ISCSI_CMDS_PER_LUN       12
#define ISCSI_MIN_CANQUEUE       64
#define ISCSI_MAX_CANQUEUE       64
#define ISCSI_PREALLOCATED_TASKS 64
#define ISCSI_MAX_SG             64
#define ISCSI_RXCTRL_SIZE        ((2 * sizeof(struct IscsiHdr)) + 4096 + 4) /* header plus alignment plus max login pdu size + pad */
#define ISCSI_MAX_CMD_LEN        12
#define ISCSI_MAX_TASKS_PER_SESSION (ISCSI_CMDS_PER_LUN * ISCSI_MAX_LUN)

#define ISCSI_TEXT_SEPARATOR     '='

/*
 * Scsi_Host Template
 */
#define ISCSI_HOST_TEMPLATE {                   \
    next : NULL,                                \
    module : NULL,                              \
    proc_dir : NULL,                            \
    proc_info : iscsi_proc_info,                \
    name : NULL,                                \
    detect : iscsi_detect,                      \
    release : iscsi_release,                    \
    info : iscsi_info,                          \
    ioctl : NULL,                               \
    command : NULL,                             \
    queuecommand : iscsi_queue,                 \
    eh_strategy_handler : NULL,                 \
    eh_abort_handler : iscsi_eh_abort,          \
    eh_device_reset_handler : iscsi_eh_device_reset, \
    eh_bus_reset_handler : iscsi_eh_bus_reset,  \
    eh_host_reset_handler : iscsi_eh_host_reset,\
    abort : NULL,                               \
    reset : NULL,                               \
    slave_attach : NULL,                        \
    bios_param : iscsi_biosparam,               \
    this_id: -1,                                \
    can_queue : ISCSI_MIN_CANQUEUE,             \
    sg_tablesize : ISCSI_MAX_SG,                \
    cmd_per_lun: ISCSI_CMDS_PER_LUN,            \
    present : 0,                                \
    unchecked_isa_dma : 0,                      \
    use_clustering : ENABLE_CLUSTERING,         \
    use_new_eh_code : 1,                        \
    emulated : 1                                \
}

/*    sg_tablesize : SG_NONE,                     */
/*    sg_tablesize : SG_ALL,                      */

/* task flags */
#define ISCSI_TASK_CONTROL       1
#define ISCSI_TASK_WRITE         2
#define ISCSI_TASK_READ          3
#define ISCSI_TASK_ABORTING      4
#define ISCSI_TASK_RESET_FAILED  31

typedef struct iscsi_task {
        struct iscsi_task  *volatile order_next;
        struct iscsi_task  *volatile order_prev;
        struct iscsi_task  *volatile next;
        struct iscsi_task  *volatile prev;
        Scsi_Cmnd          *volatile scsi_cmnd;
    	struct iscsi_session    *session;
        atomic_t                refcount;
        uint32_t                rxdata;
        unsigned int            flags;
        uint32_t                cmdsn;
        uint32_t                itt;
        uint32_t                ttt;
        uint32_t                mgmt_itt;
        unsigned int            data_offset;
        int                     data_length;
} iscsi_task_t;

typedef struct iscsi_task_collection {
    struct iscsi_task  *volatile head;
    struct iscsi_task  *volatile tail;
} iscsi_task_collection_t;

/* used for replying to NOPs */
typedef struct iscsi_nop_info {
    struct iscsi_nop_info *next;
    uint32_t ttt;
    unsigned int dlength;
    unsigned char lun[8];
    unsigned char data[1];
} iscsi_nop_info_t;

typedef struct iscsi_session {
    atomic_t                refcount;
    volatile unsigned long  generation;
    struct iscsi_session    *next;
    struct iscsi_session    *prev;
    struct iscsi_hba        *hba;
    struct socket           *socket;
    int                     iscsi_bus;
    int                     host_no;
    int                     channel;
    int                     target_id;
    uint32_t                lun_bitmap[8]; /* enough for 256 LUNS */
    uint32_t                num_luns;
    int                     address_length;
    unsigned char           ip_address[16];
    int                     port;
    int                     tcp_window_size;
    char                    username[iscsiAuthStringMaxLength];
    unsigned char           password[iscsiAuthDataMaxLength];
    int                     password_length;
    unsigned char           InitiatorName[TARGET_NAME_MAXLEN + 1];
    unsigned char           InitiatorAlias[TARGET_NAME_MAXLEN + 1];
    unsigned char           TargetName[TARGET_NAME_MAXLEN + 1];
    unsigned char           TargetAlias[TARGET_NAME_MAXLEN + 1];
    unsigned char           *log_name;
    IscsiAuthClient         *auth_client;
    /* the queue of SCSI commands that we need to send on this session */
    spinlock_t              scsi_cmnd_lock;
    Scsi_Cmnd               *scsi_cmnd_head;
    Scsi_Cmnd               *scsi_cmnd_tail;
    atomic_t                num_cmnds;
    Scsi_Cmnd               *ignored_cmnd_head;
    Scsi_Cmnd               *ignored_cmnd_tail;
    atomic_t                num_ignored_cmnds;
    uint16_t                isid;
    uint16_t                tsid;
    unsigned int            CmdSn;
    volatile uint32_t       ExpCmdSn;
    volatile uint32_t       MaxCmdSn;
    volatile uint32_t       last_peak_window_size;
    volatile uint32_t       current_peak_window_size;
    unsigned long           window_peak_check;
    int                     desired_InitialR2T;
    int                     desired_DataPDULength;
    int                     desired_FirstBurstSize;
    int                     desired_MaxBurstSize;
    int                     desired_ImmediateData;
    int                     InitialR2T;
    int                     DataPDULength;
    int                     FirstBurstSize;
    int                     MaxBurstSize;
    int                     DataPDUInOrder;
    int                     DataSequenceInOrder;
    int                     ImmediateData;
    int                     type;
    int                     current_phase;
    int                     next_phase;
    int                     partial_response;
    uint32_t                itt;
    volatile unsigned long  last_rx;
    volatile unsigned long  last_ping;
    unsigned long           last_window_check;
    unsigned long           last_kill;
    unsigned long           login_phase_timer;
    int                     login_timeout;
    int                     auth_timeout;
    int		            active_timeout;
    int		            idle_timeout;
    int                     ping_timeout;
    int		            abort_timeout;
    int		            reset_timeout;
    int		            replacement_timeout;
    /* the following fields may have to move if we decide to implement multiple connections,
     * per session, and decide to have threads for each connection rather than for each session.
     */
    /* the queue of SCSI commands that have been sent on this session, and for which we're waiting for a reply */
    spinlock_t              task_lock;
    iscsi_task_collection_t arrival_order;
    iscsi_task_collection_t rx_tasks;
    iscsi_task_collection_t tx_tasks;
    iscsi_task_collection_t completing_tasks;
    iscsi_task_collection_t rx_abort_tasks;
    iscsi_task_collection_t tx_abort_tasks;
    iscsi_task_collection_t aborted_tasks;
    iscsi_task_collection_t tx_lun_reset_tasks;
    iscsi_task_collection_t rx_lun_reset_tasks;
    iscsi_task_collection_t lun_reset_tasks;
    atomic_t                num_active_tasks;
    iscsi_nop_info_t        *nop_reply_head;
    iscsi_nop_info_t        *nop_reply_tail;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )  
    wait_queue_head_t       tx_wait_q;
    wait_queue_head_t       tx_blocked_wait_q;
    wait_queue_head_t       login_wait_q;
#else
    struct wait_queue       *tx_wait_q;
    struct wait_queue       *tx_blocked_wait_q;
    struct wait_queue       *login_wait_q;
#endif
    unsigned int            control_bits;
    volatile uint32_t       warm_reset_itt;
    volatile uint32_t       cold_reset_itt;
    volatile pid_t          rx_pid;
    volatile pid_t          tx_pid;
    volatile unsigned long  session_drop_time;
    /* the following fields are per-connection, not per session, and will need to move if 
     * we decide to support multiple connections per session.
     */
    unsigned long           logout_deadline;
    unsigned long           min_reconnect_time;
    unsigned int            ExpStatSn;
    struct iovec            RxIov[(ISCSI_MAX_SG+1)];
    struct iovec            TxIov[(ISCSI_MAX_SG+1)];
    unsigned char           RxBuf[ISCSI_RXCTRL_SIZE];
} iscsi_session_t;

/* not defined by iSCSI, but used in the driver to determine when to send the initial Login PDU */
#define ISCSI_INITIAL_LOGIN_PHASE -1

/* session control bits */
#define TX_WAKE 0
#define TX_PING 1  /* NopOut, reply requested */
#define TX_NOP  2  /* NopOut, no reply requested */
#define TX_NOP_REPLY  3

#define TX_SCSI_COMMAND 4
#define TX_R2T_DATA 5
#define TX_ABORT 6
#define TX_LUN_RESET 7

#define TX_COLD_TARGET_RESET 8
#define TX_WARM_TARGET_RESET 9

#define TX_THREAD_BLOCKED         12

#define SESSION_ESTABLISHED       20
#define SESSION_FORCE_ERROR_RECOVERY 21

#define SESSION_TASK_ALLOC_FAILED 24
#define SESSION_TIMED_OUT 25
#define SESSION_RESETTING 26
#define SESSION_RESET     27

#define SESSION_LOGOUT_REQUESTED 28
#define SESSION_WINDOW_CLOSED 29
#define SESSION_TERMINATING   30
#define SESSION_TERMINATED  31

typedef struct iscsi_hba {
        struct iscsi_hba        *next;
        struct Scsi_Host        *host;
        int                     active;
        spinlock_t              session_lock;
        iscsi_session_t         *session_list_head;
        iscsi_session_t         *session_list_tail; 
        atomic_t                num_sessions;
        spinlock_t              free_task_lock;
        iscsi_task_collection_t free_tasks;
        atomic_t                num_free_tasks;
        atomic_t                num_used_tasks;
        volatile unsigned long  last_kfree_check;
        volatile unsigned int   min_free_tasks;
} iscsi_hba_t;


/* run-time controllable logging */
#define ISCSI_LOG_ERR   1
#define ISCSI_LOG_SENSE 2
#define ISCSI_LOG_INIT  3
#define ISCSI_LOG_QUEUE 4
#define ISCSI_LOG_ALLOC 5
#define ISCSI_LOG_EH    6
#define ISCSI_LOG_FLOW  7
#define ISCSI_LOG_SMP   8
#define ISCSI_LOG_LOGIN 9

#define LOG_SET(flag) (1U << (flag))
#define LOG_ENABLED(flag) (iscsi_log_settings & (1U << (flag)))


#if DEBUG_INIT
#define DEBUG_INIT0(DI0) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0);
#define DEBUG_INIT1(DI0,DI1) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1);
#define DEBUG_INIT2(DI0,DI1,DI2) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1,DI2);
#define DEBUG_INIT3(DI0,DI1,DI2,DI3) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1,DI2,DI3);
#define DEBUG_INIT4(DI0,DI1,DI2,DI3,DI4) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1,DI2,DI3,DI4);
#define DEBUG_INIT5(DI0,DI1,DI2,DI3,DI4,DI5) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1,DI2,DI3,DI4,DI5);
#define DEBUG_INIT6(DI0,DI1,DI2,DI3,DI4,DI5,DI6) if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(DI0,DI1,DI2,DI3,DI4,DI5,DI6);
#else
#define DEBUG_INIT0(DI0)
#define DEBUG_INIT1(DI0,DI1)
#define DEBUG_INIT2(DI0,DI1,DI2)
#define DEBUG_INIT3(DI0,DI1,DI2,DI3)
#define DEBUG_INIT4(DI0,DI1,DI2,DI3,DI4)
#define DEBUG_INIT5(DI0,DI1,DI2,DI3,DI4,DI5)
#define DEBUG_INIT6(DI0,DI1,DI2,DI3,DI4,DI5,DI6)
#endif

#if DEBUG_QUEUE
#define DEBUG_QUEUE0(DE0) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0);
#define DEBUG_QUEUE1(DE0,DE1) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0,DE1);
#define DEBUG_QUEUE2(DE0,DE1,DE2) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0,DE1,DE2);
#define DEBUG_QUEUE3(DE0,DE1,DE2,DE3) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0,DE1,DE2,DE3);
#define DEBUG_QUEUE4(DE0,DE1,DE2,DE3,DE4) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0,DE1,DE2,DE3,DE4);
#define DEBUG_QUEUE5(DE0,DE1,DE2,DE3,DE4,DE5) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DE0,DE1,DE2,DE3,DE4,DE5);
#define DEBUG_QUEUE6(DF0,DF1,DF2,DF3,DF4,DF5,DF6) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6);
#define DEBUG_QUEUE7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7) if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7);
#else
#define DEBUG_QUEUE0(DE0)
#define DEBUG_QUEUE1(DE0,DE1)
#define DEBUG_QUEUE2(DE0,DE1,DE2)
#define DEBUG_QUEUE3(DE0,DE1,DE2,DE3)
#define DEBUG_QUEUE4(DE0,DE1,DE2,DE3,DE4)
#define DEBUG_QUEUE5(DE0,DE1,DE2,DE3,DE4,DE5)
#define DEBUG_QUEUE6(DF0,DF1,DF2,DF3,DF4,DF5,DF6)
#define DEBUG_QUEUE7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7)
#endif

#if DEBUG_FLOW
#define DEBUG_FLOW0(DF0) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0);
#define DEBUG_FLOW1(DF0,DF1) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1);
#define DEBUG_FLOW2(DF0,DF1,DF2) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1,DF2);
#define DEBUG_FLOW3(DF0,DF1,DF2,DF3) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1,DF2,DF3);
#define DEBUG_FLOW4(DF0,DF1,DF2,DF3,DF4) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1,DF2,DF3,DF4);
#define DEBUG_FLOW5(DF0,DF1,DF2,DF3,DF4,DF5) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1,DF2,DF3,DF4,DF5);
#define DEBUG_FLOW6(DF0,DF1,DF2,DF3,DF4,DF5,DF6) if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6);
#else
#define DEBUG_FLOW0(DF0)
#define DEBUG_FLOW1(DF0,DF1)
#define DEBUG_FLOW2(DF0,DF1,DF2)
#define DEBUG_FLOW3(DF0,DF1,DF2,DF3)
#define DEBUG_FLOW4(DF0,DF1,DF2,DF3,DF4)
#define DEBUG_FLOW5(DF0,DF1,DF2,DF3,DF4,DF5)
#define DEBUG_FLOW6(DF0,DF1,DF2,DF3,DF4,DF5,DF6)
#endif

#if DEBUG_ALLOC
#define DEBUG_ALLOC0(DE0) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0);
#define DEBUG_ALLOC1(DE0,DE1) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0,DE1);
#define DEBUG_ALLOC2(DE0,DE1,DE2) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0,DE1,DE2);
#define DEBUG_ALLOC3(DE0,DE1,DE2,DE3) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0,DE1,DE2,DE3);
#define DEBUG_ALLOC4(DE0,DE1,DE2,DE3,DE4) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0,DE1,DE2,DE3,DE4);
#define DEBUG_ALLOC5(DE0,DE1,DE2,DE3,DE4,DE5) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DE0,DE1,DE2,DE3,DE4,DE5);
#define DEBUG_ALLOC6(DF0,DF1,DF2,DF3,DF4,DF5,DF6) if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6);
#else
#define DEBUG_ALLOC0(DE0)
#define DEBUG_ALLOC1(DE0,DE1)
#define DEBUG_ALLOC2(DE0,DE1,DE2)
#define DEBUG_ALLOC3(DE0,DE1,DE2,DE3)
#define DEBUG_ALLOC4(DE0,DE1,DE2,DE3,DE4)
#define DEBUG_ALLOC5(DE0,DE1,DE2,DE3,DE4,DE5)
#define DEBUG_ALLOC6(DF0,DF1,DF2,DF3,DF4,DF5,DF6)
#endif

#if DEBUG_SMP
# define DEBUG_SMP0(DE0) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0);
# define DEBUG_SMP1(DE0,DE1) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0,DE1);
# define DEBUG_SMP2(DE0,DE1,DE2) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0,DE1,DE2);
# define DEBUG_SMP3(DE0,DE1,DE2,DE3) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0,DE1,DE2,DE3);
# define DEBUG_SMP4(DE0,DE1,DE2,DE3,DE4) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0,DE1,DE2,DE3,DE4);
# define DEBUG_SMP5(DE0,DE1,DE2,DE3,DE4,DE5) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DE0,DE1,DE2,DE3,DE4,DE5);
# define DEBUG_SMP6(DF0,DF1,DF2,DF3,DF4,DF5,DF6) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6);
# define DEBUG_SMP7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7) if (LOG_ENABLED(ISCSI_LOG_SMP)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7);
#else
# define DEBUG_SMP0(DE0)
# define DEBUG_SMP1(DE0,DE1)
# define DEBUG_SMP2(DE0,DE1,DE2)
# define DEBUG_SMP3(DE0,DE1,DE2,DE3)
# define DEBUG_SMP4(DE0,DE1,DE2,DE3,DE4)
# define DEBUG_SMP5(DE0,DE1,DE2,DE3,DE4,DE5)
# define DEBUG_SMP6(DF0,DF1,DF2,DF3,DF4,DF5,DF6)
# define DEBUG_SMP7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7)
#endif

#if DEBUG_EH
# define DEBUG_EH0(DE0) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0);
# define DEBUG_EH1(DE0,DE1) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0,DE1);
# define DEBUG_EH2(DE0,DE1,DE2) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0,DE1,DE2);
# define DEBUG_EH3(DE0,DE1,DE2,DE3) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0,DE1,DE2,DE3);
# define DEBUG_EH4(DE0,DE1,DE2,DE3,DE4) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0,DE1,DE2,DE3,DE4);
# define DEBUG_EH5(DE0,DE1,DE2,DE3,DE4,DE5) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DE0,DE1,DE2,DE3,DE4,DE5);
# define DEBUG_EH6(DF0,DF1,DF2,DF3,DF4,DF5,DF6) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6);
# define DEBUG_EH7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7) if (LOG_ENABLED(ISCSI_LOG_EH)) printk(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7);
#else
# define DEBUG_EH0(DE0)
# define DEBUG_EH1(DE0,DE1)
# define DEBUG_EH2(DE0,DE1,DE2)
# define DEBUG_EH3(DE0,DE1,DE2,DE3)
# define DEBUG_EH4(DE0,DE1,DE2,DE3,DE4)
# define DEBUG_EH5(DE0,DE1,DE2,DE3,DE4,DE5)
# define DEBUG_EH6(DF0,DF1,DF2,DF3,DF4,DF5,DF6)
# define DEBUG_EH7(DF0,DF1,DF2,DF3,DF4,DF5,DF6,DF7)
#endif


#if DEBUG_ERROR
#define DEBUG_ERR0(DE0) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0);
#define DEBUG_ERR1(DE0,DE1) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0,DE1);
#define DEBUG_ERR2(DE0,DE1,DE2) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0,DE1,DE2);
#define DEBUG_ERR3(DE0,DE1,DE2,DE3) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0,DE1,DE2,DE3);
#define DEBUG_ERR4(DE0,DE1,DE2,DE3,DE4) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0,DE1,DE2,DE3,DE4);
#define DEBUG_ERR5(DE0,DE1,DE2,DE3,DE4,DE5) if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(DE0,DE1,DE2,DE3,DE4,DE5);
#else
#define DEBUG_ERR0(DE0)
#define DEBUG_ERR1(DE0,DE1)
#define DEBUG_ERR2(DE0,DE1,DE2)
#define DEBUG_ERR3(DE0,DE1,DE2,DE3)
#define DEBUG_ERR4(DE0,DE1,DE2,DE3,DE4)
#define DEBUG_ERR5(DE0,DE1,DE2,DE3,DE4,DE5)
#endif


#endif

