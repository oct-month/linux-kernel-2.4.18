#include "iscsi-protocol.h"
#include "iscsi-login.h"

struct IscsiLoginHdr *iscsi_align_login_pdu(iscsi_session_t *session, unsigned char *buffer, int buffersize)
{
    struct IscsiLoginHdr *login_header;
    unsigned long addr = (unsigned long)buffer;

    /* find a buffer location guaranteed to be reasonably aligned for the header */
    addr += (addr % sizeof(*login_header));
    login_header = (struct IscsiLoginHdr *)addr;

    return login_header;
}


/* caller is assumed to be well-behaved and passing NUL terminated strings */
int iscsi_add_login_text(iscsi_session_t *session, struct IscsiLoginHdr *login_header, int max_pdu_length, char *param, char *value)
{
    int param_len = strlen(param);
    int value_len = strlen(value);
    int length = param_len + 1 + value_len + 1; /* param, separator, value, and trailing NUL */
    int pdu_length = ntoh24(login_header->dlength);
    char *text = (char *)(login_header + 1);
    char *end = (char *)login_header + max_pdu_length;
    char *pdu_text;
    
    /* find the end of the current text */
    text += pdu_length;
    pdu_text = text;
    pdu_length += length;

    if (text + length >= end) {
        printk("iSCSI: failed to add login text '%s:%s'\n", param, value);
        return 0;
    }

    /* param */
    iscsi_strncpy(text, param, param_len);
    text += param_len;

    /* separator */
    *text++ = ISCSI_TEXT_SEPARATOR;

    /* value */
    strncpy(text, value, value_len);
    text += value_len;
    
    /* NUL */
    *text++ = '\0';

    /* update the length in the PDU header */
    hton24(login_header->dlength, pdu_length);
    DEBUG_FLOW2("iSCSI: added text '%s', length %d\n", pdu_text, length);

    return 1;
}

int iscsi_find_key_value(char *param, char *pdu, char *pdu_end, char **value_start, char **value_end)
{
    char *str = param;
    char *text = pdu;
    char *value = NULL;

    if (value_start)
        *value_start = NULL;
    if (value_end)
        *value_end = NULL;
        
    /* make sure they contain the same bytes */
    while (*str) {
        if (text >= pdu_end) 
            return 0;
        if (*text == '\0')
            return 0;
        if (*str != *text)
            return 0;
        str++;
        text++;
    }
    
    if ((text >= pdu_end) || (*text == '\0') || (*text != ISCSI_TEXT_SEPARATOR)) {
        return 0;
    }

    /* find the value */
    value = text + 1;

    /* find the end of the value */
    while ((text < pdu_end) && (*text))
        text++;

    if (value_start)
        *value_start = value;
    if (value_end)
        *value_end = text;

    return 1;
}

/* we never authenticate the target, so we don't need a functional AuthClient callback */
static void null_callback(void *user_handle, void *message_handle, int auth_status)
{
}

/* this assumes the text data is always NUL terminated.  The caller can always arrange for that
 * by using a slightly larger buffer than the max PDU size, and then appending a NUL to the PDU.
 */
int iscsi_process_login_response(iscsi_session_t *session, struct IscsiLoginRspHdr *login_rsp_pdu, char *buffer_end)
{
    int tbit = login_rsp_pdu->tbit;
    char *text = (char *)(login_rsp_pdu + 1);
    char *end;

    end = text + ntoh24(login_rsp_pdu->dlength) + 1; 
    if (end >= buffer_end) {
        printk("iSCSI: process_login_response - buffer too small to guarantee NUL termination\n");
        return 0;
    }
    /* guarantee a trailing NUL */
    *end = '\0';

    /* validate the response */
    if (login_rsp_pdu->opcode != ISCSI_OP_LOGIN_RSP) {
        printk("iSCSI: incorrect opcode (0x%x) in Login response\n", login_rsp_pdu->opcode);
        return 0;
    }
    if (login_rsp_pdu->active_version != ISCSI_MAX_VERSION) {
        printk("iSCSI: incompatible version (0x%x) in Login response\n", login_rsp_pdu->active_version);
        return 0;
    }

    /* check the response status */
    switch(login_rsp_pdu->status_class) {
        case STATUS_CLASS_SUCCESS:
            break;
        case STATUS_CLASS_REDIRECT:
            printk("iSCSI: session %p login rejected: redirection not supported\n", session);
            /* FIXME: handle TargetAddress keys specifying where to go instead.  
             * This may require name resolution.
             */
            return 0;
        case STATUS_CLASS_INITIATOR_ERR:
            switch (login_rsp_pdu->status_detail) {
                case ISCSI_LOGIN_STATUS_AUTH_FAILED:
                    printk("iSCSI: login rejected: initiator failed authentication with target %s\n",
                           session->TargetName);
                    break;
                default:
                    printk("iSCSI: session %p login rejected: initiator error (0x%02x)\n",
                           session, login_rsp_pdu->status_detail);
                    break;
            }
            return 0;
        case STATUS_CLASS_TARGET_ERR:
            printk("iSCSI: session %p login rejected: target error (0x%02x)\n",
                   session, login_rsp_pdu->status_detail);
            return 0;
        default:
            printk("iSCSI: session %p login response with unknown status class 0x%x, detail 0x%x\n",
                   session, login_rsp_pdu->status_class, login_rsp_pdu->status_detail);
            return 0;
    }

    /* make sure the current phase matches */
    if (login_rsp_pdu->curr != session->current_phase) {
        printk("iSCSI: error - current phase mismatch, session %d, response %d\n", 
               session->current_phase, login_rsp_pdu->curr);
        return 0;
    }
    /* and that we're actually advancing if the Transit bit is set */
    if (login_rsp_pdu->tbit && (login_rsp_pdu->next <= session->current_phase)) {
        printk("iSCSI: error - in phase %d, target wants to advance to phase %d, but we want to go to phase %d\n",
               session->current_phase, login_rsp_pdu->next, session->next_phase);
        return 0;
    }

    if (session->current_phase == ISCSI_SECURITY_NEGOTIATION_PHASE) {
        if (iscsiAuthClientRecvBegin(session->auth_client) != iscsiAuthStatusNoError) {
            printk("iSCSI: authClientRecvBegin failed\n");
            return 0;
        }
        
        if (iscsiAuthClientRecvTransitBit(session->auth_client, tbit) != iscsiAuthStatusNoError) {
            printk("iSCSI: authClientRecvTransitBit failed\n");
            return 0;
        }
    }
        

    /* scan the text data */
 more_text:
    while (text && (text < end)) {
        char *value = NULL;
        char *value_end = NULL;

        /* skip any NULs separating each text key,value pair */
        while ((text < end) && (*text == '\0'))
            text++;
        if (text >= end)
            break;

        /* handle keys appropriate for each phase */
        switch (session->current_phase) {
            case ISCSI_SECURITY_NEGOTIATION_PHASE: {
                int keytype = iscsiAuthKeyTypeNone;
    
                while (iscsiAuthClientGetNextKeyType(&keytype) == iscsiAuthStatusNoError) {
                    char *key = (char *)iscsiAuthClientGetKeyName(keytype);
                    
                    if (key && iscsi_find_key_value(key, text, end, &value, &value_end)) {
                        if (iscsiAuthClientRecvKeyValue(session->auth_client, keytype, value) != iscsiAuthStatusNoError) {
                            printk("iSCSI: authentication failed to process %s\n", text);
                            return 0;
                        }
                        text = value_end;
                        goto more_text;
                    }
                }

                printk("iSCSI: security phase failed to recognize text %s\n", text);
                return 0;
            }
            case ISCSI_OP_PARMS_NEGOTIATION_PHASE: {
                if (iscsi_find_key_value("TargetAlias", text, end, &value, &value_end)) {
                    size_t size = sizeof(session->TargetAlias);

                    if ((value_end - value) < size)
                        size = value_end - value;

                    memcpy(session->TargetAlias, value, size);
                    session->TargetAlias[sizeof(session->TargetAlias)-1] = '\0';
                    session->log_name = session->TargetAlias;
                    mb();
                    text = value_end;
                }
                else if (iscsi_find_key_value("TargetAddress", text, end, &value, &value_end)) {
                    /* FIXME: we don't support redirection yet */
                    printk("iSCSI: session %p can't accept %s, redirection not supported\n", session, text);
                    return 0;
                }
                else if (iscsi_find_key_value("InitialR2T", text, end, &value, &value_end)) {
                    if (value && iscsi_strcmp(value, "yes") == 0)
                        session->InitialR2T = 1;
                    else
                        session->InitialR2T = 0;
                    DEBUG_INIT2("iSCSI: session %p InitialR2T %d\n", session, session->InitialR2T);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("ImmediateData", text, end, &value, &value_end)) {
                    if (value && iscsi_strcmp(value, "yes") == 0)
                        session->ImmediateData = 1;
                    else
                        session->ImmediateData = 0;
                    DEBUG_INIT2("iSCSI: session %p ImmediateData %d\n", session, session->ImmediateData);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("DataPDULength", text, end, &value, &value_end)) {
                    session->DataPDULength = iscsi_strtoul(value, NULL, 0);
                    DEBUG_INIT2("iSCSI: session %p DataPDULength %d\n", session, session->DataPDULength);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("FirstBurstSize", text, end, &value, &value_end)) {
                    session->FirstBurstSize = iscsi_strtoul(value, NULL, 0);
                    DEBUG_INIT2("iSCSI: session %p FirstBurstSize %d\n", session, session->FirstBurstSize);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("MaxBurstSize", text, end, &value, &value_end)) {
                    /* we don't really care, since it's a limit on the target's R2Ts, but record it anwyay */
                    session->MaxBurstSize = iscsi_strtoul(value, NULL, 0);
                    DEBUG_INIT2("iSCSI: session %p MaxBurstSize %d\n", session, session->MaxBurstSize);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("FMarker", text, end, &value, &value_end)) {
                    if (iscsi_strcmp(value, "no")) {
                        printk("iSCSI: session %p can't accept %s\n", session, text);
                        return 0;
                    }
                    text = value_end;
                } 
                else if (iscsi_find_key_value("RFMarkInt", text, end, &value, &value_end)) {
                    /* we don't do markers */
                    printk("iSCSI: session %p can't accept %s\n", session, text);
                    return 0;
                } 
                else if (iscsi_find_key_value("SFMarkInt", text, end, &value, &value_end)) {
                    /* we don't do markers */
                    printk("iSCSI: session %p can't accept %s\n", session, text);
                    return 0;
                } 
                else if (iscsi_find_key_value("BidiInitialR2T", text, end, &value, &value_end)) {
                    /* Linux doesn't use any Bidi commands, so we don't care */
                    text = value_end;
                } 
                else if (iscsi_find_key_value("LogoutLoginMaxTime", text, end, &value, &value_end)) {
                    /* FIXME: we ought to do something with this */
                    /* FIXME: if the target sends this, we need to respond */
                    text = value_end;
                } 
                else if (iscsi_find_key_value("DataPDUInOrder", text, end, &value, &value_end)) {
                    if (value && iscsi_strcmp(value, "yes") == 0)
                        session->DataPDUInOrder = 1;
                    else
                        session->DataPDUInOrder = 0;
                    DEBUG_INIT2("iSCSI: session %p DataPDUInOrder %d\n", session, session->DataPDUInOrder);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("DataSequenceInOrder", text, end, &value, &value_end)) {
                    if (value && iscsi_strcmp(value, "yes") == 0)
                        session->DataSequenceInOrder = 1;
                    else
                        session->DataSequenceInOrder = 0;
                    DEBUG_INIT2("iSCSI: session %p DataSequenceInOrder %d\n", session, session->DataSequenceInOrder);
                    text = value_end;
                } 
                else if (iscsi_find_key_value("MaxOutstandingR2T", text, end, &value, &value_end)) {
                    if (iscsi_strcmp(value, "1")) {
                        printk("iSCSI: session %p can't accept MaxOutstandingR2T %s\n", session, value);
                        return 0;
                    }
                    text = value_end;
                } 
                else if (iscsi_find_key_value("MaxConnections", text, end, &value, &value_end)) {
                    if (iscsi_strcmp(value, "1")) {
                        printk("iSCSI: session %p can't accept MaxConnections %s\n", session, value);
                        return 0;
                    }
                    text = value_end;
                } 
                else if (iscsi_find_key_value("ErrorRecoveryLevel", text, end, &value, &value_end)) {
                    if (iscsi_strcmp(value, "0")) {
                        printk("iSCSI: session %p can't accept ErrorRecovery %s\n", session, value);
                        return 0;
                    }
                    text = value_end;
                } 
                else if (iscsi_find_key_value("X-com.cisco.iscsi.draft", text, end, &value, &value_end)) {
                    if (iscsi_strcmp(value, "NotUnderstood") && iscsi_strcmp(value, "8")) {
                        /* if it has a draft, and it doesn't match ours, fail */
                        printk("iSCSI: can't accept draft %s\n", value);
                        return 0;
                    }
                    text = value_end;
                } 
                else {
                    /* FIXME: we may want to ignore X- keys sent by
                     * the target, but that would require us to have
                     * another PDU buffer so that we can generate a
                     * response while we still know what keys we
                     * received, so that we can reply with a
                     * NotUnderstood response.  For now, reject logins
                     * with keys we don't understand.
                     */
                    printk("iSCSI: failed to recognize text %s\n", text);
                    return 0;
                }
                break;
            }
            default:
                return 0;
        }
    }

    if (session->current_phase == ISCSI_SECURITY_NEGOTIATION_PHASE) {
        switch (iscsiAuthClientRecvEnd(session->auth_client, null_callback, NULL, NULL)) {
            default:
            case iscsiAuthStatusNoError: /* treat this as an error, since we should get a different code */
            case iscsiAuthStatusError:
                printk("iSCSI: session %p error authenticating with target %s\n", session, session->TargetName);
                return 0;

            case iscsiAuthStatusInProgress:
                /* this should only occur if we were authenticating the target,
                 * which we never do, so treat this as an error.
                 */
                printk("iSCSI: session %p error authenticating target %s\n", session, session->TargetName);
                return 0;
                
            case iscsiAuthStatusContinue:
                /* continue sending PDUs */
                break;

            case iscsiAuthStatusPass:
                printk("iSCSI: session %p authenticated by target %s\n", session, session->TargetName);
                break;
            case iscsiAuthStatusFail:
                printk("iSCSI: session %p failed authentication with target %s\n", session, session->TargetName);
                break;
        }
    }

    /* record some of the PDU fields for later use */
    session->tsid = ntohs(login_rsp_pdu->tsid);
    session->ExpStatSn = ntohl(login_rsp_pdu->statsn);
    session->ExpCmdSn = ntohl(login_rsp_pdu->expcmdsn);
    session->MaxCmdSn = ntohl(login_rsp_pdu->maxcmdsn);

    if (login_rsp_pdu->tbit) {
        /* advance to the next phase */
        session->partial_response = 0;
        session->current_phase = login_rsp_pdu->next;
    }
    else {
        /* we got a partial response, don't advance, more negotiation to do */
        session->partial_response = 1;
    }

    return 1;
}


int iscsi_fill_login_pdu(iscsi_session_t *session, struct IscsiLoginHdr *login_pdu, int max_pdu_length)
{
    int tbit = 0;
    char value[iscsiAuthStringMaxLength];

    /* initialize the PDU header */
    memset(login_pdu, 0, sizeof(*login_pdu));
    login_pdu->opcode = ISCSI_OP_LOGIN_CMD | ISCSI_OP_IMMEDIATE;
    login_pdu->min_version = ISCSI_MIN_VERSION;
    login_pdu->max_version = ISCSI_MAX_VERSION;
    login_pdu->cid = 0;
    login_pdu->isid = htons(session->isid);
    login_pdu->tsid = 0;
    login_pdu->cmdsn = htonl(session->CmdSn); /* don't increment on immediate */
    login_pdu->expstatsn = htonl(session->ExpStatSn);

    /* the very first Login PDU has some additional requirements, 
     * and we need to decide what phase to start in.
     */
    if (session->current_phase == ISCSI_INITIAL_LOGIN_PHASE) {
        if (session->InitiatorName && session->InitiatorName[0]) {
            if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "InitiatorName", session->InitiatorName))
                return 0;
        }
        if (session->InitiatorAlias && session->InitiatorAlias[0]) {
            if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "InitiatorAlias", session->InitiatorAlias))
                return 0;
        }
        
        if (session->TargetName[0] != '\0') {
            if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "TargetName", session->TargetName))
                return 0;
        }
        else {
            printk("iSCSI: session %p has no TargetName\n", session);
            return 0;
        }

        if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "SessionType",
                                  (session->type == ISCSI_SESSION_TYPE_DISCOVERY) ? "discovery" : "normal"))
            return 0;
        
        if (session->auth_client) {
            /* we're prepared to do authentication */
            session->current_phase = session->next_phase = ISCSI_SECURITY_NEGOTIATION_PHASE;
        }
        else {
            /* can't do any authentication, skip that phase */
            session->current_phase = session->next_phase = ISCSI_OP_PARMS_NEGOTIATION_PHASE;
        }        
    }
                
    /* fill in text based on the phase */
    switch (session->current_phase) {
        case ISCSI_OP_PARMS_NEGOTIATION_PHASE: {
            if (!session->partial_response) {
                /* request the desired settings the first time we are in this phase */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "InitialR2T", 
                                          session->desired_InitialR2T ? "yes" : "no"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "ImmediateData", 
                                          session->desired_ImmediateData ? "yes" : "no"))
                    return 0;
                iscsi_sprintf(value, "%d", session->desired_DataPDULength);
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "DataPDULength", value))
                    return 0;
                iscsi_sprintf(value, "%d", session->desired_FirstBurstSize);
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "FirstBurstSize", value))
                    return 0;
                
                /* these we must have */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "MaxOutstandingR2T", "1"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "MaxConnections", "1"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "ErrorRecoveryLevel", "0"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "FMarker", "no"))
                    return 0;

                /* these we don't really care about these, but send
                 * them anyway, so that we're always the request
                 * sender, and we don't need to check to see if the
                 * target initiated a negotiation of these options and
                 * respond appropriately.
                 */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "DataPDUInOrder", "yes"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "DataSequenceInOrder", "yes"))
                    return 0;
                /* match the InitialR2T setting, in case the OS someday starts issuing Bidi commands */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "BidiInitialR2T",
                                          session->desired_InitialR2T ? "yes" : "no"))
                    return 0;
                
                /* vendor-specific draft level specification */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "X-com.cisco.iscsi.draft", "8"))
                    return 0;
                
                /* try to go to full feature phase */
                login_pdu->curr = session->current_phase = ISCSI_OP_PARMS_NEGOTIATION_PHASE;
                login_pdu->next = session->next_phase    = ISCSI_FULL_FEATURE_PHASE;
                login_pdu->tbit = 1;
            }
            else {
                /* echo back the current negotiable settings, and request the next phase */
                /* FIXME: can we always echo everything, or do we have to only send
                 * the ones we just received from the target?  There may be some cases
                 * where we are required to send a response with no text, just a phase change.
                 */
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "InitialR2T", 
                                          session->InitialR2T ? "yes" : "no"))
                    return 0;
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "ImmediateData", 
                                          session->ImmediateData ? "yes" : "no"))
                    return 0;
                iscsi_sprintf(value, "%d", session->DataPDULength);
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "DataPDULength", value))
                    return 0;
                iscsi_sprintf(value, "%d", session->FirstBurstSize);
                if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, "FirstBurstSize", value))
                    return 0;
                
                /* try to go to full feature phase */
                login_pdu->curr = session->current_phase;
                login_pdu->next = ISCSI_FULL_FEATURE_PHASE;
                login_pdu->tbit = 1;
            }
            break;
        }
        case ISCSI_SECURITY_NEGOTIATION_PHASE: {
            int keytype = iscsiAuthKeyTypeNone;
            int rc = iscsiAuthClientSendTransitBit(session->auth_client, &tbit);

            /* see if we're ready for a phase change */
            if (rc == iscsiAuthStatusNoError) {
                login_pdu->tbit = tbit;
                if (tbit)
                    /* for discovery sessions, try to go right to full feature phase, not op param phase */
                    if (session->type == ISCSI_SESSION_TYPE_DISCOVERY)
                        login_pdu->next = session->next_phase = ISCSI_FULL_FEATURE_PHASE;
                    else
                        login_pdu->next = session->next_phase = ISCSI_OP_PARMS_NEGOTIATION_PHASE;
                else
                    session->next_phase = ISCSI_SECURITY_NEGOTIATION_PHASE;
            }
            else
                return 0;
            
            /* enumerate all the keys the auth code might want to send */
            while (iscsiAuthClientGetNextKeyType(&keytype) == iscsiAuthStatusNoError) {
                int present = 0;
                char *key = (char *)iscsiAuthClientGetKeyName(keytype);
                
                /* add the key/value pairs the auth code wants to send */
                rc = iscsiAuthClientSendKeyValue(session->auth_client, keytype, &present, value);
                if ((rc == iscsiAuthStatusNoError) && present) {
                    if (!iscsi_add_login_text(session, login_pdu, max_pdu_length, key, value))
                        return 0;
                }
            }

            break;
        }
        case ISCSI_FULL_FEATURE_PHASE:
            printk("iSCSI: can't send login PDUs in full feature phase\n");
            return 0;
        default:
            printk("iSCSI: can't send login PDUs in unknown phase %d\n", session->current_phase);
            return 0;
    }
    
    return 1;
}


/* login to the target(s) */
int iscsi_login(iscsi_session_t *session, char *buffer, size_t bufsize)
{
    int received_pdu = 0;

    /* prepare the session */
    session->CmdSn = 1;
    session->ExpCmdSn = 1;
    session->MaxCmdSn = 1;
    session->ExpStatSn = 0;

    session->current_phase = ISCSI_INITIAL_LOGIN_PHASE;
    session->partial_response = 0;

    if (session->auth_client) {
        /* prepare for authentication */
        if (iscsiAuthClientInit(session->auth_client, iscsiAuthNodeTypeInitiator) != iscsiAuthStatusNoError) {
            printk("iSCSI: couldn't init auth_client %p for session %p\n", session->auth_client, session);
            return 0;
        }

        if (iscsiAuthClientSetUsername(session->auth_client, session->username) != iscsiAuthStatusNoError) {
            printk("iSCSI: couldn't set username for session %p\n", session);
            return 0;
        }

        if (iscsiAuthClientSetPassword(session->auth_client, session->password, session->password_length) != iscsiAuthStatusNoError) {
            printk("iSCSI: couldn't set password for session %p\n", session);
            return 0;
        }
        
        if (iscsiAuthClientSetAuthRemote(session->auth_client, 0) != iscsiAuthStatusNoError) {
            printk("iSCSI: couldn't set auth remote for session %p\n", session);
            return 0;
        }
    }

    /* exchange PDUs until the login phase is complete, or an error occurs */
    do {
        struct IscsiLoginHdr *login_pdu = NULL;
        struct IscsiLoginRspHdr *login_rsp_pdu = NULL;
        char *end;
        int max_pdu_length;
        int timeout = 0;

        DEBUG_INIT3("iSCSI: beginning login for session %p, buffer %p, bufsize %d\n", 
                    session, buffer, bufsize);

        memset(buffer, 0, bufsize);
        login_pdu = iscsi_align_login_pdu(session, buffer, bufsize);
        if (!login_pdu)
            return 0;

        end = buffer + bufsize;
        max_pdu_length = end - (char *)login_pdu;
        
        /* fill in the PDU header and text data based on the login phase that we're in */
        if (!iscsi_fill_login_pdu(session, login_pdu, max_pdu_length))
            return 0;

        /* send a PDU to the target */
        if (!iscsi_send_login_pdu(session, login_pdu, max_pdu_length))
            return 0;
        
        /* read the target's response into the same buffer */
        memset(buffer, 0, bufsize);
        login_rsp_pdu = (struct IscsiLoginRspHdr *)login_pdu;
        
        /* pick the appropriate timeout. If we know the target has
         * responded before, and we're in the security phase, we use a
         * longer timeout, since the authentication alogorithms can
         * take a while, especially if the target has to go talk to a
         * tacacs or RADIUS server (which may or may not be
         * responding).
         */
        if (received_pdu && (session->current_phase == ISCSI_SECURITY_NEGOTIATION_PHASE))
            timeout = session->auth_timeout;
        else
            timeout = session->login_timeout;
        
        if (!iscsi_recv_login_pdu(session, login_rsp_pdu, max_pdu_length, timeout))
            return 0;

        received_pdu = 1;
        /* process the target's response */
        if (!iscsi_process_login_response(session, login_rsp_pdu, session->RxBuf + sizeof(session->RxBuf)))
            return 0;
        
    } while (session->current_phase != ISCSI_FULL_FEATURE_PHASE);

    if (session->auth_client) {
        if (iscsiAuthClientFinish(session->auth_client) != iscsiAuthStatusNoError) {
            printk("iSCSI: error finishing authentication\n");
            return 0;
        }
    }
    
    DEBUG_INIT1("iSCSI: session %p entering full-feature phase\n", session);

    /* convert from 512-byte blocks to bytes on the numeric operational params */
    session->DataPDULength <<= 9;
    session->FirstBurstSize <<= 9;
    session->MaxBurstSize <<= 9;
    
    return 1;
}
