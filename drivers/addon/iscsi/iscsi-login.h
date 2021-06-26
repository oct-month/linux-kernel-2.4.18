#ifndef ISCSI_LOGIN_H_
#define ISCSI_LOGIN_H_

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
 * $Id: iscsi-login.h,v 1.1 2002/02/15 00:35:04 smferris Exp $
 *
 * iscsi-login.h
 *
 *    include for iSCSI login
 * 
 */

/* handle platfrom dependencies, and get iscsi_session_t defined 
 * a minimal definition for iscsi_session_t is:
 */
#if 0
typedef struct iscsi_session {
    int type;
    int current_phase;
    int next_phase;
    int partial_response;
    int desired_InitialR2T;
    int desired_DataPDULength;
    int desired_FirstBurstSize;
    int InitialR2T;
    int DataPDULength;
    int FirstBurstSize;
    int MaxBurstSize;
    int login_timeout;
    int auth_timeout;
} iscsi_session_t;
#endif

#define iscsi_strtoul simple_strtoul
#include <linux/config.h>
#include <linux/version.h>
#include <linux/stddef.h>
#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/blk.h>
#include <sd.h>
#include <scsi.h>
#include <hosts.h>
#include "iscsi-protocol.h"
#include "iscsi.h"
/* both the kernel and userland share these names */
#define iscsi_strcmp  strcmp
#define iscsi_strncmp strncmp
#define iscsi_strlen  strlen
#define iscsi_strncpy strncpy
#define iscsi_sprintf sprintf

#define ISCSI_SESSION_TYPE_NORMAL 0
#define ISCSI_SESSION_TYPE_DISCOVERY 1

/* implemented in iscsi-login.c for use on all platforms */
extern int iscsi_login(iscsi_session_t *session, char *buffer, size_t bufsize);

/* functions used in iscsi-login.c that must be implemented for each platform */
extern int iscsi_send_login_pdu(iscsi_session_t *session, struct IscsiLoginHdr *pdu, int max_pdu_length);
extern int iscsi_recv_login_pdu(iscsi_session_t *session, struct IscsiLoginRspHdr *pdu, int max_pdu_length, int timeout);


#endif

