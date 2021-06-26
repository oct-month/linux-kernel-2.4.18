#ifndef ISCSI_COMMON_H_
#define ISCSI_COMMON_H_

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
 * $Id: iscsi-common.h,v 1.5 2002/01/31 23:09:48 smferris Exp $
 *
 * include for common info needed by both the daemon and kernel module
 *
 */

#define ISCSI_MAX_TARGETS	 16
#define ISCSI_MAX_LUN	         256

/* how many LUNs to probe by default */
#define ISCSI_DEFAULT_LUNS_TO_PROBE 32

#ifndef __cplusplus
typedef enum boolean {
    false= 0,
    true = 1
} bool;
#endif


#endif
