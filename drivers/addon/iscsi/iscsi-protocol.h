#ifndef ISCSI_PROTOCOL_H_
#define ISCSI_PROTOCOL_H_
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
 * $Id: iscsi-protocol.h,v 1.10 2002/02/15 00:18:11 smferris Exp $ 
 *
 * This file sets up definitions of messages and constants used by the
 * iSCSI protocol.
 *
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#ifdef __BIG_ENDIAN_BITFIELD
#define ISCSI_BIG_ENDIAN_BITFIELD 1
#endif
#include "config.h"

/* iSCSI listen port for incoming connections */
#define ISCSI_LISTEN_PORT 3260

/* assumes a pointer to a 3-byte array */
#define ntoh24(p) (((p)[0] << 16) | ((p)[1] << 8) | ((p)[2]))

/* assumes a pointer to a 3 byte array, and an integer value */
#define hton24(p, v) {\
        p[0] = (((v) >> 16) & 0xFF); \
        p[1] = (((v) >> 8) & 0xFF); \
        p[2] = ((v) & 0xFF); \
}        


/* for Login min and max version fields */
#define ISCSI_MIN_VERSION	0x02
#define ISCSI_MAX_VERSION	0x02                                    

/* Version Numbers - version format is: iv.vv.sv                */
/*                   where:  iv is the iSCSI version number,    */
/*                           vv is the vendor version number,   */
/*                           sv is the subsystem version number */
#define ISCSI_VERSION  3
#define CISCO_VERSION  8

/* Min. and Max. length of a PDU we can support */
#define MIN_PDU_LENGTH		(8 << 9)	// 4KB
#define MAX_PDU_LENGTH		(0xffffffff)	// Huge

/* Padding word length */
#define PAD_WORD_LEN		4

/* Max. number of Key=Value pairs in a text message */
#define MAX_KEY_VALUE_PAIRS	8192

/* Reserved value for initiator/target task tag */
#define RSVD_TASK_TAG	0xffffffff

/* maximum length for text keys/values */
#define KEY_MAXLEN 64
#define VALUE_MAXLEN 255
#define TARGET_NAME_MAXLEN    VALUE_MAXLEN

/* iSCSI Template Message Header */
struct IscsiHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  final:1,	// Final (or Poll) bit
	   rsvd1:7;
#else
    UINT8  rsvd1:7,
	   final:1;
#endif
    UINT8  rsvd2[2];
    UINT8  hlength;	/* AHSs total length */
    UINT8  dlength[3];	/* Data length */
    UINT8  lun[8];
    UINT32 itt;		/* Initiator Task Tag */
    UINT8  other[28];
};

/* Opcode encoding bits */
#define ISCSI_OP_RETRY		0x80
#define ISCSI_OP_IMMEDIATE		0x40
#define ISCSI_OP_RSP			0xC0	// The 2 MSB always set in RSP
#define ISCSI_OPCODE_MASK		0x3F

/* Client to Server Message Opcode values */
#define ISCSI_OP_NOOP_OUT		0x00
#define ISCSI_OP_SCSI_CMD		0x01
#define ISCSI_OP_SCSI_TASK_MGT_MSG	0x02
#define ISCSI_OP_LOGIN_CMD		0x03
#define ISCSI_OP_TEXT_CMD		0x04
#define ISCSI_OP_SCSI_DATA		0x05
#define ISCSI_OP_LOGOUT_CMD		0x06
#define ISCSI_OP_SNACK_CMD		0x10

/* Server to Client Message Opcode values */
#define ISCSI_OP_NOOP_IN		(0x20 | ISCSI_OP_RSP)
#define ISCSI_OP_SCSI_RSP		(0x21 | ISCSI_OP_RSP)
#define ISCSI_OP_SCSI_TASK_MGT_RSP	(0x22 | ISCSI_OP_RSP)
#define ISCSI_OP_LOGIN_RSP		(0x23 | ISCSI_OP_RSP)
#define ISCSI_OP_TEXT_RSP		(0x24 | ISCSI_OP_RSP)
#define ISCSI_OP_SCSI_DATA_RSP	(0x25 | ISCSI_OP_RSP)
#define ISCSI_OP_LOGOUT_RSP		(0x26 | ISCSI_OP_RSP)
#define ISCSI_OP_RTT_RSP		(0x31 | ISCSI_OP_RSP)
#define ISCSI_OP_ASYNC_EVENT		(0x32 | ISCSI_OP_RSP)
#define ISCSI_OP_REJECT_MSG		(0x3f | ISCSI_OP_RSP)


struct IscsiCmdFlags {
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8 	final:1,
	  	read_data:1,
		write_data:1,
		rsvd1:2,
		attr:3;		/* see SCSI Command Attribute values below */
#else
    UINT8	attr:3,
		rsvd1:2,
		write_data:1,
	  	read_data:1,
		final:1;
#endif
};

/* SCSI Command Header */
struct IscsiScsiCmdHdr {
    UINT8			opcode;
    struct IscsiCmdFlags	flags;
    UINT8			rsvd2;
    UINT8			cmdrn;
    UINT8			hlength;
    UINT8			dlength[3];
    UINT8			lun[8];
    UINT32			itt;		/* Initiator Task Tag */
    UINT32			data_length;
    UINT32			cmdsn;
    UINT32			expstatsn;
    UINT8			scb[16];	/* SCSI Command Block */
    /* Additional Data (Command Dependent) */
};

/* SCSI Command Attribute values */
#define ISCSI_ATTR_UNTAGGED		0
#define ISCSI_ATTR_SIMPLE		1
#define ISCSI_ATTR_ORDERED		2
#define ISCSI_ATTR_HEAD_OF_QUEUE	3
#define ISCSI_ATTR_ACA			4


struct IscsiCmdRespFlags {
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8	fbit:1,
	 	rsvd1:2,
	  	bidi_overflow:1,
		bidi_undeflow:1,
		overflow:1,
		underflow:1,
	  	rsvd2:1;
#else
    UINT8  	rsvd2:1,
    		underflow:1,
		overflow:1,
		bidi_underflow:1,
	  	bidi_overflow:1,
		rsvd1:2,
		fbit:1;
#endif
};

/* SCSI Response Header */
struct IscsiScsiRspHdr {
    UINT8			opcode;
    struct IscsiCmdRespFlags	flags;
    UINT8			response;
    UINT8			cmd_status;
    UINT8			hlength;
    UINT8			dlength[3];
    UINT8			rsvd[8];
    UINT32			itt;		/* Initiator Task Tag */
    UINT32			residual_count;
    UINT32			statsn;
    UINT32			expcmdsn;
    UINT32			maxcmdsn;
    UINT32			expdatasn;
    UINT32			rsvd1;
    UINT32			bi_residual_count;
    /* Response or Sense Data (optional) */
};

/* iSCSI Status values. Valid if Rsp Selector bit is not set */
#define ISCSI_STATUS_CMD_COMPLETED	0
#define ISCSI_STATUS_TARGET_FAILURE	1
#define ISCSI_STATUS_SUBSYS_FAILURE	2


/* Asynchronous Event Header */
struct IscsiAsyncEvtHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  rsvd2[2];
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  lun[8];
    UINT8  rsvd4[8];
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  async_event;	
    UINT8  async_vcode;	
    UINT16 param1;
    UINT16 param2;
    UINT16 param3;
    UINT8  rsvd5[4];
};

/* iSCSI Event Indicator values */
#define ASYNC_EVENT_SCSI_EVENT                  0
#define ASYNC_EVENT_REQUEST_LOGOUT              1
#define ASYNC_EVENT_DROPPING_CONNECTION         2
#define ASYNC_EVENT_DROPPING_ALL_CONNECTIONS	3
#define ASYNC_EVENT_VENDOR_SPECIFIC             255


/* NOP-Out Message */
struct IscsiNopOutHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  poll:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           poll:1;
#endif
    UINT16 rsvd2;
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  lun[8];
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 ttt;		/* Target Transfer Tag */
    UINT32 cmdsn;
    UINT32 expstatsn;
    UINT8  rsvd4[16];
};

/* NOP-In Message */
struct IscsiNopInHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
	   rsvd1:7;
#else
    UINT8  rsvd1:7,
	   fbit:1;
#endif
    UINT16 rsvd2;
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  lun[8];
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 ttt;		/* Target Transfer Tag */
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  rsvd4[12];
};

/* SCSI Task Management Message Header */
struct IscsiScsiTaskMgtHdr {
    UINT8  opcode;
    UINT8  function;	/* see Function values below */
    UINT8  rsvd1[6];
    UINT8  lun[8];
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 rtt;		/* Reference Task Tag */
    UINT32 cmdsn;
    UINT32 expstatsn;
    UINT32 refcmdsn;
    UINT8  rsvd2[12];
};

/* Function values */
#define ISCSI_TM_FUNC_ABORT_TASK         1
#define ISCSI_TM_FUNC_ABORT_TASK_SET     2
#define ISCSI_TM_FUNC_CLEAR_ACA          3
#define ISCSI_TM_FUNC_CLEAR_TASK_SET     4
#define ISCSI_TM_FUNC_LOGICAL_UNIT_RESET 5
#define ISCSI_TM_FUNC_TARGET_WARM_RESET  6
#define ISCSI_TM_FUNC_TARGET_COLD_RESET  7
#define ISCSI_TM_FUNC_TASK_REASSIGN	 8

/* SCSI Task Management Response Header */
struct IscsiScsiTaskMgtRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  response;	/* see Response values below */
    UINT8  qualifier;
    UINT8  rsvd2[12];
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 rtt;		/* Reference Task Tag */
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  rsvd3[12];
};

/* Response values */
#define SCSI_TCP_TM_RESP_COMPLETE	0x00
#define SCSI_TCP_TM_RESP_NO_TASK	0x01
#define SCSI_TCP_TM_RESP_NO_LUN		0x02
#define SCSI_TCP_TM_RESP_TASK_ALLEGIANT	0x03
#define SCSI_TCP_TM_RESP_NO_FAILOVER	0x04
#define SCSI_TCP_TM_RESP_IN_PRGRESS	0x05
#define SCSI_TCP_TM_RESP_REJECTED	0xff

/* Ready To Transfer Header */
struct IscsiRttHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  rsvd2[2];
    UINT8  rsvd3[12];
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 ttt;		/* Target Transfer Tag */
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT32 rttsn;
    UINT32 data_offset;
    UINT32 data_length;
};


/* SCSI Data Hdr */
struct IscsiDataHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  final:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           final:1;
#endif
    UINT8  rsvd2[2];
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  lun[8];
    UINT32 itt;
    UINT32 ttt;
    UINT32 rsvd4;
    UINT32 expstatsn;
    UINT32 rsvd5;
    UINT32 datasn;
    UINT32 offset;
    UINT32 rsvd6;
    /* Payload */
};

/* SCSI Data Response Hdr */
struct IscsiDataRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  final:1,
	   rsvd1:4,
           overflow:1,
           underflow:1,
           status_present:1;
#else
    UINT8  status_present:1,
	   underflow:1,
	   overflow:1,
	   rsvd1:4,
	   final:1;
#endif
    UINT8  rsvd2;
    UINT8  cmd_status;
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  rsvd4[8];
    UINT32 itt;
    UINT32 residual_count;
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT32 datasn;
    UINT32 offset;
    UINT32 rsvd5;
};


/* Text Header */
struct TextKeyHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  final:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           final:1;
#endif
    UINT8  rsvd2[2];
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  rsvd4[8];
    UINT32 itt;
    UINT32 ttt;
    UINT32 cmdsn;
    UINT32 expstatsn;
    UINT8  rsvd5[16];
    /* Text - key=value pairs */
};

/* Text Response Header */
struct TextKeyRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  final:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           final:1;
#endif
    UINT8  rsvd2[2];
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  rsvd4[8];
    UINT32 itt;
    UINT32 ttt;
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  rsvd5[12];
    /* Text Response - key:value pairs */
};

/* Login Header */
struct IscsiLoginHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  tbit:1,
	   rsvd1:3,
	   curr:2,
	   next:2;
#else
    UINT8  next:2,
	   curr:2,
	   rsvd1:3,
	   tbit:1;
#endif
    UINT8  max_version;	/* Max. version supported */
    UINT8  min_version;	/* Min. version supported */
    UINT8  rsvd2;
    UINT8  dlength[3];
    UINT16 cid;
    UINT16 rsvd3;
    UINT16 isid;        /* Initiator Session ID */
    UINT16 tsid;        /* Target Session ID */
    UINT32 itt;		/* Initiator Task Tag */
    UINT32 rsvd4;
    UINT32 cmdsn;
    UINT32 expstatsn;
    UINT8  rsvd5[16];
};

typedef struct {
} RBITS;

/* Login Response Header */
struct IscsiLoginRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  tbit:1,
	   rsvd:3,
	   curr:2,
	   next:2;
#else
    UINT8  next:2,
	   curr:2,
	   rsvd:3,
	   tbit:1;
#endif
    UINT8  max_version;		/* Max. version supported */
    UINT8  active_version;	/* Active version */
    UINT8  rsvd1;
    UINT8  dlength[3];
    UINT32 rsvd2;
    UINT16 isid;		/* Initiator Session ID */
    UINT16 tsid;		/* Target Session ID */
    UINT32 itt;			/* Initiator Task Tag */
    UINT32 rsvd3;
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  status_class;	/* see Login RSP ststus classes below */
    UINT8  status_detail;	/* see Login RSP Status details below */
    UINT8  rsvd4[10];
};

/* Login stage (phase) codes  for CNxSG */
#define ISCSI_SECURITY_NEGOTIATION_PHASE	0
#define ISCSI_OP_PARMS_NEGOTIATION_PHASE	1
#define ISCSI_FULL_FEATURE_PHASE		3

/* Login Status response classes */
#define STATUS_CLASS_SUCCESS		0x00
#define STATUS_CLASS_REDIRECT		0x01
#define STATUS_CLASS_INITIATOR_ERR	0x02
#define STATUS_CLASS_TARGET_ERR		0x03

/* Login Status response detail codes */
/* Class-0 (Success) */
#define ISCSI_LOGIN_STATUS_ACCEPT		0x00

/* Class-1 (Redirection) */
#define ISCSI_LOGIN_STATUS_TGT_MOVED_TEMP	0x01
#define ISCSI_LOGIN_STATUS_TGT_MOVED_PERM	0x02

/* Class-2 (Initiator Error) */
#define ISCSI_LOGIN_STATUS_INIT_ERR		0x00
#define ISCSI_LOGIN_STATUS_AUTH_FAILED		0x01
#define ISCSI_LOGIN_STATUS_TGT_FORBIDDEN	0x02
#define ISCSI_LOGIN_STATUS_TGT_NOT_FOUND	0x03
#define ISCSI_LOGIN_STATUS_TGT_REMOVED		0x04
#define ISCSI_LOGIN_STATUS_NO_VERSION		0x05
#define ISCSI_LOGIN_STATUS_ISID_ERROR		0x06
#define ISCSI_LOGIN_STATUS_MISSING_FIELDS	0x07
#define ISCSI_LOGIN_STATUS_CONN_ADD_FAILED	0x08
#define ISCSI_LOGIN_STATUS_NO_SESSION_TYPE	0x09

/* Class-3 (Target Error) */
#define ISCSI_LOGIN_STATUS_TARGET_ERROR		0x00
#define ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE	0x01
#define ISCSI_LOGIN_STATUS_NO_RESOURCES	        0x02


/* Logout Header */
struct IscsiLogoutHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  rsvd2[6];
    UINT16 cid;
    UINT8  rsvd3;
    UINT8  reason;
    UINT32 rsvd4;
    UINT32 itt;		/* Initiator Task Tag */
    UINT8  rsvd5[4];
    UINT32 cmdsn;
    UINT32 expstatsn;
    UINT8  rsvd6[16];
};

// logout reason_code values

#define ISCSI_LOGOUT_REASON_CLOSE_SESSION	0
#define ISCSI_LOGOUT_REASON_CLOSE_CONNECTION	1
#define ISCSI_LOGOUT_REASON_RECOVERY		2
#define ISCSI_LOGOUT_REASON_AEN_REQUEST	        3

/* Logout Response Header */
struct IscsiLogoutRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  rsvd2[14];
    UINT32 itt;			/* Initiator Task Tag */
    UINT8  rsvd3[8];
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT8  response;		/* see Logout response values below */
    UINT8  rsvd4[11];
};

// logout response status values

#define ISCSI_LOGOUT_SUCCESS		0
#define ISCSI_LOGOUT_CID_NOT_FOUND	1
#define ISCSI_LOGOUT_FAIL		2


/* SNACK Header */
struct IscsiSNACKHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
	   rsvd1:3,
	   type:4;
#else
    UINT8  type:4,
	   rsvd1:3,
	   fbit:1;
#endif
    UINT8  rsvd2[14];
    UINT32 itt;
    UINT32 begrun;
    UINT32 runlength;
    UINT32 expstatsn;
    UINT32 rsvd3;
    UINT32 expdatasn;
    UINT8  rsvd6[8];
};


/* Reject Message Header */
struct IscsiRejectRspHdr {
    UINT8  opcode;
#ifdef ISCSI_BIG_ENDIAN_BITFIELD
    UINT8  fbit:1,
           rsvd1:7;
#else
    UINT8  rsvd1:7,
           fbit:1;
#endif
    UINT8  reason;
    UINT8  rsvd2;
    UINT8  rsvd3;
    UINT8  dlength[3];
    UINT8  rsvd4[16];
    UINT32 statsn;
    UINT32 expcmdsn;
    UINT32 maxcmdsn;
    UINT32 datasn;
    UINT8  rsvd5[8];
    /* Text - Rejected hdr */
};

/* Reason for Reject */
#define CMD_BEFORE_LOGIN        1
#define DATA_DIGEST_ERROR       2
#define DATA_SNACK_REJECT       3
#define ISCSI_PROTOCOL_ERROR    4
#define CMD_NOT_SUPPORTED       5
#define IMM_CMD_REJECT          6
#define TASK_IN_PROGRESS        7
#define INVALID_SNACK           8
#define BOOKMARK_REJECTED       9
#define BOOKMARK_NO_RESOURCES   10
#define NEGOTIATION_RESET	11


#endif
