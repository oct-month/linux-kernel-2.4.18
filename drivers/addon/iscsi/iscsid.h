#ifndef ISCSID_H_
#define ISCSID_H_

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
 * $Id: iscsid.h,v 1.11 2002/02/15 00:22:57 smferris Exp $
 *
 * iscsi.h
 *
 *    Main include for iSCSI daemon
 * 
 */


#define ISCSI_LOGIN_TIMEOUT    15
#define ISCSI_AUTH_TIMEOUT     30
#define ISCSI_ACTIVE_TIMEOUT   5
#define ISCSI_IDLE_TIMEOUT     60
#define ISCSI_PING_TIMEOUT     5
#define ISCSI_ABORT_TIMEOUT    10
#define ISCSI_RESET_TIMEOUT    30
#define ISCSI_REPLACEMENT_TIMEOUT 180

#define TCP_WINDOW_SIZE (256 * 1024)

/* Various phases of a connection */
typedef enum connPhase {
    connNotLoggedIn_e = 0,
    secParmsNegotiate_e,
    opParmsNegotiate_e,
    fullFeaturePhase_e
} connPhase_t;

typedef struct iscsid_session {
	int		socket_fd;
	int		ActiveTimeout;
	int		IdleTimeout;
	int		PingTimeout;
	int		AbortTimeout;
	int		ResetTimeout;
	int		initialR2T;
	int		immediateData;
	int		dataPDULength;
	int		firstBurstSize;
	int		maxBurstSize;
	unsigned short	isid;
	unsigned short	tsid;
	unsigned long	CmdSn;
	unsigned long	ExpCmdSn;
	unsigned long	MaxCmdSn;
	unsigned long   InitStatSn;
	int             iscsi_bus;
    	int		TargetId;
	char		TargetName[TARGET_NAME_MAXLEN+1];
	char		TargetAlias[TARGET_NAME_MAXLEN+1];
	int             AddressLength;
	unsigned char	RemoteIpAddr[16];
	int		Port;
	connPhase_t	connPhase;  
	IscsiAuthClient *authClient;  /* FIXME: remove this, it shouldn't leave the daemon */
	IscsiAuthStatus	authStatus_m; /* FIXME: remove this, it shouldn't leave the daemon */
} iscsid_session_t;

/* header plus alignment plus max login pdu size + pad */
#define ISCSI_LOGIN_BUFFER_SIZE    ((2 * sizeof(struct IscsiHdr)) + 4096 + 4) 


#endif
