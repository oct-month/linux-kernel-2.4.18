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
 * $Id: iscsiAuthClientGlue.c,v 1.5 2002/02/15 00:24:07 smferris Exp $ 
 *
 */


#include "iscsiAuthClient.h"

int
iscsiAuthClientChapAuthRequest(
    IscsiAuthClient *client,
    char *username, unsigned int id,
    unsigned char *challengeData, unsigned int challengeLength,
    unsigned char *responseData, unsigned int responseLength)

{
    return iscsiAuthStatusFail;
}


void
iscsiAuthClientChapAuthCancel(IscsiAuthClient *client)

{
}

int
iscsiAuthClientTextToNumber(char *text, long *pNumber)

{
    char *pEnd;
    long number;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
	number = strtoul(text + 2, &pEnd, 16);
    } else {
	number = strtoul(text, &pEnd, 10);
    }

    if (*text != '\0' && *pEnd == '\0') {
	*pNumber = number;
	return 0; /* No error */
    } else {
	return 1; /* Error */
    }
}


void
iscsiAuthClientNumberToText(long number, char *text)

{
    sprintf(text, "%ld", number);
}


void
iscsiAuthRandomSetData(unsigned char *data, unsigned int length)

{
    get_random_bytes(data, length);
}


void
iscsiAuthMd5Init(IscsiAuthMd5Context *context)

{
    MD5Init(context);
}


void
iscsiAuthMd5Update(
    IscsiAuthMd5Context *context, unsigned char *data, unsigned int length)

{
    MD5Update(context, data, length);
}


void
iscsiAuthMd5Final(unsigned char *hash, IscsiAuthMd5Context *context)

{
    MD5Final(hash, context);
}


int
iscsiAuthClientData(
    unsigned char *outData, unsigned int *outLength,
    unsigned char *inData, unsigned int inLength)

{
    if (*outLength < inLength) return 1; /* error */

    memcpy(outData, inData, inLength);
    *outLength = inLength;

    return 0; /* no error */
}
