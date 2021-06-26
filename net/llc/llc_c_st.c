/*
 * llc_c_st.c - This module contains state transition of connection component.
 *
 * Description of event functions and actions there is in 802.2 LLC standard,
 * or in "llc_c_ac.c" and "llc_c_ev.c" modules.
 *
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
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>

#define LLC_NO_EVENT_QUALIFIERS   NULL
#define LLC_NO_TRANSITION_ACTIONS NULL

/* ----------------- COMMON CONNECTION STATE transitions ----------------- *
 * Common transitions for
 * LLC_CONN_STATE_NORMAL,
 * LLC_CONN_STATE_BUSY,
 * LLC_CONN_STATE_REJ,
 * LLC_CONN_STATE_AWAIT,
 * LLC_CONN_STATE_AWAIT_BUSY and
 * LLC_CONN_STATE_AWAIT_REJ states
 */
/* State transitions for LLC_CONN_EV_DISC_REQ event */
static llc_conn_action_t llc_common_actions_1[] = {
	llc_conn_ac_send_disc_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_1 = {
	llc_conn_ev_disc_req,
	LLC_CONN_STATE_D_CONN,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_1
};

/* State transitions for LLC_CONN_EV_RESET_REQ event */
static llc_conn_action_t llc_common_actions_2[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_2 = {
	llc_conn_ev_rst_req,
	LLC_CONN_STATE_RESET,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_2
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_common_actions_3[] = {
	llc_conn_ac_stop_all_timers,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_rst_ind,
	llc_conn_ac_set_p_flag_0,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_reset,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_3 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_3
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_common_actions_4[] = {
	llc_conn_ac_stop_all_timers,
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_disc_ind,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_4 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_4
};

/* State transitions for LLC_CONN_EV_RX_FRMR_RSP_Fbit_SET_X event */
static llc_conn_action_t llc_common_actions_5[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_rst_ind,
	llc_conn_ac_set_cause_flag_0,
	llc_conn_reset,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_5 = {
	llc_conn_ev_rx_frmr_rsp_fbit_set_x,
	LLC_CONN_STATE_RESET,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_5
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static llc_conn_action_t llc_common_actions_6[] = {
	llc_conn_ac_disc_ind,
	llc_conn_ac_stop_all_timers,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_6 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_6
};

/* State transitions for LLC_CONN_EV_RX_ZZZ_CMD_Pbit_SET_X_INVAL_Nr event */
static llc_conn_action_t llc_common_actions_7a[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_7a = {
	llc_conn_ev_rx_zzz_cmd_pbit_set_x_inval_nr,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_7a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_X_INVAL_Ns event */
static llc_conn_action_t llc_common_actions_7b[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_7b = {
	llc_conn_ev_rx_i_cmd_pbit_set_x_inval_ns,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_7b
};

/* State transitions for LLC_CONN_EV_RX_ZZZ_RSP_Fbit_SET_X_INVAL_Nr event */
static llc_conn_action_t llc_common_actions_8a[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_8a = {
	llc_conn_ev_rx_zzz_rsp_fbit_set_x_inval_nr,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_8a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_INVAL_Ns event */
static llc_conn_action_t llc_common_actions_8b[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_8b = {
	llc_conn_ev_rx_i_rsp_fbit_set_x_inval_ns,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_8b
};

/* State transitions for LLC_CONN_EV_RX_BAD_PDU event */
static llc_conn_action_t llc_common_actions_8c[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_8c = {
	llc_conn_ev_rx_bad_pdu,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_8c
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event */
static llc_conn_action_t llc_common_actions_9[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_9 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_common_actions_9
};

/* State transitions for LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_1 event */
#if 0
static llc_conn_ev_qfyr_t llc_common_ev_qfyrs_10[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_common_actions_10[] = {
	llc_conn_ac_send_frmr_rsp_f_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_10 = {
	llc_conn_ev_rx_xxx_rsp_fbit_set_1,
	LLC_CONN_STATE_ERROR,
	llc_common_ev_qfyrs_10,
	llc_common_actions_10
};
#endif

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11a[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	NULL
};

static llc_conn_action_t llc_common_actions_11a[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_11a = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_common_ev_qfyrs_11a,
	llc_common_actions_11a
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11b[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	NULL
};

static llc_conn_action_t llc_common_actions_11b[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_11b = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_common_ev_qfyrs_11b,
	llc_common_actions_11b
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11c[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	NULL
};

static llc_conn_action_t llc_common_actions_11c[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_11c = {
	llc_conn_ev_rej_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_common_ev_qfyrs_11c,
	llc_common_actions_11c
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11d[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	NULL
};

static llc_conn_action_t llc_common_actions_11d[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_stop_other_timers,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_common_state_trans_11d = {
	llc_conn_ev_busy_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_common_ev_qfyrs_11d,
	llc_common_actions_11d
};

/*
 * Common dummy state transition; must be last entry for all state
 * transition groups - it'll be on .bss, so will be zeroed.
 */
static struct llc_conn_state_trans llc_common_state_trans_n;

/* --------------------- LLC_CONN_STATE_ADM transitions -------------------- */
/* State transitions for LLC_CONN_EV_CONN_REQ event */
static llc_conn_action_t llc_adm_actions_1[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_s_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_adm_state_trans_1 = {
	llc_conn_ev_conn_req,
	LLC_CONN_STATE_SETUP,
	LLC_NO_EVENT_QUALIFIERS,
	llc_adm_actions_1
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_adm_actions_2[] = {
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_p_flag_0,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_ac_conn_ind,
	NULL
};

static struct llc_conn_state_trans llc_adm_state_trans_2 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_adm_actions_2
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_adm_actions_3[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_adm_state_trans_3 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_adm_actions_3
};

/* State transitions for LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_adm_actions_4[] = {
	llc_conn_ac_send_dm_rsp_f_set_1,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_adm_state_trans_4 = {
	llc_conn_ev_rx_xxx_cmd_pbit_set_1,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_adm_actions_4
};

/* State transitions for LLC_CONN_EV_RX_XXX_YYY event */
static llc_conn_action_t llc_adm_actions_5[] = {
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_adm_state_trans_5 = {
	llc_conn_ev_rx_any_frame,
	LLC_CONN_OUT_OF_SVC,
	LLC_NO_EVENT_QUALIFIERS,
	llc_adm_actions_5
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_adm_state_transitions[] = {
	&llc_adm_state_trans_1,		/* Request */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* local_busy */
	&llc_common_state_trans_n,	/* init_pf_cycle */
	&llc_common_state_trans_n,	/* timer */
	&llc_adm_state_trans_2,		/* Receive frame */
	&llc_adm_state_trans_3,
	&llc_adm_state_trans_4,
	&llc_adm_state_trans_5,
	&llc_common_state_trans_n
};

/* ---------------------  LLC_CONN_STATE_SETUP transitions ----------------- */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_setup_actions_1[] = {
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_set_s_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_1 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_SETUP,
	LLC_NO_EVENT_QUALIFIERS,
	llc_setup_actions_1
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	llc_conn_ev_qlfy_set_status_conn,
	NULL
};

static llc_conn_action_t llc_setup_actions_2[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_ac_conn_confirm,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_2 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_setup_ev_qfyrs_2,
	llc_setup_actions_2
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_s_flag_eq_1,
	llc_conn_ev_qlfy_set_status_conn,
	NULL
};

static llc_conn_action_t llc_setup_actions_3[] = {
	llc_conn_ac_set_p_flag_0,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_ac_conn_confirm,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_3 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_NORMAL,
	llc_setup_ev_qfyrs_3,
	llc_setup_actions_3
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_setup_actions_4[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_conn_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_4 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_setup_ev_qfyrs_4,
	llc_setup_actions_4
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_5[] = {
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_setup_actions_5[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_conn_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_5 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_setup_ev_qfyrs_5,
	llc_setup_actions_5
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_7[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	llc_conn_ev_qlfy_s_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_setup_actions_7[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_7 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_SETUP,
	llc_setup_ev_qfyrs_7,
	llc_setup_actions_7
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_8[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	llc_conn_ev_qlfy_s_flag_eq_0,
	llc_conn_ev_qlfy_set_status_failed,
	NULL
};

static llc_conn_action_t llc_setup_actions_8[] = {
	llc_conn_ac_conn_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_setup_state_trans_8 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ADM,
	llc_setup_ev_qfyrs_8,
	llc_setup_actions_8
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_setup_state_transitions[] = {
	&llc_common_state_trans_n,	/* Request */
	&llc_common_state_trans_n,	/* local busy */
	&llc_common_state_trans_n,	/* init_pf_cycle */
	&llc_setup_state_trans_3,	/* Timer */
	&llc_setup_state_trans_7,
	&llc_setup_state_trans_8,
	&llc_common_state_trans_n,
	&llc_setup_state_trans_1,	/* Receive frame */
	&llc_setup_state_trans_2,
	&llc_setup_state_trans_4,
	&llc_setup_state_trans_5,
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_NORMAL transitions ------------------ */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_last_frame_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_1[] = {
	llc_conn_ac_send_i_as_ack,
	llc_conn_ac_start_ack_tmr_if_not_running,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_1,
	llc_normal_actions_1
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_last_frame_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_2[] = {
	llc_conn_ac_send_i_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_2 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_2,
	llc_normal_actions_2
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_2_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_1,
	llc_conn_ev_qlfy_set_status_remote_busy,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_normal_actions_2_1[1];

static struct llc_conn_state_trans llc_normal_state_trans_2_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_2_1,
	llc_normal_actions_2_1
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_3[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_3 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_BUSY,
	llc_normal_ev_qfyrs_3,
	llc_normal_actions_3
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_4[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_4 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_BUSY,
	llc_normal_ev_qfyrs_4,
	llc_normal_actions_4
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_5a[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_5a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_normal_ev_qfyrs_5a,
	llc_normal_actions_5a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_5b[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_5b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_normal_ev_qfyrs_5b,
	llc_normal_actions_5b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_5c[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_5c = {
	llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_normal_ev_qfyrs_5c,
	llc_normal_actions_5c
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_6a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_6a[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_6a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_normal_ev_qfyrs_6a,
	llc_normal_actions_6a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_6b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_6b[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_6b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_normal_ev_qfyrs_6b,
	llc_normal_actions_6b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_normal_actions_7[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rej_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_7 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_7
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_8a[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_normal_actions_8[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	llc_conn_ac_send_ack_if_needed,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_8a = {
	llc_conn_ev_rx_i_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_8a,
	llc_normal_actions_8
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_8b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_8b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_8b,
	llc_normal_actions_8
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_9a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_9a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_ack_if_needed,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_9a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_9a,
	llc_normal_actions_9a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_9b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_9b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_ack_if_needed,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_9b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_9b,
	llc_normal_actions_9b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_normal_actions_10[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_send_ack_rsp_f_set_1,
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_data_ind,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_10 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_10
};

/* State transitions for * LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_normal_actions_11a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_11a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_11a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_normal_actions_11b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_11b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_11b
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_11c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_11c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_inc_tx_win_size,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_11c = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_11c,
	llc_normal_actions_11c
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_normal_actions_12[] = {
	llc_conn_ac_send_ack_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_adjust_npta_by_rr,
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_12 = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_12
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_normal_actions_13a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_13a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_13a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_normal_actions_13b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_13b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_13b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_13c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_13c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_13c = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_13c,
	llc_normal_actions_13c
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_normal_actions_14[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_adjust_npta_by_rnr,
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_14 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_14
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_15a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_15a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_dec_tx_win_size,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_15a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_15a,
	llc_normal_actions_15a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_15b[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_normal_actions_15b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_dec_tx_win_size,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_15b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_15b,
	llc_normal_actions_15b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_16a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_16a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_dec_tx_win_size,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_16a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_16a,
	llc_normal_actions_16a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_16b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_normal_actions_16b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_dec_tx_win_size,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_16b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_16b,
	llc_normal_actions_16b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_normal_actions_17[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_dec_tx_win_size,
	llc_conn_ac_resend_i_rsp_f_set_1,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_17 = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_normal_actions_17
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_18[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_18[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_18 = {
	llc_conn_ev_init_p_f_cycle,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_18,
	llc_normal_actions_18
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_19[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_normal_actions_19[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_rst_vs,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_19 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT,
	llc_normal_ev_qfyrs_19,
	llc_normal_actions_19
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_20a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_normal_actions_20a[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_rst_vs,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_20a = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_AWAIT,
	llc_normal_ev_qfyrs_20a,
	llc_normal_actions_20a
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_20b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_normal_actions_20b[] = {
	llc_conn_ac_rst_sendack_flag,
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_rst_vs,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_20b = {
	llc_conn_ev_busy_tmr_exp,
	LLC_CONN_STATE_AWAIT,
	llc_normal_ev_qfyrs_20b,
	llc_normal_actions_20b
};

/* State transitions for LLC_CONN_EV_TX_BUFF_FULL event */
static llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_21[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_normal_actions_21[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_normal_state_trans_21 = {
	llc_conn_ev_tx_buffer_full,
	LLC_CONN_STATE_NORMAL,
	llc_normal_ev_qfyrs_21,
	llc_normal_actions_21
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_normal_state_transitions[] = {
	&llc_normal_state_trans_1,	/* Requests */
	&llc_normal_state_trans_2,
	&llc_normal_state_trans_2_1,
	&llc_common_state_trans_1,
	&llc_common_state_trans_2,
	&llc_common_state_trans_n,
	&llc_normal_state_trans_21,
	&llc_normal_state_trans_3,	/* Local busy */
	&llc_normal_state_trans_4,
	&llc_common_state_trans_n,
	&llc_normal_state_trans_18,	/* Init pf cycle */
	&llc_common_state_trans_n,
	&llc_common_state_trans_11a,	/* Timers */
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_normal_state_trans_19,
	&llc_normal_state_trans_20a,
	&llc_normal_state_trans_20b,
	&llc_common_state_trans_n,
	&llc_normal_state_trans_8b,	/* Receive frames */
	&llc_normal_state_trans_9b,
	&llc_normal_state_trans_10,
	&llc_normal_state_trans_11b,
	&llc_normal_state_trans_11c,
	&llc_normal_state_trans_5a,
	&llc_normal_state_trans_5b,
	&llc_normal_state_trans_5c,
	&llc_normal_state_trans_6a,
	&llc_normal_state_trans_6b,
	&llc_normal_state_trans_7,
	&llc_normal_state_trans_8a,
	&llc_normal_state_trans_9a,
	&llc_normal_state_trans_11a,
	&llc_normal_state_trans_12,
	&llc_normal_state_trans_13a,
	&llc_normal_state_trans_13b,
	&llc_normal_state_trans_13c,
	&llc_normal_state_trans_14,
	&llc_normal_state_trans_15a,
	&llc_normal_state_trans_15b,
	&llc_normal_state_trans_16a,
	&llc_normal_state_trans_16b,
	&llc_normal_state_trans_17,
	&llc_common_state_trans_3,
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/*&llc_common_state_trans_10, */
	&llc_common_state_trans_n
};

/* --------------------- LLC_CONN_STATE_BUSY transitions ------------------- */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_1[] = {
	llc_conn_ac_send_i_xxx_x_set_0,
	llc_conn_ac_start_ack_tmr_if_not_running,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_1,
	llc_busy_actions_1
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_2[] = {
	llc_conn_ac_send_i_xxx_x_set_0,
	llc_conn_ac_start_ack_tmr_if_not_running,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_2 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_2,
	llc_busy_actions_2
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_2_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_1,
	llc_conn_ev_qlfy_set_status_remote_busy,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_busy_actions_2_1[1];

static struct llc_conn_state_trans llc_busy_state_trans_2_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_2_1,
	llc_busy_actions_2_1
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_data_flag_eq_1,
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_3[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_3 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_REJ,
	llc_busy_ev_qfyrs_3,
	llc_busy_actions_3
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_data_flag_eq_1,
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_4[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_4 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_REJ,
	llc_busy_ev_qfyrs_4,
	llc_busy_actions_4
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_5[] = {
	llc_conn_ev_qlfy_data_flag_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_5[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_5 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_NORMAL,
	llc_busy_ev_qfyrs_5,
	llc_busy_actions_5
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_6[] = {
	llc_conn_ev_qlfy_data_flag_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_6[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_6 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_NORMAL,
	llc_busy_ev_qfyrs_6,
	llc_busy_actions_6
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_7[] = {
	llc_conn_ev_qlfy_data_flag_eq_2,
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_7[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_7 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_REJ,
	llc_busy_ev_qfyrs_7,
	llc_busy_actions_7
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_8[] = {
	llc_conn_ev_qlfy_data_flag_eq_2,
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_8[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_8 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_REJ,
	llc_busy_ev_qfyrs_8,
	llc_busy_actions_8
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_9a[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_busy_actions_9a[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_9a = {
	llc_conn_ev_rx_i_rsp_fbit_set_x_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_9a,
	llc_busy_actions_9a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_9b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_9b[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_9b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_9b,
	llc_busy_actions_9b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_10a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_10a[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_10a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_10a,
	llc_busy_actions_10a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_10b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_10b[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_10b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_10b,
	llc_busy_actions_10b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_busy_actions_11[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_11 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_11
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_busy_actions_12[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_12 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_12
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_13a[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_busy_actions_13a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	llc_conn_ac_set_data_flag_0,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_13a = {
	llc_conn_ev_rx_i_rsp_fbit_set_x,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_13a,
	llc_busy_actions_13a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_13b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_13b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	llc_conn_ac_set_data_flag_0,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_13b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_13b,
	llc_busy_actions_13b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_14a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_14a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_14a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_14a,
	llc_busy_actions_14a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_14b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_14b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_14b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_14b,
	llc_busy_actions_14b
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_busy_actions_15a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_15a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_15a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_busy_actions_15b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_15b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_15b
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_15c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_15c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_15c = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_15c,
	llc_busy_actions_15c
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_busy_actions_16[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_16 = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_16
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_busy_actions_17a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_17a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_17a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_busy_actions_17b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_17b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_17b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_17c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_17c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_17c = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_17c,
	llc_busy_actions_17c
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_busy_actions_18[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_18 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_18
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_19a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_19a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_19a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_19a,
	llc_busy_actions_19a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_19b[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_busy_actions_19b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_19b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_x,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_19b,
	llc_busy_actions_19b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_20a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_20a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_20a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_20a,
	llc_busy_actions_20a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_20b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_busy_actions_20b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_20b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_20b,
	llc_busy_actions_20b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_busy_actions_21[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_21 = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_busy_actions_21
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_22[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_busy_actions_22[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_22 = {
	llc_conn_ev_init_p_f_cycle,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_22,
	llc_busy_actions_22
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_23[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_busy_actions_23[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_rst_vs,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_23 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_busy_ev_qfyrs_23,
	llc_busy_actions_23
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_24a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_busy_actions_24a[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_24a = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_busy_ev_qfyrs_24a,
	llc_busy_actions_24a
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_24b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_busy_actions_24b[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_24b = {
	llc_conn_ev_busy_tmr_exp,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_busy_ev_qfyrs_24b,
	llc_busy_actions_24b
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_25[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_busy_actions_25[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	llc_conn_ac_set_data_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_25 = {
	llc_conn_ev_rej_tmr_exp,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_busy_ev_qfyrs_25,
	llc_busy_actions_25
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_26[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_busy_actions_26[] = {
	llc_conn_ac_set_data_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_busy_state_trans_26 = {
	llc_conn_ev_rej_tmr_exp,
	LLC_CONN_STATE_BUSY,
	llc_busy_ev_qfyrs_26,
	llc_busy_actions_26
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_busy_state_transitions[] = {
	&llc_common_state_trans_1,	/* Request */
	&llc_common_state_trans_2,
	&llc_busy_state_trans_1,
	&llc_busy_state_trans_2,
	&llc_busy_state_trans_2_1,
	&llc_common_state_trans_n,
	&llc_busy_state_trans_3,	/* Local busy */
	&llc_busy_state_trans_4,
	&llc_busy_state_trans_5,
	&llc_busy_state_trans_6,
	&llc_busy_state_trans_7,
	&llc_busy_state_trans_8,
	&llc_common_state_trans_n,
	&llc_busy_state_trans_22,	/* Initiate PF cycle */
	&llc_common_state_trans_n,
	&llc_common_state_trans_11a,	/* Timer */
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_busy_state_trans_23,
	&llc_busy_state_trans_24a,
	&llc_busy_state_trans_24b,
	&llc_busy_state_trans_25,
	&llc_busy_state_trans_26,
	&llc_common_state_trans_n,
	&llc_busy_state_trans_9a,	/* Receive frame */
	&llc_busy_state_trans_9b,
	&llc_busy_state_trans_10a,
	&llc_busy_state_trans_10b,
	&llc_busy_state_trans_11,
	&llc_busy_state_trans_12,
	&llc_busy_state_trans_13a,
	&llc_busy_state_trans_13b,
	&llc_busy_state_trans_14a,
	&llc_busy_state_trans_14b,
	&llc_busy_state_trans_15a,
	&llc_busy_state_trans_15b,
	&llc_busy_state_trans_15c,
	&llc_busy_state_trans_16,
	&llc_busy_state_trans_17a,
	&llc_busy_state_trans_17b,
	&llc_busy_state_trans_17c,
	&llc_busy_state_trans_18,
	&llc_busy_state_trans_19a,
	&llc_busy_state_trans_19b,
	&llc_busy_state_trans_20a,
	&llc_busy_state_trans_20b,
	&llc_busy_state_trans_21,
	&llc_common_state_trans_3,
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/* &llc_common_state_trans_10, */
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_REJ transitions ------------------ */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_reject_actions_1[] = {
	llc_conn_ac_send_i_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_1,
	llc_reject_actions_1
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_remote_busy_eq_0,
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_2[] = {
	llc_conn_ac_send_i_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_2 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_2,
	llc_reject_actions_2
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_2_1[] = {
	llc_conn_ev_qlfy_remote_busy_eq_1,
	llc_conn_ev_qlfy_set_status_remote_busy,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_reject_actions_2_1[1];

static struct llc_conn_state_trans llc_reject_state_trans_2_1 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_2_1,
	llc_reject_actions_2_1
};


/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_reject_actions_3[] = {
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_2,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_3 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_BUSY,
	llc_reject_ev_qfyrs_3,
	llc_reject_actions_3
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_4[] = {
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_2,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_4 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_BUSY,
	llc_reject_ev_qfyrs_4,
	llc_reject_actions_4
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_reject_actions_5a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_5a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_5a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_reject_actions_5b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_5b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_5b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_5c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_5c[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_5c = {
	llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_5c,
	llc_reject_actions_5c
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_reject_actions_6[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_6 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_6
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_7a[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_reject_actions_7a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_send_ack_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	llc_conn_ac_stop_rej_timer,
	NULL

};

static struct llc_conn_state_trans llc_reject_state_trans_7a = {
	llc_conn_ev_rx_i_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_reject_ev_qfyrs_7a,
	llc_reject_actions_7a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_7b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_reject_actions_7b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_send_ack_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy_if_f_eq_1,
	llc_conn_ac_stop_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_7b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_reject_ev_qfyrs_7b,
	llc_reject_actions_7b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_8a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_8a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_ack_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_8a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_reject_ev_qfyrs_8a,
	llc_reject_actions_8a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_8b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_8b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_ack_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_8b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_NORMAL,
	llc_reject_ev_qfyrs_8b,
	llc_reject_actions_8b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_reject_actions_9[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_ack_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_stop_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_9 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_9
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_reject_actions_10a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_10a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_10a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_reject_actions_10b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_10b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_10b
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_10c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_10c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_10c = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_10c,
	llc_reject_actions_10c
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_reject_actions_11[] = {
	llc_conn_ac_send_ack_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_11 = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_11
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_reject_actions_12a[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_12a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_12a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_reject_actions_12b[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_12b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_12b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_12c[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_12c[] = {
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_12c = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_12c,
	llc_reject_actions_12c
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_reject_actions_13[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_13 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_13
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_14a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_reject_actions_14a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_14a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_14a,
	llc_reject_actions_14a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_14b[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	NULL
};

static llc_conn_action_t llc_reject_actions_14b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_14b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_x,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_14b,
	llc_reject_actions_14b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_15a[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_15a[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_15a = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_15a,
	llc_reject_actions_15a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_15b[] = {
	llc_conn_ev_qlfy_p_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_reject_actions_15b[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_15b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_15b,
	llc_reject_actions_15b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_reject_actions_16[] = {
	llc_conn_ac_set_vs_nr,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_resend_i_rsp_f_set_1,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_16 = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_reject_actions_16
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_17[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_reject_actions_17[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_17 = {
	llc_conn_ev_init_p_f_cycle,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_17,
	llc_reject_actions_17
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_18[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_reject_actions_18[] = {
	llc_conn_ac_send_rej_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_18 = {
	llc_conn_ev_rej_tmr_exp,
	LLC_CONN_STATE_REJ,
	llc_reject_ev_qfyrs_18,
	llc_reject_actions_18
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_19[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_reject_actions_19[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_19 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_reject_ev_qfyrs_19,
	llc_reject_actions_19
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_20a[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_reject_actions_20a[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_20a = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_reject_ev_qfyrs_20a,
	llc_reject_actions_20a
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_20b[] = {
	llc_conn_ev_qlfy_p_flag_eq_0,
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_reject_actions_20b[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	llc_conn_ac_rst_vs,
	NULL
};

static struct llc_conn_state_trans llc_reject_state_trans_20b = {
	llc_conn_ev_busy_tmr_exp,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_reject_ev_qfyrs_20b,
	llc_reject_actions_20b
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_reject_state_transitions[] = {
	&llc_common_state_trans_1,	/* Request */
	&llc_common_state_trans_2,
	&llc_common_state_trans_n,
	&llc_reject_state_trans_1,
	&llc_reject_state_trans_2,
	&llc_reject_state_trans_2_1,
	&llc_reject_state_trans_3,	/* Local busy */
	&llc_reject_state_trans_4,
	&llc_common_state_trans_n,
	&llc_reject_state_trans_17,	/* Initiate PF cycle */
	&llc_common_state_trans_n,
	&llc_common_state_trans_11a,	/* Timer */
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_reject_state_trans_18,
	&llc_reject_state_trans_19,
	&llc_reject_state_trans_20a,
	&llc_reject_state_trans_20b,
	&llc_common_state_trans_n,
	&llc_common_state_trans_3,	/* Receive frame */
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/* &llc_common_state_trans_10, */
	&llc_reject_state_trans_5a,
	&llc_reject_state_trans_5b,
	&llc_reject_state_trans_5c,
	&llc_reject_state_trans_6,
	&llc_reject_state_trans_7a,
	&llc_reject_state_trans_7b,
	&llc_reject_state_trans_8a,
	&llc_reject_state_trans_8b,
	&llc_reject_state_trans_9,
	&llc_reject_state_trans_10a,
	&llc_reject_state_trans_10b,
	&llc_reject_state_trans_10c,
	&llc_reject_state_trans_11,
	&llc_reject_state_trans_12a,
	&llc_reject_state_trans_12b,
	&llc_reject_state_trans_12c,
	&llc_reject_state_trans_13,
	&llc_reject_state_trans_14a,
	&llc_reject_state_trans_14b,
	&llc_reject_state_trans_15a,
	&llc_reject_state_trans_15b,
	&llc_reject_state_trans_16,
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_AWAIT transitions ------------------- */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static llc_conn_ev_qfyr_t llc_await_ev_qfyrs_1_0[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_await_actions_1_0[1];

static struct llc_conn_state_trans llc_await_state_trans_1_0 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_AWAIT,
	llc_await_ev_qfyrs_1_0,
	llc_await_actions_1_0
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_action_t llc_await_actions_1[] = {
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_1 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_1
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_actions_2[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_2 = {
	llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_2
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_actions_3a[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_3a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_3a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_actions_3b[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_3b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_3b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_actions_4[] = {
	llc_conn_ac_send_rej_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_start_rej_timer,
	llc_conn_ac_start_p_timer,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_4 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_4
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_actions_5[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_5 = {
	llc_conn_ev_rx_i_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_5
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_actions_6a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_6a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_6a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_actions_6b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_6b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_6b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_actions_7[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_7 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_7
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_actions_8a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_8a = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_8a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_actions_8b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_8b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_8b
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_actions_9a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_9a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_9a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_actions_9b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_9b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_9b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_actions_9c[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_9c = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_9c
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_actions_9d[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_9d = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_9d
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_actions_10a[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_10a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_10a
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_actions_10b[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_10b = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_10b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_actions_11[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_11 = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_11
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_actions_12a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_12a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_12a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_actions_12b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_12b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_12b
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_actions_13[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_13 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_actions_13
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_await_ev_qfyrs_14[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_await_actions_14[] = {
	llc_conn_ac_send_rr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_await_state_trans_14 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT,
	llc_await_ev_qfyrs_14,
	llc_await_actions_14
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_state_transitions[] = {
	&llc_common_state_trans_1,	/* Request */
	&llc_common_state_trans_2,
	&llc_await_state_trans_1_0,
	&llc_common_state_trans_n,
	&llc_await_state_trans_1,	/* Local busy */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* Initiate PF Cycle */
	&llc_common_state_trans_11a,	/* Timer */
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_await_state_trans_14,
	&llc_common_state_trans_n,
	&llc_common_state_trans_3,	/* Receive frame */
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/* &llc_common_state_trans_10, */
	&llc_await_state_trans_2,
	&llc_await_state_trans_3a,
	&llc_await_state_trans_3b,
	&llc_await_state_trans_4,
	&llc_await_state_trans_5,
	&llc_await_state_trans_6a,
	&llc_await_state_trans_6b,
	&llc_await_state_trans_7,
	&llc_await_state_trans_8a,
	&llc_await_state_trans_8b,
	&llc_await_state_trans_9a,
	&llc_await_state_trans_9b,
	&llc_await_state_trans_9c,
	&llc_await_state_trans_9d,
	&llc_await_state_trans_10a,
	&llc_await_state_trans_10b,
	&llc_await_state_trans_11,
	&llc_await_state_trans_12a,
	&llc_await_state_trans_12b,
	&llc_await_state_trans_13,
	&llc_common_state_trans_n
};

/* ------------------ LLC_CONN_STATE_AWAIT_BUSY transitions ---------------- */
/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_1_0[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_await_busy_actions_1_0[1];

static struct llc_conn_state_trans llc_await_busy_state_trans_1_0 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_await_busy_ev_qfyrs_1_0,
	llc_await_busy_actions_1_0
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_1[] = {
	llc_conn_ev_qlfy_data_flag_eq_1,
	NULL
};

static llc_conn_action_t llc_await_busy_actions_1[] = {
	llc_conn_ac_send_rej_xxx_x_set_0,
	llc_conn_ac_start_rej_timer,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_1 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_await_busy_ev_qfyrs_1,
	llc_await_busy_actions_1
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_data_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_await_busy_actions_2[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_2 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_AWAIT,
	llc_await_busy_ev_qfyrs_2,
	llc_await_busy_actions_2
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_data_flag_eq_2,
	NULL
};

static llc_conn_action_t llc_await_busy_actions_3[] = {
	llc_conn_ac_send_rr_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_3 = {
	llc_conn_ev_local_busy_cleared,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_await_busy_ev_qfyrs_3,
	llc_await_busy_actions_3
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_busy_actions_4[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_set_data_flag_1,
	llc_conn_ac_clear_remote_busy,
	llc_conn_ac_resend_i_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_4 = {
	llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_4
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_busy_actions_5a[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_5a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_5a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_busy_actions_5b[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_5b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_5b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_busy_actions_6[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_1,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_6 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_6
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_7[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_0,
	llc_conn_ac_clear_remote_busy,
	llc_conn_ac_resend_i_xxx_x_set_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_7 = {
	llc_conn_ev_rx_i_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_7
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_8a[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_8a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_8a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_8b[] = {
	llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_8b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_8b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_9[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_data_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_9 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_9
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_10a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_10a = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_10a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_10b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_10b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_10b
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_11a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_11a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_11b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_11b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_11c[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11c = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_11c
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_11d[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11d = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_11d
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_12a[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_12a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_12a
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_12b[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_12b = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_12b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_13[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_13 = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_13
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_14a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_14a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_14a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_busy_actions_14b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_14b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_14b
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_busy_actions_15[] = {
	llc_conn_ac_send_rnr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_15 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_busy_actions_15
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_16[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_await_busy_actions_16[] = {
	llc_conn_ac_send_rnr_cmd_p_set_1,
	llc_conn_ac_start_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_await_busy_state_trans_16 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT_BUSY,
	llc_await_busy_ev_qfyrs_16,
	llc_await_busy_actions_16
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_busy_state_transitions[] = {
	&llc_common_state_trans_1,		/* Request */
	&llc_common_state_trans_2,
	&llc_await_busy_state_trans_1_0,
	&llc_common_state_trans_n,
	&llc_await_busy_state_trans_1,		/* Local busy */
	&llc_await_busy_state_trans_2,
	&llc_await_busy_state_trans_3,
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,		/* Initiate PF cycle */
	&llc_common_state_trans_11a,		/* Timer */
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_await_busy_state_trans_16,
	&llc_common_state_trans_n,
	&llc_await_busy_state_trans_4,		/* Receive frame */
	&llc_await_busy_state_trans_5a,
	&llc_await_busy_state_trans_5b,
	&llc_await_busy_state_trans_6,
	&llc_await_busy_state_trans_7,
	&llc_await_busy_state_trans_8a,
	&llc_await_busy_state_trans_8b,
	&llc_await_busy_state_trans_9,
	&llc_await_busy_state_trans_10a,
	&llc_await_busy_state_trans_10b,
	&llc_await_busy_state_trans_11a,
	&llc_await_busy_state_trans_11b,
	&llc_await_busy_state_trans_11c,
	&llc_await_busy_state_trans_11d,
	&llc_await_busy_state_trans_12a,
	&llc_await_busy_state_trans_12b,
	&llc_await_busy_state_trans_13,
	&llc_await_busy_state_trans_14a,
	&llc_await_busy_state_trans_14b,
	&llc_await_busy_state_trans_15,
	&llc_common_state_trans_3,
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/* &llc_common_state_trans_10, */
	&llc_common_state_trans_n
};

/* ----------------- LLC_CONN_STATE_AWAIT_REJ transitions --------------- */
/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static llc_conn_ev_qfyr_t llc_await_reject_ev_qfyrs_1_0[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_await_reject_actions_1_0[1];

static struct llc_conn_state_trans llc_await_reject_state_trans_1_0 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_await_reject_ev_qfyrs_1_0,
	llc_await_reject_actions_1_0
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static llc_conn_action_t llc_await_rejct_actions_1[] = {
	llc_conn_ac_send_rnr_xxx_x_set_0,
	llc_conn_ac_set_data_flag_2,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_1 = {
	llc_conn_ev_local_busy_detected,
	LLC_CONN_STATE_AWAIT_BUSY,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_1
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_rejct_actions_2a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_2a = {
	llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_2a
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static llc_conn_action_t llc_await_rejct_actions_2b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_2b = {
	llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_2b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_rejct_actions_3[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_3 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_3
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_4[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_stop_rej_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_4 = {
	llc_conn_ev_rx_i_rsp_fbit_set_1,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_4
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_5a[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_xxx_x_set_0,
	llc_conn_ac_stop_rej_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_5a = {
	llc_conn_ev_rx_i_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_5a
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_5b[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_xxx_x_set_0,
	llc_conn_ac_stop_rej_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_5b = {
	llc_conn_ev_rx_i_cmd_pbit_set_0,     LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,	     llc_await_rejct_actions_5b
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_6[] = {
	llc_conn_ac_inc_vr_by_1,
	llc_conn_ac_data_ind,
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_stop_rej_timer,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_6 = {
	llc_conn_ev_rx_i_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_6
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_7a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7a = {
	llc_conn_ev_rx_rr_rsp_fbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_7a
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_7b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7b = {
	llc_conn_ev_rx_rej_rsp_fbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_7b
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static llc_conn_action_t llc_await_rejct_actions_7c[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_resend_i_xxx_x_set_0,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7c = {
	llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_7c
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_8a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_8a
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_8b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8b = {
	llc_conn_ev_rx_rr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_8b
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_8c[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8c = {
	llc_conn_ev_rx_rej_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_8c
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_8d[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8d = {
	llc_conn_ev_rx_rej_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_8d
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_9a[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_9a = {
	llc_conn_ev_rx_rr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_9a
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_9b[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_clear_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_9b = {
	llc_conn_ev_rx_rej_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_9b
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_10[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_10 = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	LLC_CONN_STATE_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_10
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_11a[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_11a = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_11a
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static llc_conn_action_t llc_await_rejct_actions_11b[] = {
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_11b = {
	llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_11b
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static llc_conn_action_t llc_await_rejct_actions_12[] = {
	llc_conn_ac_send_rr_rsp_f_set_1,
	llc_conn_ac_upd_nr_received,
	llc_conn_ac_upd_vs,
	llc_conn_ac_set_remote_busy,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_12 = {
	llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	LLC_CONN_STATE_AWAIT_REJ,
	LLC_NO_EVENT_QUALIFIERS,
	llc_await_rejct_actions_12
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_await_rejct_ev_qfyrs_13[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_await_rejct_actions_13[] = {
	llc_conn_ac_send_rej_cmd_p_set_1,
	llc_conn_ac_stop_p_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_13 = {
	llc_conn_ev_p_tmr_exp,
	LLC_CONN_STATE_AWAIT_REJ,
	llc_await_rejct_ev_qfyrs_13,
	llc_await_rejct_actions_13
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_rejct_state_transitions[] = {
	&llc_await_reject_state_trans_1_0,
	&llc_common_state_trans_1,		/* requests */
	&llc_common_state_trans_2,
	&llc_common_state_trans_n,
	&llc_await_rejct_state_trans_1,		/* local busy */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,		/* Initiate PF cycle */
	&llc_await_rejct_state_trans_13,	/* timers */
	&llc_common_state_trans_11a,
	&llc_common_state_trans_11b,
	&llc_common_state_trans_11c,
	&llc_common_state_trans_11d,
	&llc_common_state_trans_n,
	&llc_await_rejct_state_trans_2a,	/* receive frames */
	&llc_await_rejct_state_trans_2b,
	&llc_await_rejct_state_trans_3,
	&llc_await_rejct_state_trans_4,
	&llc_await_rejct_state_trans_5a,
	&llc_await_rejct_state_trans_5b,
	&llc_await_rejct_state_trans_6,
	&llc_await_rejct_state_trans_7a,
	&llc_await_rejct_state_trans_7b,
	&llc_await_rejct_state_trans_7c,
	&llc_await_rejct_state_trans_8a,
	&llc_await_rejct_state_trans_8b,
	&llc_await_rejct_state_trans_8c,
	&llc_await_rejct_state_trans_8d,
	&llc_await_rejct_state_trans_9a,
	&llc_await_rejct_state_trans_9b,
	&llc_await_rejct_state_trans_10,
	&llc_await_rejct_state_trans_11a,
	&llc_await_rejct_state_trans_11b,
	&llc_await_rejct_state_trans_12,
	&llc_common_state_trans_3,
	&llc_common_state_trans_4,
	&llc_common_state_trans_5,
	&llc_common_state_trans_6,
	&llc_common_state_trans_7a,
	&llc_common_state_trans_7b,
	&llc_common_state_trans_8a,
	&llc_common_state_trans_8b,
	&llc_common_state_trans_8c,
	&llc_common_state_trans_9,
	/* &llc_common_state_trans_10, */
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_D_CONN transitions ------------------ */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_1[] = {
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_conflict,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_1[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_disc_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_1 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_1,
	llc_d_conn_actions_1
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event,
 * cause_flag = 0
 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_1_1[] = {
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_conflict,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_1_1[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_1_1 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_1_1,
	llc_d_conn_actions_1_1
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_2[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_disc_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_2 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_2,
	llc_d_conn_actions_2
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_2_1[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_2_1[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_2_1 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_2_1,
	llc_d_conn_actions_2_1
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_d_conn_actions_3[] = {
	llc_conn_ac_send_ua_rsp_f_set_p,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_3 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_D_CONN,
	LLC_NO_EVENT_QUALIFIERS,
	llc_d_conn_actions_3
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_4[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_disc_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_4 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_4,
	llc_d_conn_actions_4
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_4_1[] = {
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_4_1[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_4_1 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_4_1,
	llc_d_conn_actions_4_1
};

/*
 * State transition for
 * LLC_CONN_EV_DATA_CONN_REQ event
 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_5[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_d_conn_actions_5[1];

static struct llc_conn_state_trans llc_d_conn_state_trans_5 = {
	llc_conn_ev_data_req, LLC_CONN_STATE_D_CONN,
	llc_d_conn_ev_qfyrs_5, llc_d_conn_actions_5
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_6[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_6[] = {
	llc_conn_ac_send_disc_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_6 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_D_CONN,
	llc_d_conn_ev_qfyrs_6,
	llc_d_conn_actions_6
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event, cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_7[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_failed,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_7[] = {
	llc_conn_ac_disc_confirm,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_7 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_7,
	llc_d_conn_actions_7
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event, cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_8[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_failed,
	NULL
};

static llc_conn_action_t llc_d_conn_actions_8[] = {
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_d_conn_state_trans_8 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ADM,
	llc_d_conn_ev_qfyrs_8,
	llc_d_conn_actions_8
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_d_conn_state_transitions[] = {
	&llc_d_conn_state_trans_5,	/* Request */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* Local busy */
	&llc_common_state_trans_n,	/* Initiate PF cycle */
	&llc_d_conn_state_trans_6,	/* Timer */
	&llc_d_conn_state_trans_7,
	&llc_d_conn_state_trans_8,
	&llc_common_state_trans_n,
	&llc_d_conn_state_trans_1,	/* Receive frame */
	&llc_d_conn_state_trans_1_1,
	&llc_d_conn_state_trans_2,
	&llc_d_conn_state_trans_2_1,
	&llc_d_conn_state_trans_3,
	&llc_d_conn_state_trans_4,
	&llc_d_conn_state_trans_4_1,
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_RESET transitions ------------------- */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_rst_actions_1[] = {
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_set_s_flag_1,
	llc_conn_ac_send_ua_rsp_f_set_p,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_1 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_RESET,
	LLC_NO_EVENT_QUALIFIERS,
	llc_rst_actions_1
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_2[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_conn,
	NULL
};

static llc_conn_action_t llc_rst_actions_2[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_rst_confirm,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_reset,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_2 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_rst_ev_qfyrs_2,
	llc_rst_actions_2
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_2_1[] = {
	llc_conn_ev_qlfy_p_flag_eq_f,
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_rst_done,
	NULL
};

static llc_conn_action_t llc_rst_actions_2_1[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_upd_p_flag,
	llc_conn_ac_rst_confirm,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_reset,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_2_1 = {
	llc_conn_ev_rx_ua_rsp_fbit_set_x,
	LLC_CONN_STATE_NORMAL,
	llc_rst_ev_qfyrs_2_1,
	llc_rst_actions_2_1
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_3[] = {
	llc_conn_ev_qlfy_s_flag_eq_1,
	llc_conn_ev_qlfy_set_status_rst_done,
	NULL
};

static llc_conn_action_t llc_rst_actions_3[] = {
	llc_conn_ac_set_p_flag_0,
	llc_conn_ac_set_remote_busy_0,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_3 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_NORMAL,
	llc_rst_ev_qfyrs_3,
	llc_rst_actions_3
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_4[] = {
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};
static llc_conn_action_t llc_rst_actions_4[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_ac_disc_ind,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_4 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_4,
	llc_rst_actions_4
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event,
 * cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_4_1[] = {
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

static llc_conn_action_t llc_rst_actions_4_1[] = {
	llc_conn_ac_send_dm_rsp_f_set_p,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_4_1 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_4_1,
	llc_rst_actions_4_1
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 1 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_5[] = {
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_disc,
	NULL
};

static llc_conn_action_t llc_rst_actions_5[] = {
	llc_conn_ac_disc_ind,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_5 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_5,
	llc_rst_actions_5
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 0 */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_5_1[] = {
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

static llc_conn_action_t llc_rst_actions_5_1[] = {
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_5_1 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_5_1,
	llc_rst_actions_5_1
};

/* State transitions for DATA_CONN_REQ event */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_6[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_rst_actions_6[1];

static struct llc_conn_state_trans llc_rst_state_trans_6 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_RESET,
	llc_rst_ev_qfyrs_6,
	llc_rst_actions_6
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_7[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	llc_conn_ev_qlfy_s_flag_eq_0,
	NULL
};

static llc_conn_action_t llc_rst_actions_7[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_7 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_rst_ev_qfyrs_7,
	llc_rst_actions_7
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_8[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	llc_conn_ev_qlfy_s_flag_eq_0,
	llc_conn_ev_qlfy_cause_flag_eq_1,
	llc_conn_ev_qlfy_set_status_failed,
	NULL
};
static llc_conn_action_t llc_rst_actions_8[] = {
	llc_conn_ac_disc_ind,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_8 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_8,
	llc_rst_actions_8
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_8_1[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	llc_conn_ev_qlfy_s_flag_eq_0,
	llc_conn_ev_qlfy_cause_flag_eq_0,
	llc_conn_ev_qlfy_set_status_failed,
	NULL
};
static llc_conn_action_t llc_rst_actions_8_1[] = {
	llc_conn_ac_disc_ind,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_rst_state_trans_8_1 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ADM,
	llc_rst_ev_qfyrs_8_1,
	llc_rst_actions_8_1
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_rst_state_transitions[] = {
	&llc_rst_state_trans_6,		/* Request */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* Local busy */
	&llc_common_state_trans_n,	/* Initiate PF cycle */
	&llc_rst_state_trans_3,		/* Timer */
	&llc_rst_state_trans_7,
	&llc_rst_state_trans_8,
	&llc_rst_state_trans_8_1,
	&llc_common_state_trans_n,
	&llc_rst_state_trans_1,		/* Receive frame */
	&llc_rst_state_trans_2,
	&llc_rst_state_trans_2_1,
	&llc_rst_state_trans_4,
	&llc_rst_state_trans_4_1,
	&llc_rst_state_trans_5,
	&llc_rst_state_trans_5_1,
	&llc_common_state_trans_n
};

/* -------------------- LLC_CONN_STATE_ERROR transitions ------------------- */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_error_actions_1[] = {
	llc_conn_ac_set_vs_0,
	llc_conn_ac_set_vr_0,
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_rst_ind,
	llc_conn_ac_set_p_flag_0,
	llc_conn_ac_set_remote_busy_0,
	llc_conn_ac_stop_ack_timer,
	llc_conn_reset,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_1 = {
	llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	LLC_CONN_STATE_NORMAL,
	LLC_NO_EVENT_QUALIFIERS,
	llc_error_actions_1
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_error_actions_2[] = {
	llc_conn_ac_send_ua_rsp_f_set_p,
	llc_conn_ac_disc_ind,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_2 = {
	llc_conn_ev_rx_disc_cmd_pbit_set_x,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_error_actions_2
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static llc_conn_action_t llc_error_actions_3[] = {
	llc_conn_ac_disc_ind,
	llc_conn_ac_stop_ack_timer,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_3 = {
	llc_conn_ev_rx_dm_rsp_fbit_set_x,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_error_actions_3
};

/* State transitions for LLC_CONN_EV_RX_FRMR_RSP_Fbit_SET_X event */
static llc_conn_action_t llc_error_actions_4[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_4 = {
	llc_conn_ev_rx_frmr_rsp_fbit_set_x,
	LLC_CONN_STATE_RESET,
	LLC_NO_EVENT_QUALIFIERS,
	llc_error_actions_4
};

/* State transitions for LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_X event */
static llc_conn_action_t llc_error_actions_5[] = {
	llc_conn_ac_resend_frmr_rsp_f_set_p,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_5 = {
	llc_conn_ev_rx_xxx_cmd_pbit_set_x,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	llc_error_actions_5
};

/* State transitions for LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_X event */
static struct llc_conn_state_trans llc_error_state_trans_6 = {
	llc_conn_ev_rx_xxx_rsp_fbit_set_x,
	LLC_CONN_STATE_ERROR,
	LLC_NO_EVENT_QUALIFIERS,
	LLC_NO_TRANSITION_ACTIONS
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_error_ev_qfyrs_7[] = {
	llc_conn_ev_qlfy_retry_cnt_lt_n2,
	NULL
};

static llc_conn_action_t llc_error_actions_7[] = {
	llc_conn_ac_resend_frmr_rsp_f_set_0,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_inc_retry_cnt_by_1,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_7 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_ERROR,
	llc_error_ev_qfyrs_7,
	llc_error_actions_7
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static llc_conn_ev_qfyr_t llc_error_ev_qfyrs_8[] = {
	llc_conn_ev_qlfy_retry_cnt_gte_n2,
	NULL
};

static llc_conn_action_t llc_error_actions_8[] = {
	llc_conn_ac_send_sabme_cmd_p_set_x,
	llc_conn_ac_set_s_flag_0,
	llc_conn_ac_start_ack_timer,
	llc_conn_ac_set_retry_cnt_0,
	llc_conn_ac_set_cause_flag_0,
	NULL
};

static struct llc_conn_state_trans llc_error_state_trans_8 = {
	llc_conn_ev_ack_tmr_exp,
	LLC_CONN_STATE_RESET,
	llc_error_ev_qfyrs_8,
	llc_error_actions_8
};

/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static llc_conn_ev_qfyr_t llc_error_ev_qfyrs_9[] = {
	llc_conn_ev_qlfy_set_status_refuse,
	NULL
};

/* just one member, NULL, .bss zeroes it */
static llc_conn_action_t llc_error_actions_9[1];

static struct llc_conn_state_trans llc_error_state_trans_9 = {
	llc_conn_ev_data_req,
	LLC_CONN_STATE_ERROR,
	llc_error_ev_qfyrs_9,
	llc_error_actions_9
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_error_state_transitions[] = {
	&llc_error_state_trans_9,	/* Request */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* Local busy */
	&llc_common_state_trans_n,	/* Initiate PF cycle */
	&llc_error_state_trans_7,	/* Timer */
	&llc_error_state_trans_8,
	&llc_common_state_trans_n,
	&llc_error_state_trans_1,	/* Receive frame */
	&llc_error_state_trans_2,
	&llc_error_state_trans_3,
	&llc_error_state_trans_4,
	&llc_error_state_trans_5,
	&llc_error_state_trans_6,
	&llc_common_state_trans_n
};

/* ------------------- LLC_CONN_STATE_TEMP transitions ----------------- */
/* State transitions for LLC_CONN_EV_DISC_REQ event */
static llc_conn_action_t llc_temp_actions_1[] = {
	llc_conn_ac_stop_all_timers,
	llc_conn_ac_send_disc_cmd_p_set_x,
	llc_conn_disc,
	NULL
};

static struct llc_conn_state_trans llc_temp_state_trans_1 = {
	llc_conn_ev_disc_req,
	LLC_CONN_STATE_ADM,
	LLC_NO_EVENT_QUALIFIERS,
	llc_temp_actions_1
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_temp_state_transitions[] = {
	&llc_temp_state_trans_1,	/* requests */
	&llc_common_state_trans_n,
	&llc_common_state_trans_n,	/* local busy */
	&llc_common_state_trans_n,	/* init_pf_cycle */
	&llc_common_state_trans_n,	/* timer */
	&llc_common_state_trans_n       /* recieve */
};

/* Connection State Transition Table */
struct llc_conn_state llc_conn_state_table[] = {
	{ LLC_CONN_STATE_ADM,		llc_adm_state_transitions },
	{ LLC_CONN_STATE_SETUP,		llc_setup_state_transitions },
	{ LLC_CONN_STATE_NORMAL,	llc_normal_state_transitions },
	{ LLC_CONN_STATE_BUSY,		llc_busy_state_transitions },
	{ LLC_CONN_STATE_REJ,		llc_reject_state_transitions },
	{ LLC_CONN_STATE_AWAIT,		llc_await_state_transitions },
	{ LLC_CONN_STATE_AWAIT_BUSY,	llc_await_busy_state_transitions },
	{ LLC_CONN_STATE_AWAIT_REJ,	llc_await_rejct_state_transitions },
	{ LLC_CONN_STATE_D_CONN,	llc_d_conn_state_transitions },
	{ LLC_CONN_STATE_RESET,		llc_rst_state_transitions },
	{ LLC_CONN_STATE_ERROR,		llc_error_state_transitions },
	{ LLC_CONN_STATE_TEMP,		llc_temp_state_transitions }
};
