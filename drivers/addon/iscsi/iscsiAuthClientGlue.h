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
 * $Id: iscsiAuthClientGlue.h,v 1.4 2002/02/16 00:11:04 smferris Exp $ 
 *
 */

#ifndef ISCSIAUTHCLIENTGLUE_H
#define ISCSIAUTHCLIENTGLUE_H

#include <linux/config.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/random.h>
#define strtol simple_strtol
#define strtoul simple_strtoul

#include "config.h"
#include "md5.h"

typedef struct MD5Context IscsiAuthMd5Context;

extern int iscsiAuthIscsiServerHandle;
extern int iscsiAuthIscsiClientHandle;

#endif /* #ifndef ISCSIAUTHCLIENTGLUE_H */
