/*
 * llc_s_st.c - Defines SAP component state machine transitions.
 *
 * The followed transitions are SAP component state machine transitions
 * which are described in 802.2 LLC protocol standard document.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 *		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/types.h>
#include <net/llc_if.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_st.h>

/* dummy last-transition indicator; common to all state transition groups */
/* last entry for this state */
/* all members are zeros, .bss zeroes it */
static struct llc_sap_state_trans llc_sap_state_trans_n;

/* state LLC_SAP_STATE_INACTIVE transition for LLC_SAP_EV_ACTIVATION_REQ event */
static llc_sap_action_t llc_sap_inactive_state_actions_1[] = {
	llc_sap_action_report_status,
	NULL
};

static struct llc_sap_state_trans llc_sap_inactive_state_trans_1 = {
	llc_sap_ev_activation_req,	LLC_SAP_STATE_ACTIVE,
					llc_sap_inactive_state_actions_1
};

/* array of pointers; one to each transition */
static struct llc_sap_state_trans *llc_sap_inactive_state_transitions[] = {
	&llc_sap_inactive_state_trans_1,
	&llc_sap_state_trans_n
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_RX_UI event */
static llc_sap_action_t llc_sap_active_state_actions_1[] = {
	llc_sap_action_unitdata_ind,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_1 = {
	llc_sap_ev_rx_ui,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_1
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_UNITDATA_REQ event */
static llc_sap_action_t llc_sap_active_state_actions_2[] = {
	llc_sap_action_send_ui,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_2 = {
	llc_sap_ev_unitdata_req,	LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_2
};


/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_XID_REQ event */
static llc_sap_action_t llc_sap_active_state_actions_3[] = {
	llc_sap_action_send_xid_c,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_3 = {
	llc_sap_ev_xid_req,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_3
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_RX_XID_C event */
static llc_sap_action_t llc_sap_active_state_actions_4[] = {
	llc_sap_action_send_xid_r,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_4 = {
	llc_sap_ev_rx_xid_c,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_4
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_RX_XID_R event */
static llc_sap_action_t llc_sap_active_state_actions_5[] = {
	llc_sap_action_xid_ind,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_5 = {
	llc_sap_ev_rx_xid_r,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_5
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_TEST_REQ event */
static llc_sap_action_t llc_sap_active_state_actions_6[] = {
	llc_sap_action_send_test_c,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_6 = {
	llc_sap_ev_test_req,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_6
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_RX_TEST_C event */
static llc_sap_action_t llc_sap_active_state_actions_7[] = {
	llc_sap_action_send_test_r,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_7 = {
	llc_sap_ev_rx_test_c,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_7
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_RX_TEST_R event */
static llc_sap_action_t llc_sap_active_state_actions_8[] = {
	llc_sap_action_test_ind,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_8 = {
	llc_sap_ev_rx_test_r,		LLC_SAP_STATE_ACTIVE,
					llc_sap_active_state_actions_8
};

/* state LLC_SAP_STATE_ACTIVE transition for LLC_SAP_EV_DEACTIVATION_REQ event */
static llc_sap_action_t llc_sap_active_state_actions_9[] = {
	llc_sap_action_report_status,
	NULL
};

static struct llc_sap_state_trans llc_sap_active_state_trans_9 = {
	llc_sap_ev_deactivation_req,	LLC_SAP_STATE_INACTIVE,
					llc_sap_active_state_actions_9
};

/* array of pointers; one to each transition */
static struct llc_sap_state_trans *llc_sap_active_state_transitions[] = {
	&llc_sap_active_state_trans_2,
	&llc_sap_active_state_trans_1,
	&llc_sap_active_state_trans_3,
	&llc_sap_active_state_trans_4,
	&llc_sap_active_state_trans_5,
	&llc_sap_active_state_trans_6,
	&llc_sap_active_state_trans_7,
	&llc_sap_active_state_trans_8,
	&llc_sap_active_state_trans_9,
	&llc_sap_state_trans_n
};

/* SAP state transition table */
struct llc_sap_state llc_sap_state_table[] = {
	{ LLC_SAP_STATE_INACTIVE, llc_sap_inactive_state_transitions },
	{ LLC_SAP_STATE_ACTIVE,	  llc_sap_active_state_transitions   }
};
