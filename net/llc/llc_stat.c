/*
 * llc_stat.c - Implementation of LLC station component state machine
 * 		transitions
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
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
#include <net/llc_sap.h>
#include <net/llc_evnt.h>
#include <net/llc_actn.h>
#include <net/llc_stat.h>

/* ------------------- COMMON STATION STATE transitions ------------------ */

/* dummy last-transition indicator; common to all state transition groups */
/* last entry for this state */
/* all members are zeros, .bss zeroes it */
static struct llc_station_state_trans llc_stat_state_trans_n;

/* ------------------------ DOWN STATE transitions ----------------------- */

/* state transition for LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_1[] = {
	llc_station_ac_start_ack_timer,
	llc_station_ac_set_retry_cnt_0,
	llc_station_ac_set_xid_r_cnt_0,
	llc_station_ac_send_null_dsap_xid_c,
	NULL
};

static struct llc_station_state_trans llc_stat_down_state_trans_1 = {
	llc_stat_ev_enable_with_dup_addr_check,
					LLC_STATION_STATE_DUP_ADDR_CHK,
					llc_stat_down_state_actions_1
};

/* state transition for LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_2[] = {
	llc_station_ac_report_status,	/* STATION UP */
	NULL
};

static struct llc_station_state_trans llc_stat_down_state_trans_2 = {
	llc_stat_ev_enable_without_dup_addr_check,
					LLC_STATION_STATE_UP,
					llc_stat_down_state_actions_2
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dwn_state_trans[] = {
	&llc_stat_down_state_trans_1,
	&llc_stat_down_state_trans_2,
	&llc_stat_state_trans_n
};

/* ------------------------- UP STATE transitions ------------------------ */
/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_up_state_actions_1[] = {
	llc_station_ac_report_status,	/* STATION DOWN */
	NULL
};

static struct llc_station_state_trans llc_stat_up_state_trans_1 = {
	llc_stat_ev_disable_req,	LLC_STATION_STATE_DOWN,
					llc_stat_up_state_actions_1
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_up_state_actions_2[] = {
	llc_station_ac_send_xid_r,
	NULL
};

static struct llc_station_state_trans llc_stat_up_state_trans_2 = {
	llc_stat_ev_rx_null_dsap_xid_c,	LLC_STATION_STATE_UP,
					llc_stat_up_state_actions_2
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_TEST_C event */
static llc_station_action_t llc_stat_up_state_actions_3[] = {
	llc_station_ac_send_test_r,
	NULL
};

static struct llc_station_state_trans llc_stat_up_state_trans_3 = {
	llc_stat_ev_rx_null_dsap_test_c,	LLC_STATION_STATE_UP,
					llc_stat_up_state_actions_3
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_up_state_trans [] = {
	&llc_stat_up_state_trans_1,
	&llc_stat_up_state_trans_2,
	&llc_stat_up_state_trans_3,
	&llc_stat_state_trans_n
};

/* ---------------------- DUP ADDR CHK STATE transitions ----------------- */
/* state transition for LLC_STATION_EV_RX_NULL_DSAP_0_XID_R_XID_R_CNT_EQ
 * event */
static llc_station_action_t llc_stat_dupaddr_state_actions_1[] = {
	llc_station_ac_inc_xid_r_cnt_by_1,
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_1 = {
	llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq,
					LLC_STATION_STATE_DUP_ADDR_CHK,
					llc_stat_dupaddr_state_actions_1
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_1_XID_R_XID_R_CNT_EQ
 * event */
static llc_station_action_t llc_stat_dupaddr_state_actions_2[] = {
	llc_station_ac_report_status,	/* DUPLICATE ADDRESS FOUND */
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_2 = {
	llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq,
					LLC_STATION_STATE_DOWN,
					llc_stat_dupaddr_state_actions_2
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_dupaddr_state_actions_3[] = {
	llc_station_ac_send_xid_r,
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_3 = {
	llc_stat_ev_rx_null_dsap_xid_c,	LLC_STATION_STATE_DUP_ADDR_CHK,
					llc_stat_dupaddr_state_actions_3
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_LT_RETRY_CNT_MAX_RETRY
 * event */
static llc_station_action_t llc_stat_dupaddr_state_actions_4[] = {
	llc_station_ac_start_ack_timer,
	llc_station_ac_inc_retry_cnt_by_1,
	llc_station_ac_set_xid_r_cnt_0,
	llc_station_ac_send_null_dsap_xid_c,
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_4 = {
	llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry,
					LLC_STATION_STATE_DUP_ADDR_CHK,
					llc_stat_dupaddr_state_actions_4
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_EQ_RETRY_CNT_MAX_RETRY
 * event */
static llc_station_action_t llc_stat_dupaddr_state_actions_5[] = {
	llc_station_ac_report_status,	/* STATION UP */
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_5 = {
	llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry,
					LLC_STATION_STATE_UP,
					llc_stat_dupaddr_state_actions_5
};

/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_dupaddr_state_actions_6[] = {
	llc_station_ac_report_status,	/* STATION DOWN */
	NULL
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_6 = {
	llc_stat_ev_disable_req,	LLC_STATION_STATE_DOWN,
					llc_stat_dupaddr_state_actions_6
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dupaddr_state_trans[] = {
	&llc_stat_dupaddr_state_trans_6,	/* Request */
	&llc_stat_dupaddr_state_trans_4,	/* Timer */
	&llc_stat_dupaddr_state_trans_5,
	&llc_stat_dupaddr_state_trans_1,	/* Receive frame */
	&llc_stat_dupaddr_state_trans_2,
	&llc_stat_dupaddr_state_trans_3,
	&llc_stat_state_trans_n
};

struct llc_station_state llc_station_state_table[LLC_NBR_STATION_STATES] = {
	{ LLC_STATION_STATE_DOWN,	  llc_stat_dwn_state_trans },
	{ LLC_STATION_STATE_DUP_ADDR_CHK, llc_stat_dupaddr_state_trans },
	{ LLC_STATION_STATE_UP,		  llc_stat_up_state_trans }
};
