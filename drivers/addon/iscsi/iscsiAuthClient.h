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
 * $Id: iscsiAuthClient.h,v 1.9 2002/02/16 00:11:50 smferris Exp $ 
 *
 * File: iscsiAuthClient.h
 * Created: March 2001  ssenum@cisco.com
 *
 */

/*
 * This file is the include file for for iscsiAuthClient.c
 */


#ifndef ISCSIAUTHCLIENT_H
#define ISCSIAUTHCLIENT_H

#include "iscsiAuthClientGlue.h"

enum {
	iscsiAuthStringMaxLength = 100,
	iscsiAuthDataMaxLength = iscsiAuthStringMaxLength / 2
};

enum {iscsiAuthRecvEndMaxCount = 10};

enum {iscsiAuthClientSignature = 0x5984B2E3};

enum {iscsiAuthChapChallengeLength = 16};

enum iscsiAuthKeyType_t {
	iscsiAuthKeyTypeNone = -1,
	iscsiAuthKeyTypeFirst = 0,
	iscsiAuthKeyTypeAuthMethod = iscsiAuthKeyTypeFirst,
	iscsiAuthKeyTypeHeaderDigest,
	iscsiAuthKeyTypeDataDigest,
	iscsiAuthKeyTypeChapAlgorithm,
	iscsiAuthKeyTypeChapIdentifier,
	iscsiAuthKeyTypeChapChallenge,
	iscsiAuthKeyTypeChapResponse,
	iscsiAuthKeyTypeChapUsername,
	iscsiAuthKeyTypeMaxCount,
	iscsiAuthKeyTypeLast = iscsiAuthKeyTypeMaxCount - 1
};
typedef enum iscsiAuthKeyType_t IscsiAuthKeyType;

enum {
	/* Common options for all keys. */
	iscsiAuthOptionReject = -2,
	iscsiAuthOptionNotPresent = -1,
	iscsiAuthOptionNone = 1,

	iscsiAuthMethodChap = 2,
	iscsiAuthMethodMaxCount = 2,

	iscsiAuthHeaderDigestCrc32c = 2,
	iscsiAuthHeaderDigestMaxCount = 2,

	iscsiAuthDataDigestCrc32c = 2,
	iscsiAuthDataDigestMaxCount = 2,

	iscsiAuthChapAlgorithmMd5 = 5,
	iscsiAuthChapAlgorithmMaxCount = 2
};

enum iscsiAuthStatus_t {
	iscsiAuthStatusNoError = 0,
	iscsiAuthStatusError,
	iscsiAuthStatusPass,
	iscsiAuthStatusFail,
	iscsiAuthStatusContinue,
	iscsiAuthStatusInProgress
};
typedef enum iscsiAuthStatus_t IscsiAuthStatus;

enum iscsiAuthDebugStatus_t {
	iscsiAuthDebugStatusNotSet = 0,
	iscsiAuthDebugStatusAuthMethodNotPresent,
	iscsiAuthDebugStatusAuthMethodReject,
	iscsiAuthDebugStatusAuthMethodNone,
	iscsiAuthDebugStatusAuthMethodBad,
	iscsiAuthDebugStatusAuthRemoteFalse,
	iscsiAuthDebugStatusAuthPass,
	iscsiAuthDebugStatusAuthFail,
	iscsiAuthDebugStatusAuthStatusBad,
	iscsiAuthDebugStatusChapAlgorithmBad,
	iscsiAuthDebugStatusChapResponseExpected,
	iscsiAuthDebugStatusChapUsernameExpected,
	iscsiAuthDebugStatusChapIdentifierExpected,
	iscsiAuthDebugStatusChapChallengeExpected,
	iscsiAuthDebugStatusChapResponseBad,
	iscsiAuthDebugStatusChapIdentifierBad,
	iscsiAuthDebugStatusChapChallengeBad,
	iscsiAuthDebugStatusChapComputeResponseFailed,
	iscsiAuthDebugStatusTbitSetIllegal,
	iscsiAuthDebugStatusTbitSetPremature,
	iscsiAuthDebugStatusMessageCountLimit,
	iscsiAuthDebugStatusPassNotValid
};
typedef enum iscsiAuthDebugStatus_t IscsiAuthDebugStatus;

enum iscsiAuthNodeType_t {
	iscsiAuthNodeTypeInitiator = 1,
	iscsiAuthNodeTypeTarget = 2
};
typedef enum iscsiAuthNodeType_t IscsiAuthNodeType;

enum iscsiAuthPhase_t {
	iscsiAuthPhaseConfigure = 1,
	iscsiAuthPhaseNegotiate,
	iscsiAuthPhaseAuthenticate,
	iscsiAuthPhaseDone,
	iscsiAuthPhaseError
};
typedef enum iscsiAuthPhase_t IscsiAuthPhase;

enum iscsiAuthLocalState_t {
	iscsiAuthLocalStateRecvChallenge = 1,
	iscsiAuthLocalStateDone,
	iscsiAuthLocalStateError
};
typedef enum iscsiAuthLocalState_t IscsiAuthLocalState;

enum iscsiAuthRemoteState_t {
	iscsiAuthRemoteStateSendAlgorithm = 1,
	iscsiAuthRemoteStateRecvAlgorithm,
	iscsiAuthRemoteStateSendChallenge,
	iscsiAuthRemoteStateRecvResponse,
	iscsiAuthRemoteStateAuthRequest,
	iscsiAuthRemoteStateDone,
	iscsiAuthRemoteStateError
};
typedef enum iscsiAuthRemoteState_t IscsiAuthRemoteState;


typedef void IscsiAuthClientCallback(void *, void *, int);


struct iscsiAuthClientGlobalStats_t {
	unsigned long requestSent;
	unsigned long responseReceived;
};
typedef struct iscsiAuthClientGlobalStats_t IscsiAuthClientGlobalStats;

struct iscsiAuthClientKey_t {
	int present;
	char value[iscsiAuthStringMaxLength];
};
typedef struct iscsiAuthClientKey_t IscsiAuthClientKey;

struct iscsiAuthClient_t {
	unsigned long signature;

	void *glueHandle;
	struct iscsiAuthClient_t *next;
	unsigned int authRequestId;

	IscsiAuthNodeType nodeType;
	unsigned int authMethodCount;
	int authMethodList[iscsiAuthMethodMaxCount];
	unsigned int headerDigestCount;
	int headerDigestList[iscsiAuthHeaderDigestMaxCount];
	unsigned int dataDigestCount;
	int dataDigestList[iscsiAuthDataDigestMaxCount];
	unsigned int chapAlgorithmCount;
	int chapAlgorithmList[iscsiAuthChapAlgorithmMaxCount];
	int authRemote;
	char username[iscsiAuthStringMaxLength];
	int passwordPresent;
	unsigned int passwordLength;
	unsigned char passwordData[iscsiAuthDataMaxLength];
	char listName[iscsiAuthStringMaxLength];

	unsigned int authMethodValidCount;
	int authMethodValidList[iscsiAuthMethodMaxCount];

	int recvInProgressFlag;
	int recvEndCount;
	IscsiAuthClientCallback *callback;
	void *userHandle;
	void *messageHandle;

	IscsiAuthPhase phase;
	IscsiAuthLocalState localState;
	IscsiAuthRemoteState remoteState;
	IscsiAuthStatus remoteAuthStatus;
	IscsiAuthDebugStatus debugStatus;
	int negotiatedAuthMethod;
	int negotiatedChapAlgorithm;
	int negotiatedHeaderDigest;
	int negotiatedDataDigest;
	int authResponseFlag;
	int transitBitSentFlag;

	unsigned int chapIdentifier;
	unsigned char chapChallengeData[iscsiAuthChapChallengeLength];

	int recvTransitBit;
	int sendTransitBit;
	IscsiAuthClientKey recv[iscsiAuthKeyTypeMaxCount];
	IscsiAuthClientKey send[iscsiAuthKeyTypeMaxCount];
};
typedef struct iscsiAuthClient_t IscsiAuthClient;


extern IscsiAuthClientGlobalStats iscsiAuthClientGlobalStats;


extern int iscsiAuthClientInit(IscsiAuthClient *, IscsiAuthNodeType);
extern int iscsiAuthClientFinish(IscsiAuthClient *);

extern int iscsiAuthClientRecvBegin(IscsiAuthClient *);
extern int iscsiAuthClientRecvEnd(
	IscsiAuthClient *, IscsiAuthClientCallback *, void *, void *);

extern const char *iscsiAuthClientGetKeyName(int);
extern int iscsiAuthClientGetNextKeyType(int *);
extern int iscsiAuthClientRecvKeyValue(IscsiAuthClient *, int, const char *);
extern int iscsiAuthClientSendKeyValue(IscsiAuthClient *, int, int *, char *);
extern int iscsiAuthClientRecvTransitBit(IscsiAuthClient *, int);
extern int iscsiAuthClientSendTransitBit(IscsiAuthClient *, int *);

extern int iscsiAuthClientSetAuthMethodList(
	IscsiAuthClient *, unsigned int, const int *);
extern int iscsiAuthClientSetHeaderDigestList(
	IscsiAuthClient *, unsigned int, const int *);
extern int iscsiAuthClientSetDataDigestList(
	IscsiAuthClient *, unsigned int, const int *);
extern int iscsiAuthClientSetChapAlgorithmList(
	IscsiAuthClient *, unsigned int, const int *);
extern int iscsiAuthClientSetUsername(IscsiAuthClient *, const char *);
extern int iscsiAuthClientSetPassword(
	IscsiAuthClient *, const unsigned char *, unsigned int);
extern int iscsiAuthClientSetAuthRemote(IscsiAuthClient *, int);
extern int iscsiAuthClientSetGlueHandle(IscsiAuthClient *, void *);
extern int iscsiAuthClientSetListName(IscsiAuthClient *, const char *);

extern int iscsiAuthClientGetAuthPhase(IscsiAuthClient *, int *);
extern int iscsiAuthClientGetAuthStatus(IscsiAuthClient *, int *);
extern int iscsiAuthClientGetAuthMethod(IscsiAuthClient *, int *);
extern int iscsiAuthClientGetHeaderDigest(IscsiAuthClient *, int *);
extern int iscsiAuthClientGetDataDigest(IscsiAuthClient *, int *);
extern int iscsiAuthClientGetChapAlgorithm(IscsiAuthClient *, int *);

extern int iscsiAuthClientGetDebugStatus(IscsiAuthClient *, int *);
extern const char *iscsiAuthClientDebugStatusToText(int);

/*
 * The following is called by platform dependent code.
 */
extern void iscsiAuthClientAuthResponse(IscsiAuthClient *, int);

/*
 * The following routines are considered platform dependent,
 * and need to be implemented for use by iscsiAuthClient.c.
 */

extern int iscsiAuthClientChapAuthRequest(
	IscsiAuthClient *, char *, unsigned int,
	unsigned char *, unsigned int, unsigned char *, unsigned int);
extern void iscsiAuthClientChapAuthCancel(IscsiAuthClient *);

extern int iscsiAuthClientTextToNumber(char *, long *);
extern void iscsiAuthClientNumberToText(long, char *);

extern void iscsiAuthRandomSetData(unsigned char *, unsigned int);
extern void iscsiAuthMd5Init(IscsiAuthMd5Context *);
extern void iscsiAuthMd5Update(
	IscsiAuthMd5Context *, unsigned char *, unsigned int);
extern void iscsiAuthMd5Final(unsigned char *, IscsiAuthMd5Context *);

extern int iscsiAuthClientData(
	unsigned char *, unsigned int *, unsigned char *, unsigned int);


#endif /* #ifndef ISCSIAUTHCLIENT_H */
