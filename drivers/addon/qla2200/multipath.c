
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

/****************************************************************************
              Please see revision.notes for revision history.
*****************************************************************************/

#include "settings.h"

#include "inioct.h"
#include "qla_gbl.h"
#include "qla_dbg.h"
#include "qla_fo.cfg"
#include "qlfo.h"
#include "qla_fo.h"
#include "qlfolimits.h"
#include "debug.h"

/*
 *  Local Function Prototypes.
 */
static mp_path_t *qla2x00_select_next_path(mp_host_t * host, mp_device_t * dp, uint8_t);

static uint32_t qla2x00_add_portname_to_mp_dev(mp_device_t *, uint8_t *);
static mp_device_t *qla2x00_allocate_mp_dev(uint8_t *, uint8_t *);
static mp_path_t *qla2x00_allocate_path(mp_host_t * host, uint16_t path_id, fc_port_t * port, uint16_t dev_id);
static mp_host_t *qla2x00_find_host_by_name(uint8_t *);
static mp_device_t *qla2x00_find_or_allocate_mp_dev(mp_host_t *, uint16_t, fc_port_t *);
static mp_path_t *qla2x00_find_or_allocate_path(mp_host_t *, mp_device_t *, uint16_t, fc_port_t *);
static uint32_t qla2x00_send_failover_notify(mp_device_t *, uint8_t lun, mp_path_t * new_path, mp_path_t * old_path);
#if 0
static void qla2x00_set_current_path_for_luns(mp_path_list_t *, uint8_t);
#endif
static uint8_t qla2x00_update_mp_host(mp_host_t *);
static uint32_t qla2x00_update_mp_tree(void);
static fc_lun_t *qla2x00_find_matching_lun(uint8_t, mp_path_t *);
static mp_path_t *qla2x00_find_path_by_id(mp_device_t *, uint8_t);
static mp_device_t *qla2x00_find_mp_dev_by_id(mp_host_t *, uint8_t);
static mp_device_t *qla2x00_find_mp_dev_by_name(mp_host_t *, uint8_t *);
static uint8_t qla2x00_is_ww_name_zero(uint8_t *);
static void qla2x00_add_path(mp_path_list_t *, mp_path_t *);
#if 0
static void qla2x00_remove_path(mp_path_list_t *, mp_path_t *);
#endif
uint8_t qla2x00_is_portname_in_device(mp_device_t *, uint8_t *);
static void qla2x00_failback_luns(mp_host_t *);
static void qla2x00_failback_single_lun(mp_device_t * dp, uint8_t lun, uint8_t new);
static void qla2x00_setup_new_path(mp_device_t *, mp_path_t *);
static void qla2x00_map_os_targets(mp_host_t *);
static void qla2x00_map_os_luns(mp_host_t *, mp_device_t *, uint16_t);
mp_host_t *qla2x00_cfg_find_host(adapter_state_t * ha);
static mp_path_list_t *qla2x00_allocate_path_list(void);
#ifndef LINUX
static void qla2x00_cfg_suspension(HBA_t * ha, uint8_t flag);
#endif
static uint32_t qla2x00_cfg_register_failover_lun(mp_device_t *, srb_t *, fc_lun_t *);
static void
 qla2x00_map_a_oslun(mp_host_t *, mp_device_t *, uint16_t, uint16_t);
static mp_path_t *qla2x00_get_visible_path(mp_device_t * dp);
struct os_lun *qla2x00_lun_alloc(struct scsi_qla_host * ha, uint16_t t, uint16_t l);
void qla2x00_lun_free(struct scsi_qla_host * ha, uint16_t t, uint16_t l);
os_tgt_t *qla2x00_tgt_alloc(struct scsi_qla_host * ha, uint16_t t, uint8_t * name);
void qla2x00_tgt_free(struct scsi_qla_host * ha, uint16_t t);
uint8_t qla2100_fabric_logout(struct scsi_qla_host *, uint16_t);
int qla2x00_issue_iocb(struct scsi_qla_host * ha, void* buffer, dma_addr_t physical, size_t size);
static int qla2x00_lun_reset(struct scsi_qla_host * ha, uint16_t loop_id, uint16_t lun);
uint8_t qla2100_mailbox_command(struct scsi_qla_host *, uint32_t, uint16_t *);
void qla2x00_cfg_build_path_tree(adapter_state_t * ha);
static void qla2x00_set_lun_data_from_config(mp_host_t * host, fc_port_t * port, uint16_t tgt, uint16_t dev_no);
 
/*
 * Global data items
 */
mp_host_t *mp_hosts_base = NULL;
uint8_t mp_config_required = FALSE;
static int mp_num_hosts = 0;
static uint8_t mp_initialized = FALSE;

/*
 * qla2100_fo_enabled
 *      Reads and validates the failover enabled property.
 *
 * Input:
 *      ha = adapter state pointer.
 *      instance = HBA number.
 *
 * Returns:
 *      TRUE when failover is authorized else FALSE
 *
 * Context:
 *      Kernel context.
 */
static uint8_t inline
qla2x00_fo_enabled(struct scsi_qla_host * ha, int instance)
{
	return ha->flags.failover_enabled!=0;
}

/*
 * qla2x00_fo_init_params
 *	Gets driver configuration file failover properties to initalize
 *	the global failover parameters structure.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_fo_init_params(adapter_state_t * ha)
{
	/* For parameters that are not completely implemented yet, */

	memset(&qla_fo_params,0, sizeof(qla_fo_params));

	if (MaxPathsPerDevice) 
		qla_fo_params.MaxPathsPerDevice = MaxPathsPerDevice;
	else
		qla_fo_params.MaxPathsPerDevice = FO_MAX_PATHS_PER_DEVICE_DEF;
	if (MaxRetriesPerPath) 
		qla_fo_params.MaxRetriesPerPath = MaxRetriesPerPath;
	else
		qla_fo_params.MaxRetriesPerPath = FO_MAX_RETRIES_PER_PATH_DEF;
	if (MaxRetriesPerIo) 
		qla_fo_params.MaxRetriesPerIo = MaxRetriesPerIo;
	else
		qla_fo_params.MaxRetriesPerIo = FO_MAX_RETRIES_PER_IO_DEF;

	qla_fo_params.Flags = 0;
	qla_fo_params.FailoverNotifyType = FO_NOTIFY_TYPE_NONE;

}


/*
 * qla2x00_cfg_init
 *      Initialize configuration structures to handle an instance of
 *      an HBA, QLA2x000 card.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Returns:
 *      qla2x00 local function return status code.
 *
 * Context:
 *      Kernel context.
 */
int
qla2x00_cfg_init(adapter_state_t * ha)
{
	int rval;

	ENTER("qla2x00_cfg_init");
	ha->cfg_active = 1;
		if (!mp_initialized) {
		/* First HBA, initialize the failover global properties */
		qla2x00_fo_init_params(ha);

		/* If the user specified a device configuration then
		 * it is use as the configuration. Otherwise, we wait
		 * for path discovery.
		 */
		if (mp_config_required)
			qla2x00_cfg_build_path_tree(ha);
	}
	rval = qla2x00_cfg_path_discovery(ha);
	ha->cfg_active = 0;
	LEAVE("qla2x00_cfg_init");
	return rval;
}

/*
 * qla2x00_is_ww_name_zero
 *
 * Input:
 *      ww_name = Pointer to WW name to check
 *
 * Returns:
 *      TRUE if name is 0 else FALSE
 *
 * Context:
 *      Kernel context.
 */
static uint8_t
qla2x00_is_ww_name_zero(uint8_t * nn)
{
	int cnt;

	/* Check for zero node name */
	for (cnt = 0; cnt < WWN_SIZE; cnt++, nn++) {
		if (*nn != 0)
			break;
	}
	/* if zero return TRUE */
	if (cnt == WWN_SIZE)
		return TRUE;
	else
		return FALSE;
}

#include "qla_gbl.h"
#include "qla_dbg.h"

#include "qlfo.h"
#include "qla_fo.h"
#include "qlfolimits.h"

/*
 * Global variables
 */
SysFoParams_t qla_fo_params;


/*
 * qla2x00_add_path
 * Add a path to the pathlist
 *
 * Input:
 * pathlist -- path list of paths
 * path -- path to be added to list
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 */
static void
qla2x00_add_path(mp_path_list_t * pathlist, mp_path_t * path)
{
	mp_path_t *last = pathlist->last;
	ENTER("qla2x00_add_path");
	DEBUG3(printk("add_path: pathlist =%p, path =%p, cnt = %d\n", pathlist, path, pathlist->path_cnt));
	if (last == NULL) {
		last = path;
	} else {
		path->next = last->next;
	}
	last->next = path;
	pathlist->last = path;
	pathlist->path_cnt++;
	LEAVE("qla2x00_add_path");
}

#if 0
/*
 * qla2x00_remove_path
 * Remove the path from the path list.
 *
 * Input:
 * pathlist -- path list of paths
 * path -- path to be removed to list
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 */
static void
qla2x00_remove_path(mp_path_list_t * pathlist, mp_path_t * path)
{
	mp_path_t *last = pathlist->last;
	mp_path_t *temp;
	mp_path_t *prev;

	if (last != NULL) {
		prev = temp = last;
		do {
			if (temp == path) {
				if (temp == last) {	/* remove last item */
					pathlist->last = temp->next;
					temp->next->next = temp->next;
				} else {
					prev->next = temp->next;
				}
				pathlist->path_cnt--;
			}
			prev = temp;
			temp = temp->next;
		} while (temp != last);
	}
}
#endif

/*
 * qla2x00_is_portname_in_device
 *	Search for the specified "portname" in the device list.
 *
 * Input:
 *	dp = device pointer
 *	portname = portname to searched for in device
 *
 * Returns:
 *      qla2x00 local function return status code.
 *
 * Context:
 *      Kernel context.
 */
uint8_t
qla2x00_is_portname_in_device(mp_device_t * dp, uint8_t * portname)
{
	int idx;

	for (idx = 0; idx < MAX_PATHS_PER_DEVICE; idx++) {
		if (memcmp(&dp->portnames[idx][0], portname, WWN_SIZE) == 0)
			return TRUE;
	}
	return FALSE;
}

#if 0
/*
 * qla2x00_set_path_for_all_luns
 *	Set the preferred path for all luns to the specified
 * 	path id.
 *
 * Input:
 *	path_list = list of paths for the device.
 *
 *
 * Returns:
 *      qla2x00 local function return status code.
 *
 * Context:
 *      Kernel context.
 */
static void
qla2x00_set_path_for_all_luns(mp_path_list_t * path_list, uint8_t id)
{
	uint16_t i;
	uint8_t lun;

	for (i = 0; i < MAX_LUNS_PER_DEVICE; i++) {
		lun = (uint8_t) (i & 0xFF);
		path_list->current_path[lun] = id;
	}

}
#endif

/*
 *  qla2x00_set_lun_data_from_bitmask
 *      Set or clear the LUN_DATA_ENABLED bits in the LUN_DATA from
 *      a LUN bitmask provided from the miniport driver.
 *
 *  Inputs:
 *      lun_data = Extended LUN_DATA buffer to set.
 *      lun_mask = Pointer to lun bit mask union.
 *
 *  Return Value: none.
 */
void
qla2x00_set_lun_data_from_bitmask(mp_lun_data_t * lun_data, lun_bit_mask_t * lun_mask)
{
	int16_t lun;

	ENTER("qla2x00_set_lun_data_from_bitmask");
	for (lun = 0; lun < MAX_LUNS; lun++) {
		/* our bit mask is inverted */
		if (!(EXT_IS_LUN_BIT_SET(lun_mask, lun)))
			lun_data->data[lun] |= LUN_DATA_ENABLED;
		else
			lun_data->data[lun] &= ~LUN_DATA_ENABLED;
		DEBUG5(print("set_lun_data_from_bitmask: lun data[%d] = 0x%x \n", lun, lun_data->data[lun]));
	}

	LEAVE("qla2x00_set_lun_data_from_bitmask");
	return;
}

/*
 * qla2x00_reset_lun_fo_counts
 *	Reset failover retry counts
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
void qla2x00_reset_lun_fo_counts(struct scsi_qla_host * ha, struct os_lun * lq) {
	srb_t *tsp;
	struct os_lun *orig_lq;
	struct list_head *list;
	unsigned long flags;


	spin_lock_irqsave(&ha->list_lock, flags);
	/*
	 * the varies input queues.
	 */
	list_for_each(list,&lq->cmd) {
		tsp = list_entry(list, srb_t, list);
		tsp->fo_retry_cnt = 0;
	}
	/*
	   * the retry queue.
	 */
	list_for_each(list,&ha->retry_queue) {
		tsp = list_entry(list, srb_t, list);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}

	/*
	 * the done queue.
	 */
	list_for_each(list, &ha->done_queue) {
		tsp = list_entry(list, srb_t, list);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

}


static void
qla2x00_failback_single_lun(mp_device_t * dp, uint8_t lun, uint8_t new)
{
	mp_path_list_t *pathlist;
	mp_path_t *new_path, *old_path;
	uint8_t old;
	mp_host_t *host;
	os_lun_t *lq;
	mp_path_t *vis_path;
	mp_host_t *vis_host;

	/* Failback and update statistics. */
	if ((pathlist = dp->path_list) == NULL)
		return;
	old = pathlist->current_path[lun];
	pathlist->current_path[lun] = new;

	if ((new_path = qla2x00_find_path_by_id(dp, new)) == NULL)
		return;
	if ((old_path = qla2x00_find_path_by_id(dp, old)) == NULL)
		return;

	/* Log to console and to event log. */
	printk(KERN_INFO
	       "qla2x00: FAILBACK device %d -> %02x%02x%02x%02x%02x%02x%02x%02x LUN %02x\n",
	       dp->dev_id,
	       dp->nodename[0],
	       dp->nodename[1],
	       dp->nodename[2],
	       dp->nodename[3], dp->nodename[4], dp->nodename[5], dp->nodename[6], dp->nodename[7], lun);
	printk(KERN_INFO "qla2x00: FROM HBA %d  to HBA %d \n", old_path->host->instance, new_path->host->instance);

	/* Send a failover notification. */
	qla2x00_send_failover_notify(dp, lun, new_path, old_path);

	host = new_path->host;

	/* remap the lun */
	qla2x00_map_a_oslun(host, dp, dp->dev_id, lun);

	/* 7/16
	 * Reset counts on the visible path
	 */
	if ((vis_path = qla2x00_get_visible_path(dp)) == NULL) {
		printk(KERN_INFO "qla2x00(%d): No visible path for target %d, dp = %p\n", host->instance, dp->dev_id,
		       dp);
		return;
	}
	vis_host = vis_path->host;
	if ((lq = qla2x00_lun_alloc(vis_host->ha, dp->dev_id, lun)) != NULL)
		qla2x00_reset_lun_fo_counts(vis_host->ha, lq);
}

/*
*  qla2x00_failback_luns
*      This routine looks through the devices on an adapter, and
*      for each device that has this adapter as the visible path,
*      it forces that path to be the current path.  This allows us
*      to keep some semblance of static load balancing even after
*      an adapter goes away and comes back.
*
*  Arguments:
*      host          Adapter that has just come back online.
*
*  Return:
*	None.
*/
static void
qla2x00_failback_luns(mp_host_t * host)
{
	uint16_t dev_no;
	uint8_t l;
	uint16_t lun;
	int i;
	mp_device_t *dp;
	mp_path_list_t *path_list;
	mp_path_t *path;

	DEBUG3(qla2100_print("ENTER: qla2x00_failback_luns\n"));
	for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		path_list = dp->path_list;
		for (path = path_list->last, i = 0; i < path_list->path_cnt; i++, path = path->next) {
			if (path->host == host) {
				DEBUG3(printk("qla2x00_failback_luns: Luns for device %p, instance %d, path id=%d\n", dp,
					host->instance, path->id));
				DEBUG3(qla2100_dump_buffer((char *) &path->lun_data.data[0], 64));
				for (lun = 0; lun < MAX_LUNS_PER_DEVICE; lun++) {
					l = (uint8_t) (lun & 0xFF);
					/* if this is the preferred lun and not the current path the
					 * failback lun.
					 */
					DEBUG4(printk("failback_luns: target= %d, lun data[%d] = %d)\n",
						dp->dev_id, lun, path->lun_data.data[lun]));
					if ((path->lun_data.data[l] & LUN_DATA_PREFERRED_PATH) &&
						/* !path->relogin && */
						path_list->current_path[l] != path->id) {
						qla2x00_failback_single_lun(dp, l, path->id);
					}
				}
			}
		}

	}
	DEBUG3(qla2100_print("LEAVE: qla2x00_failback_luns\n"));
	return;
}

/*
 *  qla2x00_setup_new_path
 *      Checks the path against the existing paths to see if there
 *      are any incompatibilities.  It then checks and sets up the
 *      current path indices.
 *
 *  Inputs:
 *      dp   =  pointer to device
 *      path = new path
 *
 *  Returns:
 *      None
 */
static void
qla2x00_setup_new_path(mp_device_t * dp, mp_path_t * path)
{
	mp_path_list_t *path_list = dp->path_list;
	mp_path_t *tmp_path, *first_path;
	mp_host_t *first_host;
	mp_host_t *tmp_host;

	uint16_t lun;
	uint8_t l;
	int i;

	ENTER("qla2x00_setup_new_path");

	/* If this is a visible path, and there is not already a
	 * visible path, save it as the visible path.  If there
	 * is already a visible path, log an error and make this
	 * path invisible.
	 */
	if (!(path->mp_byte & (MP_MASK_HIDDEN | MP_MASK_UNCONFIGURED))) {

		/* No known visible path */
		if (path_list->visible == PATH_INDEX_INVALID) {
			DEBUG3(printk("setup_new_path: No know visible path - make this path visible\n"));
			path_list->visible = path->id;
			path->mp_byte &= ~MP_MASK_HIDDEN;
		} else {
			DEBUG3(printk("setup_new_path: Second visible path - make this one hidden\n"));
			QL_PRINT_2("qla2x00_setup_new_path: Second visible path found", 0, QDBG_NO_NUM, QDBG_NL);
			path->mp_byte |= MP_MASK_HIDDEN;
		}
	}
	/*
	 * If this is not the first path added, and the setting for
	 * MaxLunsPerTarget does not match that of the first path
	 * then disable qla_cfg for all adapters.
	 */
	first_path = qla2x00_find_path_by_id(dp, 0);

	if (first_path != NULL) {
		first_host = first_path->host;
		if (path->id != 0 && (first_host->MaxLunsPerTarget != path->host->MaxLunsPerTarget)) {
			for (tmp_path = path_list->last, i = 0; (tmp_path) && i <= path->id; i++) {

				tmp_host = tmp_path->host;
				if (!(tmp_host->flags & MP_HOST_FLAG_DISABLE)) {
					QL_PRINT_10("qla2x00_setup_new_path: 2nd visible path ",
						    tmp_host, QDBG_HEX_NUM, QDBG_NL);
					QL_DUMP_10("Disabled, MaxLunsPerTarget.", tmp_host->nodename, 8, WWN_SIZE);
					tmp_host->flags |= MP_HOST_FLAG_DISABLE;
				}
			}

		}
	}

	/*
	 * For each LUN, evaluate whether the new path that is added
	 * is better than the existing path.  If it is, make it the
	 * current path for the LUN.
	 */
	for (lun = 0; lun < MAX_LUNS_PER_DEVICE; lun++) {
		l = (uint8_t) (lun & 0xFF);

		/* If this is the first path added, it is the only
		 * available path, so make it the current path.
		 */

		DEBUG4(printk("qla2x00_setup_new_path: lun_data 0x%x, LUN %d\n", path->lun_data.data[l], lun));
		if (first_path == path) {
			path_list->current_path[l] = 0;
			path->lun_data.data[l] |= LUN_DATA_PREFERRED_PATH;
		} else if (path->lun_data.data[l] & LUN_DATA_PREFERRED_PATH) {

			/*
			 * If this is not the first path added, if this is
			 * the preferred path, make it the current path.
			 */
			path_list->current_path[l] = path->id;
		}
	}
	LEAVE("qla2x00_setup_new_path");
}

/*
 * qla2x00_cfg_mem_free
 *     Free all configuration structures.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Context:
 *      Kernel context.
 */
void
qla2x00_cfg_mem_free(adapter_state_t * ha)
{
	mp_device_t *dp;
	mp_path_list_t *path_list;
	mp_path_t *tmp_path, *path;
	mp_host_t *host, *temp;
	int id, cnt;
	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		if (mp_num_hosts == 0)
			return;

		for (id = 0; id < MAX_MP_DEVICES; id++) {
			if ((dp = host->mp_devs[id]) == NULL)
				continue;
			if ((path_list = dp->path_list) == NULL)
				continue;
			if ((tmp_path = path_list->last) == NULL)
				continue;
			for (cnt = 0; cnt < path_list->path_cnt; cnt++) {
				path = tmp_path;
				tmp_path = tmp_path->next;
				DEBUG(printk(KERN_INFO "host%d - Removing path[%d]= %p\n", host->instance, cnt, path));
				kfree(path);
			}
			kfree(path_list);
			host->mp_devs[id] = NULL;
			/* remove dp from other hosts */
			for (temp = mp_hosts_base; (temp); temp = temp->next) {
				if (temp->mp_devs[id] == dp) {
					DEBUG(printk(KERN_INFO "host%d - Removing host[%d]= %p\n", host->instance,
					       temp->instance, temp));
					temp->mp_devs[id] = NULL;
				}
			}
			kfree(dp);
		}
		/* remove this host from host list */
		temp = mp_hosts_base;
		if (temp != NULL) {
			/* Remove from top of queue */
			if (temp == host) {
				mp_hosts_base = host->next;
			} else {
				/* Remove from middle of queue or bottom of queue */
				for (temp = mp_hosts_base; temp != NULL; temp = temp->next) {
					if (temp->next == host) {
						temp->next = host->next;
						break;
					}
				}
			}
		}
		kfree(host);
		mp_num_hosts--;
	}

}

/*
 * qla2x00_add_portname_to_mp_dev
 *      Add the specific port name to the list of port names for a
 *      multi-path device.
 *
 * Input:
 *      dp = pointer ti virtual device
 *      portname = Port name to add to device
 *
 * Returns:
 *      qla2x00 local function return status code.
 *
 * Context:
 *      Kernel context.
 */
static uint32_t
qla2x00_add_portname_to_mp_dev(mp_device_t * dp, uint8_t * portname)
{
	uint8_t index;
	uint32_t rval = QLA2X00_SUCCESS;

	ENTER("qla2x00_add_portname_to_mp_dev");
	QL_PRINT_10("qla2x00_add_portname_to_mp_dev: device= ", dp, QDBG_HEX_NUM, QDBG_NL);
	QL_DUMP_10("port name[8]=", portname, 8, WWN_SIZE);

	/* Look for an empty slot and add the specified portname.   */
	for (index = 0; index < MAX_NUMBER_PATHS; index++) {
		if (qla2x00_is_ww_name_zero(&dp->portnames[index][0])) {
			DEBUG3(printk("adding portname to dp = %p at index = %d\n", dp, index));
			memcpy((uint8_t *) & dp->portnames[index][0], (uint8_t *) portname, WWN_SIZE);
			break;
		}

	}
	if (index == MAX_NUMBER_PATHS) {
		rval = QLA2X00_FUNCTION_FAILED;
		QL_PRINT_10("qla2x00_add_portname_to_mp_dev: Fail no room", 0, QDBG_NO_NUM, QDBG_NL);
	} else {
		/* EMPTY */
		QL_PRINT_10("qla2x00_add_portname_to_mp_dev: Exit OK", 0, QDBG_NO_NUM, QDBG_NL);
	}
	LEAVE("qla2x00_add_portname_to_mp_dev");
	return rval;
}

/*
 *  qla2x00_allocate_mp_dev
 *      Allocate an fc_mp_dev, clear the memory, and log a system
 *      error if the allocation fails. After fc_mp_dev is allocated
 *
 *  Inputs:
 *      nodename  = pointer to nodename of new device
 *      portname  = pointer to portname of new device
 *
 *  Returns:
 *      Pointer to new mp_device_t, or NULL if the allocation fails.
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t *
qla2x00_allocate_mp_dev(uint8_t * nodename, uint8_t * portname)
{
	mp_device_t *dp;	/* Virtual device pointer */

	ENTER("qla2x00_allocate_mp_dev");

	dp = kmalloc(sizeof(mp_device_t), GFP_ATOMIC);

	if (dp != NULL) {
		memset(dp, 0, sizeof(mp_device_t));
		DEBUG3(printk("qla2x00_allocate_mp_dev: mp_device_t allocated at %p\n", dp));

		/*
		 * Copy node name into the mp_device_t.
		 */
		if (nodename)
			memcpy((uint8_t *) & dp->nodename[0], (uint8_t *) nodename, WWN_SIZE);

		/*
		 * Since this is the first port, it goes at
		 * index zero.
		 */
		if (portname)
			memcpy((uint8_t *) & dp->portnames[0][0], (uint8_t *) portname, PORT_NAME_SIZE);

		/* Allocate an PATH_LIST for the fc_mp_dev. */
		if ((dp->path_list = qla2x00_allocate_path_list()) == NULL) {
			QL_PRINT_2("qla2x00_allocate_mp_dev: allocate path_list Failed", 0, QDBG_NO_NUM, QDBG_NL);
			kfree(dp);
			dp = NULL;
		} else {
			DEBUG3(printk("qla2x00_allocate_mp_dev: mp_path_list_t allocated at %p\n", dp->path_list));
			/* EMPTY */
			QL_PRINT_2("qla2x00_allocate_mp_dev: Exit Okay", 0, QDBG_NO_NUM, QDBG_NL);
			DEBUG3(printk("update_mp_device: mp_device_t="));
			DEBUG3(qla2100_dump_buffer((uint8_t *) dp, sizeof(mp_device_t)));
		}

	} else {
		/* EMPTY */
		QL_PRINT_2("qla2x00_allocate_mp_dev: Allocate failed", 0, QDBG_NO_NUM, QDBG_NL);

	}

	LEAVE("qla2x00_allocate_mp_dev");
	return dp;
}

/*
 *  qla2x00_allocate_path
 *      Allocate a PATH.
 *
 *  Inputs:
 *     host   Host adapter for the device.
 *     path_id  path number
 *     port   port for device.
 *      dev_id  device number
 *
 *  Returns:
 *      Pointer to new PATH, or NULL if the allocation failed.
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla2x00_allocate_path(mp_host_t * host, uint16_t path_id, fc_port_t * port, uint16_t dev_id)
{
	mp_path_t *path;
	uint16_t lun;

	QL_PRINT_13("qla2x00_allocate_path: Entered", 0, QDBG_NO_NUM, QDBG_NL);

	path = kmalloc(sizeof(mp_path_t), GFP_ATOMIC);
	if (path != NULL) {
		memset(path, 0, sizeof(mp_path_t));
		DEBUG3(printk("qla2x00_allocate_path: mp_path_t allocated at %p\n", path));

		/* Copy the supplied information into the MP_PATH.  */
		path->host = host;
		if (!(port->flags & FC_CONFIG) || port->loop_id != FC_NO_LOOP_ID) 
			path->port = port;
		path->id = path_id;
		port->cur_path = path->id;
		path->mp_byte = port->mp_byte;
		path->next = NULL;
		memcpy((uint8_t *) path->portname, (uint8_t *) port->port_name, WWN_SIZE);
		for (lun = 0; lun < MAX_LUNS; lun++) 
			path->lun_data.data[lun] |= LUN_DATA_ENABLED;
	} else {
		/* EMPTY */
		QL_PRINT_2("qla2x00_allocate_path: Failed", 0, QDBG_NO_NUM, QDBG_NL);
	}

	return path;
}

/*
 *  qla2x00_allocate_path_list
 *      Allocate a PATH_LIST
 *
 *  Input:
 * 		None
 *
 *  Returns:
 *      Pointer to new PATH_LIST, or NULL if the allocation fails.
 *
 * Context:
 *      Kernel context.
 */
static mp_path_list_t *
qla2x00_allocate_path_list(void)
{
	mp_path_list_t *path_list;
	uint16_t i;
	uint8_t l;

	path_list = kmalloc(sizeof(mp_path_list_t), GFP_ATOMIC);

	if (path_list != NULL) {
		memset(path_list, 0, sizeof(mp_path_list_t));
		QL_PRINT_10("qla2x00_allocate_pathlist: allocated at ", (void *) path_list, QDBG_HEX_NUM, QDBG_NL);

		path_list->visible = PATH_INDEX_INVALID;
		/* Initialized current path */
		for (i = 0; i < MAX_LUNS_PER_DEVICE; i++) {
			l = (uint8_t) (i & 0xFF);
			path_list->current_path[l] = PATH_INDEX_INVALID;
		}
		path_list->last = NULL;

	} else {
		/* EMPTY */
		QL_PRINT_2("Alloc pool failed for MP_PATH_LIST.", 0, QDBG_NO_NUM, QDBG_NL);

	}

	return path_list;
}

/*
 *  qla2x00_cfg_find_host
 *      Look through the existing multipath tree, and find
 *      a host adapter to match the specified ha.
 *
 *  Input:
 *      ha = pointer to host adapter
 *
 *  Return:
 *      Pointer to new host, or NULL if no match found.
 *
 * Context:
 *      Kernel context.
 */
mp_host_t *
qla2x00_cfg_find_host(adapter_state_t * ha)
{
	mp_host_t *host = NULL;	/* Host found and null if not */
	mp_host_t *tmp_host;

	ENTER("qla2x00_cfg_find_host");
	for (tmp_host = mp_hosts_base; (tmp_host); tmp_host = tmp_host->next) {
		if (tmp_host->ha == ha) {
			host = tmp_host;
			DEBUG3(printk("Found host =%p, instance %d\n", host, host->instance));
			break;
		}
	}

	LEAVE("qla2x00_cfg_find_host");
	return host;
}

/*
 *  qla2x00_find_host_by_name
 *      Look through the existing multipath tree, and find
 *      a host adapter to match the specified name.
 *
 *  Input:
 *      name = node name to match.
 *
 *  Return:
 *      Pointer to new host, or NULL if no match found.
 *
 * Context:
 *      Kernel context.
 */
mp_host_t *
qla2x00_find_host_by_name(uint8_t * name)
{
	mp_host_t *host;	/* Host found and null if not */

	for (host = mp_hosts_base; (host); host = host->next) {
		if (memcmp(host->nodename, name, WWN_SIZE) == 0)
			break;
	}
	return host;
}

/*
 *  qla2x00_find_or_allocate_mp_dev
 *      Look through the existing multipath control tree, and find
 *      an mp_device_t with the supplied world-wide node name.  If
 *      one cannot be found, allocate one.
 *
 *  Input:
 *      host      Adapter to add device to.
 *      dev_id    Index of device on adapter.
 *      port      port database information.
 *
 *  Returns:
 *      Pointer to new mp_device_t, or NULL if the allocation fails.
 *
 *  Side Effects:
 *      If the MP HOST does not already point to the mp_device_t,
 *      a pointer is added at the proper port offset.
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t *
qla2x00_find_or_allocate_mp_dev(mp_host_t * host, uint16_t dev_id, fc_port_t * port)
{
	mp_device_t *dp = NULL;	/* pointer to multi-path device   */
	uint8_t node_found;	/* Found matching node name. */
	uint8_t port_found;	/* Found matching port name. */
	uint8_t names_valid;	/* Node name and port name are not zero. */

	mp_host_t *temp_host;	/* pointer to temporary host */

	uint16_t j;
	mp_device_t *temp_dp;

	ENTER("qla2x00_find_or_allocate_mp_dev");
	DEBUG3(printk("(find_or_allocate_mp_dev): host =%p, port =%p, id = %d\n", host, port, dev_id));
	QL_PRINT_10("qla2x00_find_or_allocate_mp_dev: Entered", 0, QDBG_NO_NUM, QDBG_NL);

	QL_DUMP_13("qla2x00_find_or_allocate_dev: node_name=", port->node_name, 8, WWN_SIZE);
	QL_DUMP_13("qla2x00_find_or_allocate_dev: port_name=", port->port_name, 8, WWN_SIZE);
	temp_dp = qla2x00_find_mp_dev_by_id(host, dev_id);

	DEBUG3(printk("temp dp =%p\n", temp_dp));
	/* if Device already known at this port. */
	if (temp_dp != NULL) {
		node_found = qla2x00_is_nodename_equal(temp_dp->nodename, port->node_name);
		port_found = qla2x00_is_portname_in_device(temp_dp, port->port_name);

		if (node_found && port_found) {
			DEBUG3(qla2100_print("find_or_alloacte_dev: port exsits in device\n"));
			QL_PRINT_10("qla2x00_find_or_allocate_mp_dev: Device found at ",
				    temp_dp, QDBG_HEX_NUM, QDBG_NL);

			dp = temp_dp;

			/* Copy the LUN configuration data into the mp_device_t. */

		}
#if 0
		else {
			DEBUG3(qla2100_print("find_or_alloacte_dev: substitute wwn\n"));
			/*
			 * A new WWN (node_name) has been found at this device index.
			 * A IOCTL_SUBSTITUTE_WWN was issued to swap a device. We replace the
			 * old WWN with the new WWN at specified index, then find the original
			 * pointer to the mp_device_t for this adapter, and null it out.
			 */

			QL_PRINT_10("qla2x00_find_or_allocate_mp_dev: Index ", dev_id, QDBG_DEC_NUM, QDBG_NL);
			QL_DUMP_10(" was ", temp_dp->nodename, 8, WWN_SIZE);
			QL_DUMP_10(" now ", port->node_name, 8, WWN_SIZE);

			/*
			 * Copy the node name and port name.
			 */
			memcpy((uint8_t *) temp_dp->nodename, (uint8_t *) port->node_name, WWN_SIZE);

			/*
			 * Delete all existing portnames and make this port name the first one.
			 */
			memset((void *) temp_dp->portnames, 0, sizeof(temp_dp->portnames));
			memcpy((uint8_t *) & temp_dp->portnames[0][0], (uint8_t *) port->port_name, WWN_SIZE);

			/* Copy the LUN data into the mp_device_t. */

			/* Set the flag that we have found the device. */
			dp = temp_dp;

			/*
			 * Find the old node name on this adapter and null it out.
			 */
			for (j = 0; j < MAX_MP_DEVICES; j++) {
				temp_dp = host->mp_devs[j];

				if (j == dev_id || temp_dp == NULL)
					continue;

				node_found = qla2x00_is_nodename_equal(temp_dp->nodename, port->node_name);
				port_found = qla2x00_is_portname_in_device(temp_dp, port->port_name);

				if (node_found && port_found) {
					QL_PRINT_10("qla2x00_find_or_allocate_mp_dev: ", 0, QDBG_NO_NUM, QDBG_NNL);
					QL_PRINT_10("Deleting device at index ", j, QDBG_HEX_NUM, QDBG_NL);
					host->mp_devs[j] = NULL;
					break;
				}

			}

		}
#endif

	}

	/* Sanity check the port information  */
	names_valid = (!qla2x00_is_ww_name_zero(port->node_name) && !qla2x00_is_ww_name_zero(port->port_name));

	/*
	 * If the optimized check failed, loop through each known
	 * device on each known adapter looking for the node name.
	 */
	if (dp == NULL && names_valid) {

		DEBUG3(qla2100_print("Searching each adapter for the device...\n"));
		for (temp_host = mp_hosts_base; (temp_host); temp_host = temp_host->next) {

			/* Loop through each potential device on adapter. */

			for (j = 0; j < MAX_MP_DEVICES; j++) {
				temp_dp = temp_host->mp_devs[j];

				if (temp_dp == NULL)
					continue;

				node_found = qla2x00_is_nodename_equal(temp_dp->nodename, port->node_name);
				port_found = qla2x00_is_portname_in_device(temp_dp, port->port_name);
				if (node_found || port_found) {
					DEBUG3(printk("Matching device found at %p @ %d\n", temp_dp, j));

					/*
					 * If the node name matches but the port name was not
					 * found, add the port name to the list of port names.
					 */
					if (!port_found) {
						qla2x00_add_portname_to_mp_dev(temp_dp, port->port_name);
					}

					/*  Set the flag that we have found the device. */
					dp = temp_dp;
					host->mp_devs[j] = dp;
					dp->use_cnt++;

					/*Fixme(dg)  Copy the LUN info into the mp_device_t */

					break;
				}
			}

			/* Break outer loop if inner loop succeeded. */
			if (dp != NULL)
				break;
		}

	}

	/* If we couldn't find one, allocate one. */
	if (dp == NULL && ((port->flags & FC_CONFIG) || !mp_config_required)) {
		dp = qla2x00_allocate_mp_dev(port->node_name, port->port_name);
		host->mp_devs[dev_id] = dp;
		dp->dev_id = dev_id;
		dp->use_cnt++;
	}

	LEAVE("qla2x00_allocate_mp_dev");
	return dp;
}

/*
 *  qla2x00_find_or_allocate_path
 *      Look through the path list for the supplied device, and either
 *      find the supplied adapter (path) for the adapter, or create
 *      a new one and add it to the path list.
 *
 *  Input:
 *      host      Adapter (path) for the device.
 *      dp       Device and path list for the device.
 *      dev_id    Index of device on adapter.
 *      port     Device data from port database.
 *
 *  Returns:
 *      Pointer to new PATH, or NULL if the allocation fails.
 *
 *  Side Effects:
 *      1. If the PATH_LIST does not already point to the PATH,
 *         a new PATH is added to the PATH_LIST.
 *      2. If the new path is found to be a second visible path, it is
 *         marked as hidden, and the device database is updated to be
 *         hidden as well, to keep the miniport synchronized.
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static mp_path_t *
qla2x00_find_or_allocate_path(mp_host_t * host, mp_device_t * dp, uint16_t dev_id, fc_port_t * port)
{
	mp_path_list_t *path_list = dp->path_list;
	mp_path_t *path;
	uint8_t id;

	ENTER("qla2x00_find_or_allocate_path");
	DEBUG3(printk( "(find_or_allocate_path): host =%p, port =%p, dp=%p, dev id = %d\n", host, port, dp, dev_id));
	/*
	 * Loop through each known path in the path list.  Look for
	 * a PATH that matches both the adapter and the port name.
	 */
	path = qla2x00_find_path_by_name(host, path_list, port->port_name);

	if (path != NULL) {
		DEBUG3(printk("(find_or_allocate_path): Found an existing path -  host =%p, port =%p, path id = %d\n", host,path->port, path->id));
		DEBUG3(printk("qla2x00_find_or_allocate_path: Luns for path_id %d, instance %d\n", path->id, host->instance));
		DEBUG3(qla2100_dump_buffer((char *) &path->lun_data.data[0], 64));

		/* If we found an existing path, look for any changes to it. */
		if (path->port == NULL) {
			DEBUG3(printk("update path %p, path id= %d, mp_byte=0x%x port=%p\n", path, path->id, path->mp_byte, path->port));
			path->port = port;
			port->mp_byte = path->mp_byte;
		} else {
			if ((path->mp_byte & MP_MASK_HIDDEN) && !(port->mp_byte & MP_MASK_HIDDEN)) {
				QL_PRINT_10("qla2x00_find_or_allocate_path: Adapter ", host, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(" Device ", dp, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(" Path ", path->id, QDBG_HEX_NUM, QDBG_NL);
				QL_PRINT_10(" (", path, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(") has become visible.", 0, QDBG_NO_NUM, QDBG_NL);

				path->mp_byte &= ~MP_MASK_HIDDEN;
			}

			if (!(path->mp_byte & MP_MASK_HIDDEN) && (port->mp_byte & MP_MASK_HIDDEN)) {

				QL_PRINT_10("qla2x00_find_or_allocate_path: Adapter ", host, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(" Device ", dp, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(" Path ", path->id, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(" (", path, QDBG_HEX_NUM, QDBG_NNL);
				QL_PRINT_10(") has become hidden.", 0, QDBG_NO_NUM, QDBG_NL);

				path->mp_byte |= MP_MASK_HIDDEN;
			}
		}

	} else {
		/*
		 * If we couldn't find an existing path, and there is still
		 * room to add one, allocate one and put it in the list.
		 */
		if (path_list->path_cnt < MAX_PATHS_PER_DEVICE && path_list->path_cnt < qla_fo_params.MaxPathsPerDevice) {

			id = path_list->path_cnt;

			/* Update port with bitmask info */
			path = qla2x00_allocate_path(host, id, port, dev_id);
			DEBUG3(printk("new path %p, path id= %d, mp_byte=0x%x port=%p\n", path, id, path->mp_byte, path->port));
			
			qla2x00_add_path(path_list, path);

			/* Reconcile the new path against the existing ones. */
			qla2x00_setup_new_path(dp, path);
			QL_PRINT_13("qla2x00_find_or_allocate_path: Exit Okay", 0, QDBG_NO_NUM, QDBG_NL);
		} else {
			/* EMPTY */
			QL_PRINT_12("qla2x00_find_or_allocate_path: "
				    "Err exit, no space to add path.", 0, QDBG_NO_NUM, QDBG_NL);
		}

	}

	LEAVE("qla2x00_find_or_allocate_path");
	return path;
}

static uint32_t
qla2x00_cfg_register_failover_lun(mp_device_t * dp, srb_t * sp, fc_lun_t * new_lp)
{
	uint32_t status = QLA2X00_SUCCESS;
	os_tgt_t *tq;
	os_lun_t *lq;
	fc_lun_t *old_lp;

	DEBUG3(printk("qla2x00_send_failover_notify: NEW fclun = %p, sp = %p \n", new_lp, sp));
	/*
	 * Fix lun descriptors to point to new fclun
	 * which is a new fcport.
	 */
	if (new_lp == NULL) {
		DEBUG2(printk("qla2x00_send_failover_notify: Failed new lun %p ", new_lp));
		return QLA2X00_FUNCTION_FAILED;
	}
	tq = sp->tgt_queue;
	lq = sp->lun_queue;
	if (tq == NULL) {
		DEBUG2(printk( "qla2x00_send_failover_notify: Failed to get old tq %p ", tq));
		return QLA2X00_FUNCTION_FAILED;
	}
	if (lq == NULL) {
		DEBUG2(printk("qla2x00_send_failover_notify: Failed to get old lq %p ", lq));
		return QLA2X00_FUNCTION_FAILED;
	}
	TGT_LOCK(tq);
	old_lp = lq->fclun;
	lq->fclun = new_lp;

	/* Log the failover to console */
	printk(KERN_INFO
	       "qla2x00: FAILOVER device %d from %02x%02x%02x%02x%02x%02x%02x%02x -> %02x%02x%02x%02x%02x%02x%02x%02x - LUN %02x, reason=0x%x\n",
	       dp->dev_id,
	       old_lp->fcport->port_name[0],
	       old_lp->fcport->port_name[1],
	       old_lp->fcport->port_name[2],
	       old_lp->fcport->port_name[3],
	       old_lp->fcport->port_name[4],
	       old_lp->fcport->port_name[5],
	       old_lp->fcport->port_name[6],
	       old_lp->fcport->port_name[7],
	       new_lp->fcport->port_name[0],
	       new_lp->fcport->port_name[1],
	       new_lp->fcport->port_name[2],
	       new_lp->fcport->port_name[3],
	       new_lp->fcport->port_name[4],
	       new_lp->fcport->port_name[5],
	       new_lp->fcport->port_name[6], new_lp->fcport->port_name[7], new_lp->lun, sp->err_id);
	printk(KERN_INFO
	       "qla2x00: FROM HBA %d  to HBA %d \n", (int) old_lp->fcport->ha->instance,
	       (int) new_lp->fcport->ha->instance);

	DEBUG3(printk("qla2x00_send_failover_notify: NEW fclun = %p , port =%p, loop_id =0x%x, instance %d\n",
		new_lp, new_lp->fcport, new_lp->fcport->loop_id, new_lp->fcport->ha->instance));
	TGT_UNLOCK(tq);
	return status;
}

/*
 * qla2x00_send_failover_notify
 *      A failover operation has just been done from an old path
 *      index to a new index.  Call lower level driver
 *      to perform the failover notification.
 *
 * Inputs:
 *      device           Device being failed over.
 *      lun                LUN being failed over.
 *      newpath           path that was failed over too.
 *      oldpath           path that was failed over from.
 *
 * Return:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
/* ARGSUSED */
static uint32_t
qla2x00_send_failover_notify(mp_device_t * dp, uint8_t lun, mp_path_t * newpath, mp_path_t * oldpath)
{

	fc_lun_t *old_lp, *new_lp;
	uint32_t status = QLA2X00_SUCCESS;

	QL_PRINT_13("qla2x00_send_failover_notify: Entered", 0, QDBG_NO_NUM, QDBG_NL);

	old_lp = qla2x00_find_matching_lun(lun, oldpath);
	new_lp = qla2x00_find_matching_lun(lun, newpath);

	/*
	 * If the target is the same target, but a new HBA has been selected,
	 * send a third party logout if required.
	 */
	if ((qla_fo_params.FailoverNotifyType & FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET ||
	     qla_fo_params.FailoverNotifyType & FO_NOTIFY_TYPE_LOGOUT_OR_CDB) &&
	    qla2x00_is_portname_equal(oldpath->portname, newpath->portname)) {

		status = qla2x00_send_fo_notification(old_lp, new_lp);
		if (status == QLA2X00_SUCCESS) {
			/* EMPTY */
			QL_PRINT_10("qla2x00_send_failover_notify: Logout succeded", 0, QDBG_NO_NUM, QDBG_NL);
		} else {
			/* EMPTY */
			QL_PRINT_2("qla2x00_send_failover_notify: Logout Failed", 0, QDBG_NO_NUM, QDBG_NL);
		}
	} else if ((qla_fo_params.FailoverNotifyType & FO_NOTIFY_TYPE_LUN_RESET) ||
		   (qla_fo_params.FailoverNotifyType & FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET)) {

		/* If desired, send a LUN reset as the failover notification type. */
		if (newpath->lun_data.data[lun] & LUN_DATA_ENABLED) {

			status = qla2x00_send_fo_notification(old_lp, new_lp);

			if (status == QLA2X00_SUCCESS) {
				/* EMPTY */
				QL_PRINT_10("QLCallFailoverNotify: LUN reset succeeded.", 0, QDBG_NO_NUM, QDBG_NL);
			} else {
				/* EMPTY */
				QL_PRINT_2("qla2x00_send_failover_notify: Failed reset LUN ",
					   lun, QDBG_DEC_NUM, QDBG_NL);
			}
		}

	} else if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB ||
		   qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LOGOUT_OR_CDB) {
		if (newpath->lun_data.data[lun] & LUN_DATA_ENABLED) {

			status = qla2x00_send_fo_notification(old_lp, new_lp);

			if (status == QLA2X00_SUCCESS) {
				/* EMPTY */
				QL_PRINT_13("qla2x00_send_failover_notify: Send CDB succeeded",
					    0, QDBG_NO_NUM, QDBG_NL);
			} else {
				/* EMPTY */
				QL_PRINT_12("qla2x00_send_failover_notify: Send CDB Error", lun, QDBG_DEC_NUM, QDBG_NL);
			}
		}

	} else {
		/* EMPTY */
		QL_PRINT_2("QLCallFailoverNotify: failover disabled or no notify routine defined.",
			   0, QDBG_NO_NUM, QDBG_NL);
	}

	return status;
}

/*
 *  qla2x00_select_next_path
 *      A problem has been detected with the current path for this
 *      device.  Try to select the next available path as the current
 *      path for this device.  If there are no more paths, the same
 *      path will still be selected.
 *
 *  Inputs:
 *      dp           pointer of device structure.
 *      lun                LUN to failover.
 *
 *  Return Value:
 *      	new path or same path
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla2x00_select_next_path(mp_host_t * host, mp_device_t * dp, uint8_t lun)
{
	mp_path_t *path = NULL;
	mp_path_list_t *path_list;
	mp_path_t *orig_path;
	int id;
	uint32_t status;
	mp_host_t *new_host;

	ENTER("qla2x00_select_next_path:");

	path_list = dp->path_list;
	if (path_list == NULL)
		return NULL;

	/* Get current path */
	id = path_list->current_path[lun];

	/* Get path for current path id  */
	if ((orig_path = qla2x00_find_path_by_id(dp, id)) != NULL) {

#ifndef LINUX
		qla2x00_cfg_suspension(host->ha, TRUE);
#endif

		/* select next path */
		path = orig_path->next;
		new_host = path->host;

		/* FIXME may need to check for HBA being reset */
		DEBUG3(printk("qla2x00_select_next_path: orig path = %p, ", orig_path));
		DEBUG3(printk(" new path = %p \n", path));
		DEBUG3(printk("qla2x00_select_next_path: curr idx = %d, new idx = %d\n", orig_path->id, path->id));
		QL_DUMP_10("  FAILOVER: device nodename:\r\n", dp->nodename, 8, WWN_SIZE);
		QL_DUMP_10(" Original  - host nodename:\r\n", orig_path->host->nodename, 8, WWN_SIZE);
		QL_DUMP_10("       portname:\r\n", orig_path->port->port_name, 8, WWN_SIZE);
		QL_DUMP_10(" New  - host nodename\r\n", new_host->nodename, 8, WWN_SIZE);
		QL_DUMP_10("        portname\r\n", path->port->port_name, 8, WWN_SIZE);

		path_list->current_path[lun] = path->id;

		/* If we selected a new path, do failover notification. */
		if (path != orig_path) {
			status = qla2x00_send_failover_notify(dp, lun, path, orig_path);

			/* Currently we ignore the returned status from
			 * the notify. however, if failover notify fails */

		}
	}
	LEAVE("qla2x00_select_next_path:");
	return path;
}

#if 0
/*
 *  qla2x00_set_current_path_for_luns
 * 	Set the current path for all luns to the specified path ID.
 *
 *  Input:
 *      path_list = pointer to the path list
 *      path_id = path_id to set current path
 *
 *  Returns:
 *      None
 *
 */
static void
qla2x00_set_current_path_for_luns(mp_path_list_t * path_list, uint8_t path_id)
{
	uint16_t lun_no;
	uint8_t lun;

	for (lun_no = 0; lun_no < MAX_LUNS_PER_DEVICE; lun_no++) {
		lun = (uint8_t) (lun_no & 0xFF);
		path_list->current_path[lun] = path_id;
	}
}
#endif

/*
 *  qla2x00_update_mp_host
 *      Update the multipath control information from the port
 *      database for that adapter.
 *
 *  Input:
 *      host      Adapter to update. Devices that are new are
 *                      known to be attached to this adapter.
 *
 *  Returns:
 *      TRUE if updated successfully; FALSE if error.
 *
 */
static uint8_t
qla2x00_update_mp_host(mp_host_t * host)
{
	uint8_t success = TRUE;
	uint16_t dev_id;
	fc_port_t *port;
	adapter_state_t *ha = host->ha;

	ENTER("qla2x00_update_mp_host");
	/*
	 * We make sure each port is attached to some virtual device.
	 */
	for (dev_id = 0, port = ha->fcport; (port); port = port->next, dev_id++) {
		success |= qla2x00_update_mp_device(host, port, dev_id);
	}
	if (success) {
		qla2x00_map_os_targets(host);
		QL_PRINT_10("qla2x00_update_mp_host: Exit OK", 0, QDBG_NO_NUM, QDBG_NL);
	} else {
		/* EMPTY */
		QL_PRINT_10("qla2x00_update_mp_host: Exit FAILED", 0, QDBG_NO_NUM, QDBG_NL);
	}
	LEAVE("qla2x00_update_mp_host");
	return success;
}

/*
 *  qla2x00_update_mp_device
 *      Update the multipath control information from the port
 *      database for that adapter.
 *
 *  Inputs:
 *		host   Host adapter structure
 *      port   Device to add to the path tree.
 *		dev_id  Device id
 *
 *  Synchronization:
 *      The Adapter Lock should have already been acquired
 *      before calling this routine.
 *
 *  Return
 *      TRUE if updated successfully; FALSE if error.
 *
 */
uint8_t
qla2x00_update_mp_device(mp_host_t * host, fc_port_t * port, uint16_t dev_id)
{
	uint8_t success = TRUE;
	mp_device_t *dp;
	mp_path_t *path;

	ENTER("qla2x00_update_mp_device");
	DEBUG3(printk("update_mp_device: host =%p, port =%p, id = %d\n", host, port, dev_id));
	QL_DUMP_13("qla2x00_update_mp_device: node_name=", port->node_name, 8, WWN_SIZE);
	QL_DUMP_13("qla2x00_update_mp_device: port_name=", port->port_name, 8, WWN_SIZE);

	if (!qla2x00_is_ww_name_zero(port->port_name)) {

		/* Search for a device with a matching node name, or create one. */
		dp = qla2x00_find_or_allocate_mp_dev(host, dev_id, port);

		/* We either have found or created a path list. Find this
		 * host's path in the path list or allocate a new one
		 * and add it to the list.
		 */
		if (dp == NULL) {
			QL_PRINT_10("Device NOT found or created at", 0, QDBG_NO_NUM, QDBG_NL);
			return FALSE;
		}

		/* Find the path in the current path list, or allocate
		 * a new one and put it in the list if it doesn't exist.
		 * Note that we do NOT set bSuccess to FALSE in the case
		 * of failure here.  We must tolerate the situation where
		 * the customer has more paths to a device than he can
		 * get into a PATH_LIST.
		 */

		path = qla2x00_find_or_allocate_path(host, dp, dev_id, port);
		if (path == NULL) {
			QL_PRINT_12("Path NOT found or created", 0, QDBG_NO_NUM, QDBG_NL);
			return FALSE;
		}

		/* Set the PATH flag to match the device flag
		 * of whether this device needs a relogin.  If any
		 * device needs relogin, set the relogin countdown.
		 */
		if ((port->flags & FC_CONFIG))
			path->config = TRUE;

#if 0
		if (!(port->flags & FC_ONLINE) || port->flags & FC_LOGIN_NEEDED) {
#else
		if (port->state != FC_ONLINE) {
#endif
			path->relogin = TRUE;
			if (host->relogin_countdown == 0)
				host->relogin_countdown = 30;
		} else {
			path->relogin = FALSE;
		}

	} else {
		/* EMPTY */
		QL_PRINT_10("qla2x00_update_mp_host: Failed portname empty.", 0, QDBG_NO_NUM, QDBG_NL);
	}

	LEAVE("qla2x00_update_mp_device");
	return success;
}

/*
 * qla2x00_update_mp_tree
 *      Get port information from each adapter, and build or rebuild
 *      the multipath control tree from this data.  This is called
 *      from init and during port database notification.
 *
 * Input:
 *      None
 *
 * Return:
 *      Local function return code.
 *
 */
static uint32_t
qla2x00_update_mp_tree(void)
{
	mp_host_t *host;
	uint32_t rval = QLA2X00_SUCCESS;

	ENTER("qla2x00_update_mp_tree:");

	/* Loop through each adapter and see what needs updating. */
	for (host = mp_hosts_base; (host); host = host->next) {

		QL_PRINT_10("qla2x00_update_mp_tree: Adapter ", host->instance, QDBG_DEC_NUM, QDBG_NNL);
		QL_PRINT_10(" host ", host->hba, QDBG_HEX_NUM, QDBG_NNL);
		QL_PRINT_10(" flags ", host->flags, QDBG_HEX_NUM, QDBG_NL);

		/* Clear the countdown; it may be reset in the update. */
		host->relogin_countdown = 0;

		/* Override the NEEDS_UPDATE flag if disabled. */
		if (host->flags & MP_HOST_FLAG_DISABLE || host->fcport == NULL)
			host->flags &= ~MP_HOST_FLAG_NEEDS_UPDATE;

		if (host->flags & MP_HOST_FLAG_NEEDS_UPDATE) {

			/*
			 * Perform the actual updates.  If this succeeds, clear
			 * the flag that an update is needed, and failback all
			 * devices that are visible on this path to use this
			 * path.  If the update fails, leave set the flag that
			 * an update is needed, and it will be picked back up
			 * during the next timer routine.
			 */
			if (qla2x00_update_mp_host(host)) {
				host->flags &= ~MP_HOST_FLAG_NEEDS_UPDATE;

				qla2x00_failback_luns(host);
			} else
				rval = QLA2X00_FUNCTION_FAILED;

		}

	}

	if (rval != QLA2X00_SUCCESS) {
		/* EMPTY */
		QL_PRINT_12("qla2x00_update_mp_tree: Exit FAILED", 0, QDBG_NO_NUM, QDBG_NL);

	} else {
		/* EMPTY */
		QL_PRINT_10("qla2x00_update_mp_tree: Exit OK", 0, QDBG_NO_NUM, QDBG_NL);
	}
	return rval;
}

/*
 * qla2x00_find_matching_lun
 *      Find the lun in the path that matches the
 *  specified lun number.
 *
 * Input:
 *      lun  = lun number
 *      newpath = path to search for lun
 *
 * Returns:
 *      NULL or pointer to lun
 *
 * Context:
 *      Kernel context.
 * (dg)
 */
static fc_lun_t *
qla2x00_find_matching_lun(uint8_t lun, mp_path_t * newpath)
{
	fc_lun_t *lp = NULL;	/* lun ptr */
	fc_lun_t *nlp;		/* Next lun ptr */
	fc_port_t *port;	/* port ptr */

	if ((port = newpath->port) != NULL) {
		for (nlp = port->fclun; (nlp); nlp = nlp->next) {
			if (lun == nlp->lun) {
				lp = nlp;
				break;
			}
		}
	}
	return lp;
}

/*
 * qla2x00_find_path_by_name
 *      Find the path specified portname from the pathlist
 *
 * Input:
 *      host = host adapter pointer.
 * 	pathlist =  multi-path path list
 *      portname  	portname to search for
 *
 * Returns:
 * pointer to the path or NULL
 *
 * Context:
 *      Kernel context.
 */
mp_path_t *
qla2x00_find_path_by_name(mp_host_t * host, mp_path_list_t * plp, uint8_t * portname)
{
	mp_path_t *path = NULL;	/* match if not NULL */
	mp_path_t *tmp_path;
	int cnt;

	if ((tmp_path = plp->last) != NULL) {
		for (cnt = 0; cnt < plp->path_cnt; cnt++) {
			if (tmp_path->host == host && qla2x00_is_portname_equal(tmp_path->portname, portname)) {
				path = tmp_path;
				break;
			}
			tmp_path = tmp_path->next;
		}
	}
	return path;
}

/*
 * qla2x00_find_mp_dev_by_name
 *      Find the mp_dev for the specified target name.
 *
 * Input:
 *      host = host adapter pointer.
 *      name  = Target name
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t *
qla2x00_find_mp_dev_by_name(mp_host_t * host, uint8_t * name)
{
	int id;
	mp_device_t *dp;

	ENTER("qla2x00_find_mp_dev_by_name");
	for (id = 0; id < MAX_MP_DEVICES; id++) {
		if ((dp = host->mp_devs[id]) == NULL)
			continue;

		if (qla2x00_is_nodename_equal(dp->nodename, name)) {
			DEBUG3(printk("Found matching device @ index %d:\n", id));
			DEBUG3(qla2100_dump_buffer((uint8_t *) dp, sizeof(mp_device_t)));
			return dp;
		}
	}
	LEAVE("qla2x00_find_mp_dev_by_name");
	return NULL;
}

/*
 * qla2x00_find_path_by_id
 *      Find the path for the specified path id.
 *
 * Input:
 * 	dp 		multi-path device
 * 	id 		path id
 *
 * Returns:
 *      pointer to the path or NULL
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla2x00_find_path_by_id(mp_device_t * dp, uint8_t id)
{
	mp_path_t *path = NULL;
	mp_path_t *tmp_path;
	mp_path_list_t *path_list;
	int cnt;

	path_list = dp->path_list;
	tmp_path = path_list->last;
	for (cnt = 0; (tmp_path) && cnt < path_list->path_cnt; cnt++) {
		if (tmp_path->id == id) {
			path = tmp_path;
			break;
		}
		tmp_path = tmp_path->next;
	}
	return path;
}

/*
 * qla2x00_find_mp_dev_by_id
 *      Find the mp_dev for the specified target id.
 *
 * Input:
 *      host = host adapter pointer.
 *      tgt  = Target id
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
static mp_device_t *
qla2x00_find_mp_dev_by_id(mp_host_t * host, uint8_t id)
{
	if (id < MAX_MP_DEVICES)
		return host->mp_devs[id];
	else
		return NULL;
}

/*
 * qla2x00_get_visible_path
 * Find the the visible path for the specified device.
 *
 * Input:
 *      dp = device pointer
 *
 * Returns:
 *      NULL or path
 *
 * Context:
 *      Kernel context.
 */
static mp_path_t *
qla2x00_get_visible_path(mp_device_t * dp)
{
	uint16_t id;
	mp_path_list_t *path_list;
	mp_path_t *path;

	path_list = dp->path_list;
	/* if we don't have a visible path skip it */
	if ((id = path_list->visible) == PATH_INDEX_INVALID) {
		return NULL;
	}

	if ((path = qla2x00_find_path_by_id(dp, id)) == NULL)
		return NULL;

	return path;
}

/*
 * qla2x00_map_os_targets
 * Allocate the luns and setup the OS target.
 *
 * Input:
 *      host = host adapter pointer.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 */
static void
qla2x00_map_os_targets(mp_host_t * host)
{
	adapter_state_t *ha = host->ha;
	mp_path_t *path;
	mp_device_t *dp;
	os_tgt_t *tgt;
	int t;
	ENTER("qla2x00_map_os_targets ");
	for (t = 0; t < MAX_TARGETS; t++) {
		dp = host->mp_devs[t];
		if (dp != NULL) {
			DEBUG3(printk("map_os_targets: (%d) found a dp = %p, host=%p, ha=%p\n", t, dp, host, ha));

			if ((path = qla2x00_get_visible_path(dp)) == NULL) {
				printk(KERN_INFO "qla2x00(%d): No visible path for target %d, dp = %p\n",
				       host->instance, t, dp);
				continue;
			}

			/* if not the visible path skip it */
			if (path->host == host) {
				if (TGT_Q(ha, t) == NULL) {
					tgt = qla2x00_tgt_alloc(ha, t, &dp->nodename[0]);
					tgt->vis_port = path->port;
				}
				DEBUG3(printk("map_os_targets: host=%d, device= %p has VISIBLE path=%p, path id=%d\n",
					host->instance, dp, path, path->id));
			} else {
				DEBUG3(printk("map_os_targets: host=%d, device= %p has HIDDEN path=%p, path id=%d\n",
					host->instance, dp, path, path->id));

			}
			qla2x00_map_os_luns(host, dp, t);
		} else {
			if ((tgt = TGT_Q(ha, t)) != NULL) {
				qla2x00_tgt_free(ha, t);
			}
		}
	}
	LEAVE("qla2x00_map_os_targets ");
}

/*
 * qla2x00_map_os_luns
 *      Allocate the luns for the OS target.
 *
 * Input:
 *      dp = pointer to device
 *      t  = OS target number.
 *
 * Returns:
 *      None
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_map_os_luns(mp_host_t * host, mp_device_t * dp, uint16_t t)
{
	uint16_t lun;

	for (lun = 0; lun < MAX_LUNS; lun++) {
		qla2x00_map_a_oslun(host, dp, t, lun);
	}
}

static void
qla2x00_map_a_oslun(mp_host_t * host, mp_device_t * dp, uint16_t t, uint16_t lun)
{
	fc_port_t *fcport;
	fc_lun_t *fclun;
	os_lun_t *lq;
	uint16_t id;
	mp_path_t *path, *vis_path;
	mp_host_t *vis_host;

	if ((id = dp->path_list->current_path[lun])
	    != PATH_INDEX_INVALID) {
		path = qla2x00_find_path_by_id(dp, id);
		if (path) {
			fcport = path->port;
			if (fcport) {
				fclun = qla2x00_find_matching_lun(lun, path);
				if (fclun) {
					DEBUG4(printk("map_os_luns: mapping host=%d tgt= %d dev_id=%d path_id=%d fclun= %p lun = %d\n",
							host->instance, t, dp->dev_id, path->id, fclun, lun));
				}
				/* Always map all luns if they are enabled */
				if (fclun && (path->lun_data.data[lun] & LUN_DATA_ENABLED)) {
					/*
					 * Mapped lun on the visible path
					 */
					if ((vis_path = qla2x00_get_visible_path(dp)) == NULL) {
						printk(KERN_INFO
						       "qla2x00(%d): No visible path for target %d, dp = %p\n",
						       host->instance, t, dp);
						return;
					}
					vis_host = vis_path->host;

					if ((lq = qla2x00_lun_alloc(vis_host->ha, t, lun)) != NULL) {
						lq->fclun = fclun;
						if (vis_host == path->host) {
							DEBUG3(printk("map_os_luns: mapped visible host=%d, tgt=%d, lun=%d with host=%d, fclun= %p\n",
									vis_host->instance, dp->dev_id, lun,
									path->host->instance, fclun));
						} else {
							DEBUG3(printk("map_os_luns: mapped visible HOST=%d, tgt=%d, lun=%d with host=%d, fclun= %p\n",
								    vis_host->instance, dp->dev_id, lun,
								    path->host->instance, fclun));
						}
					}
				}
			}
		}
	}
}

/*
 * qla2x00_cfg_path_discovery
 *      Discover the path configuration from the device configuration
 *      for the specified host adapter and build the path search tree.
 *      This function is called after the lower level driver has
 *      completed its port and lun discovery.
 *
 * Input:
 *      ha = adapter state pointer.
 *
 * Returns:
 *      qla2x00 local function return status code.
 *
 * Context:
 *      Kernel context.
 */
int
qla2x00_cfg_path_discovery(adapter_state_t * ha)
{
	int rval = QLA2X00_SUCCESS;
	mp_host_t *host;
	uint8_t *name;

	name = &ha->init_cb->node_name[0];

	ENTER("qla2x00_cfg_path_discovery");
	QL_DUMP_13("cfg_path_discovery: Entered", name, 8, WWN_SIZE);

	ha->cfg_active = 1;
	/* Initialize the path tree for this adapter */
	host = qla2x00_find_host_by_name(name);
	if (mp_config_required) {
		if (host == NULL) {
			QL_DUMP_12("cfg_path_discovery: host not found, node name =", name, 8, WWN_SIZE);
			rval = QLA2X00_FUNCTION_FAILED;
		} else if (ha->instance != host->instance) {
			QL_PRINT_12("cfg_path_discovery: host instance don't match - instance=.",
				    ha->instance, QDBG_HEX_NUM, QDBG_NL);
			rval = QLA2X00_FUNCTION_FAILED;
		}
	} else if (host == NULL) {
		/* New host adapter so allocate it */
		if ((host = qla2x00_alloc_host(ha)) == NULL) {
			printk(KERN_INFO "qla2x00(%d): Couldn't allocate host - ha = %p.\n", (int) ha->instance, ha);
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	/* Fill in information about host */
	if (host != NULL) {
		host->flags |= MP_HOST_FLAG_NEEDS_UPDATE;
		host->fcport = ha->fcport;

		/* Check if multipath is enabled */
		if (!qla2x00_update_mp_host(host)) 
			rval = QLA2X00_FUNCTION_FAILED;
	}

	if (rval != QLA2X00_SUCCESS) {
		/* EMPTY */
		QL_PRINT_12("qla2x00_path_discovery: Exiting FAILED", 0, QDBG_NO_NUM, QDBG_NL);
	} else {
		LEAVE("qla2x00_cfg_path_discovery");
		/* EMPTY */
		QL_PRINT_10("qla2x00_path_discovery: Exiting OK", 0, QDBG_NO_NUM, QDBG_NL);
	}
	ha->cfg_active = 0;

	return rval;
}

/*
 * qla2x00_cfg_event_notifiy
 *      Callback for host driver to notify us of configuration changes.
 *
 * Input:
 *      ha = adapter state pointer.
 *      i_type = event type
 *
 * Returns:
 *
 * Context:
 *      Kernel context.
 */
int
qla2x00_cfg_event_notify(adapter_state_t * ha, uint32_t i_type)
{
	mp_host_t *host;	/* host adapter pointer */

	ENTER("qla2x00_cfg_event_notify");
	ADAPTER_STATE_LOCK(ha);
	ha->cfg_active = 1;
	switch (i_type) {
	case MP_NOTIFY_RESET_DETECTED:
		DEBUG(qla2100_print("MP_NOTIFY_RESET_DETECTED - no action\n"));
		break;
	case MP_NOTIFY_PWR_LOSS:
		DEBUG(qla2100_print("MP_NOTIFY_PWR_LOSS - update tree\n"));
		    /* Update our path tree in case we are losing the adapter */
		qla2x00_update_mp_tree();
		/* Free our resources for adapter */
		break;
	case MP_NOTIFY_LOOP_UP:
		DEBUG(qla2100_print("MP_NOTIFY_LOOP_UP - update host tree\n"));
		/* Adapter is back up with new configuration */
		if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
			host->flags |= MP_HOST_FLAG_NEEDS_UPDATE;
			host->fcport = ha->fcport;
			qla2x00_update_mp_tree();
		}
		break;
	case MP_NOTIFY_LOOP_DOWN:
	case MP_NOTIFY_BUS_RESET:
		DEBUG(qla2100_print("MP_NOTIFY_OTHERS - no action\n"));
		break;
	default:
		break;

	}
	ha->cfg_active = 0;
	ADAPTER_STATE_UNLOCK(ha);
	LEAVE("qla2x00_cfg_event_notify");

	return QLA2X00_SUCCESS;
}

/*
 * qla2x00_cfg_failover
 *      A problem has been detected with the current path for this
 *      lun.  Select the next available path as the current path
 *      for this device.
 *
 * Inputs:
 *      ha = pointer to host adapter
 *      fp - pointer to failed fc_lun (failback lun)
 *      tgt - pointer to target
 *
 * Returns:
 *      pointer to new fc_lun_t, or NULL if failover fails.
 */
fc_lun_t *
qla2x00_cfg_failover(adapter_state_t * ha, fc_lun_t * fp, os_tgt_t * tgt, srb_t * sp)
{
	mp_host_t *host;	/* host adapter pointer */
	mp_device_t *dp;	/* virtual device pointer */
	mp_path_t *new_path;	/* new path pointer */
	fc_lun_t *new_fp = NULL;

	ENTER("qla2x00_cfg_failover");
	ha->cfg_active = 1;
	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {

		QL_DUMP_10("Search node name:\n", (uint8_t *) tgt->fc_name, 8, WWN_SIZE);
		if ((dp = qla2x00_find_mp_dev_by_name(host, tgt->fc_name)) != NULL) {
			DEBUG3(printk("qla2x00_cfg_failover: dp = %p\n", dp));
			    /*
			     * Point at the next path in the path list if there is
			     * one, and if it hasn't already been failed over by another
			     * I/O. If there is only one path continuer to point at it.
			     */
			new_path = qla2x00_select_next_path(host, dp, fp->lun);
			DEBUG3(printk("cfg_failover: new path @ %p \n", new_path));
			new_fp = qla2x00_find_matching_lun(fp->lun, new_path);
			DEBUG3(printk("cfg_failover: new fp lun @ %p \n", new_fp));

			qla2x00_cfg_register_failover_lun(dp, sp, new_fp);
		} else {
			printk(KERN_INFO "qla2x00(%d): Couldn't find device to failover\n", host->instance);
		}
	}
	ha->cfg_active = 0;
	LEAVE("qla2x00_cfg_failover");
	return new_fp;
}

/*
 * IOCTL support
 */
#define CFG_IOCTL
#ifdef CFG_IOCTL
/*
 * qla2x00_cfg_get_paths
 *      Get list of paths EXT_FO_GET_PATHS.
 *
 * Input:
 *      ha = pointer to adapter
 *      bp = pointer to buffer
 *      cmd = Pointer to kernel copy of EXT_IOCTL.
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
uint32_t
qla2x00_cfg_get_paths(EXT_IOCTL * cmd, FO_GET_PATHS * bp, int mode)
{
	FO_PATHS_INFO *paths, *u_paths;
	FO_PATH_ENTRY *entry;
	EXT_DEST_ADDR *sap = &bp->HbaAddr;
	mp_host_t *host;	/* host adapter pointer */
	mp_device_t *dp;	/* virtual device pointer */
	mp_path_t *path;	/* path pointer */
	mp_path_list_t *path_list;	/* path list pointer */
	int cnt;
	uint32_t rval = 0;
	adapter_state_t *ha;

	u_paths = (FO_PATHS_INFO *) cmd->ResponseAdr;
	ha = qla2x00_get_hba((int) bp->HbaInstance);
	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		paths = kmalloc(sizeof(FO_PATHS_INFO), GFP_ATOMIC);
		if (paths==NULL) {
			DEBUG2(printk("qla_cfg_get_paths: failed to allocate memory of size (%d)\n", sizeof(FO_PATHS_INFO)));
			return 1;
		}
		memset(paths, 0, sizeof(FO_PATHS_INFO));
		/* Scan for mp_dev by nodename *ONLY* */
		if (sap->DestType != EXT_DEF_DESTTYPE_WWNN) {
			cmd->Status = EXT_STATUS_INVALID_PARAM;
			cmd->DetailStatus = EXT_DSTATUS_TARGET;
			rval = ENODEV;
			DEBUG4(printk("qla2x00_cfg_get_paths: target ca be accessed by NodeName only."));
		}

		else if ((dp = qla2x00_find_mp_dev_by_name(host, sap->DestAddr.WWNN)) != NULL) {
			path_list = dp->path_list;

			paths->PathCount = path_list->path_cnt;
			paths->VisiblePathIndex = path_list->visible;
			/* copy current paths */
			memcpy((uint8_t *) paths->CurrentPathIndex, (uint8_t *) & path_list->current_path[0], sizeof(paths->CurrentPathIndex));

			path = path_list->last;
			for (cnt = 0; cnt < path_list->path_cnt; cnt++) {
				entry = &(paths->PathEntry[path->id]);

				entry->Visible = (path->id == path_list->visible);
				entry->HbaInstance = path->host->instance;
				memcpy( (uint8_t *) entry->PortName,(uint8_t *) path->portname, 8);
				path = path->next;
			}
			/* copy data to user */
			copy_to_user((void *) &u_paths->PathCount, (void *) &paths->PathCount, 4);
			copy_to_user((void *) &u_paths->CurrentPathIndex, (void *) &paths->CurrentPathIndex, 
					sizeof(paths->CurrentPathIndex));
			copy_to_user((void *) &u_paths->PathEntry, (void *) &paths->PathEntry, 
					sizeof(paths->PathEntry));
		} else {
			cmd->Status = EXT_STATUS_DEV_NOT_FOUND;
			cmd->DetailStatus = EXT_DSTATUS_TARGET;
			DEBUG4(printk
			       ("qla2x00_cfg_get_paths: cannot find device (%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x)\n.",
				sap->DestAddr.WWNN[0], sap->DestAddr.WWNN[1], sap->DestAddr.WWNN[2],
				sap->DestAddr.WWNN[3], sap->DestAddr.WWNN[4], sap->DestAddr.WWNN[5],
				sap->DestAddr.WWNN[6], sap->DestAddr.WWNN[7]));
			rval = ENODEV;
		}
		kfree(paths);
	} else {
		cmd->Status = EXT_STATUS_DEV_NOT_FOUND;
		cmd->DetailStatus = EXT_DSTATUS_HBA_INST;
		QL_PRINT_12("qla2x00_get_paths: cannot find target", ha->instance, QDBG_DEC_NUM, QDBG_NL);
		rval = ENODEV;
	}

	return rval;

}

/*
 * qla2x00_cfg_set_current_path
 *      Set the current failover path EXT_FO_GET_PATHS IOCTL call.
 *
 * Input:
 *      ha = pointer to adapter
 *      bp = pointer to buffer
 *      cmd = Pointer to kernel copy of EXT_IOCTL.
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
int
qla2x00_cfg_set_current_path(EXT_IOCTL * cmd, FO_SET_CURRENT_PATH * bp, int mode)
{
	uint8_t orig_id, new_id;
	mp_host_t *host, *new_host;
	mp_device_t *dp;
	mp_path_list_t *path_list;
	EXT_DEST_ADDR *sap = &bp->HbaAddr;
	uint32_t rval = 0;
	adapter_state_t *ha;
	mp_path_t *new_path, *old_path;

	/* First find the adapter with the instance number. */
	ha = qla2x00_get_hba((int) bp->HbaInstance);
	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		sap = &bp->HbaAddr;
		/* Scan for mp_dev by nodename *ONLY* */
		if (sap->DestType != EXT_DEF_DESTTYPE_WWNN) {
			cmd->Status = EXT_STATUS_DEV_NOT_FOUND;
			cmd->DetailStatus = EXT_DSTATUS_TARGET;
			rval = ENODEV;
			DEBUG4(printk("qla2x00_cfg_set_current_path: target ca be accessed by NodeName only."));
		} else if ((dp = qla2x00_find_mp_dev_by_name(host, sap->DestAddr.WWNN)) != NULL) {
			path_list = dp->path_list;

			if (bp->NewCurrentPathIndex < MAX_PATHS_PER_DEVICE &&
			    sap->Lun < MAX_LUNS && bp->NewCurrentPathIndex < path_list->path_cnt) {
				orig_id = path_list->current_path[sap->Lun];

				DEBUG(printk("qla2x00_set_current_path: dev no  %d, lun %d, newindex %d, oldindex %d\n",
					     dp->dev_id, sap->Lun, bp->NewCurrentPathIndex, orig_id));
				QL_DUMP_10("node name", (uint8_t *) host->nodename, 8, WWN_SIZE);

				if (bp->NewCurrentPathIndex != orig_id) {
					/* Acquire the update spinlock. */

					/* Set the new current path. */
					new_id = path_list->current_path[sap->Lun] = bp->NewCurrentPathIndex;

					/* Release the update spinlock. */
					old_path = qla2x00_find_path_by_id(dp, orig_id);
					new_path = qla2x00_find_path_by_id(dp, new_id);
					new_host = new_path->host;
					/* remap the lun */
					qla2x00_map_a_oslun(new_host, dp, dp->dev_id, sap->Lun);

					qla2x00_send_failover_notify(dp, sap->Lun, old_path, new_path);
				} else {
					/* EMPTY */
					QL_PRINT_10("qla2x00_set_current_path:"
						    " path index not changed", 0, QDBG_NO_NUM, QDBG_NL);
				}

			} else {
				cmd->Status = EXT_STATUS_INVALID_PARAM;
				cmd->DetailStatus = EXT_DSTATUS_PATH_INDEX;
				rval = EINVAL;
				QL_PRINT_10("qla2x00_set_current_path invalid index for device.",
					    0, QDBG_NO_NUM, QDBG_NL);
			}

		} else {
			cmd->Status = EXT_STATUS_DEV_NOT_FOUND;
			cmd->DetailStatus = EXT_DSTATUS_TARGET;
			rval = ENODEV;
			QL_PRINT_10("qla2x00_set_current_path cannot find device.", 0, QDBG_NO_NUM, QDBG_NL);
		}

	} else {
		cmd->Status = EXT_STATUS_DEV_NOT_FOUND;
		cmd->DetailStatus = EXT_DSTATUS_HBA_INST;
		rval = ENODEV;
		QL_PRINT_10("qla2x00_set_current_path cannot find adapter.", 0, QDBG_NO_NUM, QDBG_NL);
	}

	return rval;
}
#endif

/*
 * MP SUPPORT ROUTINES
 */

/*
 * qla2x00_add_mp_host
 *	Add the specified host the host list.
 *
 * Input:
 *	node_name = pointer to node name
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
mp_host_t *
qla2x00_add_mp_host(uint8_t * node_name)
{
	mp_host_t *host, *temp;

	host = kmalloc(sizeof(mp_host_t), GFP_ATOMIC);
	if (host != NULL) {
		memset(host, 0, sizeof(mp_host_t));
		memcpy((uint8_t *) host->nodename, (uint8_t *) node_name, WWN_SIZE);
		host->next = NULL;
		/* add to list */
		if (mp_hosts_base == NULL) {
			mp_hosts_base = host;
		} else {
			temp = mp_hosts_base;
			while (temp->next != NULL)
				temp = temp->next;
			temp->next = host;
		}
		mp_num_hosts++;
	}
	return host;
}

/*
 * qla2x00_alloc_host
 *      Allocate and initialize an mp host structure.
 *
 * Input:
 *      ha = pointer to base driver's adapter structure.
 *
 * Returns:
 *      Pointer to host structure or null on error.
 *
 * Context:
 *      Kernel context.
 */
mp_host_t *
qla2x00_alloc_host(HBA_t * ha)
{
	mp_host_t *host, *temp;
	uint8_t *name, *portname;
	name = &ha->init_cb->node_name[0];
	portname = &ha->init_cb->port_name[0];

	QL_PRINT_13("qla2x00_alloc_host: Entered instance ", ha->instance, QDBG_DEC_NUM, QDBG_NL);
	QL_DUMP_13("HBA node name[8]= ", name, 0, WWN_SIZE);

	host = kmalloc(sizeof(mp_host_t), GFP_ATOMIC);

	if (host != NULL) {
		memset(host, 0, sizeof(mp_host_t));
		host->ha = ha;
		memcpy((uint8_t *) host->nodename, (uint8_t *) name, WWN_SIZE);
		memcpy((uint8_t *) host->portname, (uint8_t *) portname, WWN_SIZE);
		host->next = NULL;
		host->flags = MP_HOST_FLAG_NEEDS_UPDATE;
		host->instance = ha->instance;
		/* host->MaxLunsPerTarget = qla_fo_params.MaxLunsPerTarget; */

		if (qla2x00_fo_enabled(host->ha, host->instance)) {
			host->flags |= MP_HOST_FLAG_FO_ENABLED;
			QL_PRINT_13("qla2x00_alloc_host: Failover enabled", 0, QDBG_NO_NUM, QDBG_NL);
		} else {
			/* EMPTY */
			QL_PRINT_13("qla2x00_alloc_host: Failover disabled", 0, QDBG_NO_NUM, QDBG_NL);
		}
		/* add to list */
		if (mp_hosts_base == NULL) {
			mp_hosts_base = host;
		} else {
			temp = mp_hosts_base;
			while (temp->next != NULL)
				temp = temp->next;
			temp->next = host;
		}
		mp_num_hosts++;

		DEBUG3(printk("Alloc host @ %p \n", host));
	} else {
		/* EMPTY */
		QL_PRINT_2_3("qla2x00_alloc_host: Failed", 0, QDBG_NO_NUM, QDBG_NL);
	}

	return host;
}



/*
 * qla2x00_send_fo_notification
 *      Sends failover notification if needed.  Change the fc_lun pointer
 *      in the old path lun queue.
 *
 * Input:
 *      old_lp = Pointer to old fc_lun.
 *      new_lp = Pointer to new fc_lun.
 *
 * Returns:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
uint32_t
qla2x00_send_fo_notification(fc_lun_t * old_lp, fc_lun_t * new_lp)
{
	struct scsi_qla_host *old_ha = old_lp->fcport->ha;
	int rval = QLA2X00_SUCCESS;
	inq_cmd_rsp_t *pkt;
	uint16_t loop_id, lun;
	dma_addr_t phys_address;

	QL_PRINT_13("qla2x00_fo_notification: enter", 0, QDBG_NO_NUM, QDBG_NL);

	loop_id = old_lp->fcport->loop_id;
	lun = old_lp->lun;

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LUN_RESET) {
		rval = qla2x00_lun_reset(old_ha, loop_id, lun);
		if (rval == QLA2X00_SUCCESS) {
			/* EMPTY */
			QL_PRINT_10("qla2x00_fo_notification: LUN reset succeded", 0, QDBG_NO_NUM, QDBG_NL);
		} else {
			/* EMPTY */
			QL_PRINT_2("qla2x00_fo_notification: LUN reset Failed", 0, QDBG_NO_NUM, QDBG_NL);
		}

	}
	if ((qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET) ||
	    (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LOGOUT_OR_CDB)) {
		rval = qla2100_fabric_logout(old_ha, loop_id);
		if (rval == QLA2X00_SUCCESS) {
			/* EMPTY */
			QL_PRINT_10("qla2x00_fo_failover_notify: logout succeded", 0, QDBG_NO_NUM, QDBG_NL);
		} else {
			/* EMPTY */
			QL_PRINT_2("qla2x00_fo_failover_notification: logout Failed", 0, QDBG_NO_NUM, QDBG_NL);
		}

	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB) {
		pkt = pci_alloc_consistent(old_ha->pdev, sizeof(inq_cmd_rsp_t), &phys_address);
		if (pkt == NULL) {
			QL_PRINT_12("qla2x00_fo_notification(", old_ha->instance, QDBG_DEC_NUM, QDBG_NNL);
			QL_PRINT_12("): Memory Allocation failed", 0, QDBG_NO_NUM, QDBG_NL);

			return (QLA2X00_FUNCTION_FAILED);
		}

		memset(pkt,0, sizeof(inq_cmd_rsp_t));
		pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
		pkt->p.cmd.entry_count = 1;
		pkt->p.cmd.lun = lun;
		pkt->p.cmd.target = (uint8_t) loop_id;
		pkt->p.cmd.control_flags = CF_SIMPLE_TAG;
		memcpy(qla_fo_params.FailoverNotifyCdb, pkt->p.cmd.scsi_cdb, qla_fo_params.FailoverNotifyCdbLength);
/* FIXME This setup needs to be verified with Dennis. */
		pkt->p.cmd.dseg_count = 1;
		pkt->p.cmd.byte_count = cpu_to_le32(0);
		pkt->p.cmd.dseg_0_address[0] = 
		    cpu_to_le32(pci_dma_lo32(phys_address + sizeof(sts_entry_t)));
		pkt->p.cmd.dseg_0_address[1] = 
	 	    cpu_to_le32(pci_dma_hi32(phys_address + sizeof(sts_entry_t)));
		pkt->p.cmd.dseg_0_length = cpu_to_le32(0);

		rval = qla2x00_issue_iocb(old_ha, (caddr_t) pkt, phys_address, sizeof(inq_cmd_rsp_t));

		if (rval != QLA2X00_SUCCESS ||
		    pkt->p.rsp.comp_status != CS_COMPLETE ||
		    pkt->p.rsp.scsi_status & SS_CHECK_CONDITION || pkt->inq[0] == 0x7f) {
			/* EMPTY */
			QL_PRINT_12("qla2x00_fo_notification: send CDB failed", 0, QDBG_NO_NUM, QDBG_NL);
			QL_PRINT_12("comp_status = ", pkt->p.rsp.comp_status, QDBG_HEX_NUM, QDBG_NNL);
			QL_PRINT_12("scsi_status = ", pkt->p.rsp.scsi_status, QDBG_HEX_NUM, QDBG_NNL);
			QL_PRINT_12("inq[0] = ", pkt->inq[0], QDBG_HEX_NUM, QDBG_NL);
		}

		pci_free_consistent(old_ha->pdev, sizeof(inq_cmd_rsp_t), pkt, phys_address);
	}
	return rval;
}

/*
 * qla2x00_lun_reset
 *	Issue lun reset mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *      lun = lun to be reset.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
 qla2x00_lun_reset(struct scsi_qla_host * ha, uint16_t loop_id, uint16_t lun) {
	int rval;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2x00_lun_reset");

	mb[0] = MBC_LUN_RESET;
	mb[1] = loop_id << 8;
	mb[2] = lun;
	rval = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	if (rval != QLA2X00_SUCCESS) {
		 /*EMPTY*/ printk(KERN_WARNING "qla2x00_lun_reset(%d): failed = %d", (int) ha->instance, rval);
	} else {
		 /*EMPTY*/ LEAVE("qla2x00_lun_reset: exiting normally");
	}

	return rval;
}

/*
 * qla2x00_cfg_build_path_tree
 *	Find all path properties and build a path tree. The
 *  resulting tree has no actual port assigned to it
 *  until the port discovery is done by the lower level.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_cfg_build_path_tree(adapter_state_t * ha)
{
	char propbuf[512];
	uint8_t node_name[WWN_SIZE];
	uint8_t port_name[WWN_SIZE];
	fc_port_t *port;
	uint16_t dev_no = 0, tgt_no;
	int instance, rval;
	mp_host_t *host = NULL;
	uint8_t *name;
	int done, errors;
	uint8_t control_byte;

	ENTER("qla2x00_cfg_build_path_tree");
	printk(KERN_INFO "qla02%d: ConfigRequired is set. \n", (int) ha->instance);
	DEBUG(printk("qla2x00_cfg_build_path_tree: hba =%d", (int) ha->instance));

	/* Look for adapter nodename in properties */
	DEBUG(printk("build_tree: scsi-qla%02x-adapter-port \n", ha->instance));

	rval = qla2x00_get_prop_xstr(ha, propbuf, port_name, WWN_SIZE);
	if (rval != WWN_SIZE) {
		return;
	}

	/* Does nodename match the host adapter nodename? */
	name = &ha->init_cb->port_name[0];
	if (!qla2x00_is_nodename_equal(name, port_name)) {
		printk(KERN_INFO "scsi(%d): Adapter nodenames don't match  - ha = %p.\n", (int) ha->instance, ha);
		return;
	}

	instance = ha->instance;
	if ((host = qla2x00_alloc_host(ha)) == NULL) {
		printk(KERN_INFO "scsi(%d): Couldn't allocate host - ha = %p.\n", (int) instance, ha);
	} else {
		/* create a dummy port */
		port = kmalloc(sizeof(fc_port_t), GFP_ATOMIC);
		if (port == NULL) {
			printk(KERN_INFO "scsi(%d): Couldn't allocate port.\n", (int) instance);
			/* remove host */
			return;
		}
		memset(port, 0, sizeof(fc_port_t));
		done = 0;
		errors = 0;

		/* For each target on the host bus adapter */
		for (tgt_no = 0; tgt_no < MAX_MP_DEVICES && !done; tgt_no++) {

			/* get all paths for this target */
			for (dev_no = 0; dev_no < MAX_PATHS_PER_DEVICE && !done; dev_no++) {

				memset(port,0, sizeof(fc_port_t));

				/*
				 * Get "target-N-device-N-node" is a 16-chars
				 * number
				 */
				DEBUG(printk("build_tree: scsi-qla%02x-tgt-%03x-di-%02d-node\n", instance, tgt_no, dev_no));

				rval = qla2x00_get_prop_xstr(ha, propbuf, node_name, WWN_SIZE);
				if (rval != WWN_SIZE) {
					errors++;
					if (errors == MAX_PATHS_PER_DEVICE) {
						done = 1;	/* no more targets */
					}
					continue;
				}

				errors = 0;
				memcpy(port->node_name, node_name, WWN_SIZE);

				/*
				 * Get "target-N-device-N-port" is a 16-chars
				 * number
				 */
				DEBUG(printk("build_tree: scsi-qla%02x-tgt-%03x-di-%02x-port", instance, tgt_no, dev_no));

				rval = qla2x00_get_prop_xstr(ha, propbuf, port_name, WWN_SIZE);
				if (rval != WWN_SIZE)
					continue;

				memcpy(port->node_name, node_name, WWN_SIZE);
				memcpy(port->port_name, port_name, WWN_SIZE);
				port->flags |= FC_CONFIG;

				/* 
				 * Get "target-N-device-N-control" if property 
				 * is present then all luns are visible.
				 */
				DEBUG3(printk("build_tree: scsi-qla%02x-tgt-%03x-di-%02x-control", instance, tgt_no, dev_no));

				rval = qla2x00_get_prop_xstr(ha, propbuf,
								 (uint8_t *) (&control_byte), sizeof(control_byte));
				if (rval == -1) {
					/* error getting string. go to next. */
					continue;
				}

				DEBUG(printk("build_tree: control byte 0x%x\n", control_byte));

				port->mp_byte = control_byte;
				DEBUG(printk("build_tree: update_mp_device host=%p, port=%p, tgt_no=%d\n", host, port, tgt_no));

				qla2x00_update_mp_device(host, port, tgt_no);
				qla2x00_set_lun_data_from_config(host, port, tgt_no, dev_no);
			}
		}
		kfree(port);
	}
	LEAVE("qla2x00_cfg_build_path_tree");
	DEBUG(printk("Leaving: qla2x00_cfg_build_path_tree\n"));
}

/*
 * qla2x00_set_lun_data_from_config
 * Set lun_data byte from the configuration parameters.
 *
 * Input:
 * host -- pointer to host adapter structure.
 * port -- pointer to port
 * tgt  -- target number
 * dev_no  -- device number
 */
static void
qla2x00_set_lun_data_from_config(mp_host_t * host, fc_port_t * port, uint16_t tgt, uint16_t dev_no)
{
	char propbuf[512];	/* As big as largest search string */
	int rval;
	int16_t lun, l;
	adapter_state_t *ha = host->ha;
	mp_device_t *dp;
	lun_bit_mask_t lun_mask, *mask_ptr = &lun_mask;
	mp_path_list_t *pathlist;
#if 0
	uint8_t control_byte;
#endif
	mp_path_t *path;

	dp = host->mp_devs[tgt];
	if (dp == NULL) {
		printk("qla2x00_set_lun_data_from_config: Target %d not found for hba %d\n", tgt, host->instance);
		return;
	}
	if ((pathlist = dp->path_list) == NULL) {
		printk("qla2x00_set_lun_data_from_config: path list not found for target %d\n", tgt);
		return;
	}

	if ((path = qla2x00_find_path_by_name(host, pathlist, port->port_name)) == NULL) {
		printk("qla2x00_set_lun_data_from_config: No path found for target %d\n", tgt);
		return;
	}

	/* clear port information */
	path->port = NULL;

#if 0				/* 02/06/01 - move to build path tree */
	/* 
	 * Get "target-N-device-N-control" if property is present then all
	 * luns are visible.
	 */
	DEBUG3(printkf(propbuf, "build_tree: scsi-qla%02x-tgt-%03x-di-%02x-control\n", host->instance, tgt, dev_no));
	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *) (&control_byte), sizeof(control_byte));
	if (rval != -1) {
		if (!((control_byte & MP_MASK_HIDDEN) || (control_byte & MP_MASK_UNCONFIGURED))) {
			pathlist->visible = path->id;
			DEBUG(printk("qla2x00_set_lun_data_from_config: found visible path id %d hba %d\n",
				     path->id, host->instance));
		} else {
			pathlist->visible = PATH_INDEX_INVALID;	/* 01/30 */
			DEBUG(printk("qla2x00_set_lun_data_from_config: found hidden path id %d hba %d\n",
				     path->id, host->instance));
		}
		path->mp_byte = control_byte;
		DEBUG(printk("qla2x00_set_lun_data_from_config: control byte 0x%x for path id %d hba %d\n",
			     path->mp_byte, path->id, host->instance));
	}
#endif

	/* Get "target-N-device-N-preferred" as a 256 bit lun_mask */
	sprintf(propbuf, "scsi-qla%02x-tgt-%03x-di-%02x-preferred", ha->instance, tgt, dev_no);
	DEBUG2(printk("build_tree: %s\n", propbuf));
	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *) (&lun_mask), sizeof(lun_mask));

	if (rval == -1) {
		/* EMPTY */
		DEBUG2(printk("qla2x00_set_lun_data_from_config: NO Preferred mask  - ret %d\n", rval));
	} else {
		if (rval != sizeof(lun_mask)) {
			/* EMPTY */
			printk("qla2x00_set_lun_data_from_config: Preferred mask len %d is incorrect.\n", rval);
		}

		DEBUG3(printk("qla2x00_set_lun_data_from_config: Preferred mask read:\n"));
		DEBUG3(qla2100_dump_buffer((char *) &lun_mask, sizeof(lun_mask)));
		for (lun = MAX_LUNS - 1, l = 0; lun >= 0; lun--, l++) {
			if (EXT_IS_LUN_BIT_SET(mask_ptr, lun)) {
				path->lun_data.data[l] |= LUN_DATA_PREFERRED_PATH;
				pathlist->current_path[l] = path->id;
			} else {
				path->lun_data.data[l] &= ~LUN_DATA_PREFERRED_PATH;
			}
		}

	}

	/* Get "target-N-device-N-lun-disable" as a 256 bit lun_mask */
	DEBUG3(printk("build_tree: scsi-qla%02x-tgt-%03x-di-%02x-lun-disabled", ha->instance, tgt, dev_no));

	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *) & lun_mask, sizeof(lun_mask));
	if (rval == -1) {
		/* default: all luns enabled */
		for (lun = 0; lun < MAX_LUNS; lun++) 
			path->lun_data.data[lun] |= LUN_DATA_ENABLED;
	} else {
		if (rval != sizeof(lun_mask)) {
			printk("qla2x00_set_lun_data_from_config: Enable mask has wrong size %d != %d\n",rval, sizeof(lun_mask));
		} else {
			for (lun = MAX_LUNS - 1, l = 0; lun >= 0; lun--, l++) {
				/* our bit mask is inverted */
				if (!EXT_IS_LUN_BIT_SET(mask_ptr, lun))
					path->lun_data.data[l] |= LUN_DATA_ENABLED;
				else
					path->lun_data.data[l] &= ~LUN_DATA_ENABLED;
			}

		}

	}

	DEBUG3(printk("qla2x00_set_lun_data_from_config: Luns data for device %p, instance %d, path id=%d\n", d,host->instance, path->id));
	DEBUG3(qla2100_dump_buffer((char *) &path->lun_data.data[0], 64));

	LEAVE("qla2x00_set_lun_data_from_config");
}

