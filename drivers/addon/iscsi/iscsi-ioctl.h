#ifndef ISCSI_IOCTL_H_
#define ISCSI_IOCTL_H_

/*
 * iSCSI connection daemon
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
 * $Id: iscsi-ioctl.h,v 1.10 2002/02/15 00:15:29 smferris Exp $
 *
 * include for ioctl calls between the daemon and the kernel module
 *
 */

#include "iscsi-protocol.h"
#include "iscsiAuthClient.h"
/*
 * ioctls
 */
#define ISCSI_ESTABLISH_SESSION 0x00470301
#define ISCSI_TERMINATE_SESSION 0x00470302
#define ISCSI_SHUTDOWN          0x00470303
#define ISCSI_GETTRACE	        0x00470304

typedef struct iscsi_session_ioctl {
    uint32_t        ioctl_size;
    uint32_t        ioctl_version;
    int             login_timeout;
    int             auth_timeout;
    int		    active_timeout;
    int		    idle_timeout;
    int		    ping_timeout;
    int		    abort_timeout;
    int		    reset_timeout;
    int		    replacement_timeout;
    int		    InitialR2T;
    int		    ImmediateData;
    int             DataPDULength;
    int		    FirstBurstSize;
    int		    MaxBurstSize;
    uint16_t        isid;
    int             iscsi_bus;
    int		    target_id;
    int             address_length;
    unsigned char   ip_address[16];
    int		    port;
    int             tcp_window_size;
    int             authenticate;
    char            username[iscsiAuthStringMaxLength + 1];
    unsigned char   password[iscsiAuthDataMaxLength + 1];
    unsigned char   InitiatorName[TARGET_NAME_MAXLEN + 1];
    unsigned char   InitiatorAlias[TARGET_NAME_MAXLEN + 1];
    unsigned char   TargetName[TARGET_NAME_MAXLEN + 1];
} iscsi_session_ioctl_t;

#define LINUX_SESSION_IOCTL_VERSION 9

#endif
