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
 * $Id: iscsiAuthClient.c,v 1.9 2002/02/15 00:19:03 smferris Exp $ 
 *
 * File: iscsiAuthClient.c
 * Created: March 2001  ssenum@cisco.com
 *
 */

/*
 * This file implements the iSCSI CHAP authentication method based on
 * draft-ietf-ips-iscsi-06.txt.  The code in this file is meant
 * to be platform independent, and makes use of only limited library
 * functions, presently only string.h.  Platform dependent routines are
 * defined in iscsiAuthClient.h, but implemented in another file.
 *
 * This code in this files assumes a single thread of execution
 * for each IscsiAuthClient structure, and does no locking.
 */

#include "iscsiAuthClient.h"


#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


struct iscsiAuthClientKeyInfo_t {
	const char *name;
};
typedef struct iscsiAuthClientKeyInfo_t IscsiAuthClientKeyInfo;


IscsiAuthClientGlobalStats iscsiAuthClientGlobalStats;

static IscsiAuthClientKeyInfo
	iscsiAuthClientKeyInfo[iscsiAuthKeyTypeMaxCount] = {
	{"AuthMethod"},
	{"HeaderDigest"},
	{"DataDigest"},
	{"CHAP_A"},
	{"CHAP_I"},
	{"CHAP_C"},
	{"CHAP_R"},
	{"CHAP_N"}
};

static const char iscsiAuthClientHexString[] = "0123456789abcdefABCDEF";
static const char iscsiAuthClientRejectOptionName[] = "reject";
static const char iscsiAuthClientNoneOptionName[] = "none";
static const char iscsiAuthClientAuthMethodChapOptionName[] = "CHAP";
static const char iscsiAuthClientHeaderDigestCrc32cOptionName[] = "CRC32C";
static const char iscsiAuthClientDataDigestCrc32cOptionName[] = "CRC32C";


static int
iscsiAuthClientCheckString(const char *s)

{
	int length;

	if (!s) {
		return TRUE;
	}

	for (length = 0; length < iscsiAuthStringMaxLength; length++) {
		if (*s++ == '\0') return FALSE;
	}

	return TRUE;
}


static int
iscsiAuthClientCheckNodeType(IscsiAuthNodeType nodeType)

{
	if (nodeType == iscsiAuthNodeTypeInitiator ||
		nodeType == iscsiAuthNodeTypeTarget) {
		return FALSE;
	}

	return TRUE;
}


static int
iscsiAuthClientCheckAuthMethodOption(int value)

{
	if (value == iscsiAuthOptionNone ||
		value == iscsiAuthMethodChap) {
		return FALSE;
	}

	return TRUE;
}


static const char *
iscsiAuthClientAuthMethodOptionToText(int value)

{
	const char *s;

	switch (value) {

	case iscsiAuthOptionReject:
		s = iscsiAuthClientRejectOptionName;
		break;

	case iscsiAuthOptionNone:
		s = iscsiAuthClientNoneOptionName;
		break;

	case iscsiAuthMethodChap:
		s = iscsiAuthClientAuthMethodChapOptionName;
		break;

	default:
		s = NULL;
	}

	return s;
}


static int
iscsiAuthClientCheckHeaderDigestOption(int value)

{
	if (value == iscsiAuthOptionNone ||
		value == iscsiAuthHeaderDigestCrc32c) {
		return FALSE;
	}

	return TRUE;
}


static const char *
iscsiAuthClientHeaderDigestOptionToText(int value)

{
	const char *s;

	switch (value) {

	case iscsiAuthOptionReject:
		s = iscsiAuthClientRejectOptionName;
		break;

	case iscsiAuthOptionNone:
		s = iscsiAuthClientNoneOptionName;
		break;

	case iscsiAuthHeaderDigestCrc32c:
		s = iscsiAuthClientHeaderDigestCrc32cOptionName;
		break;

	default:
		s = NULL;
	}

	return s;
}


static int
iscsiAuthClientCheckDataDigestOption(int value)

{
	if (value == iscsiAuthOptionNone ||
		value == iscsiAuthDataDigestCrc32c) {
		return FALSE;
	}

	return TRUE;
}


static const char *
iscsiAuthClientDataDigestOptionToText(int value)

{
	const char *s;

	switch (value) {

	case iscsiAuthOptionReject:
		s = iscsiAuthClientRejectOptionName;
		break;

	case iscsiAuthOptionNone:
		s = iscsiAuthClientNoneOptionName;
		break;

	case iscsiAuthDataDigestCrc32c:
		s = iscsiAuthClientDataDigestCrc32cOptionName;
		break;

	default:
		s = NULL;
	}

	return s;
}


static int
iscsiAuthClientCheckChapAlgorithmOption(int chapAlgorithm)

{
	if (chapAlgorithm == iscsiAuthOptionNone ||
		chapAlgorithm == iscsiAuthChapAlgorithmMd5) {
		return FALSE;
	}

	return TRUE;
}


static void
iscsiAuthClientDataToText(unsigned char *data, unsigned int length, char *text)

{
	*text++ = '0';
	*text++ = 'x';

	while (length > 0) {

		*text++ = iscsiAuthClientHexString[(*data >> 4) & 0xf];
		*text++ = iscsiAuthClientHexString[*data & 0xf];

		data++;
		length--;
	}

	*text++ = '\0';
}


static int
iscsiAuthClientTextToData(
	char *text, unsigned char *data, unsigned int *dataLength)

{
	char *p;
	unsigned int n1;
	unsigned int n2;
	unsigned int length = 0;

	if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		/* skip prefix */
		text += 2;
	} else {
		return 1; /* error, no '0x' prefix */
	}

	n1 = strlen(text);

	if (n1 == 0) return 1; /* error, no data */

	if (((n1 + 1) / 2) > *dataLength) {
		return 1; /* error, length too long */
	}

	if ((n1 % 2) == 1) {
		p = strchr(iscsiAuthClientHexString, *text++);
		if (!p) return 1; /* error, bad character */

		n2 = p - iscsiAuthClientHexString;
		if (n2 > 15) n2 -= 6;

		*data++ = n2;
		length++;
	}

	while (*text != '\0') {

		p = strchr(iscsiAuthClientHexString, *text++);
		if (!p) return 1; /* error, bad character */

		n1 = p - iscsiAuthClientHexString;
		if (n1 > 15) n1 -= 6;

		if (*text == '\0') return 1; /* error, odd string length */

		p = strchr(iscsiAuthClientHexString, *text++);
		if (!p) return 1; /* error, bad character */

		n2 = p - iscsiAuthClientHexString;
		if (n2 > 15) n2 -= 6;

		*data++ = (n1 << 4) | n2;
		length++;
	}

	*dataLength = length;

	return 0; /* no error */
}


static int
iscsiAuthClientChapComputeResponse(
	int passwordPresent,
	unsigned char *passwordData, unsigned int passwordLength,
	unsigned int id,
	unsigned char *challengeData, unsigned int challengeLength,
	unsigned char *responseData, unsigned int *responseLength)

{
	unsigned char idData[1];
	IscsiAuthMd5Context context;
	unsigned char outData[iscsiAuthDataMaxLength];
	unsigned int outLength = sizeof(outData);

	if (*responseLength < 16) {
		return 1; /* error, bad response length */
	}

	iscsiAuthMd5Init(&context);

	/* id byte */
	idData[0] = id;
	iscsiAuthMd5Update(&context, idData, 1);

	if (passwordPresent) {

		/* decrypt password */
		if (iscsiAuthClientData(
			outData, &outLength, passwordData, passwordLength)) {

			return 1; /* error, decrypt failed */
		}

	} else {

		outLength = 0;
	}

	/* shared secret */
	iscsiAuthMd5Update(&context, outData, outLength);

	/* clear decrypted password */
	memset(outData, 0, sizeof(outData));

	/* challenge value */
	iscsiAuthMd5Update(&context, challengeData, challengeLength);

	iscsiAuthMd5Final(responseData, &context);

	*responseLength = 16;

	return 0; /* no error */
}


static void
iscsiAuthClientCheckKey(
	int keyPresent,
	char *keyValue,
	int *negotiatedOption,
	unsigned int optionCount,
	int *optionList,
	const char *(*valueToText)(int))

{
	char option[iscsiAuthStringMaxLength];
	int length;
	unsigned int i;

	if (!keyPresent) {
		*negotiatedOption = iscsiAuthOptionNotPresent;
		return;
	}

	while (*keyValue != '\0') {

		length = 0;

		while (*keyValue != '\0' && *keyValue != ',') {
			option[length++] = *keyValue++;
		}

		if (*keyValue == ',') keyValue++;
		option[length++] = '\0';

		for (i = 0; i < optionCount; i++) {
			const char *s = (*valueToText)(optionList[i]);

			if (!s) continue;

			if (strcmp(option, s) == 0) {
				*negotiatedOption = optionList[i];
				return;
			}
		}
	}

	*negotiatedOption = iscsiAuthOptionReject;
}


static void
iscsiAuthClientSetKey(
	int *keyPresent,
	char *keyValue,
	unsigned int optionCount,
	int *optionList,
	const char *(*valueToText)(int))

{
	unsigned int i;

	if (optionCount == 0) {
		*keyPresent = FALSE;
		return;
	}

	if (optionCount == 1 && optionList[0] == iscsiAuthOptionNotPresent) {
		*keyPresent = FALSE;
		return;
	}

	for (i = 0; i < optionCount; i++) {
		const char *s = (*valueToText)(optionList[i]);

		if (!s) continue;

		if (i == 0) {
			strcpy(keyValue, s);
		} else {
			strcat(keyValue, ",");
			strcat(keyValue, s);
		}
	}

	*keyPresent = TRUE;
}


static void
iscsiAuthClientCheckAuthMethodKey(IscsiAuthClient *client)

{
	iscsiAuthClientCheckKey(
		client->recv[iscsiAuthKeyTypeAuthMethod].present,
		client->recv[iscsiAuthKeyTypeAuthMethod].value,
		&client->negotiatedAuthMethod,
		client->authMethodValidCount,
		client->authMethodValidList,
		iscsiAuthClientAuthMethodOptionToText);
}


static void
iscsiAuthClientSetAuthMethodKey(
	IscsiAuthClient *client,
	unsigned int authMethodCount,
	int *authMethodList)

{
	iscsiAuthClientSetKey(
		&client->send[iscsiAuthKeyTypeAuthMethod].present,
		client->send[iscsiAuthKeyTypeAuthMethod].value,
		authMethodCount,
		authMethodList,
		iscsiAuthClientAuthMethodOptionToText);
}


static void
iscsiAuthClientCheckHeaderDigestKey(IscsiAuthClient *client)

{
	iscsiAuthClientCheckKey(
		client->recv[iscsiAuthKeyTypeHeaderDigest].present,
		client->recv[iscsiAuthKeyTypeHeaderDigest].value,
		&client->negotiatedHeaderDigest,
		client->headerDigestCount,
		client->headerDigestList,
		iscsiAuthClientHeaderDigestOptionToText);
}


static void
iscsiAuthClientSetHeaderDigestKey(
	IscsiAuthClient *client,
	unsigned int headerDigestCount,
	int *headerDigestList)

{
	iscsiAuthClientSetKey(
		&client->send[iscsiAuthKeyTypeHeaderDigest].present,
		client->send[iscsiAuthKeyTypeHeaderDigest].value,
		headerDigestCount,
		headerDigestList,
		iscsiAuthClientHeaderDigestOptionToText);
}


static void
iscsiAuthClientCheckDataDigestKey(IscsiAuthClient *client)

{
	iscsiAuthClientCheckKey(
		client->recv[iscsiAuthKeyTypeDataDigest].present,
		client->recv[iscsiAuthKeyTypeDataDigest].value,
		&client->negotiatedDataDigest,
		client->dataDigestCount,
		client->dataDigestList,
		iscsiAuthClientDataDigestOptionToText);
}


static void
iscsiAuthClientSetDataDigestKey(
	IscsiAuthClient *client,
	unsigned int dataDigestCount,
	int *dataDigestList)

{
	iscsiAuthClientSetKey(
		&client->send[iscsiAuthKeyTypeDataDigest].present,
		client->send[iscsiAuthKeyTypeDataDigest].value,
		dataDigestCount,
		dataDigestList,
		iscsiAuthClientDataDigestOptionToText);
}


static void
iscsiAuthClientCheckChapAlgorithmKey(IscsiAuthClient *client)

{
	char *keyValue = client->recv[iscsiAuthKeyTypeChapAlgorithm].value;
	char option[iscsiAuthStringMaxLength];
	int length;
	long number;
	unsigned int i;

	if (!client->recv[iscsiAuthKeyTypeChapAlgorithm].present) {
		client->negotiatedChapAlgorithm = iscsiAuthOptionNotPresent;
		return;
	}

	while (*keyValue != '\0') {

		length = 0;

		while (*keyValue != '\0' && *keyValue != ',') {
			option[length++] = *keyValue++;
		}

		if (*keyValue == ',') keyValue++;
		option[length++] = '\0';

		if (iscsiAuthClientTextToNumber(option, &number)) continue;

		for (i = 0; i < client->chapAlgorithmCount; i++) {

			if (number == client->chapAlgorithmList[i]) {
				client->negotiatedChapAlgorithm = number;
				return;
			}
		}
	}

	client->negotiatedChapAlgorithm = iscsiAuthOptionReject;
}


static void
iscsiAuthClientSetChapAlgorithmKey(
	IscsiAuthClient *client,
	unsigned int chapAlgorithmCount,
	int *chapAlgorithmList)

{
	unsigned int i;

	if (chapAlgorithmCount == 1 &&
		((chapAlgorithmList[0] == iscsiAuthOptionNotPresent) ||
		(chapAlgorithmList[0] == iscsiAuthOptionReject))) {

		client->send[iscsiAuthKeyTypeChapAlgorithm].present = FALSE;
		return;
	}

	if (chapAlgorithmCount > 0) {

		for (i = 0; i < chapAlgorithmCount; i++) {
			char s[20];

			iscsiAuthClientNumberToText(chapAlgorithmList[i], s);

			if (i == 0) {
				strcpy(client->send[iscsiAuthKeyTypeChapAlgorithm].value, s);
			} else {
				strcat(client->send[iscsiAuthKeyTypeChapAlgorithm].value, ",");
				strcat(client->send[iscsiAuthKeyTypeChapAlgorithm].value, s);
			}
		}

		client->send[iscsiAuthKeyTypeChapAlgorithm].present = TRUE;

	} else {

		client->send[iscsiAuthKeyTypeChapAlgorithm].present = FALSE;
	}
}


static void
iscsiAuthClientSetSendTransitBit(IscsiAuthClient *client)

{
	client->transitBitSentFlag = TRUE;
	client->sendTransitBit = TRUE;
}


static void
iscsiAuthClientNextPhase(IscsiAuthClient *client)

{
	switch (client->phase) {

	case iscsiAuthPhaseConfigure:
		client->phase = iscsiAuthPhaseNegotiate;
		break;

	case iscsiAuthPhaseNegotiate:
		if (client->negotiatedAuthMethod == iscsiAuthOptionReject ||
			client->negotiatedAuthMethod == iscsiAuthOptionNotPresent ||
			client->negotiatedAuthMethod == iscsiAuthOptionNone) {

			client->localState = iscsiAuthLocalStateDone;
			client->remoteState = iscsiAuthRemoteStateDone;

			if (client->authRemote) {
				client->remoteAuthStatus = iscsiAuthStatusFail;
			} else {
				client->remoteAuthStatus = iscsiAuthStatusPass;
			}

			switch (client->negotiatedAuthMethod) {

			case iscsiAuthOptionReject:
				client->debugStatus =
					iscsiAuthDebugStatusAuthMethodReject;
				break;

			case iscsiAuthOptionNotPresent:
				client->debugStatus =
					iscsiAuthDebugStatusAuthMethodNotPresent;
				break;

			case iscsiAuthOptionNone:
				client->debugStatus =
					iscsiAuthDebugStatusAuthMethodNone;
			}

		} else if (client->negotiatedAuthMethod == iscsiAuthMethodChap) {

			client->localState = iscsiAuthLocalStateRecvChallenge;
			client->remoteState = iscsiAuthRemoteStateSendAlgorithm;

		} else {

			client->localState = iscsiAuthLocalStateDone;
			client->remoteState = iscsiAuthRemoteStateDone;
			client->remoteAuthStatus = iscsiAuthStatusFail;
			client->debugStatus = iscsiAuthDebugStatusAuthMethodBad;
		}

		client->phase = iscsiAuthPhaseAuthenticate;
		break;

	case iscsiAuthPhaseAuthenticate:
		client->phase = iscsiAuthPhaseDone;
		break;

	case iscsiAuthPhaseDone:
	case iscsiAuthPhaseError:
	default:
		client->phase = iscsiAuthPhaseError;
	}
}


static void
iscsiAuthClientLocalAuthentication(IscsiAuthClient *client)

{
	unsigned int identifier;
	unsigned char challengeData[iscsiAuthDataMaxLength];
	unsigned int challengeLength = sizeof(challengeData);
	unsigned char responseData[iscsiAuthDataMaxLength];
	unsigned int responseLength = sizeof(responseData);
	long number;
	int status;

	switch (client->localState) {

	case iscsiAuthLocalStateRecvChallenge:
		if (!client->recv[iscsiAuthKeyTypeChapIdentifier].present &&
			!client->recv[iscsiAuthKeyTypeChapChallenge].present) {

			/* do nothing */
			break;
		}

		if (!client->recv[iscsiAuthKeyTypeChapIdentifier].present) {
			client->localState = iscsiAuthLocalStateError;
			client->debugStatus = iscsiAuthDebugStatusChapIdentifierExpected;
			break;
		}

		if (!client->recv[iscsiAuthKeyTypeChapChallenge].present) {
			client->localState = iscsiAuthLocalStateError;
			client->debugStatus = iscsiAuthDebugStatusChapChallengeExpected;
			break;
		}

		status = iscsiAuthClientTextToNumber(
			 client->recv[iscsiAuthKeyTypeChapIdentifier].value, &number);

		if (status || (number < 0) || (255 < number)) {
			client->localState = iscsiAuthLocalStateError;
			client->debugStatus = iscsiAuthDebugStatusChapIdentifierBad;
			break;
		}
		identifier = number;

		status = iscsiAuthClientTextToData(
			client->recv[iscsiAuthKeyTypeChapChallenge].value,
			challengeData,
			&challengeLength);

		if (status) {
			client->localState = iscsiAuthLocalStateError;
			client->debugStatus = iscsiAuthDebugStatusChapChallengeBad;
			break;
		}

		status = iscsiAuthClientChapComputeResponse(
			client->passwordPresent,
			client->passwordData, client->passwordLength,
			identifier,
			challengeData, challengeLength,
			responseData, &responseLength);

		if (status) {
			client->localState = iscsiAuthLocalStateError;
			client->debugStatus =
				iscsiAuthDebugStatusChapComputeResponseFailed;
			break;
		}

		iscsiAuthClientDataToText(
			responseData,
			responseLength,
			client->send[iscsiAuthKeyTypeChapResponse].value);
		client->send[iscsiAuthKeyTypeChapResponse].present = TRUE;

		strcpy(
			client->send[iscsiAuthKeyTypeChapUsername].value,
			client->username);
		client->send[iscsiAuthKeyTypeChapUsername].present = TRUE;
		break;

	case iscsiAuthLocalStateDone:
		break;

	case iscsiAuthLocalStateError:
	default:
		client->phase = iscsiAuthPhaseError;
	}
}


static void
iscsiAuthClientRemoteAuthentication(IscsiAuthClient *client)

{
	unsigned char idData[1];
	unsigned char responseData[iscsiAuthDataMaxLength];
	unsigned int responseLength = sizeof(responseData);
	int authStatus;
	int status;

	switch (client->remoteState) {

	case iscsiAuthRemoteStateSendAlgorithm:
		if (client->nodeType == iscsiAuthNodeTypeInitiator) {

			iscsiAuthClientSetChapAlgorithmKey(
				client, client->chapAlgorithmCount, client->chapAlgorithmList);
			
			client->remoteState = iscsiAuthRemoteStateRecvAlgorithm;
			break;
		}

		/* Fall through */

	case iscsiAuthRemoteStateRecvAlgorithm:
		iscsiAuthClientCheckChapAlgorithmKey(client);

		if (client->nodeType == iscsiAuthNodeTypeTarget) {

			iscsiAuthClientSetChapAlgorithmKey(
				client, 1, &client->negotiatedChapAlgorithm);
		}

		/* Make sure only supported CHAP algorithm is used. */
		if (client->negotiatedChapAlgorithm != iscsiAuthChapAlgorithmMd5) {

			client->remoteState = iscsiAuthRemoteStateError;
			client->debugStatus = iscsiAuthDebugStatusChapAlgorithmBad;
			break;
		}

		if (!client->authRemote) {
			client->remoteAuthStatus = iscsiAuthStatusPass;
			client->debugStatus = iscsiAuthDebugStatusAuthRemoteFalse;
			client->remoteState = iscsiAuthRemoteStateDone;
			break;
		}

		/* Fall through */

	case iscsiAuthRemoteStateSendChallenge:
		iscsiAuthRandomSetData(idData, 1);
		client->chapIdentifier = idData[0];
		iscsiAuthClientNumberToText(
			client->chapIdentifier,
			client->send[iscsiAuthKeyTypeChapIdentifier].value);
		client->send[iscsiAuthKeyTypeChapIdentifier].present = TRUE;

		iscsiAuthRandomSetData(
			client->chapChallengeData, iscsiAuthChapChallengeLength);
		iscsiAuthClientDataToText(
			client->chapChallengeData, iscsiAuthChapChallengeLength,
			client->send[iscsiAuthKeyTypeChapChallenge].value);
		client->send[iscsiAuthKeyTypeChapChallenge].present = TRUE;

		client->remoteState = iscsiAuthRemoteStateRecvResponse;
		break;

	case iscsiAuthRemoteStateRecvResponse:
		if (!client->recv[iscsiAuthKeyTypeChapResponse].present) {
			client->remoteState = iscsiAuthRemoteStateError;
			client->debugStatus = iscsiAuthDebugStatusChapResponseExpected;
			break;
		}

		if (!client->recv[iscsiAuthKeyTypeChapUsername].present) {
			client->remoteState = iscsiAuthRemoteStateError;
			client->debugStatus = iscsiAuthDebugStatusChapUsernameExpected;
			break;
		}

		status = iscsiAuthClientTextToData(
			client->recv[iscsiAuthKeyTypeChapResponse].value,
			responseData,
			&responseLength);

		if (status) {
			client->remoteState = iscsiAuthRemoteStateError;
			client->debugStatus = iscsiAuthDebugStatusChapResponseBad;
			break;
		}

		authStatus = iscsiAuthClientChapAuthRequest(
			client,
			client->recv[iscsiAuthKeyTypeChapUsername].value,
			client->chapIdentifier,
			client->chapChallengeData, iscsiAuthChapChallengeLength,
			responseData, responseLength);

		if (authStatus == iscsiAuthStatusInProgress) {
			iscsiAuthClientGlobalStats.requestSent++;
			client->remoteState = iscsiAuthRemoteStateAuthRequest;
			break;
		}

		client->remoteAuthStatus = (IscsiAuthStatus)authStatus;
		client->authResponseFlag = TRUE;

		/* Fall through */

	case iscsiAuthRemoteStateAuthRequest:
		/* client->remoteAuthStatus already set */
		if (client->remoteAuthStatus == iscsiAuthStatusPass) {
			client->debugStatus = iscsiAuthDebugStatusAuthPass;
		} else if (client->remoteAuthStatus == iscsiAuthStatusFail) {
			client->debugStatus = iscsiAuthDebugStatusAuthFail;
		} else {
			client->remoteAuthStatus = iscsiAuthStatusFail;
			client->debugStatus = iscsiAuthDebugStatusAuthStatusBad;
		}
		client->remoteState = iscsiAuthRemoteStateDone;

		/* Fall through */

	case iscsiAuthRemoteStateDone:
		break;

	case iscsiAuthRemoteStateError:
	default:
		client->phase = iscsiAuthPhaseError;
	}
}


static void
iscsiAuthClientHandshake(IscsiAuthClient *client)

{
	if (client->phase == iscsiAuthPhaseDone) {

		/*
		 * Should only happen if authentication
		 * protocol error occured.
		 */
		return;
	}

	if (client->remoteState == iscsiAuthRemoteStateAuthRequest) {

		/*
		 * Defer until authentication response received
		 * from internal authentication service.
		 */
		return;
	}

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {

		/*
		 * Target should only have set T bit on response if
		 * initiator set it on previous command.
		 */
		if (client->recvTransitBit && !client->transitBitSentFlag) {

			client->remoteAuthStatus = iscsiAuthStatusFail;
			client->phase = iscsiAuthPhaseDone;
			client->debugStatus = iscsiAuthDebugStatusTbitSetIllegal;
			return;
		}
	}

	if (client->remoteState == iscsiAuthRemoteStateRecvResponse ||
		client->remoteState == iscsiAuthRemoteStateDone) {

		if (client->nodeType == iscsiAuthNodeTypeInitiator) {

			if (client->recvTransitBit) {
				if (client->remoteState != iscsiAuthRemoteStateDone) {
					goto recvTransitBitError;
				}
				iscsiAuthClientNextPhase(client);
			} else {
				iscsiAuthClientSetSendTransitBit(client);
			}

		} else {

			if (client->remoteState == iscsiAuthRemoteStateDone &&
				client->remoteAuthStatus != iscsiAuthStatusPass) {

				/*
				 * Authentication failed, don't do T bit handshake.
				 */
				iscsiAuthClientNextPhase(client);

			} else {

				/*
				 * Target can only set T bit on response if
				 * initiator set it on current command.
				 */
				if (client->recvTransitBit) {
					iscsiAuthClientSetSendTransitBit(client);
					iscsiAuthClientNextPhase(client);
				}
			}
		}

	} else {

		if (client->nodeType == iscsiAuthNodeTypeInitiator) {

			if (client->recvTransitBit) {
				goto recvTransitBitError;
			}
		}
	}

	return;

recvTransitBitError:
	/*
	 * Target set T bit on response but
	 * initiator was not done with authentication.
	 */
	client->remoteAuthStatus = iscsiAuthStatusFail;
	client->phase = iscsiAuthPhaseDone;
	client->debugStatus = iscsiAuthDebugStatusTbitSetPremature;
}


static int
iscsiAuthClientRecvEndStatus(IscsiAuthClient *client)

{
	int authStatus;

	if (client->phase == iscsiAuthPhaseError) {
		return iscsiAuthStatusError;
	}

	if (client->phase == iscsiAuthPhaseDone) {

		/* Perform sanity check against configured parameters. */

		if (client->authRemote && !client->authResponseFlag &&
			client->remoteAuthStatus == iscsiAuthStatusPass) {

			client->remoteAuthStatus = iscsiAuthStatusFail;
			client->debugStatus = iscsiAuthDebugStatusPassNotValid;
		}

		authStatus = client->remoteAuthStatus;

	} else if (client->remoteState == iscsiAuthRemoteStateAuthRequest) {

		authStatus = iscsiAuthStatusInProgress;

	} else {

		authStatus = iscsiAuthStatusContinue;
	}

	if (authStatus != iscsiAuthStatusInProgress) {
		client->recvInProgressFlag = FALSE;
	}

	return authStatus;
}


int
iscsiAuthClientRecvBegin(IscsiAuthClient *client)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase == iscsiAuthPhaseError) {
		return iscsiAuthStatusError;
	}

	if (client->phase == iscsiAuthPhaseDone) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (client->recvInProgressFlag) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	client->recvInProgressFlag = TRUE;

	if (client->phase == iscsiAuthPhaseConfigure) {
		iscsiAuthClientNextPhase(client);
	}

	client->recvTransitBit = FALSE;
	client->sendTransitBit = FALSE;
	memset(&client->recv, 0, sizeof(client->recv));
	memset(&client->send, 0, sizeof(client->send));

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientRecvEnd(
	IscsiAuthClient *client,
	IscsiAuthClientCallback *callback,
	void *userHandle,
	void *messageHandle)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase == iscsiAuthPhaseError) {
		return iscsiAuthStatusError;
	}

	if (!callback || !client->recvInProgressFlag) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (client->recvEndCount > iscsiAuthRecvEndMaxCount) {
		client->remoteAuthStatus = iscsiAuthStatusFail;
		client->phase = iscsiAuthPhaseDone;
		client->debugStatus = iscsiAuthDebugStatusMessageCountLimit;
	}

	client->recvEndCount++;

	client->callback = callback;
	client->userHandle = userHandle;
	client->messageHandle = messageHandle;

	switch (client->phase) {

	case iscsiAuthPhaseNegotiate:
		iscsiAuthClientCheckAuthMethodKey(client);
		iscsiAuthClientCheckHeaderDigestKey(client);
		iscsiAuthClientCheckDataDigestKey(client);

		if (client->nodeType == iscsiAuthNodeTypeTarget) {

			iscsiAuthClientSetAuthMethodKey(
				client, 1, &client->negotiatedAuthMethod);

			iscsiAuthClientSetHeaderDigestKey(
				client, 1, &client->negotiatedHeaderDigest);

			iscsiAuthClientSetDataDigestKey(
				client, 1, &client->negotiatedDataDigest);
		}

		if (client->nodeType == iscsiAuthNodeTypeInitiator) {
			iscsiAuthClientNextPhase(client);
		}
		break;

	case iscsiAuthPhaseAuthenticate:
	case iscsiAuthPhaseDone:
		break;

	default:
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	switch (client->phase) {

	case iscsiAuthPhaseNegotiate:
		if (client->nodeType == iscsiAuthNodeTypeTarget) {

			if (client->negotiatedAuthMethod == iscsiAuthOptionNotPresent &&
				client->negotiatedDataDigest == iscsiAuthOptionNotPresent &&
				client->negotiatedHeaderDigest == iscsiAuthOptionNotPresent &&
				!client->recvTransitBit) {

				break;
			}

			iscsiAuthClientNextPhase(client);
		}
		break;

	case iscsiAuthPhaseAuthenticate:
		iscsiAuthClientRemoteAuthentication(client);
		iscsiAuthClientLocalAuthentication(client);

		if (client->localState == iscsiAuthLocalStateError ||
			client->remoteState == iscsiAuthRemoteStateError) {

			client->remoteAuthStatus = iscsiAuthStatusFail;
			client->phase = iscsiAuthPhaseDone;
			/* client->debugStatus should already be set. */
		}
		break;

	case iscsiAuthPhaseDone:
		break;

	default:
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	iscsiAuthClientHandshake(client);

	return iscsiAuthClientRecvEndStatus(client);
}


void
iscsiAuthClientAuthResponse(IscsiAuthClient *client, int authStatus)

{
	iscsiAuthClientGlobalStats.responseReceived++;

	if (!client || client->signature != iscsiAuthClientSignature) {
		return;
	}

	if (!client->recvInProgressFlag ||
		client->phase != iscsiAuthPhaseAuthenticate ||
		client->remoteState != iscsiAuthRemoteStateAuthRequest) {

		client->phase = iscsiAuthPhaseError;
		return;
	}

	client->remoteAuthStatus = (IscsiAuthStatus)authStatus;
	client->authResponseFlag = TRUE;

	iscsiAuthClientRemoteAuthentication(client);

	iscsiAuthClientHandshake(client);

	authStatus = iscsiAuthClientRecvEndStatus(client);

	client->callback(
		client->userHandle, client->messageHandle, authStatus);
}


const char *
iscsiAuthClientGetKeyName(int keyType)

{
	if (keyType < iscsiAuthKeyTypeFirst ||
		keyType > iscsiAuthKeyTypeLast) {

		return NULL;
	}

	return iscsiAuthClientKeyInfo[keyType].name;
}


int
iscsiAuthClientGetNextKeyType(int *pKeyType)

{
	int keyType = *pKeyType;

	if (keyType >= iscsiAuthKeyTypeLast) {
		return iscsiAuthStatusError;
	}

	if (keyType < iscsiAuthKeyTypeFirst) {
		keyType = iscsiAuthKeyTypeFirst;
	} else {
		keyType++;
	}

	*pKeyType = keyType;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientRecvKeyValue(
	IscsiAuthClient *client, int keyType, const char *keyValue)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseNegotiate &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (keyType < iscsiAuthKeyTypeFirst ||
		keyType > iscsiAuthKeyTypeLast) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (iscsiAuthClientCheckString(keyValue)) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	strcpy(client->recv[keyType].value, keyValue);
	client->recv[keyType].present = TRUE;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSendKeyValue(
	IscsiAuthClient *client, int keyType, int *keyPresent, char *keyValue)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure &&
		client->phase != iscsiAuthPhaseNegotiate &&
		client->phase != iscsiAuthPhaseAuthenticate &&
		client->phase != iscsiAuthPhaseDone) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (keyType < iscsiAuthKeyTypeFirst ||
		keyType > iscsiAuthKeyTypeLast) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (client->send[keyType].present) {
		strcpy(keyValue, client->send[keyType].value);
	}
	*keyPresent = client->send[keyType].present;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientRecvTransitBit(
	IscsiAuthClient *client, int value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseNegotiate &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	if (value) {
		client->recvTransitBit = TRUE;
	} else {
		client->recvTransitBit = FALSE;
	}

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSendTransitBit(
	IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	*value = client->sendTransitBit;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientInit(IscsiAuthClient *client, IscsiAuthNodeType nodeType)

{
	int valueList[2];

	if (!client) {
		return iscsiAuthStatusError;
	}

	memset(client, 0, sizeof(*client));

	if (iscsiAuthClientCheckNodeType(nodeType)) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	client->signature = iscsiAuthClientSignature;
	client->nodeType = nodeType;
	client->authRemote = TRUE;

	client->phase = iscsiAuthPhaseConfigure;
	client->negotiatedAuthMethod = iscsiAuthOptionNotPresent;
	client->negotiatedHeaderDigest = iscsiAuthOptionNotPresent;
	client->negotiatedDataDigest = iscsiAuthOptionNotPresent;
	client->negotiatedChapAlgorithm = iscsiAuthOptionNotPresent;

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {
		iscsiAuthClientSetSendTransitBit(client);
	}

	valueList[0] = iscsiAuthMethodChap;
	valueList[1] = iscsiAuthOptionNone;

	/* Must call after setting authRemote and password. */
	if (iscsiAuthClientSetAuthMethodList(client, 2, valueList) !=
		iscsiAuthStatusNoError) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	valueList[0] = iscsiAuthOptionNone;

	if (iscsiAuthClientSetHeaderDigestList(client, 1, valueList) !=
		iscsiAuthStatusNoError) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	valueList[0] = iscsiAuthOptionNone;

	if (iscsiAuthClientSetDataDigestList(client, 1, valueList) !=
		iscsiAuthStatusNoError) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	valueList[0] = iscsiAuthChapAlgorithmMd5;

	if (iscsiAuthClientSetChapAlgorithmList(client, 1, valueList) !=
		iscsiAuthStatusNoError) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientFinish(IscsiAuthClient *client)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	iscsiAuthClientChapAuthCancel(client);

	memset(client, 0, sizeof(*client));

	return iscsiAuthStatusNoError;
}


static int
iscsiAuthClientSetOptionList(
	IscsiAuthClient *client,
	unsigned int optionCount,
	const int *optionList,
	unsigned int *clientOptionCount,
	int *clientOptionList,
	unsigned int optionMaxCount,
	int (*checkOption)(int))

{
	unsigned int i;

	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure ||
		optionCount > optionMaxCount) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	for (i = 0; i < optionCount; i++) {
		if ((*checkOption)(optionList[i])) {
			client->phase = iscsiAuthPhaseError;
			return iscsiAuthStatusError;
		}
	}

	for (i = 0; i < optionCount; i++) {
		clientOptionList[i] = optionList[i];
	}

	*clientOptionCount = optionCount;

	return iscsiAuthStatusNoError;
}


static void
iscsiAuthClientSetAuthMethodValid(IscsiAuthClient *client)

{
	unsigned int i;
	unsigned int j = 0;
	int option = 0;

	/*
	 * Following checks may need to be revised if
	 * authentication options other than CHAP and none
	 * are supported.
	 */

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {

		if (client->authRemote) {
			/*
			 * If initiator doing authentication,
			 * don't offer authentication option none.
			 */
			option = 1;
		} else if (!client->passwordPresent) {
			/*
			 * If initiator password not set,
			 * only offer authentication option none.
			 */
			option = 2;
		}
	}

	if (client->nodeType == iscsiAuthNodeTypeTarget) {

		if (client->authRemote) {
			/*
			 * If target doing authentication,
			 * don't accept authentication option none.
			 */
			option = 1;
		} else {
			/*
			 * If target not doing authentication,
			 * only accept authentication option none.
			 */
			option = 2;
		}
	}

	for (i = 0; i < client->authMethodCount; i++) {

		if (option == 1) {
			if (client->authMethodList[i] == iscsiAuthOptionNone) {
				continue;
			}
		} else if (option == 2) {
			if (client->authMethodList[i] != iscsiAuthOptionNone) {
				continue;
			}
		}

		client->authMethodValidList[j++] = client->authMethodList[i];
	}

	client->authMethodValidCount = j;

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {
		iscsiAuthClientSetAuthMethodKey(
			client, client->authMethodValidCount, client->authMethodValidList);
	}
}


int
iscsiAuthClientSetAuthMethodList(
	IscsiAuthClient *client,
	unsigned int optionCount,
	const int *optionList)

{
	int status;

	status = iscsiAuthClientSetOptionList(
		client,
		optionCount,
		optionList,
		&client->authMethodCount,
		client->authMethodList,
		iscsiAuthMethodMaxCount,
		iscsiAuthClientCheckAuthMethodOption);

	if (status != iscsiAuthStatusNoError) {
		return status;
	}

	/* Setting authMethod affects authMethodValid. */
	iscsiAuthClientSetAuthMethodValid(client);

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetHeaderDigestList(
	IscsiAuthClient *client,
	unsigned int optionCount,
	const int *optionList)

{
	int status;

	status = iscsiAuthClientSetOptionList(
		client,
		optionCount,
		optionList,
		&client->headerDigestCount,
		client->headerDigestList,
		iscsiAuthHeaderDigestMaxCount,
		iscsiAuthClientCheckHeaderDigestOption);

	if (status != iscsiAuthStatusNoError) {
		return status;
	}

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {

		iscsiAuthClientSetHeaderDigestKey(
			client, client->headerDigestCount, client->headerDigestList);
	}

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetDataDigestList(
	IscsiAuthClient *client,
	unsigned int optionCount,
	const int *optionList)

{
	int status;

	status = iscsiAuthClientSetOptionList(
		client,
		optionCount,
		optionList,
		&client->dataDigestCount,
		client->dataDigestList,
		iscsiAuthDataDigestMaxCount,
		iscsiAuthClientCheckDataDigestOption);

	if (status != iscsiAuthStatusNoError) {
		return status;
	}

	if (client->nodeType == iscsiAuthNodeTypeInitiator) {

		iscsiAuthClientSetDataDigestKey(
			client, client->dataDigestCount, client->dataDigestList);
	}

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetChapAlgorithmList(
	IscsiAuthClient *client,
	unsigned int optionCount,
	const int *optionList)

{
	return iscsiAuthClientSetOptionList(
		client,
		optionCount,
		optionList,
		&client->chapAlgorithmCount,
		client->chapAlgorithmList,
		iscsiAuthChapAlgorithmMaxCount,
		iscsiAuthClientCheckChapAlgorithmOption);
}


int
iscsiAuthClientSetUsername(IscsiAuthClient *client, const char *username)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure ||
		iscsiAuthClientCheckString(username)) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	strcpy(client->username, username);

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetPassword(
	IscsiAuthClient *client,
	const unsigned char *passwordData,
	unsigned int passwordLength)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure ||
		passwordLength > sizeof(client->passwordData)) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	memcpy(client->passwordData, passwordData, passwordLength);
	client->passwordLength = passwordLength;
	client->passwordPresent = TRUE;

	/* Setting password may affect authMethodValid. */
	iscsiAuthClientSetAuthMethodValid(client);

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetAuthRemote(IscsiAuthClient *client, int authRemote)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure) {
		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	client->authRemote = authRemote;

	/* Setting authRemote may affect authMethodValid. */
	iscsiAuthClientSetAuthMethodValid(client);

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetGlueHandle(IscsiAuthClient *client, void *glueHandle)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure &&
		client->phase != iscsiAuthPhaseNegotiate &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	client->glueHandle = glueHandle;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientSetListName(IscsiAuthClient *client, const char *listName)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseConfigure ||
		iscsiAuthClientCheckString(listName)) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	strcpy(client->listName, listName);

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetAuthPhase(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	*value = client->phase;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetAuthStatus(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->remoteAuthStatus;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetAuthMethod(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->negotiatedAuthMethod;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetHeaderDigest(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->negotiatedHeaderDigest;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetDataDigest(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone &&
		client->phase != iscsiAuthPhaseAuthenticate) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->negotiatedDataDigest;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetChapAlgorithm(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->negotiatedChapAlgorithm;

	return iscsiAuthStatusNoError;
}


int
iscsiAuthClientGetDebugStatus(IscsiAuthClient *client, int *value)

{
	if (!client || client->signature != iscsiAuthClientSignature) {
		return iscsiAuthStatusError;
	}

	if (client->phase != iscsiAuthPhaseDone) {

		client->phase = iscsiAuthPhaseError;
		return iscsiAuthStatusError;
	}

	*value = client->debugStatus;

	return iscsiAuthStatusNoError;
}


const char *
iscsiAuthClientDebugStatusToText(int debugStatus)

{
	const char *s;

	switch (debugStatus) {

	case iscsiAuthDebugStatusNotSet:
		s = "Debug status not set";
		break;

	case iscsiAuthDebugStatusAuthMethodNotPresent:
		s = "AuthMethod key not present";
		break;

	case iscsiAuthDebugStatusAuthMethodReject:
		s = "AuthMethod negotiation failed";
		break;

	case iscsiAuthDebugStatusAuthMethodNone:
		s = "AuthMethod negotiated to none";
		break;

	case iscsiAuthDebugStatusAuthMethodBad:
		s = "AuthMethod bad";
		break;

	case iscsiAuthDebugStatusAuthRemoteFalse:
		s = "Authentication not enabled";
		break;

	case iscsiAuthDebugStatusAuthPass:
		s = "Authentication request passed";
		break;

	case iscsiAuthDebugStatusAuthFail:
		s = "Authentication request failed";
		break;

	case iscsiAuthDebugStatusAuthStatusBad:
		s = "Authentication request status bad";
		break;

	case iscsiAuthDebugStatusChapAlgorithmBad:
		s = "CHAP algorithm bad";
		break;

	case iscsiAuthDebugStatusChapResponseExpected:
		s = "CHAP response expected";
		break;

	case iscsiAuthDebugStatusChapUsernameExpected:
		s = "CHAP username expected";
		break;

	case iscsiAuthDebugStatusChapIdentifierExpected:
		s = "CHAP identifier expected";
		break;

	case iscsiAuthDebugStatusChapChallengeExpected:
		s = "CHAP challenge expected";
		break;

	case iscsiAuthDebugStatusChapResponseBad:
		s = "CHAP response bad";
		break;

	case iscsiAuthDebugStatusChapIdentifierBad:
		s = "CHAP identifier bad";
		break;

	case iscsiAuthDebugStatusChapChallengeBad:
		s = "CHAP challenge bad";
		break;

	case iscsiAuthDebugStatusChapComputeResponseFailed:
		s = "CHAP compute response failed";
		break;

	case iscsiAuthDebugStatusTbitSetIllegal:
		s = "T bit set on response, but not on previous command";
		break;

	case iscsiAuthDebugStatusTbitSetPremature:
		s = "T bit set on response, but authenticaton not complete";
		break;

	case iscsiAuthDebugStatusMessageCountLimit:
		s = "message count limit reached";
		break;

	case iscsiAuthDebugStatusPassNotValid:
		s = "pass status not valid";
		break;

	default:
		s = "Unknown error";
	}

	return s;
}
