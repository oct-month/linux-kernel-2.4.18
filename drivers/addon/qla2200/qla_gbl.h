/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.2.x and 2.4.x 
* Copyright (C) 2001 Qlogic Corporation 
* (www.qlogic.com)
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
******************************************************************************
* Global include file.
******************************************************************************/


#ifndef _QLA_GBL_H
#define	_QLA_GBL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "exioct.h"
#include "qla_fo.h"

/*
 * Global Data in qla_fo.c source file.
 */
extern SysFoParams_t qla_fo_params;
/*
 * Global Function Prototypes in qla2x00.c source file.
 */
extern int qla2x00_get_prop_xstr(struct scsi_qla_host *, char *, uint8_t *, int);
extern void qla2x00_print(char *, uint64_t , uint8_t, uint8_t);
extern void qla2x00_dump_buffer(char *, uint8_t *, uint8_t ,
    uint32_t );
extern uint32_t qla2x00_fo_path_change(uint32_t ,
 fc_lun_t *, fc_lun_t *);
extern struct scsi_qla_host *qla2x00_get_hba(int);

/*
 * Global Function Prototypes in qla_fo.c source file.
 */
extern uint32_t qla2x00_send_fo_notification(fc_lun_t *fclun_p, fc_lun_t *olun_p);
extern void qla2x00_fo_init_params(struct scsi_qla_host *ha);

/*
 * Global Data in qla_cfg.c source file.
 */
extern mp_host_t  *mp_hosts_base;
extern uint8_t   mp_config_required;
/*
 * Global Function Prototypes in qla_cfg.c source file.
 */
extern int qla2x00_cfg_init (struct scsi_qla_host *ha);
extern int qla2x00_cfg_path_discovery(struct scsi_qla_host *ha);
extern int qla2x00_cfg_event_notify(struct scsi_qla_host *ha, uint32_t i_type);
extern fc_lun_t *qla2x00_cfg_failover(struct scsi_qla_host *ha, fc_lun_t *fp,
    os_tgt_t *tgt, srb_t *sp);
extern uint32_t qla2x00_cfg_get_paths( EXT_IOCTL *, FO_GET_PATHS *, int);
extern int qla2x00_cfg_set_current_path( EXT_IOCTL *, 
	FO_SET_CURRENT_PATH *, int);
extern void qla2x00_fo_properties(struct scsi_qla_host *ha);
extern mp_host_t * qla2x00_add_mp_host(uint8_t *);
extern void qla2x00_cfg_mem_free(struct scsi_qla_host *ha);
extern mp_host_t * qla2x00_alloc_host(HBA_t *);
extern uint8_t qla2x00_fo_check(HBA_t *ha, srb_t *sp);
extern mp_path_t *qla2x00_find_path_by_name(mp_host_t *, mp_path_list_t *,
    uint8_t *name);

/*
 * Global Function Prototypes in qla_cfgln.c source file.
 */
extern inline void *kmem_zalloc( int siz, int code, int id);
extern inline void qla_bcopy(unsigned char *, unsigned char *, int );
extern void qla2x00_cfg_build_path_tree( struct scsi_qla_host *ha);
extern uint8_t qla2x00_update_mp_device(mp_host_t *, 
	fc_port_t  *, uint16_t	);
extern void qla2x00_cfg_display_devices(void);

/*
 * Global Function Prototypes in qla_ioctl.c source file.
 */
extern int qla2x00_fo_ioctl(struct scsi_qla_host *, int, void *, int);

#ifdef __cplusplus
}
#endif

#endif /* _QLA_GBL_H */
