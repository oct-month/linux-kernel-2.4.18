#ifndef ISCSI_TRACE_H_
#define ISCSI_TRACE_H_

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
 * $Id: iscsi-trace.h,v 1.6 2001/12/04 21:18:11 smferris Exp $
 *
 * iscsi-trace.h
 *
 *    include for driver trace info
 * 
 */

#define ISCSI_TRACE_COUNT  10000

typedef struct iscsi_trace_entry {
    unsigned char   type;
    unsigned char   cmd;
    unsigned char   host;
    unsigned char   channel;
    unsigned char   target;
    unsigned char   lun;
    unsigned int    itt;
    unsigned long   data1;
    unsigned long   data2;
    unsigned long   jiffies;
} iscsi_trace_entry_t;

typedef struct iscsi_trace_dump {
    uint32_t dump_ioctl_size;
    uint32_t dump_version;
    uint32_t trace_entry_size;
    uint32_t num_entries;
    iscsi_trace_entry_t trace[1];
} iscsi_trace_dump_t;

#define TRACE_DUMP_VERSION 0x1

/*
 * Trace flags
 */
#define ISCSI_TRACE_Qd                  0x01
#define ISCSI_TRACE_QFailed             0x11
#define ISCSI_TRACE_QSessionLockFailed  0x21
#define ISCSI_TRACE_QCmndLockFailed     0x31
#define ISCSI_TRACE_TxCmd               0x02
#define ISCSI_TRACE_RxCmd               0x03
#define ISCSI_TRACE_RxCmdStatus         0x13
#define ISCSI_TRACE_RxCmdFlow           0x23
#define ISCSI_TRACE_RxAsyncEvent        0x41
#define ISCSI_TRACE_TxData              0x04
#define ISCSI_TRACE_TxDataPDU           0x14
#define ISCSI_TRACE_RxData              0x05
#define ISCSI_TRACE_RxDataCmdStatus     0x15
#define ISCSI_TRACE_TxAbort             0x06
#define ISCSI_TRACE_RxAbort             0x07
#define ISCSI_TRACE_TxReset             0x08
#define ISCSI_TRACE_RxReset             0x09
#define ISCSI_TRACE_CmdDone             0x0a
#define ISCSI_TRACE_CmndAborted         0x0b
#define ISCSI_TRACE_TaskAborted         0x1b
#define ISCSI_TRACE_R2T                 0x0c
#define ISCSI_TRACE_TxPing              0x0d
#define ISCSI_TRACE_RxNop               0x0e
#define ISCSI_TRACE_TxNopReply          0x0f

#endif
