/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.2.x and 2.4.x
 * Copyright (C) 2001 Qlogic Corporation
 * (www.qlogic.com)
 *
 * Portions (C) Arjan van de Ven <arjanv@redhat.com> for Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
 
#include "settings.h"
#include "debug.h"

/* external prototypes */
int qla2x00_read_nvram(adapter_state_t *, EXT_IOCTL *, int);
int qla2x00_update_nvram(adapter_state_t *, EXT_IOCTL *, int);
int qla2x00_write_nvram_word(adapter_state_t *, uint8_t, uint16_t);
void qla2100_isp_cmd(struct scsi_qla_host *);
void qla2100_poll(struct scsi_qla_host *);
uint8_t qla2100_mailbox_command(struct scsi_qla_host *, uint32_t, uint16_t *);
void qla2x00_next(struct scsi_qla_host * ha, os_tgt_t * tq, struct os_lun * lq);
mp_host_t * qla2x00_cfg_find_host(adapter_state_t * ha);
uint8_t inline qla2x00_fo_enabled(struct scsi_qla_host * ha, int instance);
uint8_t qla2x00_is_portname_in_device(mp_device_t * dp, uint8_t * portname);


/* local prototypes */
static request_t *qla2100_ms_req_pkt(struct scsi_qla_host *, srb_t *);
static int qla2x00_find_curr_ha(int, struct scsi_qla_host **);
static int qla2x00_get_statistics(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_get_fc_statistics(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_aen_reg(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_aen_get(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query_hba_node(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query_hba_port(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query_disc_port(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query_disc_tgt(adapter_state_t *, EXT_IOCTL *, int);
static int qla2x00_query_chip(adapter_state_t *, EXT_IOCTL *, int);
int qla2x00_send_loopback(adapter_state_t *, EXT_IOCTL *, int);
static uint8_t qla2100_get_link_status(struct scsi_qla_host *, uint8_t, void *, uint16_t *);
static uint32_t qla2x00_fo_get_params(PFO_PARAMS pp);
static uint32_t qla2x00_fo_set_params(PFO_PARAMS pp);
static int qla2x00_fo_get_lun_data(EXT_IOCTL * pext, FO_LUN_DATA_INPUT * bp, int mode);
static int qla2x00_fo_set_lun_data(EXT_IOCTL * pext, FO_LUN_DATA_INPUT * bp, int mode);
static uint32_t qla2x00_stats(FO_HBA_STAT * stat_p, uint8_t reset);
static int qla2x00_fo_set_target_data(EXT_IOCTL * pext, FO_TARGET_DATA_INPUT * bp, int mode);
static int qla2x00_fo_get_target_data(EXT_IOCTL * pext, FO_TARGET_DATA_INPUT * bp, int mode);


extern int num_hosts;
static int apiHBAInstance = 0;	/* ioctl related keeps track of API HBA Instance */

#define QLA_CMD_TIMEOUT         (60 * 2)


/*************************************************************************
 * qla2x00_ioctl_sleep
 *
 * Description:
 *   Schedule a timeout for the ioctl request.
 *
 * Returns:
 *   None.
 *************************************************************************/
static inline void 
qla2x00_ioctl_sleep(struct scsi_qla_host * ha, int timeout)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(timeout);
}


/*************************************************************************
 * qla2x00_scsi_pt_done
 *
 * Description:
 *   Sets completion flag.
 *
 * Returns:
 *************************************************************************/
void
qla2x00_scsi_pt_done(Scsi_Cmnd * pscsi_cmd)
{
	struct Scsi_Host *host;
	adapter_state_t *ha;
	srb_t *sp;

	host = pscsi_cmd->host;
	ha = (struct scsi_qla_host *) host->hostdata;
	sp = (srb_t *) CMD_SP(pscsi_cmd);

	/* printk("[[qla2x00_scsi_pt_done post function called OK]]\n"); */
	DEBUG4(printk("qla2x00_scsi_pt_done post function called OK\n"));

	/* save detail status for IOCTL reporting */
	sp->flags = sp->flags | SRB_ISP_COMPLETED;
	ha->IoctlPassThru_InProgress = 0;
	ha->ioctl_timer = 0;

	return;
}

/*************************************************************************
 * qla2x00_fcct_done
 *
 * Description:
 *   Sets completion flag.
 *
 * Returns:
 *************************************************************************/
void
qla2x00_fcct_done(Scsi_Cmnd * pscsi_cmd)
{
	struct Scsi_Host *host;
	adapter_state_t *ha;
	srb_t *sp;

	host = pscsi_cmd->host;
	ha = (struct scsi_qla_host *) host->hostdata;
	sp = (srb_t *) CMD_SP(pscsi_cmd);

	/* printk("[[qla2x00_fcct_done post function called OK]]\n"); */
	DEBUG4(printk("qla2x00_fcct_done post function called OK\n"););

	sp->flags = sp->flags | SRB_ISP_COMPLETED;
	ha->IoctlPassFCCT_InProgress = 0;
	ha->ioctl_timer = 0;

	return;
}

/*
 * qla2100_get_link_status
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	ret_buf = pointer to link status return buffer.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = mem alloc error.
 *	BIT_1 = mailbox error.
 */
static uint8_t
qla2100_get_link_status(struct scsi_qla_host * ha, uint8_t loop_id, void *ret_buf, uint16_t * status)
{
	uint8_t rval = 0;
	link_stat_t *stat_buf;
	dma_addr_t phys_address = 0;
#if DISABLE_REMOTE_MAILBOX
	uint16_t mb[MAILBOX_REGISTER_COUNT];
#else
	unsigned long mbx_flags = 0;
	int cnt = 60;
#endif

	stat_buf = pci_alloc_consistent(ha->pdev, sizeof(link_stat_t), &phys_address);

	if (stat_buf == NULL) {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - get_link_status", (int) ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}

	memset(stat_buf, 0, sizeof(link_stat_t));

#if DISABLE_REMOTE_MAILBOX
	mb[0] = MBC_GET_LINK_STATUS;
	mb[1] = loop_id << 8;
	mb[2] = MSW(phys_address);
	mb[3] = LSW(phys_address);
	mb[6] = QL21_64BITS_4THWD(phys_address);
	mb[7] = QL21_64BITS_3RDWD(phys_address);
	if (QL_STATUS_SUCCESS == qla2100_mailbox_command(ha, MBX_0 | MBX_1 | MBX_2 | MBX_3 | MBX_6 | MBX_7, mb)) {

		if (mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2(printk("qla2100_get_link_status: cmd failed. " "mbx0=%x.\n", mb[0]));
			status[0] = mb[0];
			rval = BIT_1;
		} else {
			/* copy over data */
			memcpy( ret_buf,stat_buf, sizeof(link_stat_t));
			DEBUG(printk("qla2100_get_link_status: stat dump: "
				     "fail_cnt=%d loss_sync=%d loss_sig=%d seq_err=%d "
				     "inval_xmt_word=%d inval_crc=%d.\n",
				     stat_buf->link_fail_cnt, stat_buf->loss_sync_cnt,
				     stat_buf->loss_sig_cnt, stat_buf->prim_seq_err_cnt,
				     stat_buf->inval_xmit_word_cnt, stat_buf->inval_crc_cnt););
		}
	} else {
		/* Failed. */
		rval = BIT_1;
	}
#else
	if (LOOP_NOT_READY(ha)) {
		QLA2X00_MBX_REGISTER_LOCK(ha);
		ha->mc.mb[0] = MBC_GET_LINK_STATUS;
		ha->mc.mb[1] = loop_id << 8;
		ha->mc.mb[2] = MSW(phys_address);
		ha->mc.mb[3] = LSW(phys_address);
		ha->mc.mb[6] = QL21_64BITS_4THWD(phys_address);
		ha->mc.mb[7] = QL21_64BITS_3RDWD(phys_address);
		ha->mc.out_mb = (MBX_0 | MBX_1 | MBX_2 | MBX_3 | MBX_6 | MBX_7);
		ha->dpc_flags &= ~MAILBOX_DONE;
		ha->dpc_flags |= MAILBOX_NEEDED;
		do {
			up(ha->dpc_wait);
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ);
		} while (!(ha->dpc_flags & MAILBOX_DONE) && cnt--);
		QLA2X00_MBX_REGISTER_UNLOCK(ha);
	}
#endif

	pci_free_consistent(ha->pdev, sizeof(link_stat_t), stat_buf, phys_address);
	return rval;
}


static int
qla2x00_query(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int rval;

	DEBUG3(printk("qla2x00_query: entered.\n"));

	    /* All Query type ioctls are done here */
	    switch (pext->SubCode) {

	case EXT_SC_QUERY_HBA_NODE:
		/* fill in HBA NODE Information */
		rval = qla2x00_query_hba_node(ha, pext, mode);
		break;

	case EXT_SC_QUERY_HBA_PORT:
		/* return HBA PORT related info */
		rval = qla2x00_query_hba_port(ha, pext, mode);
		break;

	case EXT_SC_QUERY_DISC_PORT:
		/* return discovered port information */
		rval = qla2x00_query_disc_port(ha, pext, mode);
		break;

	case EXT_SC_QUERY_DISC_TGT:
		/* printk("[Start SC_QUERY_DISC_TGT active ha=%x]\n",ha); */
		rval = qla2x00_query_disc_tgt(ha, pext, mode);
		break;

	case EXT_SC_QUERY_CHIP:
		rval = qla2x00_query_chip(ha, pext, mode);
		break;

	case EXT_SC_QUERY_DISC_LUN:
		pext->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		rval = pext->Status;
		break;

	default:
		DEBUG2(printk("qla2x00_query: unknown SubCode.\n");
		    );
		    pext->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		rval = pext->Status;
		break;
	}

	DEBUG3(printk("qla2x00_query: exiting.\n");
	    );
	    return (rval);
}

static int
qla2x00_query_hba_node(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int ret = EXT_STATUS_OK;
	uint32_t i, transfer_size;
	static EXT_HBA_NODE tmp_hba_node;
	struct qla_boards *bdp;

	DEBUG3(printk("qla2x00_query_hba_node: entered.\n");
	    );

	    /* fill all available HBA NODE Information */
	bdp = &QLBoardTbl_fc[ha->devnum];
	for (i = 0; i < 8; i++)
		tmp_hba_node.WWNN[i] = ha->node_name[i];

	sprintf((char *) (tmp_hba_node.Manufacturer), "Qlogic Corp.");
	sprintf((char *) (tmp_hba_node.Model), (char *) &bdp->bdName[0]);

	tmp_hba_node.SerialNum[0] = ha->serial0;
	tmp_hba_node.SerialNum[1] = ha->serial1;
	tmp_hba_node.SerialNum[2] = ha->serial2;

	sprintf((char *) (tmp_hba_node.DriverVersion), QLA2100_VERSION);
	sprintf((char *) (tmp_hba_node.FWVersion), "%2d.%02d.%02d", bdp->fwver[0], bdp->fwver[1], bdp->fwver[2]);

	sprintf((char *) (tmp_hba_node.OptRomVersion), "%d.%d", ha->optrom_major, ha->optrom_minor);

	tmp_hba_node.InterfaceType = EXT_DEF_FC_INTF_TYPE;
	tmp_hba_node.PortCount = 1;

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_HBA_NODE));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_query_hba_node: ERROR verify write rsp " "buffer.\n");
		    );
		    return (pext->Status);
	}

	/* now copy up the HBA_NODE to user */
	if (pext->ResponseLen < sizeof(EXT_HBA_NODE))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_HBA_NODE);

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & tmp_hba_node, transfer_size);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_query_hba_node: exiting.\n");
	    );
	    return (ret);
}

static int
qla2x00_query_hba_port(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int ret = EXT_STATUS_OK;
	uint32_t tgt_cnt, tgt, transfer_size;
	uint32_t port_cnt;
	fc_port_t *fcport;
	static EXT_HBA_PORT tmp_hba_port;

	DEBUG3(printk("qla2x00_query_hba_port: entered.\n");
	    );

	    /* reflect all HBA PORT related info */
	    tmp_hba_port.WWPN[7] = ha->init_cb->port_name[7];
	tmp_hba_port.WWPN[6] = ha->init_cb->port_name[6];
	tmp_hba_port.WWPN[5] = ha->init_cb->port_name[5];
	tmp_hba_port.WWPN[4] = ha->init_cb->port_name[4];
	tmp_hba_port.WWPN[3] = ha->init_cb->port_name[3];
	tmp_hba_port.WWPN[2] = ha->init_cb->port_name[2];
	tmp_hba_port.WWPN[1] = ha->init_cb->port_name[1];
	tmp_hba_port.WWPN[0] = ha->init_cb->port_name[0];
	tmp_hba_port.Id[0] = 0;
	tmp_hba_port.Id[1] = ha->d_id.r.d_id[2];
	tmp_hba_port.Id[2] = ha->d_id.r.d_id[1];
	tmp_hba_port.Id[3] = ha->d_id.r.d_id[0];
	tmp_hba_port.Type = EXT_DEF_INITIATOR_DEV;

	switch (ha->current_topology) {
	case ISP_CFG_NL:
	case ISP_CFG_FL:
		tmp_hba_port.Mode = EXT_DEF_LOOP_MODE;
		break;
	case ISP_CFG_N:
	case ISP_CFG_F:
		tmp_hba_port.Mode = EXT_DEF_P2P_MODE;
		break;
	default:
		tmp_hba_port.Mode = EXT_DEF_UNKNOWN_MODE;
		break;
	}

	port_cnt = 0;
	for (fcport = ha->fcport; (fcport); fcport = fcport->next) {
		/* if removed or missing */
		if (fcport->state != FC_ONLINE)
			continue;
		port_cnt++;
	}

	tgt_cnt = 0;
	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if (ha->otgt[tgt] == NULL) {
			continue;
		}
		tgt_cnt++;
	}
	tmp_hba_port.DiscPortCount = port_cnt;
	tmp_hba_port.DiscTargetCount = tgt_cnt;

	if (ha->loop_down_timer == 0 && ha->loop_state == LOOP_DOWN) {
		tmp_hba_port.State = EXT_DEF_HBA_LOOP_DOWN;
	} else {
		tmp_hba_port.State = EXT_DEF_HBA_OK;
	}

	tmp_hba_port.DiscPortNameType = EXT_DEF_USE_PORT_NAME;

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_HBA_PORT));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_query_hba_port: ERROR verify write rsp " "buffer.\n");
		    );
		    return (ret);
	}

	/* now copy up the HBA_PORT to user */
	if (pext->ResponseLen < sizeof(EXT_HBA_PORT))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_HBA_PORT);

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & tmp_hba_port, transfer_size);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_query_hba_port: exiting.\n");
	    );
	    return (ret);
}

static int
qla2x00_query_disc_port(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int ret = EXT_STATUS_OK;
	uint32_t tgt, transfer_size, inst = 0;
	fc_port_t *fcport;
	os_tgt_t *tq;
	static EXT_DISC_PORT tmp_disc_port;

	DEBUG3(printk("qla2x00_query_disc_port: entered.\n");
	    );

	    for (fcport = ha->fcport; fcport != NULL; fcport = fcport->next) {
		if (fcport->state != FC_ONLINE)
			continue;

		if (inst != pext->Instance) {
			inst++;
			continue;
		}
		break;
	}

	if (fcport == NULL) {
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	memcpy( &tmp_disc_port.WWNN[0],&fcport->node_name[0], 8);
	memcpy( &tmp_disc_port.WWPN[0],&fcport->port_name[0], 8);

	tmp_disc_port.Id[0] = 0;
	tmp_disc_port.Id[1] = fcport->d_id.r.d_id[2];
	tmp_disc_port.Id[2] = fcport->d_id.r.d_id[1];
	tmp_disc_port.Id[3] = fcport->d_id.r.d_id[0];

	/* Currently all devices on fcport list are target capable devices */
	/* This default value may need to be changed after we add non target
	 * devices also to this list.
	 */
	tmp_disc_port.Type = EXT_DEF_TARGET_DEV;

	if (fcport->flags & FC_FABRIC_DEVICE) {
		tmp_disc_port.Type |= EXT_DEF_FABRIC_DEV;
	}
	if (fcport->flags & FC_TAPE_DEVICE) {
		tmp_disc_port.Type |= EXT_DEF_TAPE_DEV;
	}
	if (fcport->flags & FC_INITIATOR_DEVICE) {
		tmp_disc_port.Type |= EXT_DEF_INITIATOR_DEV;
	}

	tmp_disc_port.LoopID = fcport->loop_id;
	tmp_disc_port.Status = 0;
	tmp_disc_port.Bus = 0;

	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if ((tq = ha->otgt[tgt]) == NULL) {
			continue;
		}

		if (tq->vis_port == NULL)	/* dg 08/14/01 */
			continue;
		if (memcmp(&fcport->port_name[0], &tq->vis_port->port_name[0], EXT_DEF_WWN_NAME_SIZE) == 0) {
			tmp_disc_port.TargetId = tgt;
			break;
		}
	}

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_DISC_PORT));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_query_disc_port: ERROR verify write rsp " "buffer.\n");
		    );
		    return (ret);
	}

	/* now copy up the DISC_PORT to user */
	if (pext->ResponseLen < sizeof(EXT_DISC_PORT))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_DISC_PORT);

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & tmp_disc_port, transfer_size);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_query_disc_port: exiting.\n");
	    );
	    return (ret);
}

static int
qla2x00_query_disc_tgt(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int ret = EXT_STATUS_OK;
	uint32_t tgt, transfer_size, inst;
	uint32_t cnt, i;
	fc_port_t *tgt_fcport;
	os_tgt_t *tq;
	static EXT_DISC_TARGET tmp_disc_target;

	DEBUG3(printk("qla2x00_query_disc_tgt: entered.\n");
	    );

	    tq = NULL;
	for (tgt = 0, inst = 0; tgt < MAX_TARGETS; tgt++) {
		if (ha->otgt[tgt] == NULL) {
			continue;
		}
		/* if wrong target id then skip to next entry */
		if (inst != pext->Instance) {
			inst++;
			continue;
		}
		tq = ha->otgt[tgt];
		break;
	}

	if (tq == NULL || tgt == MAX_TARGETS) {
		DEBUG2(printk("qla2x00_query_disc_tgt: target dev not found. "
			      "tq=%x, tgt=%x.\n", (unsigned int) tq, tgt);
		    );
		    pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (pext->Status);
	}

	if (tq->vis_port == NULL) {	/* dg 08/14/01 */
		DEBUG2(printk("qla2x00_query_disc_tgt: target dev not found. "
			      "tq=%x, tgt=%x.\n", (unsigned int) tq, tgt);
		    );
		    pext->Status = EXT_STATUS_BUSY;
		return (pext->Status);
	}
	tgt_fcport = tq->vis_port;
	memcpy( &tmp_disc_target.WWNN[0],&tgt_fcport->node_name[0], 8);
	memcpy( &tmp_disc_target.WWPN[0],&tgt_fcport->port_name[0], 8);

	tmp_disc_target.Id[0] = 0;
	tmp_disc_target.Id[1] = tgt_fcport->d_id.r.d_id[2];
	tmp_disc_target.Id[2] = tgt_fcport->d_id.r.d_id[1];
	tmp_disc_target.Id[3] = tgt_fcport->d_id.r.d_id[0];

	/* All devices on ha->otgt list are target capable devices. */
	tmp_disc_target.Type = EXT_DEF_TARGET_DEV;

	if (tgt_fcport->flags & FC_FABRIC_DEVICE) {
		tmp_disc_target.Type |= EXT_DEF_FABRIC_DEV;
	}
	if (tgt_fcport->flags & FC_TAPE_DEVICE) {
		tmp_disc_target.Type |= EXT_DEF_TAPE_DEV;
	}
	if (tgt_fcport->flags & FC_INITIATOR_DEVICE) {
		tmp_disc_target.Type |= EXT_DEF_INITIATOR_DEV;
	}

	tmp_disc_target.LoopID = tgt_fcport->loop_id;
	tmp_disc_target.Status = 0;
	tmp_disc_target.Bus = 0;
	tmp_disc_target.TargetId = tgt;

	cnt = 0;
	/* enumerate available LUNs under this TGT (if any) */
	if (ha->otgt[tgt] != NULL) {
		for (i = 0; i < MAX_LUNS; i++) {
			if ((ha->otgt[tgt])->olun[i] != 0)
				cnt++;
		}
	}

	tmp_disc_target.LunCount = cnt;

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_DISC_TARGET));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_query_disc_tgt: ERROR verify write rsp " "buffer.\n");
		    );
		    return (pext->Status);
	}

	/* now copy up the DISC_PORT to user */
	if (pext->ResponseLen < sizeof(EXT_DISC_PORT))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_DISC_TARGET);

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & tmp_disc_target, transfer_size);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_query_disc_tgt: exiting.\n");
	    );
	    return (ret);
}

static int
qla2x00_query_chip(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int ret = EXT_STATUS_OK;
	uint32_t transfer_size, i;
	static EXT_CHIP tmp_isp;
	struct Scsi_Host *host;

	DEBUG3(printk("qla2x00_query_chip: entered.\n");
	    );

	    host = ha->host;
	tmp_isp.VendorId = QLA2X00_VENDOR_ID;
	tmp_isp.DeviceId = ha->device_id;
	tmp_isp.SubVendorId = QLA2X00_VENDOR_ID;
	tmp_isp.SubSystemId = 0;
	tmp_isp.PciBusNumber = ha->pci_bus;
	tmp_isp.PciSlotNumber = (ha->pci_device_fn & 0xf8) >> 3;
	tmp_isp.IoAddr = host->io_port;
	tmp_isp.IoAddrLen = 512;
	tmp_isp.MemAddr = 0;	/* ? */
	tmp_isp.MemAddrLen = 0;	/* ? */
	tmp_isp.ChipType = 0;	/* ? */
	tmp_isp.InterruptLevel = host->irq;

	for (i = 0; i < 8; i++)
		tmp_isp.OutMbx[i] = 0;

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_CHIP));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_query_chip: ERROR verify write rsp " "buffer.\n");
		    );
		    return (pext->Status);
	}

	/* now copy up the ISP to user */
	if (pext->ResponseLen < sizeof(EXT_CHIP))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_CHIP);

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & tmp_isp, transfer_size);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_query_chip: exiting.\n");
	    );
	    return (ret);
}






/*
 * busy-waits for 30 seconds or more
 */
int
qla2x00_send_loopback(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	int status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	unsigned long cpu_flags = 0;
	INT_LOOPBACK_REQ req;
	INT_LOOPBACK_RSP rsp;

	DEBUG3(printk("qla2x00_send_loopback: entered.\n"));

	if (pext->RequestLen != sizeof(INT_LOOPBACK_REQ)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG2(printk("qla2x00_send_loopback: invalid RequestLen =%d.\n", pext->RequestLen));
		return pext->Status;
	}

	if (pext->ResponseLen != sizeof(INT_LOOPBACK_RSP)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG2(printk("qla2x00_send_loopback: invalid ResponseLen =%d.\n", pext->ResponseLen));
		return pext->Status;
	}

	status = verify_area(VERIFY_READ, (void *) pext->RequestAdr, pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_send_loopback: ERROR verify read of " "request buffer.\n"));
		return pext->Status;
	}

	copy_from_user((uint8_t *) & req, (uint8_t *) pext->RequestAdr, pext->RequestLen);

	status = verify_area(VERIFY_READ, (void *) pext->ResponseAdr, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_send_loopback: ERROR verify read of " "response buffer.\n"));
		return pext->Status;
	}

	copy_from_user((uint8_t *) & rsp, (uint8_t *) pext->ResponseAdr, pext->ResponseLen);

	if (req.TransferCount > req.BufferLength || req.TransferCount > rsp.BufferLength) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG2(printk("qla2x00_send_loopback: invalid TransferCount =%d. "
			      "req BufferLength =%d rspBufferLength =%d.\n",
			      req.TransferCount, req.BufferLength, rsp.BufferLength));
		return pext->Status;
	}

	status = verify_area(VERIFY_READ, (void *) req.BufferAddress, req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_send_loopback: ERROR verify read of " "user loopback data buffer.\n"));
		return pext->Status;
	}

	copy_from_user((uint8_t *) ha->ioctl_mem, (uint8_t *) req.BufferAddress, req.TransferCount);

	DEBUG3(printk("qla2x00_send_loopback: req -- bufadr=%x, buflen=%x, "
		      "xfrcnt=%x, rsp -- bufadr=%x, buflen=%x.\n",
		      (uint32_t) req.BufferAddress, req.BufferLength, req.TransferCount,
		      (uint32_t) rsp.BufferAddress, rsp.BufferLength));

	memset(mb,0, MAILBOX_REGISTER_COUNT);
	mb[0] = MBC_DIAGNOSTIC_LOOP_BACK;
	mb[1] = req.Options;
	mb[10] = LSW(req.TransferCount);
	mb[11] = MSW(req.TransferCount);

	mb[14] = LSW(ha->ioctl_mem_phys);	/* send data address */
	mb[15] = MSW(ha->ioctl_mem_phys);
	mb[20] = QL21_64BITS_3RDWD(ha->ioctl_mem_phys);
	mb[21] = QL21_64BITS_4THWD(ha->ioctl_mem_phys);

	mb[16] = LSW(ha->ioctl_mem_phys);	/* rcv data address */
	mb[17] = MSW(ha->ioctl_mem_phys);
	mb[6] = QL21_64BITS_3RDWD(ha->ioctl_mem_phys);
	mb[7] = QL21_64BITS_4THWD(ha->ioctl_mem_phys);

	mb[18] = LSW(req.IterationCount);	/* iteration count lsb */
	mb[19] = MSW(req.IterationCount);	/* iteration count msb */

	DEBUG3(printk("qla2x00_send_loopback: req.Options=%x iterations=%x "
		      "MAILBOX_CNT=%d.\n", req.Options, req.IterationCount, MAILBOX_REGISTER_COUNT);
	    );

	/* get spin lock for this operation */
	spin_lock_irqsave(&io_request_lock, cpu_flags);

	DEBUG3(printk("qla2x00_send_loopback: issue loop back mailbox command\n"));

	status = qla2100_mailbox_command(ha, BIT_21 | BIT_20 | BIT_19 | BIT_18 |
					     BIT_17 | BIT_16 | BIT_15 | BIT_14 | BIT_13 | BIT_12 | BIT_11 |
					     BIT_10 | BIT_7 | BIT_6 | BIT_1 | BIT_0, &mb[0]);

	/* release spin lock since command is issued */
	spin_unlock_irqrestore(&io_request_lock, cpu_flags);

	if (status) {
		/* Empty. Just proceed to copy all mailbox values back. */
		DEBUG2(printk("qla2x00_send_loopback: mailbox command FAILED=%x.\n", mb[0]));
	}

	status = verify_area(VERIFY_WRITE, (void *) rsp.BufferAddress, req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_send_loopback: ERROR verify " "write of return data buffer.\n"));
		return pext->Status;
	}

	DEBUG3(printk("qla2x00_send_loopback: loopback mbx cmd ok. " "copying data.\n"));

	    /* put loopback return data in user buffer */
	copy_to_user((uint8_t *) rsp.BufferAddress, (uint8_t *) ha->ioctl_mem, req.TransferCount);

	rsp.CompletionStatus = mb[0];
	rsp.CrcErrorCount = mb[1];
	rsp.DisparityErrorCount = mb[2];
	rsp.FrameLengthErrorCount = mb[3];
	rsp.IterationCountLastError = (mb[19] << 16) | mb[18];

	status = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_send_loopback: ERROR verify " "write of response buffer.\n"));
		return pext->Status;
	}

	copy_to_user((uint8_t *) pext->ResponseAdr, (uint8_t *) & rsp, pext->ResponseLen);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("qla2x00_send_loopback: exiting.\n"));

	return pext->Status;
}


/*
 * qla2x00_aen_reg
 *	IOCTL management server Asynchronous Event Tracking Enable/Disable.
 *
 * Input:
 *	ha = adapter state pointer.
 *	cmd = EXT_IOCTL cmd struct pointer.
 *	mode = flags.
 *
 * Returns:
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
/* ARGSUSED */
static int
qla2x00_aen_reg(adapter_state_t * ha, EXT_IOCTL * cmd, int mode)
{
	int rval = 0;
	EXT_REG_AEN reg_struct;

	DEBUG3(printk("qla2x00_aen_reg: entered.\n"));

	rval = copy_from_user(&reg_struct,(void *) (cmd->RequestAdr), cmd->RequestLen);

	if (rval == 0) {
		cmd->Status = EXT_STATUS_OK;
		if (reg_struct.Enable) {
			ha->ioctl->flags |= IOCTL_AEN_TRACKING_ENABLE;
		} else {
			ha->ioctl->flags &= ~IOCTL_AEN_TRACKING_ENABLE;
		}
	} else {
		cmd->Status = EXT_STATUS_COPY_ERR;
		rval = EFAULT;
	}
	DEBUG3(printk("qla2x00_aen_reg: reg_struct. Enable(%d) "
		      "ha->ioctl_flag(%x) cmd->Status(%d) cmd->DetailStatus (%d).",
		      reg_struct.Enable, ha->ioctl->flags, cmd->Status, cmd->DetailStatus);
	    );

	    DEBUG3(printk("qla2x00_aen_reg: exiting.\n");
	    );

	    return (rval);
}

/*
 * qla2x00_aen_get
 *	IOCTL management server Asynchronous Event Record Transfer.
 *
 * Input:
 *	ha = adapter state pointer.
 *	cmd = EXT_IOCTL cmd struct pointer.
 *	mode = flags.
 *
 * Returns:
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_aen_get(adapter_state_t * ha, EXT_IOCTL * cmd, int mode)
{
	int rval = 0;
	EXT_ASYNC_EVENT *tmp_q;
	static EXT_ASYNC_EVENT aen[EXT_DEF_MAX_AEN_QUEUE];
	uint8_t i;
	uint8_t queue_cnt;
	uint8_t request_cnt;
	uint32_t stat = EXT_STATUS_OK;
	uint32_t dstat = EXT_STATUS_OK;
	uint32_t ret_len = 0;
	unsigned long flags = 0;

	DEBUG3(printk("qla2x00_aen_get: entered.\n");
	    );

	    request_cnt = (uint8_t) (cmd->ResponseLen / sizeof(EXT_ASYNC_EVENT));

	if (request_cnt < EXT_DEF_MAX_AEN_QUEUE) {
		/* We require caller to alloc for the maximum request count */
		cmd->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		rval = 0;
		DEBUG2(printk("qla2x00_aen_get: Buffer too small. Exiting normally.");
		    );
		    return (rval);
	}

	/* 1st: Make a local copy of the entire queue content. */
	tmp_q = (EXT_ASYNC_EVENT *) ha->ioctl->aen_tracking_queue;
	queue_cnt = 0;

	DRIVER_LOCK();
	spin_lock_irqsave(&io_request_lock, flags);
	i = ha->ioctl->aen_q_head;

	for (; queue_cnt < EXT_DEF_MAX_AEN_QUEUE;) {
		if (tmp_q[i].AsyncEventCode != 0) {
			memcpy( (void *) &aen[queue_cnt],(void *) &tmp_q[i], sizeof(EXT_ASYNC_EVENT));
			queue_cnt++;
			tmp_q[i].AsyncEventCode = 0;	/* empty out the slot */
		}

		if (i == ha->ioctl->aen_q_tail) {
			/* done. */
			break;
		}

		i++;

		if (i == EXT_DEF_MAX_AEN_QUEUE) {
			i = 0;
		}
	}

	/* Empty the queue. */
	ha->ioctl->aen_q_head = 0;
	ha->ioctl->aen_q_tail = 0;

	spin_unlock_irqrestore(&io_request_lock, flags);
	DRIVER_UNLOCK();
	    /* 2nd: Now transfer the queue content to user buffer */
	    /* Copy the entire queue to user's buffer. */
	ret_len = (uint32_t) (queue_cnt * sizeof(EXT_ASYNC_EVENT));
	if (queue_cnt != 0) {
		rval = copy_to_user((void *) (cmd->ResponseAdr), (void *) &aen[0],  ret_len);

	}
	cmd->ResponseLen = ret_len;

	if (rval != 0) {
		stat = EXT_STATUS_COPY_ERR;
		rval = EFAULT;
		DEBUG2(printk("qla2x00_aen_get: FAILED. error = %d\n", stat);
		    );
	} else {
		stat = EXT_STATUS_OK;
		rval = 0;
		DEBUG3(printk("qla2x00_aen_get: exiting normally.\n");
		    );
	}

	cmd->Status = stat;
	cmd->DetailStatus = dstat;

	DEBUG3(printk("qla2x00_aen_get: exiting. rval= %d\n", rval);
	    );

	    return (rval);
}


/*
 */
static int
qla2x00_get_fc_statistics(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	EXT_HBA_PORT_STAT tmp_stat;
	EXT_DEST_ADDR addr_struct;
	int ret;
	link_stat_t stat_buf;
	uint8_t i, rval, tgt;
	uint8_t *usr_temp, *kernel_tmp;
	uint8_t *req_name;
	uint16_t mb_stat[1];
	uint32_t transfer_size;

	DEBUG3(printk("entered qla2x00_get_fc_statistics function.\n"));

	ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_HBA_PORT_STAT));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_get_fc_statistics(%ld): ERROR " "VERIFY_WRITE.\n", ha->instance);
		    );
		    return (pext->Status);
	}

	rval = copy_from_user(&addr_struct,(void *) (pext->RequestAdr),  pext->RequestLen);
	if (rval != 0) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return (pext->Status);
	}

	/* find the device's loop_id */
	switch (addr_struct.DestType) {
	case EXT_DEF_DESTTYPE_WWPN:
		req_name = addr_struct.DestAddr.WWPN;
		for (tgt = 0; tgt < MAX_FIBRE_DEVICES; tgt++) {
			if (memcmp(ha->fc_db[tgt].wwn, req_name, EXT_DEF_WWN_NAME_SIZE) == 0)
				break;
		}
		break;

	case EXT_DEF_DESTTYPE_WWNN:
	case EXT_DEF_DESTTYPE_PORTID:
	case EXT_DEF_DESTTYPE_FABRIC:
	case EXT_DEF_DESTTYPE_SCSI:
	default:
		pext->Status = EXT_STATUS_INVALID_PARAM;
		pext->DetailStatus = EXT_DSTATUS_NOADNL_INFO;
		DEBUG2(printk("qla2x00_get_statistics(%ld): ERROR "
			      "Unsupported subcode address type.\n", ha->instance);
		    );
		    return (pext->Status);
		break;
	}

	if (tgt == MAX_FIBRE_DEVICES) {
		/* not found */
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		pext->DetailStatus = EXT_DSTATUS_TARGET;
		return (pext->Status);
	}

	/* check for suspended/lost device */
/*
	if (ha->fcport is suspended/lost) {
		pext->Status = EXT_STATUS_SUSPENDED;
		pext->DetailStatus = EXT_DSTATUS_TARGET;
		return (pext->Status);
	}
*/

	/* Send mailbox cmd to get more. */
	if ((rval = qla2100_get_link_status(ha, ha->fc_db[tgt].loop_id, &stat_buf, mb_stat)) != QL_STATUS_SUCCESS) {
		if (rval == BIT_0) {
			pext->Status = EXT_STATUS_NO_MEMORY;
		} else if (rval == BIT_1) {
			pext->Status = EXT_STATUS_MAILBOX;
			pext->DetailStatus = EXT_DSTATUS_NOADNL_INFO;
		} else {
			pext->Status = EXT_STATUS_ERR;
		}

		DEBUG2(printk("qla2x00_get_fc_statistics(%ld): ERROR "
			      "mailbox failed. mb[0]=%x.\n", ha->instance, mb_stat[0]);
		    );
		    return (pext->Status);
	}

	tmp_stat.ControllerErrorCount = ha->total_isp_aborts;
	tmp_stat.DeviceErrorCount = ha->total_dev_errs;
	tmp_stat.TotalIoCount = ha->total_ios;
	tmp_stat.TotalMBytes = ha->total_bytes / (1024 * 1024);
	tmp_stat.TotalLipResets = ha->total_lip_cnt;
/*
	tmp_stat.TotalInterrupts        =  ha->total_isr_cnt;
*/

	tmp_stat.TotalLinkFailures = stat_buf.link_fail_cnt;
	tmp_stat.TotalLossOfSync = stat_buf.loss_sync_cnt;
	tmp_stat.TotalLossOfSignals = stat_buf.loss_sig_cnt;
	tmp_stat.PrimitiveSeqProtocolErrorCount = stat_buf.prim_seq_err_cnt;
	tmp_stat.InvalidTransmissionWordCount = stat_buf.inval_xmit_word_cnt;
	tmp_stat.InvalidCRCCount = stat_buf.inval_crc_cnt;

	/* now copy up the STATISTICS to user */
	if (pext->ResponseLen < sizeof(EXT_HBA_PORT_STAT))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_HBA_PORT_STAT);

	for (i = 0; i < transfer_size; i++) {
		usr_temp = (uint8_t *) pext->ResponseAdr + i;
		kernel_tmp = (uint8_t *) & tmp_stat + i;
		if (put_user(*kernel_tmp, usr_temp))
			return -EFAULT;
	}
	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("finished qla2x00_get_fc_statistics function.\n"));

	return pext->Status;
}

/*
 */
static int
qla2x00_get_statistics(adapter_state_t * ha, EXT_IOCTL * pext, int mode)
{
	EXT_HBA_PORT_STAT tmp_stat;
	int ret;
	link_stat_t stat_buf;
	uint8_t i, rval;
	uint8_t *usr_temp, *kernel_tmp;
	uint16_t mb_stat[1];
	uint32_t transfer_size;

	DEBUG3(printk("entered qla2x00_get_statistics function.\n");
	    );

	    ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_HBA_PORT_STAT));
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG2(printk("qla2x00_get_statistics(%ld): ERROR " "VERIFY_WRITE.\n", ha->instance);
		    );
		    return (pext->Status);
	}

	/* Send mailbox cmd to get more. */
	if ((rval = qla2100_get_link_status(ha, ha->loop_id, &stat_buf, mb_stat)) != QL_STATUS_SUCCESS) {
		if (rval == BIT_0) {
			pext->Status = EXT_STATUS_NO_MEMORY;
		} else if (rval == BIT_1) {
			pext->Status = EXT_STATUS_MAILBOX;
			pext->DetailStatus = EXT_DSTATUS_NOADNL_INFO;
		} else {
			pext->Status = EXT_STATUS_ERR;
		}

		DEBUG2(printk("qla2x00_get_statistics(%ld): ERROR "
			      "mailbox failed. mb[0]=%x.\n", ha->instance, mb_stat[0]);
		    );
		    printk(KERN_WARNING
			   "qla2x00_get_statistics(%ld): ERROR "
			   "mailbox failed. mb[0]=%x.\n", ha->instance, mb_stat[0]);

		return (pext->Status);
	}

	tmp_stat.ControllerErrorCount = ha->total_isp_aborts;
	tmp_stat.DeviceErrorCount = ha->total_dev_errs;
	tmp_stat.TotalIoCount = ha->total_ios;
	tmp_stat.TotalMBytes = ha->total_bytes / (1024 * 1024);
	tmp_stat.TotalLipResets = ha->total_lip_cnt;
/*
	tmp_stat.TotalInterrupts        =  ha->total_isr_cnt;
*/

	tmp_stat.TotalLinkFailures = stat_buf.link_fail_cnt;
	tmp_stat.TotalLossOfSync = stat_buf.loss_sync_cnt;
	tmp_stat.TotalLossOfSignals = stat_buf.loss_sig_cnt;
	tmp_stat.PrimitiveSeqProtocolErrorCount = stat_buf.prim_seq_err_cnt;
	tmp_stat.InvalidTransmissionWordCount = stat_buf.inval_xmit_word_cnt;
	tmp_stat.InvalidCRCCount = stat_buf.inval_crc_cnt;

	/* now copy up the STATISTICS to user */
	if (pext->ResponseLen < sizeof(EXT_HBA_PORT_STAT))
		transfer_size = pext->ResponseLen;
	else
		transfer_size = sizeof(EXT_HBA_PORT_STAT);

	for (i = 0; i < transfer_size; i++) {
		usr_temp = (uint8_t *) pext->ResponseAdr + i;
		kernel_tmp = (uint8_t *) & tmp_stat + i;
		if (put_user(*kernel_tmp, usr_temp))
			return -EFAULT;
	}
	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG3(printk("finished qla2x00_get_statistics function.\n");
	    );

	    return (pext->Status);
}


static int
qla2x00_find_curr_ha(int inst, struct scsi_qla_host ** ret_ha)
{
	int rval = QL_STATUS_SUCCESS;
	struct scsi_qla_host *search_ha = NULL;

	/*
	 * Check for valid apiHBAInstance (set previously by EXT_SETINSTANCE 
	 * or default 0)  and set ha context for this IOCTL
	 */
	for (search_ha = qla2100_hostlist;
	     (search_ha != NULL) && search_ha->instance != inst; search_ha = search_ha->next)
		continue;

	if (search_ha == NULL) {
		DEBUG2(printk("qla2x00_ioctl: ERROR in matching apiHBAInstance "
			      "%d to an HBA Instance.\n", apiHBAInstance);
		    );
		    rval = QL_STATUS_ERROR;
	} else {
		*ret_ha = search_ha;
	}

	return (rval);
}

/*
 * qla2100_ms_req_pkt
 *      Function is responsible for locking ring and
 *      getting a zeroed out Managment Server request packet.
 *      This function is called with spinlocks helt and can
 *      therefore not sleep. However it does seem to busy-wait for
 *      over 60 seconds ;(
 *
 * Input:
 *      ha  = adapter block pointer.
 *      sp  = srb_t pointer to handle post function call
 * Returns:
 *      0 = failed to get slot.
 */
static request_t *qla2100_ms_req_pkt(struct scsi_qla_host * ha, srb_t * sp) {
	device_reg_t *reg = ha->iobase;
	request_t *pkt = 0;
	uint16_t cnt;
	uint32_t timer;

	ENTER("qla2100_ms_req_pkt");


	/* Wait for 60 seconds for slot. */
	for (timer = 300000; timer; timer--) {
		/* Acquire ring specific lock */
		QLA2100_RING_LOCK(ha);

		if (!ha->req_q_cnt) {
			/* Calculate number of free request entries. */
#if defined(ISP2100) || defined(ISP2200)
			cnt = qla2100_debounce_register(&reg->mailbox4);
#else
			cnt = qla2100_debounce_register(&reg->req_q_out);
#endif
			if (ha->req_ring_index < cnt)
				ha->req_q_cnt = cnt - ha->req_ring_index;
			else
				ha->req_q_cnt = REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
		}

		/* Check for room in outstanding command list. */
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS && (ha->outstanding_cmds[cnt] != 0); cnt++) ;

		if ((cnt < MAX_OUTSTANDING_COMMANDS) && (ha->req_q_cnt != 0)) {

			pkt = ha->request_ring_ptr;
	
			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			DEBUG5(printk("qla2100_ms_req: putting sp=%x in outstanding_cmds[%x]\n", sp, cnt));

			ha->outstanding_cmds[cnt] = sp;
			/* save the handle */
			sp->cmd->host_scribble = (unsigned char *) (u_long) cnt;

			ha->req_q_cnt--;
			pkt->handle = (uint32_t) cnt;

			/* Set system defined field. */
			pkt->sys_define = (uint8_t) ha->req_ring_index;
			pkt->entry_status = 0;

			break;
		}

		/* Release ring specific lock */
		QLA2100_RING_UNLOCK(ha);
		
		#warning long delay with io_request_lock helt
		udelay(20);

		/* Check for pending interrupts. */
		qla2100_poll(ha);
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_ms_req_pkt: **** FAILED ****\n");
	else
		LEAVE("qla2100_ms_req_pkt");
#endif
	return pkt;
}


/*************************************************************************
 * qla2x00_ioctl
 *
 * Description:
 *   Performs ioctl requests not satified by the upper levels.
 *
 * Returns:
 *   ret  = 0    Success
 *   ret != 0    Failed; detailed status copied to EXT_IOCTL structure
 *               if applicable
 *************************************************************************/
int
qla2x00_ioctl(Scsi_Device * dev, int cmd, void *arg)
{
	int mode = 0;
	int rval;
	int ret = EINVAL;
	unsigned long cpu_flags = 0;
	void *start_of_entry_list, *current_offset;

	uint8_t *usr_temp, *kernel_tmp;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint32_t i, b, t, l, port_cnt, status, entry;
	uint32_t tgt_cnt, tgt, transfer_size;
	uint32_t number_of_entries = 0;
	uint32_t qwait;
	uint32_t which_pass, scsi_direction;
	unsigned long handle;

	static EXT_DRIVER driver_prop;
	static EXT_DEVICEDATA devicedata;
	static EXT_DEVICEDATAENTRY dd_entry;
	static EXT_IOCTL ext;
	PEXT_IOCTL pext = &ext;
	static EXT_FC_SCSI_PASSTHRU fc_scsi_pass;
	EXT_FC_SCSI_PASSTHRU *pfc_scsi_pass = &fc_scsi_pass;
	static EXT_FW fw_prop;
	static EXT_SCSI_PASSTHRU scsi_pass;
	EXT_SCSI_PASSTHRU *pscsi_pass = &scsi_pass;

	static Scsi_Cmnd scsi_cmd;
	Scsi_Cmnd *pscsi_cmd = &scsi_cmd;
	static Scsi_Device scsi_device;
	struct Scsi_Host *host;

	adapter_state_t *ha;
	cmd_ms_iocb_entry_t *pkt;
	fc_lun_t temp_fclun;
	fc_lun_t *fclun = NULL;
	fc_port_t *fcport;
	os_lun_t *lq;
	os_tgt_t *tq;
	struct qla_boards *bdp;
	static srb_t ioctl_sp;
	srb_t *sp = &ioctl_sp;

	DEBUG3(printk("qla2x00_ioctl: entry to command (%x), arg (%p)\n", cmd, arg);	    );

	memset(sp,0, sizeof(srb_t));

	host = dev->host;
	ha = (struct scsi_qla_host *) host->hostdata;	/* midlayer chosen instance */

	ret = verify_area(VERIFY_READ, (void *) arg, sizeof(EXT_IOCTL));
	if (ret) {
		DEBUG2(printk("qla2x00_ioctl: ERROR VERIFY_READ of EXT_IOCTL " " sturct. ha=%8x.\n", (uint32_t) ha));
		return ret;
	}

	/* copy in application layer EXT_IOCTL */
	copy_from_user(pext, arg, sizeof(EXT_IOCTL));

	/* check signature of this ioctl */

	status = memcmp(&pext->Signature,"QLOGIC",4);

	if (status != 0) {
		DEBUG2(printk("qla2x00_ioctl: signature did not match. " "ha=%8x\n", (uint32_t) ha));
		ret = EXT_STATUS_INVALID_PARAM;
		return (ret);
	}

	/* check version of this ioctl */
	if (pext->Version > EXT_VERSION) {
		printk(KERN_WARNING "qla2x00: interface version not supported = %d.\n", pext->Version);
		pext->Status = EXT_STATUS_UNSUPPORTED_VERSION;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));
		ret = EXT_STATUS_ERR;
		return (ret);
	}

	/* check for API setting HBA Instance for subsequent operations */
	if (cmd == (int) EXT_CC_STARTIOCTL) {
		DEBUG3(printk("qla2x00_ioctl: got startioctl command.\n"));

		pext->Instance = num_hosts;
		pext->Status = EXT_STATUS_OK;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));
		return (EXT_STATUS_OK);

	};
	if (cmd == (int) EXT_CC_SETINSTANCE) {
		/*
		 * Since API opens devices once and uses handle for
		 * subsequent calls, we keep a parameter to designate
		 * the "active HBA" for ioctls.
		 */
		if (pext->HbaSelect < num_hosts) {
			apiHBAInstance = pext->Instance;
			/*
			 * Return host number in pext->HbaSelect for
			 * reference by IOCTL caller.
			 */
			if (qla2x00_find_curr_ha(apiHBAInstance, &ha) != 0) {
				DEBUG2(printk("qla2x00_ioctl: ERROR finding ha "
					      "in EXT_SETINSTANCE. Instance=%d "
					      "num_hosts=%d ha=%8x.\n", pext->Instance, num_hosts, (uint32_t) ha));
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				return (EXT_STATUS_ERR);
			}

			pext->HbaSelect = ha->host_no;
			pext->Status = EXT_STATUS_OK;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			DEBUG3(printk("qla2x00_ioctl: Setting instance to " "%d.\n", apiHBAInstance));
			return EXT_STATUS_OK;
		} 
		DEBUG2(printk("qla2x00_ioctl: ERROR in EXT_SETINSTANCE "
				      "Instance=%d num_hosts=%d ha=%8x.\n", pext->Instance, num_hosts, (uint32_t) ha));
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));
		return EXT_STATUS_ERR;
	}

	/*
	 * Check for valid apiHBAInstance (set previously by EXT_SETINSTANCE
	 * or default 0)  and set ha context for this IOCTL.
	 */
	if (qla2x00_find_curr_ha(apiHBAInstance, &ha) != 0) {
		DEBUG2(printk("qla2x00_ioctl: ERROR in matching apiHBAInstance "
			      "%d to an HBA Instance.\n", apiHBAInstance));

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));
		return EXT_STATUS_ERR;
	}

	/*
	 * Set EXT_IOCTL.HbaSelect to host number for reference by IOCTL
	 * caller.
	 */
	pext->HbaSelect = ha->host_no;

	DEBUG3(printk("qla2x00_ioctl: active apiHBAInstance=%d CC=%x SC=%x.\n", apiHBAInstance, cmd, pext->SubCode));

	while (ha->cfg_active || ha->dpc_active) {
		if (signal_pending(current))
			break;	/* get out */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ);
	};
	#warning race
#ifdef FC_MP_SUPPORT
	/* handle qlmultipath ioctls and return to user */
	if (cmd > SDM_QLMULTIPATH_IOCTLS) {
		printk(KERN_INFO "qla2x00: calling handle_mp_ioctl ha=%8x\n\n", (uint32_t) ha);
		ret = handle_mp_ioctl(ha, cmd, pext, arg);
		return ret;
	}
#endif

	switch (cmd) {		/* switch on EXT IOCTL COMMAND CODE */

	case EXT_CC_QUERY:
		DEBUG3(printk("qla2x00_ioctl: got query command.\n"));
		rval = qla2x00_query(ha, pext, 0);
		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

	case EXT_CC_GET_DATA:
		/* printk("[EXT_CC_GET_DATA SubCode=%x]\n",pext->SubCode); */
		switch (pext->SubCode) {
		case EXT_SC_GET_STATISTICS:
			rval = qla2x00_get_statistics(ha, pext, mode);
			ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			break;

		case EXT_SC_GET_FC_STATISTICS:
			rval = qla2x00_get_fc_statistics(ha, pext, mode);
			ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			break;

		case EXT_SC_GET_PORT_SUMMARY:
			port_cnt = 0;
			tgt_cnt = 0;

			for (fcport = ha->fcport; fcport != NULL; fcport = fcport->next) {
				port_cnt++;
			}

			number_of_entries = pext->ResponseLen / sizeof(EXT_DEVICEDATAENTRY);

			devicedata.TotalDevices = port_cnt;
			/* we want the lesser of   port_cnt and number_of_entries */
			if (number_of_entries > port_cnt)
				number_of_entries = port_cnt;
			devicedata.ReturnListEntryCount = number_of_entries;

			DEBUG3(printk("qla2x00_ioctl: EXT_SC_GET_PORT_SUMMARY port_cnt=%x, "
				      "return entry cnt=%x.\n", port_cnt, number_of_entries);
			    );

			transfer_size = sizeof(devicedata.ReturnListEntryCount) + sizeof(devicedata.TotalDevices);

			/* copy top of devicedata here */
			ret = verify_area(VERIFY_WRITE, (void *) (pext->ResponseAdr), transfer_size);
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area WRITE ha=%8x]\n", (uint32_t) ha));
				return ret;
			}
			for (i = 0; i < transfer_size; i++) {
				usr_temp = (uint8_t *) pext->ResponseAdr + i;
				kernel_tmp = (uint8_t *) & devicedata + i;
				if (put_user(*kernel_tmp, usr_temp))
					return -EFAULT;
			}

			start_of_entry_list = (void *) (pext->ResponseAdr) + transfer_size;

			for (entry = 0, fcport = ha->fcport;
			     (entry < number_of_entries) && (fcport); entry++, fcport = fcport->next) {

				/* copy from fc_db of this target (port) to dd_entry */

				memcpy( &dd_entry.NodeWWN[0],&fcport->node_name[0], 8);
				memcpy( &dd_entry.PortWWN[0],&fcport->port_name[0], 8);

				for (b = 0; b < 3; b++)
					dd_entry.PortID[b] = fcport->d_id.r.d_id[2 - b];

				if (fcport->flags & FC_FABRIC_DEVICE) {
					dd_entry.ControlFlags = EXT_DEF_GET_FABRIC_DEVICE;
				} else {
					dd_entry.ControlFlags = 0;
				}
				dd_entry.TargetAddress.Bus = 0;
				dd_entry.TargetAddress.Target = 0;
				dd_entry.TargetAddress.Lun = 0;
				dd_entry.DeviceFlags = 0;
				dd_entry.LoopID = fcport->loop_id;
				dd_entry.BaseLunNumber = 0;

				current_offset = (void *) (entry * sizeof(EXT_DEVICEDATAENTRY));
				ret = verify_area(VERIFY_WRITE,
						  (void *) ((start_of_entry_list +
							     (u_long) current_offset)), sizeof(EXT_DEVICEDATAENTRY));
				if (ret) {
					pext->Status = EXT_STATUS_COPY_ERR;
					copy_to_user(arg, pext, sizeof(EXT_IOCTL));
					DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area WRITE ha=%8x]\n", (uint32_t) ha));
					return (ret);
				}

				/* now copy up this dd_entry to user */
				transfer_size = sizeof(EXT_DEVICEDATAENTRY);
				for (i = 0; i < transfer_size; i++) {
					usr_temp = (uint8_t *) ((u_long) start_of_entry_list +
								(u_long) current_offset + i);
					kernel_tmp = (uint8_t *) & dd_entry + i;
					if (put_user(*kernel_tmp, usr_temp))
						return -EFAULT;
				}
			}	/* for number_of_entries */

			pext->Status = EXT_STATUS_OK;
			pext->DetailStatus = EXT_STATUS_OK;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_OK;
			/* printk("[finished QLA2100 EXT_SC_GET_PORT_SUMMARY ]\n"); */
			break;

		case EXT_SC_QUERY_DRIVER:
			/* printk("[started EXT_SC_QUERY_DRIVER]\n"); */
			sprintf(driver_prop.Version, QLA2100_VERSION);
			driver_prop.NumOfBus = MAX_BUSES;
			driver_prop.TargetsPerBus = MAX_FIBRE_DEVICES;
			driver_prop.LunsPerTarget = MAX_LUNS;
			driver_prop.MaxTransferLen = 0xffffffff;
			driver_prop.MaxDataSegments = 0xffffffff;
			if (ha->flags.enable_64bit_addressing == 1)
				driver_prop.DmaBitAddresses = 64;
			else
				driver_prop.DmaBitAddresses = 32;
			driver_prop.IoMapType = 0;
			driver_prop.Attrib = 0;
			driver_prop.InternalFlags[0] = 0;
			driver_prop.InternalFlags[1] = 0;
			driver_prop.InternalFlags[2] = 0;
			driver_prop.InternalFlags[3] = 0;

			ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_DRIVER));
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area WRITE ha=%8x]\n", (uint32_t) ha));
				return (ret);
			}
			/* now copy up the ISP to user */
			if (pext->ResponseLen < sizeof(EXT_DRIVER))
				transfer_size = pext->ResponseLen;
			else
				transfer_size = sizeof(EXT_DRIVER);
			for (i = 0; i < transfer_size; i++) {
				usr_temp = (uint8_t *) pext->ResponseAdr + i;
				kernel_tmp = (uint8_t *) & driver_prop + i;
				if (put_user(*kernel_tmp, usr_temp)) 
					return -EFAULT;
			}
			pext->Status = EXT_STATUS_OK;
			pext->DetailStatus = EXT_STATUS_OK;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_OK;
			break;

		case EXT_SC_QUERY_FW:
			/*  printk("[started EXT_SC_QUERY_FW]\n"); */
			bdp = &QLBoardTbl_fc[ha->devnum];
			fw_prop.Version[0] = bdp->fwver[0];
			fw_prop.Version[1] = bdp->fwver[1];
			fw_prop.Version[2] = bdp->fwver[2];
			ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, sizeof(EXT_FW));
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area WRITE ha=%8x]\n", (uint32_t) ha));
				    return (ret);
			}
			for (i = 0; i < 3; i++) {
				usr_temp = (uint8_t *) pext->ResponseAdr + i;
				kernel_tmp = (uint8_t *) & fw_prop + i;
				/*    printk("{%x}",*kernel_tmp); */
				if (put_user(*kernel_tmp, usr_temp))
					return -EFAULT;
			}
			/*  printk("[finished EXT_SC_QUERY_FW]\n"); */
			pext->Status = EXT_STATUS_OK;
			pext->DetailStatus = EXT_STATUS_OK;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_OK;
			break;

		default:
			DEBUG(printk("[unknown SubCode for EXT_CC_QUERY]\n"));
			ret = EXT_STATUS_ERR;
			break;
		}		/* end of SubCode decode for EXT_CC_GET_DATA */
		break;

	case EXT_CC_SEND_FCCT_PASSTHRU:
		DEBUG(printk("qla2x00_ioctl: start EXT_CC_SEND_FCCT_PASSTHRU\n"));
		/* Management Server type (fc switch) pass thru ioctl */
		/* same as EXT_FCSCSI_REQ but it is going to the FC switch */
		/* clear ioctl_sp and scsi_cmd to be used */
		kernel_tmp = (uint8_t *) sp;
		for (i = 0; i < sizeof(srb_t); i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) ha->ioctl_mem;
		for (i = 0; i < PAGE_SIZE; i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) pscsi_cmd;
		for (i = 0; i < sizeof(Scsi_Cmnd); i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) & scsi_device;
		for (i = 0; i < sizeof(Scsi_Device); i++)
			*kernel_tmp = 0;

		/*printk("[start EXT_CC_SEND_FCCT_PASSTHRU]\n"); */
		if (pext->ResponseLen > PAGE_SIZE)
			pext->ResponseLen = PAGE_SIZE;
		if (pext->RequestLen > PAGE_SIZE) {
			pext->Status = EXT_STATUS_INVALID_PARAM;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			/*printk("[EXT_CC_SEND_FCCT_PASSTHRU too big ResponseLen=%x ReqLen=%x]\n",pext->ResponseLen,pext->RequestLen); */
			DEBUG2(printk
			       ("[qla2x00_ioctl: ERROR size of requested Resp_len in EXT_CC_SEND_FCCT_PASSTHRU]\n"));
			return ret;
		}
		ret = verify_area(VERIFY_READ, (void *) pext->RequestAdr, pext->RequestLen);
		if (ret) {
			pext->Status = EXT_STATUS_COPY_ERR;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			/*printk("[EXT_CC_SEND_FCCT_PASSTHRU verify read error]\n"); */
			DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area READ of EXT_CC_SEND_FCCT_PASSTHRU]\n"));
			return (ret);
		}
		for (i = 0; i < pext->RequestLen; i++) {
			/* copy in from user space the fcct command to be sent */
			usr_temp = (uint8_t *) pext->RequestAdr + i;
			kernel_tmp = (uint8_t *) ha->ioctl_mem + i;
			if (get_user(*kernel_tmp, usr_temp))
				return -EFAULT;
			/* printk("{%x}",*kernel_tmp); */
		}
		/* check on current topology or loop down */
		if ((ha->current_topology != ISP_CFG_F) && (ha->current_topology != ISP_CFG_FL)) {
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			/*printk("[EXT_CC_SEND_FCCT_PASSTHRU wrong topology current=%x]\n",
			   ha->current_topology); */
			DEBUG2(printk("[qla2x00_ioctl: ERROR EXT_CC_SEND_FCCT_PASSTHRU not in F-Port or FL-Port mode]\n"));
			return ret;
		}
		/* check on loop down */
		if (ha->loop_down_timer == 0 && ha->loop_state == LOOP_DOWN) {
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			/* printk("[EXT_CC_SEND_FCCT_PASSTHRU loop down]\n"); */
			DEBUG2(printk("[qla2x00_ioctl: ERROR EXT_CC_SEND_FCCT_PASSTHRU not in F-Port mode]\n"));
			return ret;
		}
		/* login to management server device */
		if (ha->flags.managment_server_logged_in == 0) {
			mb[0] = MBC_LOGIN_FABRIC_PORT;
			mb[1] = MANAGEMENT_SERVER << 8;
			mb[2] = 0xff;
			mb[3] = 0xfffa;

			ret = qla2100_mailbox_command(ha, BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0]);
			if ((ret != 0) || (mb[0] == 0x4006) || (mb[0] == 0x4009) || (mb[0] != 0x4000)) {
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				/* printk("[EXT_CC_SEND_FCCT_PASSTHRU could not login to sns]\n"); */
				DEBUG2(printk("[qla2x00_ioctl: ERROR could not login to Management Server]\n"));
				return ret;
			}
			ha->flags.managment_server_logged_in = 1;
		}

		/* setup  sp  for this FCCT pass thru */
		pscsi_cmd->host = ha->host;
		sp->cmd = pscsi_cmd;
		sp->flags = SRB_WATCHDOG;

		/* mark this as a special delivery and collection command */
		scsi_cmd.flags = 0;
		scsi_cmd.scsi_done = qla2x00_fcct_done;

		DEBUG(printk("FCCT ioctl: FABRIC_LOGIN OK, call qla2x00_ms_req_pkt\n"));

		/* get spin lock for this operation */
		spin_lock_irqsave(&io_request_lock, cpu_flags);

		/* Get MS request packet. */
		/* Note: ms_req_pkt busy waits for 60 seconds max */
		if ((pkt = (cmd_ms_iocb_entry_t *) qla2100_ms_req_pkt(ha, sp))) {
			pkt->entry_type = MS_IOCB_TYPE;
			pkt->entry_count = 1;
			pkt->loop_id = MANAGEMENT_SERVER;
			pkt->timeout = 10;
			pkt->DSDcount = 1;
			pkt->RespDSDcount = 2;
			pkt->Response_bytecount = pext->ResponseLen;
			pkt->Request_bytecount = pext->RequestLen;

			/* loading command payload address */
			pkt->dseg_req_address[0] = LS_64BITS(ha->ioctl_mem_phys);
			pkt->dseg_req_address[1] = MS_64BITS(ha->ioctl_mem_phys);
			pkt->dseg_req_length = pext->RequestLen;

			/* loading command response address */
			pkt->dseg_rsp_address[0] = LS_64BITS(ha->ioctl_mem_phys);
			pkt->dseg_rsp_address[1] = MS_64BITS(ha->ioctl_mem_phys);
			pkt->dseg_rsp_length = pext->ResponseLen;

			/* set flag to indicate IOCTL FCCT PassThru in progress */
			ha->IoctlPassFCCT_InProgress = 1;
			ha->ioctl_timer = pkt->timeout + 1;	/* 1 second more */

			/* Issue command to ISP */
			qla2100_isp_cmd(ha);

		}
		/* release spin lock since command is issued */
		spin_unlock_irqrestore(&io_request_lock, cpu_flags);

		DEBUG(printk("FCCT ioctl: Command issued and released spin lock\n"));

		if (!pkt) {
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			DEBUG2(printk("qla2x00_ioctl: FCCT_PASSTHRU - could not get " "Request Packet.\n"));
			return ret;
		}

		/* wait for post function or timer to zero the InProgress flag */
#if 0
		qwait = ha->ioctl_timer * 1000;
		do {
			if (ha->IoctlPassFCCT_InProgress == 0)
				break;

			mdelay(1);
		} while (qwait--);
#else
		qwait = ha->ioctl_timer * 10;
		do {
			if (ha->IoctlPassThru_InProgress == 0)
				break;

			qla2x00_ioctl_sleep(ha, 10);
		} while (qwait--);
#endif
		DEBUG3(printk("qla2x00_fcct_passthru: finished while loop.\n"));

		if (ha->IoctlPassFCCT_InProgress == 1) {
			/* We waited and post function did not get called */
			/* printk("[FCCT IOCTL post function not called]\n"); */
			DEBUG(printk("FCCT ioctl: post function not called \n"));
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_ERR;
		} else {
			/* getting device data and putting in pext->ResponseAdr */
			/* printk("[post function called; start FCCT IOCTL returning up data ]\n"); */
			ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr, pext->ResponseLen);
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area WRITE for IOCTL PT ha=%8x]\n",
					      (uint32_t) ha));
				return ret;
			}
			/* sending back data returned from Management Server */
			for (i = 0; i < pext->ResponseLen; i++) {
				usr_temp = (uint8_t *) pext->ResponseAdr + i;
				kernel_tmp = (uint8_t *) ha->ioctl_mem + i;
				/*printk("[%x]",*kernel_tmp); */
				if (put_user(*kernel_tmp, usr_temp))
					return -EFAULT;
			}
			/*printk("[finished QLA2100 IOCTL EXT_CC_SEND_FCCT_PASSTHRU]\n"); */
			pext->Status = EXT_STATUS_SCSI_STATUS;
			pext->DetailStatus = sp->scode;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_OK;
		}
		break;

	case EXT_CC_SEND_SCSI_PASSTHRU:
		/* clear ioctl_sp and scsi_cmd and scsi_device to be used */
		kernel_tmp = (uint8_t *) sp;
		for (i = 0; i < sizeof(srb_t); i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) pscsi_cmd;
		for (i = 0; i < sizeof(Scsi_Cmnd); i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) & scsi_device;
		for (i = 0; i < sizeof(Scsi_Device); i++)
			*kernel_tmp = 0;
		kernel_tmp = (uint8_t *) ha->ioctl_mem;
		for (i = 0; i < PAGE_SIZE; i++)
			*kernel_tmp = 0;
		pscsi_cmd->sense_buffer[0] = 0;
		pscsi_cmd->sense_buffer[1] = 0;
		pscsi_cmd->sense_buffer[2] = 0;

		/* printk("[start EXT_CC_SEND_SCSI_PASSTHRU]\n"); */

		switch (pext->SubCode) {	/* get target specification accordingly */
		case EXT_SC_SEND_SCSI_PASSTHRU:
			memset(sp,0, sizeof(*sp));
			/* printk("[start EXT_SC_SEND_SCSI_PASSTHRU]\n"); */
			ret = verify_area(VERIFY_READ, (void *) pext->RequestAdr, sizeof(EXT_SCSI_PASSTHRU));
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area READ of EXT_SCSI_PASSTHRU]\n"));
				return (ret);
			}
			for (i = 0; i < sizeof(EXT_SCSI_PASSTHRU); i++) {
				usr_temp = (uint8_t *) pext->RequestAdr + i;
				kernel_tmp = (uint8_t *) pscsi_pass + i;
				if (get_user(*kernel_tmp, usr_temp))
					return -EFAULT;
			}
			/* set target coordinates */
			scsi_cmd.target = pscsi_pass->TargetAddr.Target;
			scsi_cmd.lun = pscsi_pass->TargetAddr.Lun;
			printk(KERN_INFO "[start EXT_SC_SEND_SCSI_PASSTHRU T=%x L=%x cmd=%x] ",
			       pscsi_pass->TargetAddr.Target, pscsi_pass->TargetAddr.Lun, pscsi_pass->Cdb[0]);
			if (pscsi_pass->CdbLength == 6) {
				scsi_cmd.cmd_len = 6;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pscsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pscsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pscsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pscsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pscsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pscsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = 0;
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = 0;
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = 0;
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = 0;
			} else if (pscsi_pass->CdbLength == 10) {
				scsi_cmd.cmd_len = 0x0A;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pscsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pscsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pscsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pscsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pscsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pscsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = pscsi_pass->Cdb[6];
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = pscsi_pass->Cdb[7];
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = pscsi_pass->Cdb[8];
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = pscsi_pass->Cdb[9];
			} else if (pscsi_pass->CdbLength == 12) {
				scsi_cmd.cmd_len = 0x0C;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pscsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pscsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pscsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pscsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pscsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pscsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = pscsi_pass->Cdb[6];
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = pscsi_pass->Cdb[7];
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = pscsi_pass->Cdb[8];
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = pscsi_pass->Cdb[9];
				scsi_cmd.data_cmnd[10] = scsi_cmd.cmnd[10] = pscsi_pass->Cdb[10];
				scsi_cmd.data_cmnd[11] = scsi_cmd.cmnd[11] = pscsi_pass->Cdb[11];
			} else {
				printk(KERN_WARNING "qla2x00_ioctl: SCSI_PASSTHRU Unknown Cdb Length=%x.\n",
				       pscsi_pass->CdbLength);
			}
			which_pass = EXT_SC_SEND_SCSI_PASSTHRU;
			break;

		case EXT_SC_SEND_FC_SCSI_PASSTHRU:
			DEBUG4(printk("[start EXT_SC_SEND_FC_SCSI_PASSTHRU]\n"));
			ret = verify_area(VERIFY_READ, (void *) pext->RequestAdr, sizeof(EXT_FC_SCSI_PASSTHRU));
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area READ of EXT_SCSI_FC_PASSTHRU]\n"));
				return ret;
			}
			for (i = 0; i < sizeof(EXT_FC_SCSI_PASSTHRU); i++) {
				usr_temp = (uint8_t *) pext->RequestAdr + i;
				kernel_tmp = (uint8_t *) pfc_scsi_pass + i;
				if (get_user(*kernel_tmp, usr_temp))
					return -EFAULT;
			}
			if (fc_scsi_pass.FCScsiAddr.DestType != EXT_DEF_DESTTYPE_WWPN) {
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: [EXT_FC_SCSI_PASSTHRU] - ERROR wrong Dest type. \n"));
				ret = EXT_STATUS_ERR;
				return ret;
			}

			fclun = NULL;
			tgt = MAX_FIBRE_DEVICES;
			for (fcport = ha->fcport; (fcport); fcport = fcport->next) {
				if (memcmp(fcport->port_name, fc_scsi_pass.FCScsiAddr.DestAddr.WWPN, 8) == 0) {
					for (fclun = fcport->fclun; fclun; fclun = fclun->next) {
						if (fclun->lun == fc_scsi_pass.FCScsiAddr.Lun) {
							break;
						}
					}
					break;
				}
			}

			if (fcport == NULL) {
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[SCSI_PASSTHRU FC AddrFormat  DID NOT FIND Port for WWPN]\n"));
				return (pext->Status);
			}
			/* v5.21b9 - use a temporary fclun */
			if (fclun == NULL) {
				fclun = &temp_fclun;
				fclun->fcport = fcport;
				fclun->lun = fc_scsi_pass.FCScsiAddr.Lun;
				fclun->flags = 0;
				fclun->next = NULL;
			}
			/* set target coordinates */
			scsi_cmd.target = tgt;
			scsi_cmd.lun = fc_scsi_pass.FCScsiAddr.Lun;
			DEBUG4(printk("[SCSI_PASSTHRU FC AddrFormat  T=%x L=%x]\n", tgt, scsi_cmd.lun));
			if (pfc_scsi_pass->CdbLength == 6) {
				scsi_cmd.cmd_len = 6;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pfc_scsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pfc_scsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pfc_scsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pfc_scsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pfc_scsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pfc_scsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = 0;
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = 0;
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = 0;
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = 0;
			} else if (pfc_scsi_pass->CdbLength == 10) {
				scsi_cmd.cmd_len = 0x0A;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pfc_scsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pfc_scsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pfc_scsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pfc_scsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pfc_scsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pfc_scsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = pfc_scsi_pass->Cdb[6];
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = pfc_scsi_pass->Cdb[7];
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = pfc_scsi_pass->Cdb[8];
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = pfc_scsi_pass->Cdb[9];
			} else if (pfc_scsi_pass->CdbLength == 12) {
				scsi_cmd.cmd_len = 0x0C;
				scsi_cmd.data_cmnd[0] = scsi_cmd.cmnd[0] = pfc_scsi_pass->Cdb[0];
				scsi_cmd.data_cmnd[1] = scsi_cmd.cmnd[1] = pfc_scsi_pass->Cdb[1];
				scsi_cmd.data_cmnd[2] = scsi_cmd.cmnd[2] = pfc_scsi_pass->Cdb[2];
				scsi_cmd.data_cmnd[3] = scsi_cmd.cmnd[3] = pfc_scsi_pass->Cdb[3];
				scsi_cmd.data_cmnd[4] = scsi_cmd.cmnd[4] = pfc_scsi_pass->Cdb[4];
				scsi_cmd.data_cmnd[5] = scsi_cmd.cmnd[5] = pfc_scsi_pass->Cdb[5];
				scsi_cmd.data_cmnd[6] = scsi_cmd.cmnd[6] = pfc_scsi_pass->Cdb[6];
				scsi_cmd.data_cmnd[7] = scsi_cmd.cmnd[7] = pfc_scsi_pass->Cdb[7];
				scsi_cmd.data_cmnd[8] = scsi_cmd.cmnd[8] = pfc_scsi_pass->Cdb[8];
				scsi_cmd.data_cmnd[9] = scsi_cmd.cmnd[9] = pfc_scsi_pass->Cdb[9];
				scsi_cmd.data_cmnd[10] = scsi_cmd.cmnd[10] = pfc_scsi_pass->Cdb[10];
				scsi_cmd.data_cmnd[11] = scsi_cmd.cmnd[11] = pfc_scsi_pass->Cdb[11];
			} else {
				printk(KERN_WARNING "qla2x00_ioctl: FC_SCSI_PASSTHRU Unknown Cdb Length=%x.\n",
				       pfc_scsi_pass->CdbLength);
			}
			which_pass = EXT_SC_SEND_FC_SCSI_PASSTHRU;
			break;

		default:
			/* printk("[SCSI_PASSTHRU UNKNOWN ADDRESS FORMAT]\n"); */
			pext->Status = EXT_STATUS_INVALID_PARAM;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			DEBUG2(printk
			       ("[qla2x00_ioctl: ERROR in SubCode decode of EXT_CC_SEND_SCSI_PASSTHRU ha=%8x]\n",
				(uint32_t) ha);
			    );
			ret = EXT_STATUS_ERR;
			return (ret);
		}		/* end of switch for EXT_SC_SEND_SCSI_PASSTHRU or EXT_SC_SEND_FC_SCSI_PASSTHRU */

		DEBUG(printk("Dump of cdb buffer:\n"));
		DEBUG(qla2100_dump_buffer((uint8_t *) & scsi_cmd.data_cmnd[0], 16));
      /******** Common Portion of SCSI PassThru Operations ********/
		if (pext->ResponseLen > PAGE_SIZE) {
			pext->Status = EXT_STATUS_INVALID_PARAM;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			DEBUG2(printk("[qla2x00_ioctl: ERROR size of requested EXT_SCSI_PASSTHRU]\n"));
			return (ret);
		}
		pscsi_cmd->host = ha->host;
		sp->cmd = pscsi_cmd;
		sp->flags = 0;	//SRB_WATCHDOG;
		/* set local scsi_cmd's sp pointer to sp */
		CMD_SP(pscsi_cmd) = (void *) sp;

		/* mark this as a special delivery and collection command */
		scsi_cmd.flags = 0;
		scsi_cmd.scsi_done = qla2x00_scsi_pt_done;

		scsi_cmd.device = &scsi_device;
		scsi_cmd.device->tagged_queue = 0;
		scsi_cmd.use_sg = 0;	/* no ScatterGather */
		scsi_cmd.request_bufflen = pext->ResponseLen;
		scsi_cmd.request_buffer = ha->ioctl_mem;
		scsi_cmd.timeout_per_command = QLA_CMD_TIMEOUT * HZ;

		DEBUG4(printk("[start FCSCSI IOCTL look at direction t=%x l=%x]\n", scsi_cmd.target, scsi_cmd.lun));
		if (pscsi_pass->Direction == EXT_DEF_SCSI_PASSTHRU_DATA_OUT ||
			pfc_scsi_pass->Direction == EXT_DEF_SCSI_PASSTHRU_DATA_OUT) {
			/* sending user data from pext->ResponseAdr to device */
			ret = verify_area(VERIFY_READ, (void *) pext->ResponseAdr, pext->ResponseLen);
			if (ret) {
				pext->Status = EXT_STATUS_COPY_ERR;
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				DEBUG2(printk("[qla2x00_ioctl: ERROR verify_area READ EXT_SCSI_PASSTHRU]\n"));
				return pext->Status;
			}
/* 03/20 -- missing direction */
			scsi_cmd.sc_data_direction = SCSI_DATA_WRITE;
			usr_temp = (uint8_t *) pext->ResponseAdr;
			kernel_tmp = (uint8_t *) ha->ioctl_mem;
			copy_from_user(kernel_tmp, usr_temp, pext->ResponseLen);
		}
		else
			scsi_cmd.sc_data_direction = SCSI_DATA_READ;
		/* Generate LU queue on bus, target, LUN */
		b = SCSI_BUS_32(pscsi_cmd);
		t = SCSI_TCN_32(pscsi_cmd);
		l = SCSI_LUN_32(pscsi_cmd);

		tq = ha->ioctl->ioctl_tq;
		lq = ha->ioctl->ioctl_lq;
		DEBUG4(printk("[FC_SCSI_PASSTHRU: ha instance=%d tq=%p lq=%p fclun=%p]\n", ha->instance, tq, lq, fclun));
		if (which_pass == EXT_SC_SEND_FC_SCSI_PASSTHRU && fclun && tq && lq) {
			memset(tq,0, sizeof(os_tgt_t));
			memset(lq,0, sizeof(os_lun_t));

			tq->olun[fclun->lun] = lq;
			tq->ha = ha;
			lq->fclun = fclun;
			fcport = fclun->fcport;
		} else if ((tq = (os_tgt_t *) TGT_Q(ha, t)) != NULL && (lq = (os_lun_t *) LUN_Q(ha, t, l)) != NULL) {

			fcport = lq->fclun->fcport;
		} else {
			lq = NULL;
			fcport = NULL;
		}
		DEBUG4(printk("[FC_SCSI_PT CDB=%x %x %x %x ; b=%x t=%x l=%x]\n",
			      scsi_cmd.cmnd[0], scsi_cmd.cmnd[1], scsi_cmd.cmnd[2], scsi_cmd.cmnd[3], b, t, l));
		/* set sp->target for 32bit/64bit delivery */
		sp->wdg_time = 0;

		/* Setup device queue pointers. */
		sp->tgt_queue = tq;
		sp->lun_queue = lq;

		/* check presense of requested target and other conditions */
		if (fcport == NULL || (ha->loop_down_timer == 0 && ha->loop_state == LOOP_DOWN)) {
			DEBUG3(printk("[SCSI PASSTHRU IOCTL Target/Lun MISSING fcport=%x]\n", fcport));
			if (fcport == NULL) {
				printk(KERN_INFO "scsi(%2d:%2d:%2d:%2d): qla2x00 PassThur port is unavailable\n",
				       (int) ha->host_no, b, t, l);
			} else {
				printk(KERN_INFO "scsi(%2d:%2d:%2d:%2d): qla2x00 PassThur - loop is down\n",
				       (int) ha->host_no, b, t, l);
			}
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			return (pext->Status);
		}

		/* If lun is suspended wait for it */
		while ((lq->q_flag & LUN_QUEUE_SUSPENDED)) {
			qla2x00_ioctl_sleep(ha, HZ);
		};

		/* get spin lock for this operation */
		spin_lock_irqsave(&io_request_lock, cpu_flags);

		/* Set an invalid handle until we issue the command to ISP */
		/* then we will set the real handle value.                 */
		handle = INVALID_HANDLE;
		pscsi_cmd->host_scribble = (unsigned char *) handle;

		if (sp->flags) {
			sp->port_down_retry_count = ha->port_down_retry_count - 1;
			sp->retry_count = ha->retry_count;
			DEBUG3(printk("qla2x00: PT Set retry counts =0x%x,0x%x\n\r",
				      sp->port_down_retry_count, sp->retry_count));
		}
		ha->qthreads++;
		ha->total_ios++;
		add_to_cmd_queue(ha, lq, sp);

		DEBUG5(printk("qla2x00 ioctl scsi passthru: queue hndl=0x%x\n\r", handle));

		/* set flag to indicate IOCTL SCSI PassThru in progress */
		ha->IoctlPassThru_InProgress = 1;

		DEBUG5(qla2x00_print_scsi_cmd(pscsi_cmd));
		    //printk("[start FC_SCSI IOCTL restart queues]\n");
		    /* send command to adapter */
		qla2x00_next(ha, tq, lq);
		ha->ioctl_timer = (int) QLA_CMD_TIMEOUT + 1;
		/* release spin lock since command is queued */
		spin_unlock_irqrestore(&io_request_lock, cpu_flags);

		/* wait for post function or timer to zero the InProgress flag */
#if 0
		qwait = ha->ioctl_timer * 1000;
		do {
			if (ha->IoctlPassThru_InProgress == 0)
				break;

			mdelay(1);
		} while (qwait--);
#else
		qwait = ha->ioctl_timer * 10;
		do {
			if (ha->IoctlPassThru_InProgress == 0)
				break;

			qla2x00_ioctl_sleep(ha, 10);
		} while (qwait--);
#endif
		/* Look for successful completion */

		if (ha->IoctlPassThru_InProgress == 1) {

			printk(KERN_WARNING "qla2x00: scsi%ld ERROR passthru command timeout.\n", ha->host_no);

			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			copy_to_user(arg, pext, sizeof(EXT_IOCTL));
			ret = EXT_STATUS_ERR;

		} else {

			switch (sp->ccode) {
			case CS_INCOMPLETE:
			case CS_ABORTED:
			case CS_PORT_UNAVAILABLE:
			case CS_PORT_LOGGED_OUT:
			case CS_PORT_CONFIG_CHG:
			case CS_PORT_BUSY:
				DEBUG2(printk("qla2x00_ioctl: scsi passthru cs err = %x.\n", sp->ccode);
				    );
				    ret = EXT_STATUS_ERR;
				pext->Status = EXT_STATUS_BUSY;
				break;
			}
			if (!(sp->flags & SRB_ISP_COMPLETED)) {
				/* We waited and post function did not get called */
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				DEBUG2(printk("qla2100_ioctl: scsi%ld ERROR command timeout\n", ha->host_no);
				    );
				    copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				ret = EXT_STATUS_ERR;
			} else if ((sp->ccode == CS_DATA_UNDERRUN) || (sp->scode != 0)) {

				/* have done the post function */
				pext->Status = EXT_STATUS_SCSI_STATUS;
				pext->DetailStatus = sp->scode;
				DEBUG2(printk("qla2x00_ioctl: data underrun or scsi err. host "
					      "status =0x%x, scsi status = 0x%x.\n", sp->ccode, sp->scode));
				if (which_pass == EXT_SC_SEND_FC_SCSI_PASSTHRU) {
					/* printk("[[ioctl completion which_pass from FC_SCSI_PT]]\n"); */
					scsi_direction = pfc_scsi_pass->Direction;
				} else {
					/* printk("[[ioctl completion which_pass from SCSI_PT]]\n"); */
					scsi_direction = pscsi_pass->Direction;
				}
				if (scsi_direction == EXT_DEF_SCSI_PASSTHRU_DATA_IN) {
					DEBUG3(printk("qla2x00_ioctl: copying data.\n"));

					    /* getting device data and putting in pext->ResponseAdr */
					ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr,
							      pext->ResponseLen);
					if (ret) {
						pext->Status = EXT_STATUS_COPY_ERR;
						copy_to_user(arg, pext, sizeof(EXT_IOCTL));
						DEBUG2(printk
						       ("[qla2100_ioctl: ERROR verify_area WRITE for IOCTL PT ha=%8x]\n",
							(uint32_t) ha));
						    return ret;
					}
					/* now copy up the READ data to user */
					for (i = 0; i < pext->ResponseLen; i++) {
						usr_temp = (uint8_t *) pext->ResponseAdr + i;
						kernel_tmp = (uint8_t *) ha->ioctl_mem + i;
						/*  printk("[%x]",*kernel_tmp); */
						if (put_user(*kernel_tmp, usr_temp))
							return -EFAULT;
					}
				}

				/* printk("[[sense0=%x sense2=%x]]\n",
				   pscsi_cmd->sense_buffer[0],
				   pscsi_cmd->sense_buffer[2]); */
				/* copy up structure to make sense data available to user */
				for (i = 0; i < 16; i++)
					pfc_scsi_pass->SenseData[i] = pscsi_cmd->sense_buffer[i];
				ret = verify_area(VERIFY_WRITE, (void *) pext->RequestAdr, sizeof(EXT_SCSI_PASSTHRU));
				if (ret) {
					pext->Status = EXT_STATUS_COPY_ERR;
					copy_to_user(arg, pext, sizeof(EXT_IOCTL));
					DEBUG2(printk
					       ("[qla2100_ioctl: ERROR verify_area WRITE of EXT_FC_SCSI_PASSTHRU]\n"));
					return (ret);
				}
				for (i = 0; i < sizeof(EXT_FC_SCSI_PASSTHRU); i++) {
					usr_temp = (uint8_t *) pext->RequestAdr + i;
					kernel_tmp = (uint8_t *) pfc_scsi_pass + i;
					if (put_user(*kernel_tmp, usr_temp))
						return -EFAULT;
				}
				DEBUG4(printk("[finished QLA2100 IOCTL EXT_FC_SCSI_REQ - Status= %d]\n", pext->Status));
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				ret = EXT_STATUS_OK;
			} else if (sp->ccode != 0) {
				DEBUG2(printk("qla2x00_ioctl: scsi passthru cs err = %x. copying "
					      "ext stat %x\n", sp->ccode, pext->Status));
				copy_to_user(arg, pext, sizeof(EXT_IOCTL));
				return (pext->Status);
			} else {
				DEBUG3(printk("qla2x00_ioctl: complete. host status =0x%x, scsi "
					      "status = 0x%x.\n", sp->ccode, sp->scode));

				if (which_pass == EXT_SC_SEND_FC_SCSI_PASSTHRU) {
					DEBUG(printk("ioctl completion which_pass from FC_SCSI_PT\n"));
					scsi_direction = pfc_scsi_pass->Direction;
				} else {
					DEBUG(printk("ioctl completion which_pass from SCSI_PT\n"));
					scsi_direction = pscsi_pass->Direction;
				}
				/* copy up structure to make sense data available to user */
				for (i = 0; i < 16; i++)
					pfc_scsi_pass->SenseData[i] = pscsi_cmd->sense_buffer[i];

				if (scsi_direction == EXT_DEF_SCSI_PASSTHRU_DATA_IN && pscsi_cmd->sense_buffer[2] == 0) {
					DEBUG3(printk("qla2x00_ioctl: copying data.\n");
					    );

					    /* getting device data and putting in pext->ResponseAdr */
					    ret = verify_area(VERIFY_WRITE, (void *) pext->ResponseAdr,
							      pext->ResponseLen);
					if (ret) {
						pext->Status = EXT_STATUS_COPY_ERR;
						copy_to_user(arg, pext, sizeof(EXT_IOCTL));
						DEBUG2(printk("qla2x00_ioctl: ERROR verify write resp buf.\n"));
						return (ret);
					}
					/* now copy up the READ data to user */
					usr_temp = (uint8_t *) pext->ResponseAdr;
					kernel_tmp = (uint8_t *) ha->ioctl_mem;
					copy_to_user(usr_temp, kernel_tmp, pext->ResponseLen);
					ret = EXT_STATUS_OK;
				}
			}
		}
		break;

	case EXT_CC_REG_AEN:
		rval = qla2x00_aen_reg(ha, pext, mode);

		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

	case EXT_CC_GET_AEN:
		rval = qla2x00_aen_get(ha, pext, mode);

		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

	case INT_CC_READ_NVRAM:
		rval = qla2x00_read_nvram(ha, pext, mode);

		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		if (rval == -EFAULT)
			ret = -EFAULT;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

	case INT_CC_UPDATE_NVRAM:
		rval = qla2x00_update_nvram(ha, pext, mode);

		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

	case INT_CC_LOOPBACK:
		rval = qla2x00_send_loopback(ha, pext, mode);

		ret = (rval == QL_STATUS_SUCCESS) ? EXT_STATUS_OK : EXT_STATUS_ERR;
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));

		break;

/* all others go here */
/*
    case EXT_CC_ELS_RNID_SEND:
      break;
    case EXT_CC_ELS_RTIN_SEND:
      break;
    case EXT_CC_PLATFORM_REG:
      break;
*/

/* Failover IOCTLs */
	case FO_CC_GET_PARAMS:
	case FO_CC_SET_PARAMS:
	case FO_CC_GET_PATHS:
	case FO_CC_SET_CURRENT_PATH:
	case FO_CC_RESET_HBA_STAT:
	case FO_CC_GET_HBA_STAT:
	case FO_CC_GET_LUN_DATA:
	case FO_CC_SET_LUN_DATA:
	case FO_CC_GET_TARGET_DATA:
	case FO_CC_SET_TARGET_DATA:
		DEBUG4(printk("qla2x00_ioctl: failover arg (%p):\n", arg));
		qla2x00_fo_ioctl(ha, cmd, arg, mode);
		copy_to_user(arg, pext, sizeof(EXT_IOCTL));
		break;

	default:
		ret = -EINVAL;
		break;
	}			/* end of CC decode switch */

	DEBUG3(printk("qla2x00_ioctl: exiting. rval(%d) ret(%d)\n", rval, ret));
	return ret;

}

/*
 * qla2x00_alloc_ioctl_mem
 *	Allocates memory needed by IOCTL code.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_alloc_ioctl_mem(adapter_state_t * ha)
{
	DEBUG3(printk("qla2x00_alloc_ioctl_mem entered.\n");
	    );
	    /* Get consistent memory allocated for ioctl I/O operations. */
	    ha->ioctl_mem = pci_alloc_consistent(ha->pdev, PAGE_SIZE, &ha->ioctl_mem_phys);
	if (ha->ioctl_mem == NULL) {
		printk(KERN_WARNING "qla2x00: ERROR in ioctl physical memory allocation\n");
		return (QL_STATUS_RESOURCE_ERROR);
	}

	/* Allocate context memory buffer */
	ha->ioctl = kmalloc(sizeof(hba_ioctl_context), GFP_ATOMIC);
	if (ha->ioctl == NULL) {
		/* error */
		printk(KERN_WARNING "qla2x00: ERROR in ioctl context allocation.\n");
		return (QL_STATUS_RESOURCE_ERROR);
	}
	memset(ha->ioctl, 0, sizeof(hba_ioctl_context));

	/* Allocate AEN tracking buffer */
	ha->ioctl->aen_tracking_queue = kmalloc(EXT_DEF_MAX_AEN_QUEUE * sizeof(EXT_ASYNC_EVENT), GFP_ATOMIC);
	if (ha->ioctl->aen_tracking_queue == NULL) {
		printk(KERN_WARNING "qla2x00: ERROR in ioctl aen_queue allocation.\n");
		kfree(ha->ioctl);
		ha->ioctl = NULL;
		return (QL_STATUS_RESOURCE_ERROR);
	}
	memset(ha->ioctl->aen_tracking_queue, 0, EXT_DEF_MAX_AEN_QUEUE * sizeof(EXT_ASYNC_EVENT));

	ha->ioctl->ioctl_tq = kmalloc(sizeof(os_tgt_t), GFP_ATOMIC);
	if (ha->ioctl->ioctl_tq == NULL) {
		kfree(ha->ioctl->aen_tracking_queue);
		ha->ioctl->aen_tracking_queue = NULL;
		kfree(ha->ioctl);
		ha->ioctl = NULL;
		printk(KERN_WARNING "qla2x00: ERROR in ioctl tgt queue allocation.\n");
		return (QL_STATUS_RESOURCE_ERROR);
	}
	memset(ha->ioctl->ioctl_tq, 0, sizeof(os_tgt_t));

	ha->ioctl->ioctl_lq = kmalloc(sizeof(os_lun_t), GFP_ATOMIC);
	if (ha->ioctl->ioctl_lq == NULL) {
		kfree(ha->ioctl->ioctl_tq);
		ha->ioctl->ioctl_tq = NULL;
		kfree(ha->ioctl->aen_tracking_queue);
		ha->ioctl->aen_tracking_queue = NULL;
		kfree(ha->ioctl);
		ha->ioctl = NULL;
		printk(KERN_WARNING "qla2x00: ERROR in ioctl lun queue allocation.\n");
		return (QL_STATUS_RESOURCE_ERROR);
	}
	memset(ha->ioctl->ioctl_lq, 0, sizeof(os_lun_t));

	DEBUG3(printk("qla2x00_alloc_ioctl_mem exiting.\n"));

	return (QLA2X00_SUCCESS);
}

/*
 * qla2x00_free_ioctl_mem
 *	Frees memory used by IOCTL code.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_free_ioctl_mem(adapter_state_t * ha)
{
	DEBUG3(printk("qla2x00_free_ioctl_mem entered.\n");
	    );

	    if (ha->ioctl != NULL) {
		if (ha->ioctl->ioctl_tq != NULL) {
			kfree(ha->ioctl->ioctl_tq);
			ha->ioctl->ioctl_tq = NULL;
		}

		if (ha->ioctl->ioctl_lq != NULL) {
			kfree(ha->ioctl->ioctl_lq);
			ha->ioctl->ioctl_lq = NULL;
		}

		if (ha->ioctl->aen_tracking_queue != NULL) {
			kfree(ha->ioctl->aen_tracking_queue);
			ha->ioctl->aen_tracking_queue = NULL;
		}

		kfree(ha->ioctl);
		ha->ioctl = NULL;
	}

	/* free memory allocated for ioctl operations */
	pci_free_consistent(ha->pdev, PAGE_SIZE, ha->ioctl_mem, ha->ioctl_mem_phys);

	DEBUG3(printk("qla2x00_free_ioctl_mem exiting.\n");
	    );
}

/*
 * qla2x00_fo_ioctl
 *	Provides functions for failover ioctl() calls.
 *
 * Input:
 *	ha = adapter state pointer.
 *	ioctl_code = ioctl function to perform
 *	arg = Address of application EXT_IOCTL cmd data
 *	mode = flags
 *
 * Returns:
 *	Return value is the ioctl rval_p return value.
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
/* ARGSUSED */
int
qla2x00_fo_ioctl(struct scsi_qla_host * ha, int ioctl_code, void *arg, int mode)
{
	static EXT_IOCTL cmd_struct;
	int stat, rval = 0;
	/* EXT_IOCTL status values */
	size_t in_size, out_size;
	static union {
		FO_PARAMS params;
		FO_GET_PATHS path;
		FO_SET_CURRENT_PATH set_path;
		/* FO_HBA_STAT_INPUT stat; */
		FO_HBA_STAT stat;
		FO_LUN_DATA_INPUT lun_data;
		FO_TARGET_DATA_INPUT target_data;
	} buff;
	QL_PRINT_3("qla2x00_fo_ioctl: entered", 0, QDBG_NO_NUM, QDBG_NL);
//printk("qla2x00_fo_ioctl: entered\n");
	copy_from_user( (void *) &cmd_struct,arg, sizeof(cmd_struct));
	DEBUG3(printk("qla2x00_fo_ioctl: arg (%p):\n", arg));
	/*
	 * default case for this switch not needed,
	 * ioctl_code validated by caller.
	 */
	in_size = out_size = 0;
	switch (ioctl_code) {
	case FO_CC_GET_PARAMS:
		out_size = sizeof(FO_PARAMS);
		break;
	case FO_CC_SET_PARAMS:
		in_size = sizeof(FO_PARAMS);
		break;
	case FO_CC_GET_PATHS:
		in_size = sizeof(FO_GET_PATHS);
		break;
	case FO_CC_SET_CURRENT_PATH:
		in_size = sizeof(FO_SET_CURRENT_PATH);
		break;
	case FO_CC_GET_HBA_STAT:
	case FO_CC_RESET_HBA_STAT:
		in_size = sizeof(FO_HBA_STAT_INPUT);
		break;
	case FO_CC_GET_LUN_DATA:
		in_size = sizeof(FO_LUN_DATA_INPUT);
		break;
	case FO_CC_SET_LUN_DATA:
		in_size = sizeof(FO_LUN_DATA_INPUT);
		break;
	case FO_CC_GET_TARGET_DATA:
		in_size = sizeof(FO_TARGET_DATA_INPUT);
		break;
	case FO_CC_SET_TARGET_DATA:
		in_size = sizeof(FO_TARGET_DATA_INPUT);
		break;

	}
	if (in_size != 0) {
		if ((int) cmd_struct.RequestLen < in_size) {
			cmd_struct.Status = EXT_STATUS_INVALID_PARAM;
			cmd_struct.DetailStatus = EXT_DSTATUS_REQUEST_LEN;
			rval = EINVAL;
		} else {

			stat = copy_to_user((void *) &buff, (void *) cmd_struct.RequestAdr, in_size);
			DEBUG4(printk("qla2x00_fo_ioctl: printing request buffer:\n"));
			DEBUG4(qla2100_dump_buffer(&buff, 64));
			if (stat != 0) {
				cmd_struct.Status = EXT_STATUS_COPY_ERR;
				rval = EFAULT;
			}
		}
	} else if (out_size != 0 && (int) cmd_struct.ResponseLen < out_size) {
		cmd_struct.Status = EXT_STATUS_BUFFER_TOO_SMALL;
		cmd_struct.DetailStatus = out_size;
		rval = EINVAL;
	}
	if (rval == 0)
		cmd_struct.Status = EXT_STATUS_OK;
	
	cmd_struct.DetailStatus = EXT_STATUS_OK;
	switch (ioctl_code) {
	case FO_CC_GET_PARAMS:
		rval = qla2x00_fo_get_params(&buff.params);
		break;
	case FO_CC_SET_PARAMS:
		rval = qla2x00_fo_set_params(&buff.params);
		break;
	case FO_CC_GET_PATHS:
		rval = qla2x00_cfg_get_paths(&cmd_struct, &buff.path, mode);
		if (rval != 0)
			out_size = 0;
		break;
	case FO_CC_SET_CURRENT_PATH:
		rval = qla2x00_cfg_set_current_path(&cmd_struct, &buff.set_path, mode);
		break;
	case FO_CC_RESET_HBA_STAT:
		rval = qla2x00_stats(&buff.stat, TRUE);
		break;
	case FO_CC_GET_HBA_STAT:
		rval = qla2x00_stats(&buff.stat, FALSE);
		break;
	case FO_CC_GET_LUN_DATA:
		DEBUG4(printk("calling qla2x00_fo_get_lun_data\n"));
		DEBUG4(printk("	((EXT_IOCTL*)arg)->RequestAdr (%p):\n", (((EXT_IOCTL *) arg)->RequestAdr)));
		rval = qla2x00_fo_get_lun_data(&cmd_struct, &buff.lun_data, mode);
		if (rval != 0)
			out_size = 0;
		break;
	case FO_CC_SET_LUN_DATA:
		DEBUG4(printk("calling qla2x00_fo_set_lun_data\n"));
		DEBUG4(printk("	((EXT_IOCTL*)arg)->RequestAdr (%p):\n", (((EXT_IOCTL *) arg)->RequestAdr)));
		rval = qla2x00_fo_set_lun_data(&cmd_struct, &buff.lun_data, mode);
		break;
	case FO_CC_GET_TARGET_DATA:
		DEBUG4(printk("calling qla2x00_fo_get_target_data\n"));
		DEBUG4(printk("	((EXT_IOCTL*)arg)->RequestAdr (%p):\n", (((EXT_IOCTL *) arg)->RequestAdr)));
		rval = qla2x00_fo_get_target_data(&cmd_struct, &buff.target_data, mode);
		if (rval != 0) {
			out_size = 0;
		}
		break;
	case FO_CC_SET_TARGET_DATA:
		DEBUG4(printk("calling qla2x00_fo_set_target_data\n"));
		DEBUG4(printk("	((EXT_IOCTL*)arg)->RequestAdr (%p):\n", (((EXT_IOCTL *) arg)->RequestAdr)));
		rval = qla2x00_fo_set_target_data(&cmd_struct, &buff.target_data, mode);
		break;

	}
	if ((cmd_struct.ResponseLen = out_size) != 0) {
		copy_to_user( (void *) &(((EXT_IOCTL *) arg)->ResponseAdr), (void *) &buff, out_size);
	}

	/* Set Status and DetailStatus fields in application EXT_IOCTL */
	copy_to_user(
		   (void *) &(((EXT_IOCTL *) arg)->Status), (void *) &cmd_struct.Status, sizeof(cmd_struct.Status));
	copy_to_user(
		   (void *) &(((EXT_IOCTL *) arg)->DetailStatus), (void *) &cmd_struct.DetailStatus, sizeof(cmd_struct.DetailStatus));
	copy_to_user(
		   (void *) &(((EXT_IOCTL *) arg)->ResponseLen), (void *) &cmd_struct.ResponseLen,sizeof(cmd_struct.ResponseLen));

	if (rval != 0) {
		 /*EMPTY*/ QL_PRINT_2_3("qla2x00_sdm_ioctl: **** FAILED ****", 0, QDBG_NO_NUM, QDBG_NL);
	} else {
		 /*EMPTY*/ QL_PRINT_3("qla2x00_sdm_ioctl: exiting normally", 0, QDBG_NO_NUM, QDBG_NL);
	}

	return (rval);
}

/*
 * qla2x00_fo_get_lun_data
 *      Get lun data from all devices attached to a HBA (FO_GET_LUN_DATA).
 *
 * Input:
 *      ha = pointer to adapter
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_lun_data(EXT_IOCTL * pext, FO_LUN_DATA_INPUT * bp, int mode)
{
	mp_host_t *host;
	mp_device_t *dp;
	mp_path_list_t *pathlist;
	mp_path_t *path;
	uint16_t dev_no;
	uint16_t lun;
	uint8_t path_id;
	adapter_state_t *ha;
	FO_LUN_DATA_LIST *u_list, *list;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;
	fc_port_t *fcport;
	int ret = 0;

	ha = qla2x00_get_hba((int) bp->HbaInstance);
	DEBUG4(printk("qla_fo_get_lun_data: hba %p, buff %p (int)bp->HbaInstance(%x).\n",
		       ha, bp, (int) bp->HbaInstance));

	DEBUG3(printk("qla_fo_get_lun_data: hba %p, buff %p.\n", ha, bp));

	list = kmalloc(sizeof(FO_LUN_DATA_LIST), GFP_ATOMIC);
	if (list == NULL)
		return 1;
	memset(list,0,sizeof(FO_LUN_DATA_LIST));
	
	u_list = (FO_LUN_DATA_LIST *) pext->ResponseAdr;
	entry = &list->DataEntry[0];

	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		u_entry = &u_list->DataEntry[0];
		for (fcport = host->fcport; (fcport); fcport = fcport->next, u_entry++) {
			memcpy( (void *) &entry->NodeName[0],(void *) fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
			memcpy( (void *) &entry->PortName[0],(void *) fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

			for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
				dp = host->mp_devs[dev_no];

				if (dp == NULL)
					continue;
				/* Lookup entry name */
				if (qla2x00_is_portname_in_device(dp, entry->PortName) && (pathlist = dp->path_list)) {
					path = pathlist->last;
					for (path_id = 0; path_id < pathlist->path_cnt; path_id++, path = path->next) {

						if (path->host == host &&
						    qla2x00_is_portname_equal(path->portname, entry->PortName)) {

							for (lun = 0; lun < MAX_LUNS; lun++) {
								entry->Data[lun] = path->lun_data.data[lun];
							}
							entry->TargetId = dp->dev_id;
							entry->Dev_No = path->id;
							list->EntryCount++;
							copy_to_user((void *) u_entry, (void *) entry,
								   sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
							DEBUG2(printk
							       ("qla_fo: (output) get_lun_data - u_entry(%p) - lun entry[%d] :\n",
								u_entry, list->EntryCount - 1));
							DEBUG2(qla2100_dump_buffer((void *) entry, 64));
							/* No need to look for more paths. we found it */
							break;
						}
					}
					break;
				}
			}
		}
		/* copy number of entries */
		DEBUG4(printk("qla_fo: get_lun_data - entry count = [%d]\n", list->EntryCount);
		    );
		copy_to_user((void *) &u_list->EntryCount, (void *) &list->EntryCount,
			       sizeof(list->EntryCount));
		pext->ResponseLen = FO_LUN_DATA_LIST_MAX_SIZE;

	} else {
		/* EMPTY */
		DEBUG2(printk("qla2x00_get_lun_data: no HOST for ha %p.\n", ha));
		ret = 1;
	}
	kfree(list);
	return ret;
}

/*
 * qla2x00_fo_set_lun_data
 *      Set lun data for the specified device on the attached hba (FO_SET_LUN_DATA).
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_lun_data(EXT_IOCTL * pext, FO_LUN_DATA_INPUT * bp, int mode)
{
	mp_host_t *host;
	mp_device_t *dp;
	mp_path_list_t *pathlist;
	mp_path_t *path;
	uint16_t dev_no;
	uint16_t lun;
	uint8_t path_id;
	adapter_state_t *ha;
	FO_LUN_DATA_LIST *u_list, *list;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;
	int i;
	int ret = 0;

	typedef struct _tagStruct {
		FO_LUN_DATA_INPUT foLunDataInput;
		FO_LUN_DATA_LIST foLunDataList;
	} com_struc;
	com_struc *com_iter;

	ha = qla2x00_get_hba((int) bp->HbaInstance);
	DEBUG(printk("qla_fo_set_lun_data: hba %p, buff %p.\n", ha, bp));

	list = kmalloc(sizeof(FO_LUN_DATA_LIST),GFP_ATOMIC);
	if (list == NULL) {
		DEBUG2(printk("qla_fo_set_lun_data: failed to allocate memory of size (%d)\n", sizeof(FO_LUN_DATA_LIST)));
		return 1;
	}
	memset(list,0,sizeof(FO_LUN_DATA_LIST));

	/* get lun data list */
	com_iter = (com_struc *) pext->RequestAdr;
	u_list = &(com_iter->foLunDataList);

	copy_from_user((void *) list, (void *) (u_list), sizeof(FO_LUN_DATA_LIST));
	DEBUG2(printk("qla_fo_set_lun_data: pext->RequestAdr(%p) u_list (%p) sizeof(FO_LUN_DATA_INPUT) =(%d) and 64 bytes...\n",
		pext->RequestAdr, u_list, sizeof(FO_LUN_DATA_INPUT)));
	DEBUG2(qla2100_dump_buffer((void *) u_list, 64));

	entry = &list->DataEntry[0];

	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {

		u_entry = &u_list->DataEntry[0];

		for (i = 0; i < list->EntryCount; i++, u_entry++) {
			copy_from_user((void *) entry, (void *) u_entry, sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));

			for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
				dp = host->mp_devs[dev_no];

				if (dp == NULL)
					continue;
				/* Lookup entry name */
				if (qla2x00_is_portname_in_device(dp, entry->PortName) && (pathlist = dp->path_list)) {
					path = pathlist->last;
					for (path_id = 0; path_id < pathlist->path_cnt; path_id++, path = path->next) {

						if (path->host == host &&
						    qla2x00_is_portname_equal(path->portname, entry->PortName)) {

							for (lun = 0; lun < MAX_LUNS; lun++) {
								path->lun_data.data[lun] = entry->Data[lun];
								DEBUG4(printk
								       ("cfg_set_lun_data: lun data[%d] = 0x%x \n", lun,
									path->lun_data.data[lun]));
							}

							break;	/* No need to look for more paths. we found it */
						}
					}
					break;
				}
			}
		}
	} else {
		/* EMPTY */
		DEBUG(printk("qla2x00_set_lun_data: no HOST for ha %p.\n", ha));
		ret = 1;
	}

	kfree(list);
	return ret;

}

/*
 * qla2x00_fo_get_target_data
 *      Get the target control byte for all devices attached to a HBA.
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_target_data(EXT_IOCTL * pext, FO_TARGET_DATA_INPUT * bp, int mode)
{
	mp_host_t *host;
	mp_device_t *dp;
	mp_path_list_t *pathlist;
	mp_path_t *path;
	uint16_t dev_no;
	uint8_t path_id;
	adapter_state_t *ha;
	int i;
	FO_DEVICE_DATA *entry, *u_entry;
	int ret = 0;
	fc_port_t *fcport;

	ha = qla2x00_get_hba((int) bp->HbaInstance);
	DEBUG4(printk("qla_fo_get_target_data: hba %p, buff %p.\n", ha, bp));

	entry = kmalloc(sizeof(FO_DEVICE_DATA), GFP_ATOMIC);
	if (entry == NULL)
		return 1;
	
	u_entry = (FO_DEVICE_DATA *) pext->ResponseAdr;

	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {

		for (fcport = host->fcport, i = 0; (fcport) && i < 256; i++, fcport = fcport->next, u_entry++) {
			memcpy( (void *) &entry->WorldWideName[0],(void *) fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
			memcpy( (void *) &entry->PortName[0],(void *) fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

			for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
				dp = host->mp_devs[dev_no];

				if (dp == NULL)
					continue;
				/* Lookup entry name */
				if (qla2x00_is_portname_in_device(dp, entry->PortName) && (pathlist = dp->path_list)) {
					path = pathlist->last;
					for (path_id = 0; path_id < pathlist->path_cnt; path_id++, path = path->next) {

						if (path->host == host &&
						    qla2x00_is_portname_equal(path->portname, entry->PortName)) {

							entry->TargetId = dp->dev_id;
							entry->Dev_No = path->id;
							entry->MultipathControl = path->mp_byte;
							DEBUG3(("cfg_get_target_data: path->id = %d, target data = 0x%x \n",
								path->id, path->mp_byte));
							copy_to_user((void *) u_entry, (void *) entry,
								       sizeof(FO_DEVICE_DATA));
							/* No need to look for more paths. we found it */
							break;
						}
					}
					break;
				}
			}
		}
		pext->ResponseLen = sizeof(FO_DEVICE_DATABASE);

	} else {
		/* EMPTY */
		DEBUG4(printk("qla2x00_get_target_data: no HOST for ha %p.\n", ha));
		ret = 1;
	}

	kfree(entry);
	return ret;
}

/*
 * qla2x00_fo_set_target_data
 *      Set multipath control byte for all devices on the attached hba
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_target_data(EXT_IOCTL * pext, FO_TARGET_DATA_INPUT * bp, int mode)
{
	mp_host_t *host;
	mp_device_t *dp;
	mp_path_list_t *pathlist;
	mp_path_t *path;
	uint16_t dev_no;
	uint8_t path_id;
	adapter_state_t *ha;
	int i;
	FO_DEVICE_DATA *entry, *u_entry;
	int ret = 0;

	ha = qla2x00_get_hba((int) bp->HbaInstance);
	DEBUG4(printk("qla_fo_set_target_data: hba %p, buff %p.\n", ha, bp));

	entry = kmalloc(sizeof(FO_DEVICE_DATA), GFP_ATOMIC);
	if (entry == NULL)
		return 1;
	memset(entry,0,sizeof(FO_DEVICE_DATA));

	u_entry = (FO_DEVICE_DATA *) (pext->RequestAdr + sizeof(FO_TARGET_DATA_INPUT));

	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		for (i = 0; i < 256; i++, u_entry++) {
			copy_from_user((void *) entry, (void *) u_entry, sizeof(FO_DEVICE_DATA));

			for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
				dp = host->mp_devs[dev_no];

				if (dp == NULL)
					continue;
				/* Lookup entry name */
				if (qla2x00_is_portname_in_device(dp, entry->PortName) && (pathlist = dp->path_list)) {
					path = pathlist->last;
					for (path_id = 0; path_id < pathlist->path_cnt; path_id++, path = path->next) {

						if (path->host == host &&
						    qla2x00_is_portname_equal(path->portname, entry->PortName)) {

							path->mp_byte = entry->MultipathControl;
							DEBUG4(printk("cfg_set_target_data: %d target data = 0x%x \n",
								path->id, path->mp_byte));
							/*
							 * if this is the visible  path, then make it available on next reboot.
							 */
							if (!((path->mp_byte & MP_MASK_HIDDEN) ||
								  (path->mp_byte & MP_MASK_UNCONFIGURED))) {
								pathlist->visible = path->id;
							}
							/* No need to look for more paths. we found it */
							break;
						}
					}
					break;
				}
			}
		}

	} else {
		/* EMPTY */
		DEBUG4(printk("qla2x00_set_target_data: no HOST for ha %p.\n", ha));
		ret = 1;
	}

	kfree(entry);
	return ret;

}

/*
 * qla2x00_stats
 *	Searches the hba structure chan for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	stat_p = Pointer to FO_HBA_STAT union.
 *      reset  = Flag, TRUE = reset statistics.
 *                     FALSE = return statistics values.
 *
 * Returns:
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_stats(FO_HBA_STAT * stat_p, uint8_t reset)
{
	int32_t inst, idx;
	uint32_t rval = 0;
	struct scsi_qla_host *hbap;

	inst = stat_p->input.HbaInstance;
	stat_p->info.HbaCount = 0;
	GLOBAL_STATE_LOCK();
#if defined(linux)
	hbap = (struct scsi_qla_host *) qla2100_hostlist;
#else
	hbap = (struct scsi_qla_host *) (qla2x00_hba.first);
#endif
	while (hbap != NULL) {
		if (inst == FO_ADAPTER_ALL) {
			stat_p->info.HbaCount++;
			idx = hbap->instance;
			ADAPTER_STATE_LOCK(hbap);
		} else if (hbap->instance == inst) {
			stat_p->info.HbaCount = 1;
			ADAPTER_STATE_LOCK(hbap);
			idx = inst;
		}
		if (reset == TRUE) {
			hbap->IosRequested = 0;
			hbap->BytesRequested = 0;
			hbap->IosExecuted = 0;
			hbap->BytesExecuted = 0;
		} else {
#if  defined(linux)
#if 0
			stat_p->info.StatEntry[idx].IosRequested = hbap->IosRequested;
			stat_p->info.StatEntry[idx].BytesRequested = hbap->BytesRequested;
			stat_p->info.StatEntry[idx].IosExecuted = hbap->IosExecuted;
			stat_p->info.StatEntry[idx].BytesExecuted = hbap->BytesExecuted;
#endif
#else
			stat_p->info.StatEntry[idx].IosRequested = hbap->IosRequested;
			stat_p->info.StatEntry[idx].BytesRequested = hbap->BytesRequested;
			stat_p->info.StatEntry[idx].IosExecuted = hbap->IosExecuted;
			stat_p->info.StatEntry[idx].BytesExecuted = hbap->BytesExecuted;
#endif
		}
		ADAPTER_STATE_UNLOCK(hbap);
		if (inst != FO_ADAPTER_ALL)
			break;
		else
#if defined(linux)
			hbap = (HBA_p) hbap->next;
#else
			hbap = (HBA_p) hbap->hba.next;
#endif
	}
	GLOBAL_STATE_UNLOCK();
	return (rval);
}

/*
 * qla2x00_fo_get_params
 *	Process an ioctl request to get system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_get_params(PFO_PARAMS pp)
{
	pp->MaxPathsPerDevice = qla_fo_params.MaxPathsPerDevice;
	pp->MaxRetriesPerPath = qla_fo_params.MaxRetriesPerPath;
	pp->MaxRetriesPerIo = qla_fo_params.MaxRetriesPerIo;
	pp->Flags = qla_fo_params.Flags;
	pp->FailoverNotifyType = qla_fo_params.FailoverNotifyType;
	pp->FailoverNotifyCdbLength = qla_fo_params.FailoverNotifyCdbLength;
	memset(&pp->FailoverNotifyCdb[0], 0, sizeof(pp->FailoverNotifyCdb));
	memcpy(&qla_fo_params.FailoverNotifyCdb[0], &pp->FailoverNotifyCdb[0], sizeof(pp->FailoverNotifyCdb));
	return (EXT_STATUS_OK);
}

/*
 * qla2x00_fo_set_params
 *	Process an ioctl request to set system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_set_params(PFO_PARAMS pp)
{
	/* Check values for defined MIN and MAX */
	if ((pp->MaxPathsPerDevice > SDM_DEF_MAX_PATHS_PER_DEVICE) ||
	    (pp->MaxRetriesPerPath < FO_MAX_RETRIES_PER_PATH_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_PATH_MAX) ||
	    (pp->MaxRetriesPerIo < FO_MAX_RETRIES_PER_IO_MIN) || (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_IO_MAX))
		return (EXT_STATUS_INVALID_PARAM);

	/* Update the global structure. */
	qla_fo_params.MaxPathsPerDevice = pp->MaxPathsPerDevice;
	qla_fo_params.MaxRetriesPerPath = pp->MaxRetriesPerPath;
	qla_fo_params.MaxRetriesPerIo = pp->MaxRetriesPerIo;
	qla_fo_params.Flags = pp->Flags;
	qla_fo_params.FailoverNotifyType = pp->FailoverNotifyType;
	qla_fo_params.FailoverNotifyCdbLength = pp->FailoverNotifyCdbLength;
	if (pp->FailoverNotifyType & FO_NOTIFY_TYPE_CDB) {
		if (pp->FailoverNotifyCdbLength > sizeof(qla_fo_params.FailoverNotifyCdb))
			return (EXT_STATUS_INVALID_PARAM);

		memset(&qla_fo_params.FailoverNotifyCdb[0],0, sizeof(qla_fo_params.FailoverNotifyCdb));
		memcpy(&qla_fo_params.FailoverNotifyCdb[0],&pp->FailoverNotifyCdb[0], sizeof(qla_fo_params.FailoverNotifyCdb));
	}
	return (EXT_STATUS_OK);
}
