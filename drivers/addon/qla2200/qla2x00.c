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
 
 /*************
  TODO
  * check copy_to_user/copy_from_user return values
  * convert to new error-handling
  * dpc_lock isn't atomic
  
  
  *************/

#include "settings.h"


int num_hosts;		/* ioctl related  */
#if QL_TRACE_MEMORY
static unsigned long mem_trace[1000];
static unsigned long mem_id[1000];
#endif

#define UNIQUE_FW_NAME		/* unique F/W array names */
#ifdef ISP2200
#ifdef FC_IP_SUPPORT
#include "ql2200ip_fw.h"	/* ISP RISC 2200 IP code */
#else
#include "ql2200_fw.h"		/* ISP RISC 2200 TP code */
#include "ql2100_fw.h"
#endif
#endif
#ifdef ISP2300
#ifdef FC_IP_SUPPORT
#include "ql2300ip_fw.h"	/* ISP RISC 2300 IP code */
#else
#include "ql2300_fw.h"		/* ISP RISC 2300 TP code */
#endif
#endif
#include <linux/stat.h>
#include <linux/slab.h>


#if NO_LONG_DELAYS
#define  SYS_DELAY(x)		qla2x00_sleep(x)
#define  QLA2100_DELAY(sec)  qla2x00_sleep(sec * HZ)
#define NVRAM_DELAY() qla2x00_sleep(500)	/* 2 microsecond delay */
#else
#define  SYS_DELAY(x)		udelay(x);barrier()
#define  QLA2100_DELAY(sec)  mdelay(sec * HZ)
#endif


/*
* TIMER MACROS
*/

#if 0
#define  QLA2100_DPC_LOCK(ap)   spin_lock_irqsave(&(ap)->dpc_lock, cpu_flags);
#define  QLA2100_DPC_UNLOCK(ap) spin_unlock_irqrestore(&(ap)->dpc_lock, cpu_flags);
#else
#define  QLA2100_DPC_LOCK(ap)
#define  QLA2100_DPC_UNLOCK(ap)
#endif

#define	WATCH_INTERVAL		1	/* number of seconds */
#define	START_TIMER(f, h, w)	\
{ \
init_timer(&(h)->timer); \
(h)->timer.expires = jiffies + w * HZ;\
(h)->timer.data = (unsigned long) h; \
(h)->timer.function = (void (*)(unsigned long))f; \
(h)->flags.start_timer = FALSE; \
add_timer(&(h)->timer); \
(h)->timer_active = 1;\
}

#define	RESTART_TIMER(f, h, w)	\
{ \
(h)->timer.expires = jiffies + w * HZ;\
(h)->flags.start_timer = FALSE; \
add_timer(&(h)->timer); \
}

#define	STOP_TIMER(f, h)	\
{ \
del_timer_sync(&(h)->timer); \
(h)->timer_active = 0;\
}


#ifdef ISP2200
#define DRIVER_NAME "qla2200"
#endif
#ifdef ISP2300
#define DRIVER_NAME "qla2300"
#endif

typedef unsigned long paddr32_t;

/*
*  Qlogic Driver support Function Prototypes.
*/
static uint8_t qla2100_register_with_Linux(struct scsi_qla_host * ha, uint8_t maxchannels);
static int qla2100_done(struct scsi_qla_host *);
static void qla2100_select_queue_depth(struct Scsi_Host *, Scsi_Device *);

static void qla2100_timer(struct scsi_qla_host *);
static void qla2100_timeout_insert(struct scsi_qla_host *, srb_t *);

static uint8_t qla2100_mem_alloc(struct scsi_qla_host *);

static void qla2100_dump_regs(struct Scsi_Host *host);
#if  STOP_ON_ERROR
static void qla2100_panic(char *, struct Scsi_Host *host);
#endif
void qla2100_print_scsi_cmd(Scsi_Cmnd * cmd);
static void qla2100_abort_lun_queue(struct scsi_qla_host *, uint32_t, uint32_t, uint32_t, uint32_t);

static int qla2100_return_status(struct scsi_qla_host * ha, sts_entry_t * sts, Scsi_Cmnd * cp);
static void qla2100_mem_free(struct scsi_qla_host * ha);
void qla2100_do_dpc(void *p);

static inline void qla2100_callback(struct scsi_qla_host * ha, Scsi_Cmnd * cmd);

static inline void qla2100_enable_intrs(struct scsi_qla_host *);
static inline void qla2100_disable_intrs(struct scsi_qla_host *);
static void qla2100_extend_timeout(Scsi_Cmnd * cmd, int timeout);
static int qla2100_get_tokens(char *line, char **argv, int maxargs);

/*
*  QLogic ISP2100 Hardware Support Function Prototypes.
*/
static uint8_t qla2100_initialize_adapter(struct scsi_qla_host *);
static uint8_t qla2100_isp_firmware(struct scsi_qla_host *);
static uint8_t qla2100_pci_config(struct scsi_qla_host *);
static uint8_t qla2100_set_cache_line(struct scsi_qla_host *);
static uint8_t qla2100_chip_diag(struct scsi_qla_host *);
static uint8_t qla2100_setup_chip(struct scsi_qla_host *);
static uint8_t qla2100_init_rings(struct scsi_qla_host *);
static uint8_t qla2100_fw_ready(struct scsi_qla_host *);
uint8_t qla2100_mailbox_command(struct scsi_qla_host *, uint32_t, uint16_t *);
#ifndef FAILOVER
static uint8_t  qla2100_map_targets(struct scsi_qla_host *);
#endif
#if USE_FLASH_DATABASE
static uint8_t qla2100_get_database(struct scsi_qla_host *);
static uint8_t qla2100_save_database(struct scsi_qla_host *);
static uint8_t qla2100_program_flash_address(struct scsi_qla_host *, uint32_t, u_char);
static uint8_t qla2100_erase_flash_sector(struct scsi_qla_host *, uint32_t);
static uint8_t qla2100_poll_flash(struct scsi_qla_host *, uint32_t, u_char);
#endif
static uint8_t qla2100_loop_reset(struct scsi_qla_host *);
static uint8_t qla2100_abort_command(struct scsi_qla_host *, srb_t *);
#if 0
static uint8_t qla2100_device_reset(struct scsi_qla_host *, uint32_t, uint32_t);
static uint8_t qla2100_abort_device(struct scsi_qla_host *, uint32_t, uint32_t, uint32_t);
static uint8_t qla2100_64bit_start_scsi(struct scsi_qla_host *, srb_t *);
static uint8_t qla2100_32bit_start_scsi(struct scsi_qla_host *, srb_t *);
#endif
static uint8_t qla2100_abort_isp(struct scsi_qla_host *);
static uint8_t qla2100_loop_resync(struct scsi_qla_host *);
static void qla2100_cmd_wait(struct scsi_qla_host * ha);

void qla2100_poll(struct scsi_qla_host *);
static void qla2100_init_fc_db(struct scsi_qla_host *);
static void qla2100_init_tgt_map(struct scsi_qla_host *);
static void qla2100_reset_adapter(struct scsi_qla_host *);
static void qla2100_marker(struct scsi_qla_host *, uint32_t, uint32_t, uint32_t,
	       u_char);
static void qla2100_enable_lun(struct scsi_qla_host *);
void qla2100_isp_cmd(struct scsi_qla_host *);
static void qla2100_isr(struct scsi_qla_host *, uint16_t);
static void qla2100_rst_aen(struct scsi_qla_host *);
#if 0
static void qla2100_restart_watchdog_queue(struct scsi_qla_host *);
#endif
static void qla2x00_response_pkt(struct scsi_qla_host *, uint16_t);
static void qla2x00_status_entry(struct scsi_qla_host *, sts_entry_t *);
static void qla2x00_error_entry(struct scsi_qla_host *, response_t *);
static void qla2x00_restart_queues(struct scsi_qla_host *, uint8_t);
static void qla2x00_abort_queues(struct scsi_qla_host *, uint8_t);

uint16_t qla2x00_get_flash_version(struct scsi_qla_host *);

uint16_t qla2100_debounce_register(volatile uint16_t *);

static request_t *qla2100_req_pkt(struct scsi_qla_host *);
static uint8_t qla2100_configure_hba(struct scsi_qla_host * ha);
static void qla2100_reset_chip(struct scsi_qla_host * ha);
#if QL2100_TARGET_MODE_SUPPORT
static void qla2100_enable_lun(struct scsi_qla_host *, uint8_t, uint32_t);
static void qla2100_notify_ack(struct scsi_qla_host *, notify_entry_t *);
static void qla2100_immed_notify(struct scsi_qla_host *, notify_entry_t *);
static void qla2100_accept_io(struct scsi_qla_host *, ctio_ret_entry_t *);
static void qla2100_64bit_continue_io(struct scsi_qla_host *, atio_entry_t *, uint32_t,
			  u_long *);
static void qla2100_32bit_continue_io(struct scsi_qla_host *, atio_entry_t *, uint32_t,
			  u_long *);
static void qla2100_atio_entry(struct scsi_qla_host *, atio_entry_t *);
static void qla2100_notify_entry(struct scsi_qla_host *, notify_entry_t *);
#endif				/* QLA2100_TARGET_MODE_SUPPORT */
static void qla2100_display_fc_names(struct scsi_qla_host * ha);
void ql2100_dump_requests(struct scsi_qla_host * ha);
static void qla2100_get_properties(struct scsi_qla_host * ha, char *string);
static uint8_t qla2100_find_propname(struct scsi_qla_host * ha, char *propname, char *propstr, char *db, int siz);
static int qla2100_get_prop_16chars(struct scsi_qla_host * ha, char *propname, char *propval, char *cmdline);
static char *qla2100_get_line(char *str, char *line);
void qla2100_check_fabric_devices(struct scsi_qla_host * ha);

#ifdef FC_IP_SUPPORT

/* Entry points for IP network driver */
int qla2x00_ip_inquiry(uint16_t wAdapterNumber, BD_INQUIRY_DATA * pInquiryData);
int qla2x00_ip_enable(struct scsi_qla_host * ha, BD_ENABLE_DATA * pEnableData);
void qla2x00_ip_disable(struct scsi_qla_host * ha);
void qla2x00_add_buffers(struct scsi_qla_host * ha, uint16_t wBufferCount);
int qla2x00_send_packet(struct scsi_qla_host * ha, SEND_CB * pSendCB);

static int qla2x00_ip_initialize(struct scsi_qla_host * ha);
static int qla2x00_add_new_ip_device(struct scsi_qla_host * ha,
				     uint16_t wLoopId, uint8_t * pPortId, uint8_t * pPortName, int bForceAdd);
static int qla2x00_convert_to_arp(struct scsi_qla_host * ha, SEND_CB * pSendCB);
static void qla2x00_free_ip_block(struct scsi_qla_host * ha, IP_DEVICE_BLOCK * pIpDevice);
static int qla2x00_get_ip_loopid(struct scsi_qla_host * ha, uint8_t * pNodeName, uint8_t * pLoopId);
static int qla2x00_send_farp_request(struct scsi_qla_host * ha, uint8_t * pPortName);
static int qla2x00_register_ip_device(struct scsi_qla_host * ha);
static int qla2x00_reserve_ip_block(struct scsi_qla_host * ha, PIP_DEVICE_BLOCK * pIpDevBlk);
static int qla2x00_update_ip_device_data(struct scsi_qla_host * ha, device_data_t * pDeviceData);
static int qla2x00_reserve_loopid(struct scsi_qla_host * ha, uint16_t * pLoopId);
static void qla2x00_free_loopid(struct scsi_qla_host * ha, uint16_t wLoopId);
static int qla2x00_login_public_device(struct scsi_qla_host * ha, uint16_t * pLoopId, uint8_t * pPortID, uint16_t wOptions);
static int qla2x00_logout_public_device(struct scsi_qla_host * ha, uint16_t wLoopId, uint16_t wOptions);
#endif

static void qla2100_device_resync(struct scsi_qla_host *);
static uint8_t qla2100_update_fc_database(struct scsi_qla_host *, fcdev_t *, uint8_t);

static uint8_t qla2100_configure_fabric(struct scsi_qla_host *, uint8_t);
static uint8_t qla2100_find_all_fabric_devs(struct scsi_qla_host *, sns_data_t *,
					    dma_addr_t, struct new_dev *, uint16_t *, uint8_t *);
static uint8_t qla2100_gan(struct scsi_qla_host *, sns_data_t *, dma_addr_t, fcdev_t *);
static uint8_t qla2100_fabric_login(struct scsi_qla_host *, fcdev_t *);
uint8_t qla2100_fabric_logout(struct scsi_qla_host *, uint16_t);
static uint8_t qla2x00_get_port_database(struct scsi_qla_host *, fcdev_t *, uint8_t);

static int qla2100_configure_loop(struct scsi_qla_host *);
static uint8_t qla2100_configure_local_loop(struct scsi_qla_host *, uint8_t);

void qla2100_free_sp(Scsi_Cmnd *, srb_t *);
srb_t *qla2100_allocate_sp(struct scsi_qla_host *, Scsi_Cmnd *);
static uint8_t qla2x00_32bit_start_scsi(srb_t * sp);

static uint8_t qla2x00_64bit_start_scsi(srb_t * sp);

/* Routines for Failover */
os_tgt_t *qla2x00_tgt_alloc(struct scsi_qla_host * ha, uint16_t t, uint8_t * name);
#if  APIDEV
static int apidev_init(struct Scsi_Host *);
static int apidev_cleanup(void);
#endif
void qla2x00_tgt_free(struct scsi_qla_host * ha, uint16_t t);
struct os_lun *qla2x00_lun_alloc(struct scsi_qla_host * ha, uint16_t t, uint16_t l);

static void qla2x00_lun_free(struct scsi_qla_host * ha, uint16_t t, uint16_t l);
void qla2x00_next(struct scsi_qla_host * ha, os_tgt_t * tq, struct os_lun * lq);
static int qla2x00_build_fcport_list(struct scsi_qla_host * ha);
static void qla2x00_config_os(struct scsi_qla_host * ha);
static uint16_t qla2x00_fcport_bind(struct scsi_qla_host * ha, fc_port_t * fcport);
static int qla2x00_update_fcport(struct scsi_qla_host * ha, fc_port_t * fcport, int);
static int qla2x00_lun_discovery(struct scsi_qla_host * ha, fc_port_t * fcport, int);
int qla2x00_issue_iocb(struct scsi_qla_host * ha, void* buffer, dma_addr_t physical, size_t size);
static void qla2x00_process_failover(struct scsi_qla_host * ha);
void qla2x00_srb_abort(void *arg, int);
static int qla2x00_device_reset(struct scsi_qla_host *, uint16_t);
static int qla2x00_abort_device(struct scsi_qla_host *, uint16_t, uint16_t);
static int qla2x00_is_wwn_zero(uint8_t * nn);
static int qla2x00_marker(struct scsi_qla_host *, uint16_t, uint16_t, uint8_t);
void qla2100_get_lun_mask_from_config(struct scsi_qla_host * ha, fc_port_t * port, uint16_t tgt, uint16_t dev_no);
void qla2100_print_q_info(struct os_lun * q);
srb_t *qla2100_allocate_sp_from_ha(struct scsi_qla_host *, Scsi_Cmnd *);
static void qla2x00_failover_cleanup(struct scsi_qla_host *, os_tgt_t *, struct os_lun *, srb_t *);
void qla2x00_chg_endian(uint8_t buf[], size_t size);
static void qla2x00_check_sense(Scsi_Cmnd * cp, struct os_lun *);
static void qla2x00_suspend_lun(struct scsi_qla_host *, struct os_lun *, int, int);
static uint8_t qla2x00_remote_mailbox_command(struct scsi_qla_host * ha);

static void qla2x00_enqueue_aen(adapter_state_t *, uint16_t, void *);

uint8_t qla2200_nvram_config(struct scsi_qla_host *);
uint8_t qla2100_nvram_config(struct scsi_qla_host *);

int qla2x00_alloc_ioctl_mem(adapter_state_t * ha);
void qla2x00_free_ioctl_mem(adapter_state_t * ha);

#if  DEBUG_QLA2100
#ifndef QL_DEBUG_ROUTINES
#define QL_DEBUG_ROUTINES
#endif
#endif

#ifdef QL_DEBUG_ROUTINES
/*
*  Driver Debug Function Prototypes.
*/
qla2100_dump_buffer(uint8_t *, uint32_t);
static uint8_t ql2x_debug_print = 1;
#endif

#if DEBUG_GET_FW_DUMP
static void qla2300_dump_isp(struct scsi_qla_host * ha), qla2100_dump_word(uint8_t *, uint32_t, uint32_t);
#endif
#if  NO_LONG_DELAYS
static void qla2x00_sleep_done(struct semaphore *sem);
#endif
void qla2x00_sleep(struct scsi_qla_host * ha, int timeout);

/*
* insmod needs to find the variable and make it point to something
*/
static char *ql2xdevconf = NULL;
static int ql2xretrycount = 10;
static int qla2xenbinq = 1;
#ifdef MODULE
static char *ql2xopts = NULL;
static int ql2xmaxqdepth = 0;
static int ConfigRequired = 0;
static int recoveryTime = 10;
static int failbackTime = 3;

/* insmod qla2100 ql2xopts=verbose" */
MODULE_PARM(ql2xopts, "s");
MODULE_PARM(ql2xmaxqdepth, "i");
MODULE_PARM(ConfigRequired, "i");
MODULE_PARM(recoveryTime, "i");
MODULE_PARM(failbackTime, "i");
MODULE_LICENSE("GPL");

/*
* Just in case someone uses commas to separate items on the insmod
* command line, we define a dummy buffer here to avoid having insmod
* write wild stuff into our code segment
*/
static char dummy_buffer[60] = "Please don't add commas in your insmod command!!\n";

#endif

/*
* This is the pointer to the /proc/scsi/qla2100 code.
* access the driver.
*/
#if QLA2100_LIPTEST
static int qla2100_lip = 0;
#endif

#include <linux/ioctl.h>
#include <scsi/scsi_ioctl.h>

/***********************************************************************
* We use the Scsi_Pointer structure that's included with each command
* SCSI_Cmnd as a scratchpad for our SRB. This allows us to accept
* an unlimited number of commands.
*
* SCp will always point to the SRB structure (defined in qla2100.h).
* It is defined as follows:
*  - SCp.ptr  -- > pointer back to the cmd
*  - SCp.this_residual --> used as forward pointer to next srb
*  - SCp.buffer --> used as backward pointer to next srb
*  - SCp.buffers_residual --> used as flags field
*  - SCp.have_data_in --> not used
*  - SCp.sent_command --> not used
*  - SCp.phase --> not used
***********************************************************************/

#define	CMD_HANDLE(Cmnd)	((Cmnd)->host_scribble)

#define DID_RETRY		DID_ERROR

#include "debug.h"

uint8_t copyright[48] = "Copyright 1999-2001, Qlogic Corporation";

/****************************************************************************/
/*  LINUX -  Loadable Module Functions.                                     */
/****************************************************************************/



#if defined(ISP2100) || defined(ISP2200)
#define NUM_OF_ISP_DEVICES  3
static struct pci_device_id qla2200_pci_tbl[] = {
        {QLA2X00_VENDOR_ID, QLA2100_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
        {QLA2X00_VENDOR_ID, 0x1216, PCI_ANY_ID, PCI_ANY_ID, },
        {QLA2X00_VENDOR_ID, QLA2200_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
                { 0, },
};
MODULE_DEVICE_TABLE(pci, qla2200_pci_tbl);
                #endif
#ifdef ISP2300
#define NUM_OF_ISP_DEVICES  3
static struct pci_device_id qla2300_pci_tbl[] = {
        {QLA2X00_VENDOR_ID, QLA2300_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
        {QLA2X00_VENDOR_ID, QLA2312_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
        { 0, },
};
MODULE_DEVICE_TABLE(pci, qla2300_pci_tbl);
                
#endif


struct qla_boards QLBoardTbl_fc[NUM_OF_ISP_DEVICES] = {
/* Name ,  Board PCI Device ID,         Number of ports */
#ifdef ISP2300
	{"QLA2312 ", QLA2312_DEVICE_ID, MAX_BUSES,
#ifdef FC_IP_SUPPORT
	 &fw2300ip_code01[0], (unsigned long *) &fw2300ip_length01, &fw2300ip_addr01, &fw2300ip_version_str[0]},
#else
	 &fw2300tp_code01[0], (unsigned long *) &fw2300tp_length01, &fw2300tp_addr01, &fw2300tp_version_str[0]},
#endif
	{"QLA2300 ", QLA2300_DEVICE_ID, MAX_BUSES,
#ifdef FC_IP_SUPPORT
	 &fw2300ip_code01[0], (unsigned long *) &fw2300ip_length01, &fw2300ip_addr01, &fw2300ip_version_str[0]},
#else
	 &fw2300tp_code01[0], (unsigned long *) &fw2300tp_length01, &fw2300tp_addr01, &fw2300tp_version_str[0]},
#endif
#endif
#ifdef ISP2200
	{"QLA2200 ", QLA2200_DEVICE_ID, MAX_BUSES,
#ifdef FC_IP_SUPPORT
	 &fw2200ip_code01[0], (unsigned long *) &fw2200ip_length01, &fw2200ip_addr01, &fw2200ip_version_str[0]},
#else
	 &fw2200tp_code01[0], (unsigned long *) &fw2200tp_length01, &fw2200tp_addr01, &fw2200tp_version_str[0]},
#endif
	{"QLA2100 ", QLA2100_DEVICE_ID,           MAX_BUSES,&fw2100tp_code01[0],  
		(unsigned long *)&fw2100tp_length01,&fw2100tp_addr01, &fw2100tp_version_str[0] },
#endif
	{"        ", 0, 0}
};

/*
* Stat info for all adapters
*/
struct qla2100stats qla2100_stats;

/*
 * Declare our global semaphores
 */
DECLARE_MUTEX_LOCKED(qla2x00_detect_sem);
DECLARE_MUTEX_LOCKED(qla2x00_dpc_sem);

static unsigned long dpc_init = 0L;

/*
* Command line options
*/
static unsigned long qla2100_verbose = 1L;
static unsigned long qla2100_quiet = 0L;
static unsigned long qla2100_reinit = 0L;
static unsigned long qla2100_req_dmp = 0L;

/* Enable for failover */
static unsigned long qla2100_failover = 1L;
/*
 * List of host adapters
 */
struct scsi_qla_host *qla2100_hostlist = NULL;

#ifdef QLA2100_PROFILE
static int qla2100_buffer_size = 0;
static char *qla2100_buffer = NULL;
#endif

#include <linux/ioctl.h>
#include <scsi/scsi_ioctl.h>
#include <asm/uaccess.h>



/*************************************************************************
*   qla2100_set_info
*
* Description:
*   Set parameters for the driver from the /proc filesystem.
*
* Returns:
*************************************************************************/
int
qla2100_set_info(char *buffer, int length, struct Scsi_Host *HBAptr)
{
#if 0
	struct scsi_qla_host *ha;

	ha = (struct scsi_qla_host *) HBAptr->hostdata;

	if (!buffer || length < 8 || !strncmp("scsi-qla", buffer, 8))
		return -EINVAL;

	ha->cmdline = qla2x00_dupstr(buffer);
	if (!ha->flags.failover_enabled)
		qla2100_get_properties(ha, ha->cmdline);

#endif
	return -ENOSYS;	/* Currently this is a no-op */
}


/*
 * qla2x00_enqueue_aen
 *
 * Input:
 *	ha = adapter state pointer.
 *	event_code = async event code of the event to add to queue.
 *	payload = event payload for the queue.
 *	INTR_LOCK must be already obtained.
 *
 * Context:
 *	Interrupt context.
 */
void
qla2x00_enqueue_aen(adapter_state_t * ha, uint16_t event_code, void *payload)
{
	uint8_t new_entry;	/* index to current entry */
	uint16_t *mbx;
	EXT_ASYNC_EVENT *aen_queue;

	DEBUG3(printk("qla2x00_enqueue_aen: entered.\n"));

	aen_queue = (EXT_ASYNC_EVENT *) ha->ioctl->aen_tracking_queue;
	if (aen_queue[ha->ioctl->aen_q_tail].AsyncEventCode != 0) {
		/* Need to change queue pointers to make room. */

		/* Increment tail for adding new entry. */
		ha->ioctl->aen_q_tail++;
		if (ha->ioctl->aen_q_tail == EXT_DEF_MAX_AEN_QUEUE) {
			ha->ioctl->aen_q_tail = 0;
		}
		if (ha->ioctl->aen_q_head == ha->ioctl->aen_q_tail) {
			/*
			 * We're overwriting the oldest entry, so need to
			 * update the head pointer.
			 */
			ha->ioctl->aen_q_head++;
			if (ha->ioctl->aen_q_head == EXT_DEF_MAX_AEN_QUEUE) {
				ha->ioctl->aen_q_head = 0;
			}
		}
	}

	DEBUG(printk("qla2x00_enqueue_aen: Adding code 0x%x to aen_q %p @ %d\n",
		     event_code, aen_queue, ha->ioctl->aen_q_tail);
	    );
	new_entry = ha->ioctl->aen_q_tail;
	aen_queue[new_entry].AsyncEventCode = event_code;

	DEBUG(printk("qla2x00_enqueue_aen: Adding code 0x%8x\n", aen_queue[new_entry].AsyncEventCode));

	    /* Update payload */
	switch (event_code) {
	case MBA_LIP_OCCURRED:
	case MBA_LOOP_UP:
	case MBA_LOOP_DOWN:
	case MBA_LIP_RESET:
	case MBA_PORT_UPDATE:
		/* empty */
		break;

	case MBA_SCR_UPDATE:
		mbx = (uint16_t *) payload;
		/* al_pa */
		aen_queue[new_entry].Payload.RSCN.RSCNInfo[0] = LSB(mbx[2]);
		/* area */
		aen_queue[new_entry].Payload.RSCN.RSCNInfo[1] = MSB(mbx[2]);
		/* domain */
		aen_queue[new_entry].Payload.RSCN.RSCNInfo[2] = LSB(mbx[1]);
		/* save in big endian */
		BIG_ENDIAN_24(aen_queue[new_entry].Payload.RSCN.RSCNInfo[0]);

		aen_queue[new_entry].Payload.RSCN.AddrFormat = MSB(mbx[1]);

		break;

	default:
		/* Not supported */
		aen_queue[new_entry].AsyncEventCode = 0;
		break;
	}

	DEBUG3(printk("qla2x00_enqueue_aen: exiting.\n"));
}


/*************************************************************************
* qla2100_proc_info
*
* Description:
*   Return information to handle /proc support for the driver.
*
* inout : decides on the direction of the dataflow and the meaning of the
*         variables
* buffer: If inout==FALSE data is being written to it else read from it
*         (ptrs to a page buffer)
* *start: If inout==FALSE start of the valid data in the buffer
* offset: If inout==FALSE offset from the beginning of the imaginary file
*         from which we start writing into the buffer
* length: If inout==FALSE max number of bytes to be written into the buffer
*         else number of bytes in the buffer
* Returns:
*************************************************************************/
#define	PROC_BUF	(&qla2100_buffer[len])
int
qla2100_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout)
{
#if QLA2100_PROFILE
	struct Scsi_Host *host;
	int i;
	int len = 0;
	int size = 0;
	struct os_lun *up;
	struct qla_boards *bdp;
	struct scsi_qla_host *ha;
	uint32_t t, l;
	uint32_t tmp_sn;
	unsigned long *flags;

#if REQ_TRACE
	Scsi_Cmnd *cp;
	srb_t *sp;
#endif

	/*
	   printk(KERN_INFO "Entering proc_info buff_in=%p,offset=0x%x,length=0x%x\n",buffer,offset,length);
	 */
	host = NULL;

	/* Find the host that was specified */
	for (ha = qla2100_hostlist; (ha != NULL) && ha->host->host_no != hostno; ha = ha->next) ;

	/* if host wasn't found then exit */
	if (!ha) {
		size = sprintf(buffer, "Can't find adapter for host number %d\n", hostno);
		if (size > length) {
			return size;
		} else {
			return 0;
		}
	}

	host = ha->host;

	if (inout == TRUE) {	/* Has data been written to the file? */
		printk("qla2100_proc: has data been written to the file. \n");
		return qla2100_set_info(buffer, length, host);
	}

	/*
	   * if our old buffer is the right size use it otherwise
	   * allocate a new one.
	 */
	size = 4096 * 4;	/* get a page */
	if (qla2100_buffer_size != size) {
		/* deallocate this buffer and get a new one */
		if (qla2100_buffer != NULL) {
			kfree(qla2100_buffer);
			qla2100_buffer_size = 0;
		}
		qla2100_buffer = kmalloc(size, GFP_KERNEL);
	}
	if (qla2100_buffer == NULL) {
		size = sprintf(buffer, "qla2100 - kmalloc error at line %d\n", __LINE__);
		return size;
	}
	/* save the size of our buffer */
	qla2100_buffer_size = size;

	/* start building the print buffer */
	bdp = &QLBoardTbl_fc[ha->devnum];
	size = sprintf(PROC_BUF, "QLogic PCI to Fibre Channel Host Adapter for ISP2100/ISP2200/ISP2200A:\n"	/* 72 */
		       "        Firmware version: %2d.%02d.%02d, Driver version %s\n",	/* 66 */
		       bdp->fwver[0], bdp->fwver[1], bdp->fwver[2], QLA2100_VERSION);
	len += size;
	size = sprintf(PROC_BUF, "Entry address = %p\n", qla2100_set_info);
	len += size;

	tmp_sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | ha->serial1;
	size = sprintf(PROC_BUF, "HBA: %s, Serial# %c%05d\n", bdp->bdName, ('A' + tmp_sn / 100000), (tmp_sn % 100000));
	len += size;

#if 0
	size = sprintf(PROC_BUF,
		       "[%c%c%c%c%c%c]; Part#%c%c%c%c%c%c%c%c; FRU#%c%c%c%c%c%c%c%c; EC#%c%c%c%c%c%c%c%c\n",
		       ha->oem_string[0],
		       ha->oem_string[1],
		       ha->oem_string[2],
		       ha->oem_string[3],
		       ha->oem_string[4],
		       ha->oem_string[5],
		       ha->oem_part[0],
		       ha->oem_part[1],
		       ha->oem_part[2],
		       ha->oem_part[3],
		       ha->oem_part[4],
		       ha->oem_part[5],
		       ha->oem_part[6],
		       ha->oem_part[7],
		       ha->oem_fru[0],
		       ha->oem_fru[1],
		       ha->oem_fru[2],
		       ha->oem_fru[3],
		       ha->oem_fru[4],
		       ha->oem_fru[5],
		       ha->oem_fru[6],
		       ha->oem_fru[7],
		       ha->oem_ec[0],
		       ha->oem_ec[1],
		       ha->oem_ec[2], ha->oem_ec[3], ha->oem_ec[4], ha->oem_ec[5], ha->oem_ec[6], ha->oem_ec[7]);
	len += size;
#endif

	size = sprintf(PROC_BUF, "Request Queue = 0x%lx, Response Queue = 0x%lx\n",
		       (long unsigned int) ha->request_dma, (long unsigned int) ha->response_dma);
	len += size;
	size = sprintf(PROC_BUF, "Request Queue count= %ld, Response Queue count= %ld\n",
		       (long) REQUEST_ENTRY_CNT, (long) RESPONSE_ENTRY_CNT);
	len += size;
	size = sprintf(PROC_BUF, "Number of pending commands = 0x%lx\n", ha->actthreads);
	len += size;
	size = sprintf(PROC_BUF, "Number of queued commands = 0x%lx\n", ha->qthreads);
	len += size;
	size = sprintf(PROC_BUF, "Number of free request entries = %d\n", ha->req_q_cnt);
	len += size;
	size = sprintf(PROC_BUF, "Number of mailbox timeouts = %ld\n", qla2100_stats.mboxtout);
	len += size;
	size = sprintf(PROC_BUF, "Number of ISP aborts = %ld\n", qla2100_stats.ispAbort);
	len += size;
	size = sprintf(PROC_BUF, "Number of loop resyncs = %ld\n", qla2100_stats.ispAbort);
	len += size;
	size = sprintf(PROC_BUF, "Number of retries for empty slots = %ld\n", qla2100_stats.outarray_full);
	len += size;
	size = sprintf(PROC_BUF, "Number of reqs in retry_q = %ld\n", qla2100_stats.retry_q_cnt);
	len += size;
	size = sprintf(PROC_BUF, "Number of reqs in done_q = %ld\n", ha->done_q_cnt);
	len += size;
	size = sprintf(PROC_BUF, "Number of pending in_q reqs = %ld\n", ha->pending_in_q);
	len += size;
	flags = (unsigned long *) &ha->flags;
	size =
	    sprintf(PROC_BUF, "Host adapter: state = %s, flags= 0x%lx\n", (ha->loop_state == LOOP_DOWN) ? "DOWN" : "UP",
		    *flags);
	len += size;
	size = sprintf(PROC_BUF, "\n");
	len += size;

#if REQ_TRACE
	if (qla2100_req_dmp) {
		size = sprintf(PROC_BUF, "Outstanding Commands on controller:\n");
		len += size;
		for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
			if ((sp = ha->outstanding_cmds[i]) == NULL)
				continue;
			if ((cp = sp->cmd) == NULL)
				continue;
			size =
			    sprintf(PROC_BUF, "(%d): Pid=%d, sp flags=0x%lx, cmd=0x%p, state=%d\n", i,
				    (int) sp->cmd->serial_number, (long) sp->flags, CMD_SP(sp->cmd), (int) sp->state);
			len += size;
			if (len >= qla2100_buffer_size - 256)
				goto profile_stop;
		}
	}
#endif

	/* 2.25 node/port display to proc */
	/* Display the node name for adapter */
	size = sprintf(PROC_BUF, "\nSCSI Device Information:\n");
	len += size;
	size = sprintf(PROC_BUF,
		       "scsi-qla%d-adapter-node=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
		       (int) ha->instance,
		       ha->init_cb->node_name[0],
		       ha->init_cb->node_name[1],
		       ha->init_cb->node_name[2],
		       ha->init_cb->node_name[3],
		       ha->init_cb->node_name[4],
		       ha->init_cb->node_name[5], ha->init_cb->node_name[6], ha->init_cb->node_name[7]);
	len += size;

	/* display the port name for adapter */
	size = sprintf(PROC_BUF,
		       "scsi-qla%d-adapter-port=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
		       (int) ha->instance,
		       ha->init_cb->port_name[0],
		       ha->init_cb->port_name[1],
		       ha->init_cb->port_name[2],
		       ha->init_cb->port_name[3],
		       ha->init_cb->port_name[4],
		       ha->init_cb->port_name[5], ha->init_cb->port_name[6], ha->init_cb->port_name[7]);
	len += size;

	/* Print out device port names */
	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		if (ha->fc_db[i].loop_id == PORT_UNUSED)
			continue;

		size = sprintf(PROC_BUF,
			       "scsi-qla%d-port-%d=%02x%02x%02x%02x%02x%02x%02x%02x:"
			       "%02x%02x%02x%02x%02x%02x%02x%02x;\n",
			       (int) ha->instance, i,
			       ha->fc_db[i].name[0], ha->fc_db[i].name[1],
			       ha->fc_db[i].name[2], ha->fc_db[i].name[3],
			       ha->fc_db[i].name[4], ha->fc_db[i].name[5],
			       ha->fc_db[i].name[6], ha->fc_db[i].name[7],
			       ha->fc_db[i].wwn[0], ha->fc_db[i].wwn[1],
			       ha->fc_db[i].wwn[2], ha->fc_db[i].wwn[3],
			       ha->fc_db[i].wwn[4], ha->fc_db[i].wwn[5], ha->fc_db[i].wwn[6], ha->fc_db[i].wwn[7]);

		len += size;
	}			/* 2.25 node/port display to proc */
/* 	qla2x00_cfg_display_devices(); */

	size = sprintf(PROC_BUF, "\nSCSI LUN Information:\n");
	len += size;
	size = sprintf(PROC_BUF, "(Id:Lun)\n");
	len += size;
	/* scan for all equipment stats */
	for (t = 0; t < MAX_FIBRE_DEVICES; t++) {
		/* valid target */
		if (ha->fc_db[t].loop_id == PORT_UNUSED)
			continue;
		/* scan all luns */
		for (l = 0; l < ha->max_luns; l++) {
			up = (struct os_lun *) GET_LU_Q(ha, t, l);
			if (up == NULL)
				continue;
			if (up->io_cnt == 0 || up->io_cnt < 4)
				continue;
			/* total reads since boot */
			/* total writes since boot */
			/* total requests since boot  */
			size = sprintf(PROC_BUF, "(%2d:%2d): Total reqs %ld,", t, l, up->io_cnt);
			len += size;
			/* current number of pending requests */
			size = sprintf(PROC_BUF, " Pending %d,", (int) up->q_outcnt);
			len += size;
			/* current number of pending requests */
			size = sprintf(PROC_BUF, " Queued %d,", (int) up->q_incnt);
			len += size;
			/* current number of busy */
			size = sprintf(PROC_BUF, " full %d,", (int) up->q_bsycnt);
			len += size;
			size = sprintf(PROC_BUF, " flags 0x%x,", (int) up->q_flag);
			len += size;
			size = sprintf(PROC_BUF, " %d:%d:%02x,",
				       up->fclun->fcport->ha->instance,
				       up->fclun->fcport->cur_path, up->fclun->fcport->loop_id);
			len += size;
#if 0
			/* avg response time */
			size = sprintf(PROC_BUF, " Avg resp time %d(s),", (up->resp_time / up->io_cnt) / HZ);
			len += size;

			/* avg active time */
			size = sprintf(PROC_BUF, " Avg disk time %d(s)\n", (up->act_time / up->io_cnt) / HZ);
			len += size;
#endif
			size = sprintf(PROC_BUF, "\n");
			len += size;
			if (len >= qla2100_buffer_size-256)
				goto profile_stop;
		}
		if (len >= qla2100_buffer_size-256)
			break;
	}
      profile_stop:
	if (len >= qla2100_buffer_size) {
		printk(KERN_WARNING "qla2x00: Overflow buffer in qla2100_proc.c\n");
	}

	if (offset > len - 1) {
		kfree(qla2100_buffer);
		qla2100_buffer = NULL;
		qla2100_buffer_size = length = 0;
		*start = NULL;
	} else {
		*start = buffer;
		if (len - offset < length) {
			length = len - offset;
		}
		memcpy(buffer, &qla2100_buffer[offset], length);
	}
	/* printk(KERN_INFO "Exiting proc_info: qlabuff=%p,offset=0x%x,length=0x%x\n",qla2100_buffer,offset,length); */
#else
	return 0;
#endif

	return length;
}

/**************************************************************************
* qla2100_detect
*
* Description:
*    This routine will probe for Qlogic FC SCSI host adapters.
*    It returns the number of host adapters of a particular
*    type that were found.	 It also initialize all data necessary for
*    the driver.  It is passed-in the host number, so that it
*    knows where its first entry is in the scsi_hosts[] array.
*
* Input:
*     template - pointer to SCSI template
*
* Returns:
*  num - number of host adapters found.
**************************************************************************/
int
qla2100_detect(Scsi_Host_Template * template)
{
	device_reg_t *reg;
	int i;
	struct Scsi_Host *host;
	struct scsi_qla_host *ha, *cur_ha;
	struct qla_boards *bdp;
	unsigned long wait_switch = 0;
	struct pci_dev *pdev = NULL;
#if 0
	DECLARE_MUTEX_LOCKED(sem);
#endif

	ENTER("qla2100_detect");

#ifndef NEW_SP
	if (sizeof(srb_t) > sizeof(Scsi_Pointer))
		printk(KERN_WARNING "qla2x00: srb_t must be re-defined " "- it's too big.\n");

#ifdef CODECHECK
	if (sizeof(srb_t) > sizeof(Scsi_Pointer)) {
		printk("Redefine srb_t - it's too big.\n");
		return 0;
	}
#endif
#endif

#ifdef MODULE
	DEBUG2(printk( "DEBUG: qla2100_set_info starts at address = %x\n", (uint32_t) qla2100_set_info););
	    /*
	       * If we are called as a module, the qla2100 pointer may not be null
	       * and it would point to our bootup string, just like on the lilo
	       * command line.  IF not NULL, then process this config string with
	       * qla2100_setup
	       *
	       * Boot time Options
	       * To add options at boot time add a line to your lilo.conf file like:
	       * append="qla2100=verbose,tag_info:{{32,32,32,32},{32,32,32,32}}"
	       * which will result in the first four devices on the first two
	       * controllers being set to a tagged queue depth of 32.
	     */
	if (ql2xopts)
		qla2100_setup(ql2xopts, NULL);
	if (dummy_buffer[0] != 'P')
		printk(KERN_WARNING "qla2100: Please read the file "
		       "/usr/src/linux/drivers/scsi/README.qla2x00\n"
		       "qla2x00: to see the proper way to specify options to "
		       "the qla2x00 module\n"
		       "qla2x00: Specifically, don't use any commas when "
		       "passing arguments to\n" "qla2x00: insmod or else it might trash certain memory " "areas.\n");
#endif

	if (!pci_present())
		return 0;


	/* end of IF */
	bdp = &QLBoardTbl_fc[0];
	qla2100_hostlist = NULL;
	template->proc_name = DRIVER_NAME;

	/* Try and find each different type of adapter we support */
	for (i = 0; bdp->device_id != 0 && i < NUM_OF_ISP_DEVICES; i++, bdp++) {
		/* PCI_SUBSYSTEM_IDS supported */
		while ((pdev = pci_find_subsys(QLA2X00_VENDOR_ID, bdp->device_id, PCI_ANY_ID, PCI_ANY_ID, pdev))) {
			if (pci_enable_device(pdev))
				continue;

			/* found a adapter */
			printk("qla2x00: Found  VID=%x DID=%x SSVID=%x SSDID=%x\n",
			       pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device);

			/* If it's an XXX SubSys Vendor ID adapter, skip it. */
			/* if (pdev->subsystem_vendor == PCI_VENDOR_ID_XXX)
			   {
			   printk(
			   "qla2x00: Skip XXX SubSys Vendor ID "
			   "Controller\n");
			   continue;
			   }
			 */

			if ((host = scsi_register(template, sizeof(struct scsi_qla_host))) == NULL) {
				printk("qla2x00: couldn't register with scsi layer\n");
				return 0;
			}

			ha = (struct scsi_qla_host *) host->hostdata;

			/* Clear our data area */
			memset((void *) (ha), 0, sizeof(struct scsi_qla_host));

			/* Sanitize the information from PCI BIOS.  */
			host->irq = pdev->irq;
			host->io_port = pci_resource_start(pdev,0);
			ha->pci_bus = pdev->bus->number;
			ha->pci_device_fn = pdev->devfn;
			ha->pdev = pdev;
			scsi_set_pci_device(host, pdev);
			ha->device_id = bdp->device_id;
			ha->devnum = i;
			if (qla2100_verbose) {
				printk("scsi%d: Found a %s @ bus %d, device 0x%x, "
				       "irq %d, iobase 0x%lx\n",
				       host->host_no,
				       bdp->bdName, ha->pci_bus,
				       (ha->pci_device_fn & 0xf8) >> 3, host->irq, (unsigned long) host->io_port);
			}

			ha->iobase = (device_reg_t *) host->io_port;
			ha->host = host;

			/* 4.23 Initialize /proc/scsi/qla2x00 counters */
			ha->actthreads = 0;
			ha->qthreads = 0;
			ha->dump_done = 0;
			ha->total_isr_cnt = 0;
			ha->total_isp_aborts = 0;
			ha->total_lip_cnt = 0;
			ha->total_dev_errs = 0;
			ha->total_ios = 0;
			ha->total_bytes = 0;

			if (qla2100_mem_alloc(ha)) {
				printk(KERN_WARNING
				       "scsi%d: [ERROR] Failed to allocate " "memory for adapter\n", host->host_no);
				qla2100_mem_free(ha);
				continue;
			}

			ha->prev_topology = 0;
			ha->ports = bdp->numPorts;
			ha->host_no = host->host_no;

			ha->max_targets = MAX_TARGETS_2200;

			/* load the F/W, read paramaters, and init the H/W */
			ha->instance = num_hosts;

			if (qla2100_failover)
				ha->flags.failover_enabled = 1;

			ha->mbox_lock = SPIN_LOCK_UNLOCKED;
			INIT_LIST_HEAD(&ha->done_queue);
			INIT_LIST_HEAD(&ha->retry_queue);
			INIT_LIST_HEAD(&ha->failover_queue);
	
			if (qla2100_initialize_adapter(ha)) {
				printk(KERN_WARNING "qla2x00: Failed to initialize adapter\n");
				DEBUG2(printk("scsi%ld: Failed to initialized adapter"
					      " - Adapter flags %d\n", ha->host_no, ha->device_flags));
		
				qla2100_mem_free(ha);
				scsi_unregister(host);
				continue;
			}

			ha->next = NULL;
			/*  Mark preallocated Loop IDs in use. */
			ha->fabricid[SNS_FL_PORT].in_use = TRUE;
			ha->fabricid[FABRIC_CONTROLLER].in_use = TRUE;
			ha->fabricid[SIMPLE_NAME_SERVER].in_use = TRUE;

			/* Register our resources with Linux */
			if (qla2100_register_with_Linux(ha, bdp->numPorts - 1)) {
				printk("scsi%ld: Failed to register our " "resources\n", ha->host_no);
				qla2100_mem_free(ha);
				scsi_unregister(host);
				continue;
			}

			DEBUG2(printk("DEBUG: detect hba %d at address = %p\n", ha->host_no, ha));

			reg = ha->iobase;
			/* Disable ISP interrupts. */
			qla2100_disable_intrs(ha);

			/*
			 * These locks are used to prevent more than one CPU
			 * from modifying the queue at the same time. The
			 * higher level "io_request_lock" will reduce most
			 * contention for these locks.
			 */
			ha->list_lock = SPIN_LOCK_UNLOCKED;
			ha->dpc_lock = SPIN_LOCK_UNLOCKED;


			/*
			   * Startup the kernel thread for this host adapter
			 */
			if (dpc_init == 0) {
				ha->dpc_notify = &qla2x00_detect_sem;
				kernel_thread((int (*)(void *)) qla2100_do_dpc, (void *) ha, 0);
				/* NOTE: the above sleeps! This needs attention when swithing to
		  		   new style error-handling */

				/*
				 * Now wait for the kernel dpc thread to initialize
				 * and go to sleep.
				 */

				down(&qla2x00_detect_sem);
				dpc_init++;
				ha->dpc_notify = NULL;
			}
			ha->dpc_wait = &qla2x00_dpc_sem;


			/* Insure mailbox registers are free. */
			WRT_REG_WORD(&reg->semaphore, 0);
			WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);

			/*
			 * Wait around max 5 secs for the devices to come
			 * on-line. We don't want Linux scanning before we
			 * are ready.
			 */
			/* v2.19.5b6                                              */
			for (wait_switch = jiffies + (ha->loop_reset_delay * HZ);
			     time_after(wait_switch, jiffies) && !(ha->device_flags & DFLG_FABRIC_DEVICES);) {
				qla2100_check_fabric_devices(ha); /* <- this sleeps */
				#warning long delay
			}
			/* just in case we turned it on */
			ha->dpc_flags &= ~COMMAND_WAIT_NEEDED;

			/* List the target we have found */
			if (!ha->flags.failover_enabled)
				qla2100_display_fc_names(ha);

			/*       if failover is enabled read the user configuration */
			if (ha->flags.failover_enabled) {
				if (ConfigRequired > 0)
					mp_config_required = 1;
				else
					mp_config_required = 0;

				DEBUG(printk("qla2x00_detect: qla2x00_cfg_init for hba %d \n", ha->instance););
				qla2x00_cfg_init(ha);
			}

			/* Enable chip interrupts. */
			qla2100_enable_intrs(ha);

			/* Insert new entry into the list of adapters */
			ha->next = NULL;

			if (qla2100_hostlist == NULL) {
				qla2100_hostlist = ha;
			} else {
				cur_ha = qla2100_hostlist;

				while (cur_ha->next != NULL)
					cur_ha = cur_ha->next;

				cur_ha->next = ha;
			}
			num_hosts++;
			up(ha->dpc_wait);
		}
	}			/* end of FOR */

	qla2x00_cfg_display_devices();

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(10*HZ);
	LEAVE("qla2100_detect");
	return num_hosts;
}

/**************************************************************************
*   qla2100_register_with_Linux
*
* Description:
*   Free the passed in Scsi_Host memory structures prior to unloading the
*   module.
*
* Input:
*     ha - pointer to host adapter structure
*     maxchannels - MAX number of channels.
*
* Returns:
*  0 - Sucessfully reserved resources.
*  1 - Failed to reserved a resource.
**************************************************************************/
static uint8_t
qla2100_register_with_Linux(struct scsi_qla_host * ha, uint8_t maxchannels)
{

	struct Scsi_Host *host = ha->host;

	host->can_queue = 0xfffff;	/* unlimited  */
	host->cmd_per_lun = 1;
	host->select_queue_depths = qla2100_select_queue_depth;
	host->n_io_port = 0xFF;
	host->base = (u_long) ha->mmpbase;
	host->max_channel = maxchannels;
	/* fix: 07/31 host->max_lun = MAX_LUNS-1; */
	host->max_lun = ha->max_luns;
	host->unique_id = ha->instance;
	host->max_id = ha->max_targets;

	/* set our host ID  (need to do something about our two IDs) */
	host->this_id = 255;

	/* Register the I/O space with Linux */
	if (check_region(host->io_port, 0xff)) {
		printk(KERN_WARNING
		       "qla2x00 : Failed to reserved i/o base region 0x%04lx-0x%04lx already in use\n",
		       host->io_port, host->io_port + 0xff);
		/* 6/15/01 - free_irq(host->irq, NULL); */
		return 1;
	}

	sprintf(ha->devname, "qla2x00#%02d", host->unique_id);
	request_region(host->io_port, 0xff, ha->devname);

	/* Register the IRQ with Linux (sharable) */
	if (request_irq(host->irq, qla2100_intr_handler, SA_INTERRUPT | SA_SHIRQ, DRIVER_NAME, ha)) {
		printk(KERN_WARNING "qla2x00 : Failed to reserved interrupt %d already in use\n", host->irq);
		return 1;
	}
	/* Initialized the timer */
	START_TIMER(qla2100_timer, ha, WATCH_INTERVAL);

	return 0;
}

/**************************************************************************
*   qla2100_release
*
* Description:
*   Free the passed in Scsi_Host memory structures prior to unloading the
*   module.
*
* Input:
*     ha - pointer to host adapter structure
*
* Returns:
*  0 - Always returns good status
**************************************************************************/
int
qla2100_release(struct Scsi_Host *host)
{
	struct scsi_qla_host *ha = (struct scsi_qla_host *) host->hostdata;
#if  QL_TRACE_MEMORY
	int t;
#endif

	ENTER("qla2100_release");
	/* if adapter is running and online */
	if (!ha->flags.online)
		return 0;

	/* turn-off interrupts on the card */
	qla2100_disable_intrs(ha);


	/* Detach interrupts */
	if (host->irq)
		free_irq(host->irq, ha);

	/* release io space registers  */
	if (host->io_port)
		release_region(host->io_port, 0xff);

	/* Disable timer */
	if (ha->timer_active)
		STOP_TIMER(qla2100_timer, ha)

		    /* Kill the kernel thread for this host */
	if (ha->dpc_handler != NULL) {
			ha->dpc_notify = &qla2x00_detect_sem;
			send_sig(SIGHUP, ha->dpc_handler, 1);
			down(&qla2x00_detect_sem);
			ha->dpc_notify = NULL;
		}
#if USE_FLASH_DATABASE
	/* Move driver database to flash, if enabled. */
	if (ha->flags.enable_flash_db_update && ha->flags.updated_fc_db) {
		ha->flags.updated_fc_db = FALSE;
		qla2100_save_database(ha);
	}
#endif
#if MEMORY_MAPPED_IO
	if (ha->mmpbase) {
		iounmap((void *) (((unsigned long) ha->mmpbase) & PAGE_MASK));
	}
#endif				/* MEMORY_MAPPED_IO */

#if  APIDEV
	apidev_cleanup();
#endif
	qla2100_mem_free(ha);
	if (qla2100_failover)
		qla2x00_cfg_mem_free(ha);
	qla2x00_free_ioctl_mem(ha);

	if (qla2100_buffer != NULL) {
		kfree(qla2100_buffer);
		qla2100_buffer_size = 0;
		qla2100_buffer = NULL;
	}

#if QL_TRACE_MEMORY
	for (t = 0; t < 1000; t++) {
		if (mem_trace[t] == 0L)
			continue;
		printk("mem_trace[%d]=%lx, %lx\n", t, mem_trace[t], mem_id[t]);
	}
#endif
	ha->flags.online = FALSE;

	LEAVE("qla2100_release");
	return 0;
}

/**************************************************************************
*   qla2100_info
*
* Description:
*
* Input:
*     host - pointer to Scsi host adapter structure
*
* Returns:
*     Return a text string describing the driver.
**************************************************************************/
const char *
qla2100_info(struct Scsi_Host *host)
{
	static char qla2100_buffer[255];
	char *bp;
	struct scsi_qla_host *ha;
	struct qla_boards *bdp;

#if  APIDEV
/* We must create the api node here instead of qla2100_detect since we want
   the api node to be subdirectory of /proc/scsi/qla2x00 which will not
   have been created when qla2100_detect exits, but which will have been
   created by this point. */

	apidev_init(host);
#endif
	bp = &qla2100_buffer[0];
	ha = (struct scsi_qla_host *) host->hostdata;
	bdp = &QLBoardTbl_fc[ha->devnum];
	memset(bp, 0, sizeof(qla2100_buffer));
	sprintf(bp,
		"QLogic %sPCI to Fibre Channel Host Adapter: bus %d device %d irq %d\n"
		"        Firmware version: %2d.%02d.%02d, Driver version %s",
		(char *) &bdp->bdName[0], ha->pci_bus, (ha->pci_device_fn & 0xf8) >> 3, host->irq,
		bdp->fwver[0], bdp->fwver[1], bdp->fwver[2], QLA2100_VERSION);
	return bp;
}

/**************************************************************************
*   qla1200_queuecommand
*
* Description:
*     Queue a command to the controller.
*
* Input:
*     cmd - pointer to Scsi cmd structure
*     fn - pointer to Scsi done function
*
* Returns:
*   0 - Always
*
* Note:
* The mid-level driver tries to ensures that queuecommand never gets invoked
* concurrently with itself or the interrupt handler (although the
* interrupt handler may call this routine as part of request-completion
* handling).
**************************************************************************/
int
qla2100_queuecommand(Scsi_Cmnd * cmd, void (*fn) (Scsi_Cmnd *))
{
	struct scsi_qla_host *ha, *ha2;
	srb_t *sp;
	struct Scsi_Host *host;
	uint32_t b, t, l;
	struct os_lun *lq;
	os_tgt_t *tq;
	unsigned long handle;
	fc_port_t *fcport;

	ENTER("qla2100_queuecommand");
	COMTRACE('C');

	host = cmd->host;
	ha = (struct scsi_qla_host *) host->hostdata;

	/* Get our previous SCSI request pointer */
	sp = (srb_t *) CMD_SP(cmd);
	cmd->scsi_done = fn;

	DEBUG5(qla2100_print_scsi_cmd(cmd););
	DEBUG5(printk("qla2100_queuecmd: pid=%d, opcode=%d, timeout=%d\n", cmd->serial_number, cmd->cmnd[0],
		    cmd->timeout_per_command););

	/* Generate LU queue on bus, target, LUN */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	if ((tq = (os_tgt_t *) TGT_Q(ha, t)) != NULL && (lq = (struct os_lun *) LUN_Q(ha, t, l)) != NULL) {
		fcport = lq->fclun->fcport;
		ha2 = fcport->ha;
	} else {
		lq = NULL;
		fcport = NULL;
		ha2 = ha;
	}

	/* Set an invalid handle until we issue the command to ISP */
	/* then we will set the real handle value.                 */
	handle = INVALID_HANDLE;
	cmd->host_scribble = (unsigned char *) handle;

	/*
	   * Allocate a command packet if necessary. If we can't
	   * get one from the lun pool or the hba pool then
	   * let the scsi layer come back later.
	 */
	if (sp == NULL) {
		if ((sp = qla2100_allocate_sp(ha, cmd)) == NULL) {
			cmd->result = DID_RETRY << 16;
			printk(KERN_WARNING "qla2100_command: Couldn't allocate memory for sp - retried.\n");
			(*(cmd)->scsi_done) (cmd);
			LEAVE("qla2100_queuecommand");
			return 0;
		}
	}
	sp->cmd = cmd;

	DEBUG4(printk
	       ("scsi(%2d:%2d:%2d): (queuecmd) queue sp = %p, flags=0x%x fo retry=%d, pid=%d, cmd flags= 0x%x\n",
		(int) ha->host_no, t, l, sp, sp->flags, sp->fo_retry_cnt, cmd->serial_number, cmd->flags););

	    /* Bookkeeping information */
	sp->r_start = jiffies;	/* time the request was recieved */
	sp->u_start = 0;

	/* Setup device queue pointers. */
	sp->tgt_queue = tq;
	sp->lun_queue = lq;

	/*
	 * NOTE : q is NULL
	 * 1. When device is added from persistent binding
	 *    but has not been discovered yet.The state of
	 *    loopid == PORT_AVAIL.
	 * 2. When device is never found on the bus.(loopid == UNUSED)
	 *
	 * IF Device Queue is not created, or device is not in
	 * a valid state and link down error reporting is enabled,
	 * reject IO.
	 */
	if (fcport == NULL) {
		DEBUG4(printk("scsi(%2d:%2d:%2d): port unavailable\n", (int) ha->host_no, t, l););
	
		cmd->result = DID_NO_CONNECT << 16;
		qla2100_callback(ha, cmd);
		return 0;
	}

	/* reset flags if new command */
	if (cmd->flags == 0 && !(sp->flags & SRB_BUSY) &&	/* 10/20/00 */
	    cmd->retries == 0) {
		sp->flags = 0;
	}

	sp->fclun = lq->fclun;
	/*  Check destination HBA and port on backend */
	if (((ha2->loop_down_timer == 0 &&
	      ha2->flags.link_down_error_enable &&
	      ha2->loop_state == LOOP_DOWN) ||
	     (fcport->loop_id == FC_NO_LOOP_ID) || (ha2->flags.link_down_error_enable && fcport->state != FC_ONLINE))) {

		cmd->result = DID_NO_CONNECT << 16;
		if (fcport->loop_id == FC_NO_LOOP_ID) {
			sp->err_id = 1;
			DEBUG(printk
			      ("scsi(%2d:%2d:%2d): port is down - pid=%ld, sp=%p loopid=0x%x queued to scsi%d done_q\n",
			       (int) ha2->host_no, t, l, cmd->serial_number, sp, fcport->loop_id, (int) ha->host_no););

		} else {
			sp->err_id = 2;
			if (fcport->state != FC_ONLINE)
				sp->err_id = 3;
			DEBUG(printk
			      ("scsi(%2d:%2d:%2d): loop is down - pid=%ld , sp=%p loopid=0x%x queued to scsi%d done_q \n",
			       (int) ha2->host_no, t, l, cmd->serial_number, sp, fcport->loop_id, (int) ha->host_no););
			DEBUG(printk("scsi(%2d:%2d:%2d): port state=%d, loop state = %d, lq flags = 0x%x\n",
				   (int) ha2->host_no, t, l, fcport->state, ha2->loop_state, lq->q_flag););
		}
		ha->flags.done_requests_needed = TRUE;
		add_to_done_queue(ha, sp);
		LEAVE("qla2100_queuecommand");
		return 0;
	}

	/*
	 * SCSI Kluge
	 * ========
	 * Whenever, we need to wait for an event such as loop down
	 * (i.e. loop_down_timer ) or port down (i.e. LUN request qeueue is
	 * suspended) then we will recycle new commands back to the SCSI layer.
	 * We do this because this is normally a temporary condition and we don't
	 * want the mid-level scsi.c driver to get upset and start aborting
	 * commands.
	 *
	 * The timeout value is extracted from the command minus 1-second
	 * and put on a retry queue (watchdog). Once the command timeout it
	 * is returned to the mid-level with a BUSY status, so the mid-level
	 * will retry it. This process continues until the LOOP DOWN time
	 * expires or the condition goes away.
	 */
	if (ha2->loop_down_timer ||
	    ha->cfg_active || ha2->loop_state == LOOP_DOWN || PORT_DOWN(fcport) > 0 || fcport->login_retry > 0 ||
	    /* ((ha->device_flags & LOGIN_RETRY_NEEDED)  &&
	       ha2->loop_state != LOOP_READY) || */
	    (ha2->loop_state != LOOP_READY) || (sp->flags & SRB_FAILOVER) || (lq->q_flag & QLA2100_QSUSP)) {
		/* Insert command into watchdog queue */
		qla2100_timeout_insert(ha, sp);
		LEAVE("qla2100_queuecommand");
		return 0;
	} else
		sp->flags &= ~SRB_BUSY;	/* v5.21b16 */

	/* Set retry count if this is a new command */
	if (sp->flags == 0 && !(lq->q_flag & QLA2100_QSUSP)) {
		sp->retry_count = ha->retry_count;
		if (PORT_DOWN_RETRY(fcport) == 0)
			sp->port_down_retry_count = ha->port_down_retry_count;
	}

	/* No timeout necessary, because the upper layer is doing it for us */
	sp->wdg_time = 0;

	DEBUG3(printk("pid=%d ", cmd->serial_number););

	/* add the command to the lun queue */
	ha->qthreads++;
	ha->total_ios++;
	add_to_cmd_queue(ha, lq, sp);

	/* First start cmds for this lun if possible */
	qla2x00_next(ha, tq, lq);

	/* start any other cmds for loop down conditions */
	qla2x00_restart_queues(ha, FALSE);

	COMTRACE('c');
	LEAVE("qla2100_queuecommand"); 
	return 0;
}

/**************************************************************************
*   qla1200_abort
*
* Description:
*     Abort the specified SCSI command.
*
* Input:
*     cmd - pointer to Scsi cmd structure
*
* Returns:
**************************************************************************/
int
qla2100_abort(Scsi_Cmnd * cmd)
{
	struct scsi_qla_host *ha;
	srb_t *sp;
	srb_t *rp;
	struct Scsi_Host *host;
	uint32_t b, t, l;
	struct os_lun *q;
	int return_status = SCSI_ABORT_SUCCESS;
	int found = 0;
	int i;
	u_long handle;
	uint16_t data;
	struct list_head *list, *temp;
	unsigned long flags;

	ENTER("qla2100_abort");
	COMTRACE('A');

	ha = (struct scsi_qla_host *) cmd->host->hostdata;
	host = cmd->host;
#if DEBUG_GET_FW_DUMP
	if (ha->device_id == QLA2300_DEVICE_ID || ha->device_id == QLA2312_DEVICE_ID) {
		if (ha->dump_done != 1) {
			DEBUG(printk("\nqla2100_abort handle=%x: >>>>>>> DUMP 2300 FW <<<<<<<\n", CMD_HANDLE(cmd)));
			qla2300_dump_isp(ha);
			ha->dump_done = 1;
		}
	}
#endif
	DRIVER_LOCK();
	    /* Get the SCSI request ptr */
	sp = (srb_t *) CMD_SP(cmd);

	/*
	 * if the handle is NULL then we already completed the command.
	 * We always give the handle a value of "INVALID_HANDLE" when
	 * we received it.
	 */
	if (cmd->host_scribble == NULL) {
		DRIVER_UNLOCK();
#if  STOP_ON_ABORT
		qla2100_panic("qla2100_abort", ha->host);
#endif
		return SCSI_ABORT_NOT_RUNNING;	/* no action - we don't have command */
	}

	/* Check for a pending interrupt. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&ha->iobase->istatus);
	if (!(ha->flags.in_isr) && (data & RISC_INT))
		qla2100_isr(ha, data);
#else
	data = RD_REG_WORD(&ha->iobase->host_status_lo);
	if (!(ha->flags.in_isr) && (data & HOST_STATUS_INT))
		qla2100_isr(ha, data);
#endif

	/*
	 * if no LUN queue then something is very wrong!!!
	 */
	handle = (u_long) cmd->host_scribble;
	/* Generate LU queue on bus, target, LUN */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	if ((q = GET_LU_Q(ha, t, l)) == NULL) {
		COMTRACE('a');

		    DRIVER_UNLOCK();
		    printk(KERN_WARNING "qla2x00 (%d:%d:%d): No LUN queue for the specified device\n", (int) b, (int) t,
			   (int) l);
		return SCSI_ABORT_NOT_RUNNING;	/* no action - we don't have command */
	}
#if AUTO_ESCALATE_ABORT
	if ((sp->flags & SRB_ABORTED)) {
		DRIVER_UNLOCK();
		DEBUG2(printk("qla2100_abort: Abort escalated - returning SCSI_ABORT_SNOOZE.\n"));
		return SCSI_ABORT_SNOOZE;
	}
#endif
	/*
	 * if the command has a abort pending then tell the upper layer
	 */
	if ((sp->flags & SRB_ABORT_PENDING)) {
		COMTRACE('a');
		DRIVER_UNLOCK(); 
		if (qla2100_verbose)
			 printk("scsi(): Command has a pending abort message - ABORT_PENDING.\n");
		DEBUG2(printk("qla2100: Command has a pending abort message - ABORT_PENDING.\n"));
		return SCSI_ABORT_PENDING;
	}

	DEBUG2(printk("ABORTing command= 0x%x, jiffies = 0x%lx\n", (int) cmd, jiffies););
	DEBUG2(qla2100_print_scsi_cmd(cmd););
	DEBUG2(qla2100_print_q_info(q););

	/*
	 * Clear retry queue
	 */ 
	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->retry_queue) {
		rp = list_entry(list, srb_t, list);
	
		if (sp != rp)
			continue;
	
		DEBUG2(printk("ABORTing from retry queue - SP = %p\n", sp););
	
	    	__del_from_retry_queue(ha, sp);
		sp->cmd->result = DID_BUS_BUSY << 16;
		sp->cmd->host_scribble = (unsigned char *) NULL;
		__add_to_done_queue(ha, sp);
		found++;
	}
        spin_unlock_irqrestore(&ha->list_lock, flags);
        
	/*
	 * Clear failover queue
	 */
	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->failover_queue) {
		rp = list_entry(list, srb_t, list);

		if (sp != rp)
			continue;

		printk("ABORTing from failover queue - SP = %p\n", sp);
		/* Remove srb from failover queue. */
		__del_from_failover_queue(ha, rp);
		sp->cmd->result = DID_BUS_BUSY << 16;
		sp->cmd->host_scribble = NULL;
		__add_to_done_queue(ha, sp);
		found++;

	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	/*
	 * Our SP pointer points at the command we want to remove from the
	 * LUN queue providing we haven't already sent it to the adapter.
	 */
	if (found)
		return_status = SCSI_ABORT_SUCCESS;
	else if (!(sp->flags & SRB_SENT)) {
		found++;
		DEBUG2(printk("qla2100: Cmd in LUN queue aborted serial_number %ld.\n", sp->cmd->serial_number););
		/* Remove srb from SCSI LU queue. */
		del_from_cmd_queue(ha, q, sp);
		sp->flags |= SRB_ABORTED;
		cmd->result = DID_ABORT << 16;
		add_to_done_queue(ha, sp);

		return_status = SCSI_ABORT_SUCCESS;
	} else {		
		/* find the command in our active list */
		for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
			if (sp == ha->outstanding_cmds[i]) {
				found++;
				DEBUG2(printk
				       ("qla2100: aborting in RISC serial_number %ld \n", sp->cmd->serial_number););
#if 0
				DEBUG(qla2100_print_scsi_cmd(cmd));
				DEBUG(qla2100_print_q_info(q););
#endif
				/* v2.19.8 Ignore abort request if port is down or */
				/* loop is down because we will send back the request */
				sp->flags |= SRB_ABORTED;
				if (qla2100_abort_command(ha, sp)) {
					/* return_status = SCSI_ABORT_NOT_RUNNING; */
					return_status = SCSI_ABORT_PENDING;
				} else {
					return_status = SCSI_ABORT_PENDING;
				}
#if  0
				/* DEBUG PANIC */
				sp = (srb_t *) NULL;
				sp->cmd = cmd;
#endif
				break;
			}
		}
	}

#if  STOP_ON_ABORT
	qla2100_panic("qla2100_abort", ha->host);
#endif
	if (found == 0)
		return_status = SCSI_ABORT_NOT_RUNNING;	/* no action */

	DEBUG(printk("qla2100_abort: Aborted status returned = 0x%x.\n", return_status););
	/*
	   * Complete any commands
	 */
	if (!list_empty(&ha->done_queue))
		qla2100_done(ha);

	if ((q->q_flag & LUN_QUEUE_SUSPENDED))
		q->q_flag = q->q_flag & ~LUN_QUEUE_SUSPENDED;

	if (found) 
		qla2x00_restart_queues(ha, TRUE);
	
	DRIVER_UNLOCK();
	LEAVE("qla2100_abort");
	COMTRACE('a');
	return return_status;
}

/**************************************************************************
* qla1200_reset
*
* Description:
*    The reset function will reset the SCSI bus and abort any executing
*    commands.
*
* Input:
*      cmd = Linux SCSI command packet of the command that cause the
*            bus reset.
*      flags = SCSI bus reset option flags (see scsi.h)
*
* Returns:
*      DID_RESET in cmd.host_byte of aborted command(s)
*
* Note:
*      Resetting the bus always succeeds - is has to, otherwise the
*      kernel will panic! Try a surgical technique - sending a BUS
*      DEVICE RESET message - on the offending target before pulling
*      the SCSI bus reset line.
**************************************************************************/
int
qla2100_reset(Scsi_Cmnd * cmd, unsigned int flags)
{
	struct scsi_qla_host *ha;
	uint32_t b, t, l;
	srb_t *sp;
	typedef enum {
		ABORT_DEVICE = 1,
		DEVICE_RESET = 2,
		BUS_RESET = 3,
		ADAPTER_RESET = 4,
		RESET_DELAYED = 5,
		FAIL = 6
	} action_t;
	action_t action = ADAPTER_RESET;
	uint16_t data;
	struct os_lun *q;
	fc_port_t *fcport;
	int result;

	ENTER("qla2100_reset");
	COMTRACE('R');
	if (cmd == NULL) {
		printk(KERN_WARNING "(scsi?:?:?:?) Reset called with NULL Scsi_Cmnd " "pointer, failing.\n");
		return SCSI_RESET_SNOOZE;
	}
	ha = (struct scsi_qla_host *) cmd->host->hostdata;
	sp = (srb_t *) CMD_SP(cmd);

#if  STOP_ON_RESET
	printk("ABORTing command= 0x%x\n", (int) cmd);
	qla2100_print_scsi_cmd(cmd);
	qla2100_panic("qla2100_reset", ha->host);
#endif

	DRIVER_LOCK();
	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&ha->iobase->istatus);
	if (!(ha->flags.in_isr) && (data & RISC_INT))
		qla2100_isr(ha, data);
#else
	data = RD_REG_WORD(&ha->iobase->host_status_lo);
	if (!(ha->flags.in_isr) && (data & HOST_STATUS_INT))
		qla2100_isr(ha, data);
#endif

	DRIVER_UNLOCK(); 
	DEBUG2(printk("qla2x00 (%d:%d:%d): Reset() device\n", (int) b, (int) t, (int) l););
	    /*
	     * Determine the suggested action that the mid-level driver wants
	     * us to perform.
	     */
	if (cmd->host_scribble == NULL) {
		/*
		 * if mid-level driver called reset with a orphan SCSI_Cmnd
		 * (i.e. a command that's not pending ), so perform the
		 * function specified.
		 */
		/* 4.23 */
		if ((flags & SCSI_RESET_SUGGEST_HOST_RESET))
			action = ADAPTER_RESET;
		else
			action = DEVICE_RESET;
	} else {		/*
				   * Mid-level driver has called reset with this SCSI_Cmnd and
				   * its pending.
				 */
		if (flags & SCSI_RESET_SUGGEST_HOST_RESET)
			action = ADAPTER_RESET;
		else if (flags & SCSI_RESET_SUGGEST_BUS_RESET)
			action = BUS_RESET;
		else
			action = DEVICE_RESET;
	}

	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	q = GET_LU_Q(ha, t, l);

	/*
	   *  By this point, we want to already know what we are going to do,
	   *  so we only need to perform the course of action.
	 */

	DRIVER_LOCK(); result = SCSI_RESET_ERROR;
	switch (action) {
	case FAIL:
		break;

	case RESET_DELAYED:
		result = SCSI_RESET_PENDING;
		break;

	case ABORT_DEVICE:
		if (qla2100_verbose)
			printk(KERN_INFO "scsi(%d:%d:%d:%d): ABORT DEVICE ISSUED.\n", (int) ha->host_no, (int) b,
			       (int) t, (int) l);
		qla2100_abort_lun_queue(ha, b, t, l, DID_ABORT);
		if (!ha->loop_down_timer) {
			fcport = q->fclun->fcport;
			if (qla2x00_abort_device(ha, fcport->loop_id, l) == 0)
				result = SCSI_RESET_PENDING;
		}
		break;

	case DEVICE_RESET:
		if (qla2100_verbose)
			printk(KERN_INFO "scsi(%d:%d:%d:%d): DEVICE RESET ISSUED.\n", (int) ha->host_no, (int) b,
			       (int) t, (int) l);
	    	for (l = 0; l < ha->max_luns; l++)
			qla2100_abort_lun_queue(ha, b, t, l, DID_ABORT);
		if (!ha->loop_down_timer) {
			if (qla2x00_device_reset(ha, t) == 0)
				result = SCSI_RESET_PENDING;
			q->q_flag |= QLA2100_QRESET;
		}
		break;

	case BUS_RESET:
		if (qla2100_verbose)
			printk(KERN_INFO "scsi(%d:%d:%d:%d): LOOP RESET ISSUED.\n", (int) ha->host_no, (int) b, (int) t,
			       (int) l);
		for (t = 0; t < ha->max_targets; t++)
			for (l = 0; l < ha->max_luns; l++)
				qla2100_abort_lun_queue(ha, b, t, l, DID_RESET);
		
		if (!ha->loop_down_timer) {
			if (qla2100_loop_reset(ha) == 0)
				result = SCSI_RESET_SUCCESS | SCSI_RESET_BUS_RESET;
			/*
			 * The reset loop routine returns all the outstanding commands back
			 * with "DID_RESET" in the status field.
			 */
			if (flags & SCSI_RESET_SYNCHRONOUS) {
				cmd->result = (int) (DID_BUS_BUSY << 16);
				(*(cmd)->scsi_done) (cmd);
			}
		}

		/* ha->reset_start = jiffies; */
		break;

	case ADAPTER_RESET:
	default:
		if (qla2100_verbose) 
			printk(KERN_INFO "scsi(%d:%d:%d:%d): ADAPTER RESET ISSUED.\n", (int) ha->host_no, (int) b, (int) t, (int) l);
		
		ha->flags.reset_active = TRUE;
		/*
		 * We restarted all of the commands automatically, so the mid-level code can expect
		 * completions momentitarily.
		 */
		if (qla2100_abort_isp(ha) == 0) /* LONG DELAY */
			result = SCSI_RESET_SUCCESS | SCSI_RESET_HOST_RESET;

		ha->flags.reset_active = FALSE;
	}

	if (!list_empty(&ha->done_queue)) 
		qla2100_done(ha);

	qla2x00_restart_queues(ha, TRUE);
	DRIVER_UNLOCK(); 
	COMTRACE('r');
	LEAVE("qla2100_reset");
	return result;
}

/**************************************************************************
* qla1200_biosparam
*
* Description:
*   Return the disk geometry for the given SCSI device.
**************************************************************************/
int
qla2100_biosparam(Disk * disk, kdev_t dev, int geom[])
{
	int heads, sectors, cylinders;

	heads = 64;
	sectors = 32;
	cylinders = disk->capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = disk->capacity / (heads * sectors);
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
}

/**************************************************************************
 * qla2100_intr_handler
 *
 * Description:
 *   Handles the actual interrupt from the adapter.
 *
 * Context: Interrupt
 **************************************************************************/
void
qla2100_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{

	unsigned long cpu_flags = 0;
	struct scsi_qla_host *ha;
	uint16_t data;
	device_reg_t *reg;

	ENTER_INTR("qla2100_intr_handler");
	COMTRACE('I');

	ha = (struct scsi_qla_host *) dev_id;
	if (unlikely(!ha)) {
		printk(KERN_INFO "qla2100_intr_handler: NULL host ptr\n");
		COMTRACE('X');
		return;
	}

	/* Prevent concurrent access to adapters register */
	spin_lock_irqsave(&io_request_lock, cpu_flags);

	reg = ha->iobase;

	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&reg->istatus);
	if ((data & RISC_INT)) {
		ha->total_isr_cnt++;
		qla2100_isr(ha, data);
	}
#else
	data = RD_REG_WORD(&reg->host_status_lo);
	if ((data & HOST_STATUS_INT)) {
		ha->total_isr_cnt++;
		qla2100_isr(ha, data);
	}
#endif

	if (!list_empty(&ha->done_queue)) 
		qla2100_done(ha);


	spin_unlock_irqrestore(&io_request_lock, cpu_flags);


	/* Wakeup the DPC routine */
	if ((!ha->flags.mbox_busy && (ha->dpc_flags & (ISP_ABORT_NEEDED | RESET_MARKER_NEEDED | LOOP_RESYNC_NEEDED)))
	    && ha->dpc_wait) {	/* v2.19.4 */
		up(ha->dpc_wait);
	}

	if (unlikely(!ha->dpc_wait)) 
		DEBUG(printk("qla2x00 %d: DPC handler died.\n", (int) ha->host_no););

	COMTRACE('i');
	LEAVE_INTR("qla2100_intr_handler");
}

/**************************************************************************
* qla2100_do_dpc
*   This kernel thread is a task that is schedule by the interrupt handler
*   to perform the background processing for interrupts.
*
* Notes:
* This task always run in the context of a kernel thread.  It
* is kick-off by the driver's detect code and starts up
* up one per adapter. It immediately goes to sleep and waits for
* some fibre event.  When either the interrupt handler or
* the timer routine detects a event it will one of the task
* bits then wake us up.
**************************************************************************/
void
qla2100_do_dpc(void *p)
{
	struct scsi_qla_host *ha_in = (struct scsi_qla_host *) p;
	struct scsi_qla_host *ha;
	DECLARE_MUTEX_LOCKED(sem);
	srb_t *sp;
	uint32_t t, l;
	struct os_lun *q;
	unsigned long flags;
	fcdev_t dev;
	fc_port_t *fcport;
	uint8_t status;
	struct list_head *list, *templist;

#ifdef MODULE
	siginitsetinv(&current->blocked, SHUTDOWN_SIGS);
#else
	siginitsetinv(&current->blocked, 0);
#endif
	lock_kernel();

	/*
	   * If we were started as result of loading a module, close all of the
	   * user space pages.  We don't need them, and if we didn't close them
	   * they would be locked into memory.
	 */


	/* Flush resources */
	daemonize();
	

	reparent_to_init();
	/*
	 * Set the name of this process.
	 */

	sprintf(current->comm, "qla2x00_dpc");
	ha_in->dpc_wait = &qla2x00_dpc_sem;
	ha_in->dpc_handler = current;

	unlock_kernel();

	/*
	 * Wake up the thread that created us.
	 */
	DEBUG(printk("qla2100_dpc: Wake up parent %d\n", ha_in->dpc_notify->count.counter));

	up(ha_in->dpc_notify);

	while (1) {
		/*
		 * If we get a signal, it means we are supposed to go
		 * away and die.  This typically happens if the user is
		 * trying to unload a module.
		 */
		DEBUG3(printk("qla2x00: DPC handler sleeping\n"));
		LEAVE("qla2100: DPC");

		down_interruptible(&qla2x00_dpc_sem);
	
		if (signal_pending(current))
			break;	/* get out */

		ENTER("qla2100: DPC");
		DEBUG3(printk("qla2x00: DPC handler waking up\n"));

		for (ha = qla2100_hostlist; (ha != NULL); ha = ha->next) {
			DEBUG3(printk("scsi%d: DPC handler\n", (int) ha->host_no));
			QLA2100_DPC_LOCK(ha);
			spin_lock_irqsave(&io_request_lock, flags);
restart:
			ha->dpc_active = 1;
			if (!ha->flags.mbox_busy) {
	
				if (ha->dpc_flags & COMMAND_WAIT_NEEDED) {
					ha->dpc_flags &= ~COMMAND_WAIT_NEEDED;
					if (!(ha->dpc_flags & COMMAND_WAIT_ACTIVE)) {
						ha->dpc_flags |= COMMAND_WAIT_ACTIVE;
						QLA2100_DPC_UNLOCK(ha);
						DEBUG(printk("qla%d: waiting on commands\n", (int) ha->host_no););
						
						spin_unlock_irqrestore(&io_request_lock, flags);

						qla2100_cmd_wait(ha); /* This sleeps !!!! */
			
						spin_lock_irqsave(&io_request_lock, flags);
			
						DEBUG(printk("qla%d: cmd_wait - done\n", (int) ha->host_no););
						QLA2100_DPC_LOCK(ha);
						ha->dpc_flags &= ~COMMAND_WAIT_ACTIVE;
						goto restart;
					}
				}


				/* Determine what action is necessary */

				/* Flush all commands in watchdog queue */
				if (ha->flags.port_restart_needed) {
					DEBUG(printk("qla%ld: DPC port restarting - flushing all cmds in watchdog queue.\n", ha->instance));

					ha->flags.port_restart_needed = FALSE;
					spin_lock_irqsave(&ha->list_lock, flags);
					list_for_each_safe(list, templist, &ha->retry_queue) {
						sp = list_entry(list, srb_t, list);
						q = sp->lun_queue;
						DEBUG3(printk("qla2100_retry_q: pid=%d sp=%p, spflags=0x%x,q_flag= 0x%x\n", sp->cmd->serial_number, sp, sp->flags, q->q_flag));

						if (q == NULL)
							continue;

						q->q_flag &= ~QLA2100_QSUSP;
						__del_from_retry_queue(ha, sp);
			
						DEBUG3(printk("qla2100_retry_q: remove sp - pid=%d spfalgs=0x%x q_flag= 0x%x\n",sp->cmd->serial_number, sp->flags, q->q_flag));

						sp->cmd->result = DID_BUS_BUSY << 16;
						sp->cmd->host_scribble = NULL;
						__add_to_done_queue(ha, sp);
					}
					spin_unlock_irqrestore(&ha->list_lock, flags);
					/* v2.19 -  We want to wait until the end to
					 * return all requests back to OS.
					 */
					ha->flags.restart_queues_needed = TRUE;
				}

				/* Process any pending mailbox commands */
				if (!ha->flags.mbox_busy) {
					if (ha->dpc_flags & ISP_ABORT_NEEDED) {
						DEBUG(printk("dpc: qla2100_abort_isp ha = %p\n", ha));
						ha->dpc_flags &= ~ISP_ABORT_NEEDED;
						if (!(ha->dpc_flags & ABORT_ISP_ACTIVE)) {
							ha->dpc_flags |= ABORT_ISP_ACTIVE;
							qla2100_abort_isp(ha); 
			/* LONG DELAY with io_request_lock helt */
							ha->dpc_flags &= ~ABORT_ISP_ACTIVE;
						}
					}

					if (ha->dpc_flags & RESET_MARKER_NEEDED) {
						ha->dpc_flags &= ~RESET_MARKER_NEEDED;
						if (!(ha->dpc_flags & RESET_ACTIVE)) {
							ha->dpc_flags |= RESET_ACTIVE;
							DEBUG(printk("dpc: qla2100_reset_marker \n"););
							qla2100_rst_aen(ha);
							ha->dpc_flags &= ~RESET_ACTIVE;
						}
					}
					/* v2.19.8 Retry each device up to login retry count */
					if ((ha->device_flags & RELOGIN_NEEDED) && !ha->loop_state != LOOP_DOWN) {	/* v2.19.5 */
						DEBUG(printk("dpc: qla2100_login\n"););
						ha->device_flags &= ~RELOGIN_NEEDED;
						for (fcport = ha->fcport; fcport != NULL; fcport = fcport->next) {
							if (fcport->login_retry) {
								fcport->login_retry--;
								dev.d_id.b24 = fcport->d_id.b24;
								if ((status = qla2100_fabric_login(ha, &dev)) == 0) {
									/* 30 seconds delay */
									DEBUG(printk("dpc: logged in ID %x\n",
									       fcport->loop_id));
									fcport->login_retry = 0;
								} else if (status == 1) {
									/* dg 07/05/01  
									   ha->dpc_flags |= ISP_ABORT_NEEDED;
									 */
									fcport->login_retry = 0;
									/* 07/05 */
									fcport->state = FC_LOGIN_NEEDED;
								} else {
									ha->device_flags |= RELOGIN_NEEDED;
									/* retry the login again */
									DEBUG(printk("dpc: Retry %d logged in ID %x\n",
										     fcport->login_retry,
										     fcport->loop_id););
								}
							}
						}

					}

					/* v2.19.5 */
					if ((ha->device_flags & LOGIN_RETRY_NEEDED) && !ha->loop_state != LOOP_DOWN) {	/* v2.19.5 */
						ha->device_flags &= ~LOGIN_RETRY_NEEDED;
						ha->dpc_flags |= LOOP_RESYNC_NEEDED;
						DEBUG(printk("dpc: qla2100_login_retry\n"););
					}

					/* v2.19.5b5 */
					if (ha->dpc_flags & LOOP_RESYNC_NEEDED) {
						DEBUG(printk("dpc: qla2100_LOOP_RESYNC\n"););
						ha->dpc_flags &= ~LOOP_RESYNC_NEEDED;
						if (!(ha->dpc_flags & LOOP_RESYNC_ACTIVE)) {
							ha->dpc_flags |= LOOP_RESYNC_ACTIVE;
							QLA2100_DPC_UNLOCK(ha);
							qla2100_loop_resync(ha);
							QLA2100_DPC_LOCK(ha);
							ha->dpc_flags &= ~LOOP_RESYNC_ACTIVE;

						}
						DEBUG(printk("dpc: qla2100_LOOP_RESYNC done\n"););
					}

					if (!ha->cfg_active) {
						if ((ha->dpc_flags & FAILOVER_EVENT_NEEDED)) {
							ha->dpc_flags &= ~FAILOVER_EVENT_NEEDED;
							/* ha->dpc_flags &= ~FAILOVER_EVENT; */
							DEBUG(printk("dpc: qla2100_cfg_event_notify\n"););
							if (ha->flags.online) {	/* 11/21/00 */
								qla2x00_cfg_event_notify(ha, ha->failover_type);
								/* v5.21b16 */
								for (t = 0; t < MAX_TARGETS; t++)
									for (l = 0; l < MAX_LUNS; l++) {
										if ((q = GET_LU_Q(ha, t, l)) != NULL) {
											qla2x00_suspend_lun(ha, q,
													    recoveryTime,
													    1);
										}
									}
							}
							DEBUG(printk("\ndpc: qla2100_cfg_event_notify - done\n"););
						}

						if ((ha->dpc_flags & FAILOVER_NEEDED)) {
							/* get any requests from failover queue */
							ha->dpc_flags &= ~FAILOVER_NEEDED;
							DEBUG(printk("dpc: qla2100_process failover\n"););
							qla2x00_process_failover(ha);
							DEBUG(printk("dpc: qla2100_process failover - done\n"););
						}
					}


					if (ha->flags.restart_queues_needed) {
						DEBUG(printk("dpc: qla2100_restart_queues\n"););
						qla2x00_restart_queues(ha, FALSE);
						DEBUG(printk("dpc: qla2100_restart_queues - done\n"););
					}

					if ((ha->dpc_flags & MAILBOX_NEEDED)) {
						ha->dpc_flags &= ~MAILBOX_NEEDED;
						ha->dpc_flags |= MAILBOX_ACTIVE;
						qla2x00_remote_mailbox_command(ha);
						ha->dpc_flags &= ~MAILBOX_ACTIVE;
						ha->dpc_flags |= MAILBOX_DONE;
					}
					if (ha->flags.abort_queue_needed) {
						DEBUG(printk("dpc: qla2100_abort_queues\n"););
						qla2x00_abort_queues(ha, FALSE);
					}
					if (!ha->interrupts_on)
						qla2100_enable_intrs(ha);
				}
			}

			if (ha->flags.done_requests_needed)
				ha->flags.done_requests_needed = FALSE;

			if (!list_empty(&ha->done_queue)) 
				qla2100_done(ha);
			
			ha->dpc_active = 0;
			spin_unlock_irqrestore(&io_request_lock, flags);

			QLA2100_DPC_UNLOCK(ha);
		}

		/* The spinlock is really needed up to this point. (DB) */
	}
	DEBUG(printk("dpc: DPC handler exiting\n"););

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	ha_in->dpc_wait = NULL;
	ha_in->dpc_handler = NULL;
	ha_in->dpc_active = 0;

	/*
	   * If anyone is waiting for us to exit (i.e. someone trying to unload
	   * a driver), then wake up that process to let them know we are on
	   * the way out the door.  This may be overkill - I *think* that we
	   * could probably just unload the driver and send the signal, and when
	   * the error handling thread wakes up that it would just exit without
	   * needing to touch any memory associated with the driver itself.
	 */
	if (ha_in->dpc_notify != NULL)
		up(ha_in->dpc_notify);

}

/**************************************************************************
 * qla2100_device_queue_depth
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   If it is defined, then it is used
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 **************************************************************************/
void
qla2100_device_queue_depth(struct scsi_qla_host * p, Scsi_Device * device)
{
	int default_depth = 16;

	device->queue_depth = default_depth;
	if (device->tagged_supported) {
		device->tagged_queue = 1;
		device->current_tag = 0;
#ifdef MODULE
		if (!(ql2xmaxqdepth == 0 || ql2xmaxqdepth > 256))
			device->queue_depth = ql2xmaxqdepth;
#endif

		printk(KERN_INFO "scsi(%d:%d:%d:%d): Enabled tagged queuing, queue depth %d.\n",
		       (int) p->host_no, device->channel, device->id, device->lun, device->queue_depth);
	}

}

/**************************************************************************
 *   qla2100_select_queue_depth
 *
 * Description:
 *   Sets the queue depth for each SCSI device hanging off the input
 *   host adapter.  We use a queue depth of 2 for devices that do not
 *   support tagged queueing.
 **************************************************************************/
static void
qla2100_select_queue_depth(struct Scsi_Host *host, Scsi_Device * scsi_devs)
{
	Scsi_Device *device;
	struct scsi_qla_host *p = (struct scsi_qla_host *) host->hostdata;

	ENTER("qla2100_select_queue_depth");
	for (device = scsi_devs; device != NULL; device = device->next) {
		if (device->host == host)
			qla2100_device_queue_depth(p, device);
	}
	LEAVE("qla2100_select_queue_depth");
}

/**************************************************************************
 * ** Driver Support Routines **
 *
 * qla2100_enable_intrs
 * qla2100_disable_intrs
 **************************************************************************/
static inline void
qla2100_enable_intrs(struct scsi_qla_host * ha)
{
	device_reg_t *reg;

	reg = ha->iobase;
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, (ISP_EN_INT + ISP_EN_RISC));
	CACHE_FLUSH(&reg->ictrl);
}

static inline void
qla2100_disable_intrs(struct scsi_qla_host * ha)
{
	device_reg_t *reg;

	reg = ha->iobase;
	ha->interrupts_on = 0;
	/* disable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, 0);
	CACHE_FLUSH(&reg->ictrl);
}

/**************************************************************************
 * qla2100_done
 *      Process completed commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 *
 * Returns:
 *     None
 **************************************************************************/
static int
qla2100_done(struct scsi_qla_host * old_ha)
{
	srb_t *sp;
	os_tgt_t *tq;
	struct os_lun *lq;
	Scsi_Cmnd *cmd;
	unsigned long flags;
	struct scsi_qla_host *ha;
	int cnt;
		
	ENTER("qla2100_done");
	COMTRACE('D');
	cnt = 0;
	/*
 	 * Note: the check for empty is outside the lock, but that is ok; if
 	 * someone else adds an entry as race, that someone will
 	 * call qla2100_done() themselves; for the case where qla2100_done
 	 * is already running and someone removes all entries from the list,
 	 * we recheck inside the lock.
         */
	while (!list_empty(&old_ha->done_queue)) {
		/* remove command from done list */
		spin_lock_irqsave(&old_ha->list_lock, flags);
	
		/* 
		 * recheck to see if there was a race where someone removed all
	         * entries since we last checked 
		 */
		if (list_empty(&old_ha->done_queue)) {
			spin_unlock_irqrestore(&old_ha->list_lock, flags);
			break;
		}
	
		sp = list_entry(old_ha->done_queue.next, srb_t, list);
		list_del_init(&sp->list);
		old_ha->done_q_cnt--;
		spin_unlock_irqrestore(&old_ha->list_lock, flags);
	
	
		sp->flags &= ~SRB_SENT;
		DEBUG(sp->state = 5;);
		DEBUG3(printk("D%ld ", sp->cmd->serial_number));
		DEBUG4(printk("qla2100_done: callback pid %d\n", sp->cmd->serial_number));

		cnt++;
		cmd = sp->cmd;
		tq = sp->tgt_queue;
		lq = sp->lun_queue;
		ha = lq->fclun->fcport->ha;

		DEBUG3(printk("qla2100_done: cmd=%x tq=%x lq=%x ha=%x\n", cmd, tq, lq, ha));

		/* Decrement outstanding commands on device. */
		if (lq->q_outcnt)
			lq->q_outcnt--;
		if (lq->q_outcnt < ha->hiwat) {
			/* lq->q_flag &= ~QLA2100_QBUSY; */
			lq->q_flag = lq->q_flag & ~LUN_BUSY;
			if (lq->q_bsycnt)
				lq->q_bsycnt--;
		}

		lq->io_cnt++;

		if (ha->flags.failover_enabled) {
			DEBUG(sp->state = 6;);
			qla2x00_fo_check(ha, sp);
			if ((sp->flags & SRB_FAILOVER)) {
				DEBUG(sp->state = 10;);
				continue;
			}
		}

		switch ((cmd->result >> 16)) {
		case DID_RESET:
			lq->q_flag &= ~QLA2100_QRESET;
			/* Issue marker command. */
#if 0
			b = SCSI_BUS_32(cmd);
			t = SCSI_TCN_32(cmd);
			qla2100_marker(ha, b, t, 0, MK_SYNC_ID);
#else
			if (LOOP_RDY(ha)) {
				qla2x00_marker(ha, lq->fclun->fcport->loop_id, lq->fclun->lun, MK_SYNC_ID);
			}
#endif

			break;
		case DID_ABORT:
			sp->flags &= ~SRB_ABORT_PENDING;
			sp->flags |= SRB_ABORTED;
			if (sp->flags & SRB_TIMEOUT)
				cmd->result = DID_TIME_OUT << 16;
			break;
		default:
			break;
		}

		/* 4.10   64 and 32 bit */
		/* Release memory used for this I/O */
		if (cmd->use_sg) {
			/* DEBUG(printk("S/G unmap_sg cmd=%x\n",cmd)); */
			pci_unmap_sg(ha->pdev, cmd->request_buffer,
				     cmd->use_sg, scsi_to_pci_dma_dir(cmd->sc_data_direction));
		} else if (cmd->request_bufflen) {
			/* DEBUG(printk(
			   "No S/G unmap_single cmd=%x saved_dma_handle=%lx\n",
			   cmd,sp->saved_dma_handle)); */
			pci_unmap_single(ha->pdev, sp->saved_dma_handle,
					 cmd->request_bufflen, scsi_to_pci_dma_dir(cmd->sc_data_direction));
		}
		/* Call the mid-level driver interrupt handler */
		DEBUG3(printk("De(%d) ", cmd->serial_number););
#if 0
		if ((cmd->result & STATUS_MASK) == 2) {
			qla2x00_check_sense(cmd, lq);
		}
#endif
		qla2100_callback(ha, cmd);
		qla2x00_next(ha, tq, lq);
	}

	COMTRACE('d');
	LEAVE("qla2100_done");
	return cnt;
}

/*
 *  qla2x00_suspend_lun
 *	Suspend lun and start port down timer
 *
 * Input:
 *	ha = adapter block pointer.
 *  lq = lun queue
 *  time = time in seconds
 *  count = number of times to let time expire
 *
 * Context:
 *	Interrupt context.
 */
static void
qla2x00_suspend_lun(struct scsi_qla_host * ha, struct os_lun * lq, int time, int count)
{
	fc_port_t *fcport;
	srb_t *sp1;
	struct list_head *list, *temp;
	unsigned long flags;

	/* suspend lun queue */
	lq->q_flag = lq->q_flag | LUN_QUEUE_SUSPENDED;

	DEBUG3(printk("SUSPEND_LUN: lq=%p ha instance=%d count=%d\n", lq, (int) ha->instance, count););
	/* Remove all pending commands from request queue */
	/* and return them back to OS.                    */

	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &lq->cmd) {
		/* Remove srb from request LUN queue. */
		sp1 = list_entry(list, srb_t, list);
		__del_from_cmd_queue(lq,sp1);
	
		/* Set ending status. */
		sp1->cmd->result = DID_BUS_BUSY << 16;
		sp1->cmd->host_scribble = NULL;
		__add_to_done_queue(ha,sp1);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);
	qla2100_done(ha);

	/* suspend port */
	if (lq->fclun) {
		fcport = lq->fclun->fcport;
		if (fcport) {
			if (PORT_DOWN(fcport) == 0)
				PORT_DOWN(fcport) = time;

			if (PORT_DOWN_RETRY(fcport) == 0)
				PORT_DOWN_RETRY(fcport) = count * time;
		}
	}
}

/*
 *  qla2x00_check_sense
 *
 * Input:
 * cp = SCSI command structure
 * lq = lun queue
 *
 * Context:
 *	Interrupt context.
 */
static void
qla2x00_check_sense(Scsi_Cmnd * cp, struct os_lun * lq)
{
	struct scsi_qla_host *ha;
	srb_t *sp = (srb_t *) CMD_SP(cp);
	if (((cp->sense_buffer[0] & 0x70) >> 4) != 7) {
		return;
	}

	switch (cp->sense_buffer[2] & 0xf) {
	case 1:
		/* recoverable errors or like OK */
		cp->result = DID_OK << 16;
		break;

	case 2:		/* need to retry until device becomes ready */
		if (sp->retry_count > 0) {
			sp->retry_count--;
			cp->result = (int) DID_BUS_BUSY << 16;
			ha = lq->fclun->fcport->ha;
			DEBUG2(printk("CHECK_SENSE: cmd=%p lq=%p ha instance=%d retries=%d\n",
				      cp, lq, (int) ha->instance, sp->retry_count));
			if (lq->q_flag & LUN_QUEUE_SUSPENDED) {
				qla2x00_suspend_lun(ha, lq, 6, 1);
			}
		}
		break;
	}

}

/**************************************************************************
* qla2100_return_status
* 	Translates a ISP error to a Linux SCSI error
**************************************************************************/
static int
qla2100_return_status(struct scsi_qla_host * ha, sts_entry_t * sts, Scsi_Cmnd * cp)
{
	int host_status = DID_ERROR;
	int scsi_status;
	int comp_status;
	unsigned resid;
	srb_t *sp;
	uint8_t *strp;
	os_tgt_t *tq;
	struct os_lun *lq;
	fc_port_t *fcport;
#if DEBUG_QLA2100_INTR
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif				/* DEBUG_QLA2100_INTR */

	ENTER("qla2100_return_status");

#if DEBUG_QLA2100_INTR
	/*
	   DEBUG(printk("qla2x00: compl status = 0x%04x\n", sts->comp_status));
	 */
#endif
	scsi_status = sts->scsi_status;
	comp_status = sts->comp_status;
	sp = (srb_t *) CMD_SP(cp);
	tq = sp->tgt_queue;
	lq = sp->lun_queue;

	if ((scsi_status & SS_RESIDUAL_OVER))
		comp_status = CS_DATA_OVERRUN;
	else if ((scsi_status & SS_RESPONSE_INFO_LEN_VALID) && sts->rsp_info[3] == (uint8_t) 0)
		comp_status = CS_COMPLETE;

	/* If scanning and missing lun then let the scsi layer skip it */
	/* 4.24   dg 01/18/2001 */
#if  DUMP_INQ_DATA
	if (cp->cmnd[0] == 0x12) {
		strp = (uint8_t *) cp->request_buffer;
		DEBUG(printk("scsi%d: Inquiry data\n", ha->instance));
		DEBUG(qla2100_dump_buffer((uint8_t *) strp, 16));
	}
#endif
	if (cp->cmnd[0] == 0x12 && qla2xenbinq && cp->lun == 0) {	
		/* inquiry */
		strp = (uint8_t *) cp->request_buffer;
		if (*strp == 0x7f && lq->io_cnt < 5) {
			/* Make lun unassigned and wrong type */
			*strp = 0x23;
		}
	}
	switch (comp_status) {
	case CS_COMPLETE:
		host_status = DID_OK;
		/* v2.19.5b2 Reset port down retry on success. */
		sp->port_down_retry_count = ha->port_down_retry_count;
		sp->fo_retry_cnt = 0;
		break;
	case CS_PORT_UNAVAILABLE:
		/* release target data structure */
		host_status = DID_NO_CONNECT;
		sp->err_id = 4;
		DEBUG3(printk("scsi: Unavail port detected 0x%x-0x%x.\n", sts->comp_status, sts->scsi_status));
		break;
	case CS_PORT_LOGGED_OUT:
		sp->err_id = 5;
		host_status = DID_NO_CONNECT;
		break;
	case CS_PORT_CONFIG_CHG:
		sp->err_id = 6;
		host_status = DID_NO_CONNECT;
		break;
	case CS_PORT_BUSY:
		sp->err_id = 7;
		host_status = DID_NO_CONNECT;
		break;
	case CS_INCOMPLETE:
		sp->err_id = 8;
		host_status = DID_NO_CONNECT;
		DEBUG(printk("hba%ld: Port Error detected 0x%x-0x%x.\n",
			     ha->instance, sts->comp_status, sts->scsi_status));
		break;
	case CS_RESET:
		host_status = DID_RESET;
		break;
	case CS_ABORTED:
		host_status = DID_ABORT;
		break;
	case CS_TIMEOUT:
		host_status = DID_ERROR;
		/* v2.19.8 if timeout then check to see if logout occurred */
		fcport = lq->fclun->fcport;
		if ((fcport->flags & FC_FABRIC_DEVICE) && (sts->status_flags & IOCBSTAT_SF_LOGO)) {
			/* fcport->state = FC_LOGIN_NEEDED; */
			DEBUG(printk
			      ("scsi(%d): Timeout occurred with Logo, status flag (%x) with public device loop id (%x)\n",
			       (int) ha->instance, sts->status_flags, fcport->loop_id));
			/* Suspend port */
			fcport->login_retry = ha->port_down_retry_count;
			ha->device_flags |= RELOGIN_NEEDED;
		}
		break;
	case CS_DATA_UNDERRUN:
		resid = sts->residual_length;
		/* if RISC reports underrun and target does not report it
		 * then we must have a lost frame, so tell upper layer
		 * to retry it by reporting an error.
		 */
		if (!(sts->scsi_status & SS_RESIDUAL_UNDER)) {
			resid = (unsigned) cp->request_bufflen;
		}

		if ((unsigned) (cp->request_bufflen - resid) < cp->underflow) {
			host_status = DID_ERROR;
			DEBUG3(printk("scsi: Underflow detected - retrying command.\n"));
		} else {
			/* v2.19.5b2 Reset port down retry on success. */
			sp->port_down_retry_count = ha->port_down_retry_count;
			host_status = DID_OK;
			sp->fo_retry_cnt = 0;
		}
		break;

	default:
		DEBUG3(printk("scsi: Error detected 0x%x-0x%x.\n", sts->comp_status, sts->scsi_status));
		host_status = DID_ERROR;
		break;
	}

#if DEBUG_QLA2100_INTR
	if (host_status == DID_ERROR) 
		printk("status: host status (%s) scsi status %x\n", reason[host_status], scsi_status);
#endif

	LEAVE("qla2100_return_status");

	return ((scsi_status & STATUS_MASK) | (host_status << 16));
}


/**************************************************************************
*   qla2100_timer
*
* Description:
*   One second timer
*
* Context: Interrupt
***************************************************************************/
static void
qla2100_timer(struct scsi_qla_host * ha)
{

	srb_t *sp;
	int stop_timer, kick_off = 0;
	int cnt;
	int t;
	unsigned long flags = 0;

	fc_port_t *fcport;
	struct list_head *list, *temp;

	/* v2.19.02 spin_lock_irqsave(&io_request_lock, cpu_flags); */

	stop_timer = 0;

	for (t = 0, fcport = ha->fcport; fcport != NULL; fcport = fcport->next, t++) {
		if (PORT_DOWN_RETRY(fcport) != 0) {
			DEBUG(printk("qla%ld: fcport-%d - port retry count %d remainning\n",
				     ha->instance, t, PORT_DOWN_RETRY(fcport)));
			PORT_DOWN_RETRY(fcport)--;
		}
		/* Port Down Handler. */
		if (PORT_DOWN(fcport)) {
			PORT_DOWN(fcport)--;
			DEBUG(printk("qla%ld: Port down time in secs %d\n", ha->instance, PORT_DOWN(fcport)););
			if (PORT_DOWN(fcport) == 0) {
				ha->flags.port_restart_needed = TRUE;
				DEBUG(printk
				      ("qla%ld: Port Down complete - restarting commands in the queues\n",
				       ha->instance));
				stop_timer++;
			}
		}
	}

	/* Port Down Handler. */
	if (ha->queue_restart_timer > 0) {
		ha->queue_restart_timer--;
		DEBUG3(printk("qla%d: Port down in secs %d\n", ha->instance, ha->queue_restart_timer););
		/*
		 * When a port goes DOWN, we suspend the queue and wait 1 second
		 * (one timer tick) before trying to kick off the commands again.
		 * We will do this for "port_down_retry_count" times per
		 * command before giving up on the command altogether.
		 */
		if (!ha->queue_restart_timer) {
			ha->flags.port_restart_needed = TRUE;
			DEBUG(printk("qla%ld: Port Down complete - restarting commands in the queues\n", ha->instance));
			stop_timer++;
		}
	}
	/* Loop down handler. */
	if (ha->loop_down_timer > 0 && !(ha->dpc_flags & ABORT_ISP_ACTIVE) && ha->flags.online) {
		if (ha->loop_down_timer == LOOP_DOWN_TIME) {
			DEBUG(printk("qla%ld: Loop Down - aborting the queues before time expire\n", ha->instance));
			QLA2100_DPC_LOCK(ha);
			ha->flags.abort_queue_needed = TRUE;
			QLA2100_DPC_UNLOCK(ha);
		}

		ha->loop_down_timer--;
		DEBUG3(printk("qla%d: Loop Down - seconds remainning %d\n", ha->instance, ha->loop_down_timer););
		    /* if the loop has been down for 4 minutes, reinit adapter */
		    if (!ha->loop_down_timer) {
			DEBUG(printk
			      ("qla%ld: Loop down exceed 4 mins -restarting queues and abort ISP.\n", ha->instance););
			ha->flags.restart_queues_needed = TRUE;
			if (qla2100_reinit && !ha->flags.failover_enabled) {
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
			}
			stop_timer++;
		}
	}

	/*
	 * Retry Handler -- This handler will recycle queued requests until the
	 * temporary loop down condition terminates.
	 */
	if (!(ha->dpc_flags & ABORT_ISP_ACTIVE)) {
		cnt = 0;
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &ha->retry_queue) {
			sp = list_entry(list, srb_t, list);
			if (sp->wdg_time)
				sp->wdg_time--;
			if (sp->wdg_time == 0) {
				kick_off++;
				DEBUG4(printk("timer: CMD timeout %p,  pid %d\n", sp, sp->cmd->serial_number));
				cnt++;
				__del_from_retry_queue(ha, sp);
				sp->cmd->result = DID_BUS_BUSY << 16;
				sp->cmd->host_scribble = NULL;
				__add_to_done_queue(ha, sp);
			}
		}
		spin_unlock_irqrestore(&ha->list_lock, flags);
		if (cnt > 0) 
			DEBUG2(printk("qla%ld: timer - found %d requests\n", ha->instance, cnt););
	}

	if (!list_empty(&ha->done_queue))
		ha->flags.done_requests_needed = TRUE;

#if 0
	if (ha->dpc_flags & ISP_RESET_NEEDED) {
		ha->dpc_flags &= ~ISP_RESET_NEEDED;
		ha->dpc_flags |= ISP_RESET_ONCE;
	}
#endif
#if QLA2100_LIPTEST
	if ((ha->forceLip++) == (60 * 2) && qla2100_lip) {
		ha->dpc_flags |= ISP_ABORT_NEEDED;
	}
#endif

	/* Schedule the DPC routine if needed */
	if (((ha->dpc_flags & (ISP_ABORT_NEEDED | LOOP_RESYNC_NEEDED | COMMAND_WAIT_NEEDED)) ||
	     ha->flags.restart_queues_needed ||
	     ha->flags.port_restart_needed ||
	     ha->flags.done_requests_needed || (ha->device_flags & LOGIN_RETRY_NEEDED) ||
#if MPIO_SUPPORT
	     (ha->dpc_flags & FAILOVER_EVENT) || (ha->dpc_flags & FAILOVER_NEEDED) ||
#endif
	     (ha->dpc_flags & MAILBOX_NEEDED) || kick_off > 0 || ha->flags.abort_queue_needed) && ha->dpc_wait) {	/* v2.19.4 */
		DEBUG(qla2100_dump_buffer((uint8_t *) & qla2x00_dpc_sem, sizeof(qla2x00_dpc_sem)););
		up(ha->dpc_wait);
	}

	RESTART_TIMER(qla2100_timer, ha, WATCH_INTERVAL);
}

/*
 * qla2100_timeout_insert
 *      Function used to insert a command block onto the
 *      watchdog timer queue.
 *
 *      Note: Must insure that sc_time is not zero
 *            before calling qla2100_timeout_insert.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 */
static void
qla2100_timeout_insert(struct scsi_qla_host * ha, srb_t * sp)
{
	uint8_t timeoutcnt;

	ENTER("qla2100_timeout_insert");

	/* Compute number of time intervals */
	timeoutcnt = (uint8_t) (sp->cmd->timeout_per_command / HZ);
	if (timeoutcnt >= 10)	/* 10 or more */
		sp->wdg_time = timeoutcnt - 10;
	else if (timeoutcnt > 1)
		sp->wdg_time = timeoutcnt;
	else
		sp->wdg_time = 1;

	DEBUG5(printk("Watchdog (insert) - pid=%d, tmo=%d, start=0x%x\n", sp->cmd->serial_number, sp->wdg_time,
		sp->r_start));


	add_to_retry_queue(ha,sp);

	LEAVE("qla2100_timeout_insert");
}


/*
* qla2100_callback
*      Returns the completed SCSI command to LINUX.
*
* Input:
*	ha -- Host adapter structure
*	cmd -- SCSI mid-level command structure.
* Returns:
*      None
*/
static inline void
qla2100_callback(struct scsi_qla_host * ha, Scsi_Cmnd * cmd)
{
	srb_t *sp = (srb_t *) CMD_SP(cmd);

	ENTER("qla2100_callback");

	cmd->host_scribble = NULL;
	/*
	 * if command status is not DID_BUS_BUSY then go ahead
	 * and freed sp.
	 */
	if (sp != NULL && (cmd->result != (DID_BUS_BUSY << 16))) {
		/* Never free this resource since we can reuse it */
		if (sp->used == 0x81)
			qla2100_free_sp(cmd, sp);
		sp->flags &= ~SRB_BUSY;

	} else
		sp->flags |= SRB_BUSY;
	if (sp != NULL) {
		sp->flags &= ~SRB_SENT;
		DEBUG(sp->state = 12;);
		    /* v2.19.14 
		     * Perform internal retries, if needed.
		     */
#if 0
		if ((cmd->result >> 16) == DID_ERROR) {
			if (sp->retry_count > 0) {
				sp->retry_count--;
				DEBUG3(printk("qla2x00: RETRY - os retry %d, drv retry %d, port retry %d\n",
					      cmd->retries, sp->retry_count, sp->port_down_retry_count));
				cmd->result = (int) DID_BUS_BUSY << 16;
			} else {
				sp->retry_count = ha->retry_count;
				/* all resetted commands must return with RESET */
				if (cmd->flags & IS_RESETTING) {
					cmd->result = (int) DID_RESET << 16;
					DEBUG3(printk("qla2x00: RESET cmd %p\n", cmd));
				}
				DEBUG3(printk("qla2x00: OSerr = %p\n", cmd));
			}
		}
#endif
	}
	if ((cmd->result >> 16) == DID_OK) {
		/* device ok */
		ha->total_bytes += cmd->bufflen;
	} else if ((cmd->result >> 16) == DID_ERROR) {
		/* device error */
		ha->total_dev_errs++;
	}

	DEBUG3(printk("(%ld) ", cmd->serial_number));

	/* Call the mid-level driver interrupt handler */
	(*(cmd)->scsi_done) (cmd);
}

/*
* qla2100_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      1  = failure.
*
* This function sleeps
*/
static uint8_t
qla2100_mem_alloc(struct scsi_qla_host * ha)
{
	uint8_t status = 1;
	int retry = 10;

	DEBUG3(printk("qla2100_mem_alloc: entered.\n"));

	do {
		/*
		 * This will loop only once, just to provide quick exits for
		 * errors.
		 */
		ha->request_ring = pci_alloc_consistent(ha->pdev,
							((REQUEST_ENTRY_CNT + 1) * (sizeof(request_t))),
							&ha->request_dma);
		if (ha->request_ring == NULL) {
			/* error */
			printk(KERN_WARNING "scsi(%d): Memory Allocation failed - request_ring", (int) ha->host_no);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}
		ha->response_ring = pci_alloc_consistent(ha->pdev,
							 ((RESPONSE_ENTRY_CNT + 1) * (sizeof(response_t))),
							 &ha->response_dma);
		if (ha->response_ring == NULL) {
			/* error */
			printk(KERN_WARNING "scsi(%d): Memory Allocation failed - response_ring", (int) ha->host_no);
			qla2100_mem_free(ha);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		/* get consistent memory allocated for init control block */
		ha->init_cb = pci_alloc_consistent(ha->pdev, sizeof(init_cb_t), &ha->init_cb_dma);
		if (ha->init_cb == NULL) {
			/* error */
			printk(KERN_WARNING "scsi(%d): Memory Allocation failed - init_cb", (int) ha->host_no);
			qla2100_mem_free(ha);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}
		memset(ha->init_cb,0, sizeof(init_cb_t));

		/* Allocate ioctl related memory. */
		if (qla2x00_alloc_ioctl_mem(ha)) {
			/* error */
			printk(KERN_WARNING "scsi(%d): Memory Allocation failed - ioctl_mem", (int) ha->host_no);
			qla2100_mem_free(ha);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		/* Done all allocations without any error. */
		status = 0;

	} while (retry-- && status != 0);

	if (status) 
		printk(KERN_WARNING "qla2100_mem_alloc: **** FAILED ****\n");

	DEBUG3(printk("qla2100_mem_alloc: exiting.\n"));

	return status;
}

/*
* qla2100_mem_free
*      Frees all adapter allocated memory.
*
* Input:
*      ha = adapter block pointer.
*/
static void
qla2100_mem_free(struct scsi_qla_host * ha)
{
	uint32_t t;
	fc_lun_t *fclun, *fclun_next;
	fc_port_t *fcport, *fcport_next;

	DEBUG3(printk("qla2100_mem_free: entered.\n"));

	if (ha == NULL) {
		/* error */
		DEBUG2(printk("qla2100_mem_free: ERROR invalid ha pointer.\n"););
		return;
	}

	/* Free the target queues */
	for (t = 0; t < MAX_TARGETS; t++) {
		qla2x00_tgt_free(ha, t);
	}

	/* free ioctl memory */
	qla2x00_free_ioctl_mem(ha);

	/* 4.10 */
	/* free memory allocated for init_cb */
	if (ha->init_cb)
		pci_free_consistent(ha->pdev, sizeof(init_cb_t), ha->init_cb, ha->init_cb_dma);

	if (ha->request_ring)
		pci_free_consistent(ha->pdev,
				    ((REQUEST_ENTRY_CNT + 1) * (sizeof(request_t))), ha->request_ring, ha->request_dma);

	if (ha->response_ring)
		pci_free_consistent(ha->pdev,
				    ((RESPONSE_ENTRY_CNT + 1) * (sizeof(response_t))),
				    ha->response_ring, ha->response_dma);

	ha->init_cb = NULL;
	ha->request_ring = NULL;
	ha->request_dma = 0;
	ha->response_ring = NULL;
	ha->response_dma = 0;

	/* fc ports */
	for (fcport = ha->fcport; fcport != NULL; fcport = fcport_next) {

		fcport_next = fcport->next;

		/* fc luns */
		for (fclun = fcport->fclun; fclun != NULL; fclun = fclun_next) {
			fclun_next = fclun->next;
			kfree(fclun);
		}
		kfree(fcport);
	}

	DEBUG3(printk("qla2100_mem_free: exiting.\n"));
}

/*
*  qla2100_abort_lun_queue
*      Abort all commands on a device queues.
*
* Input:
*      ha = adapter block pointer.
*/
static void
qla2100_abort_lun_queue(struct scsi_qla_host * ha, uint32_t b, uint32_t t, uint32_t l, uint32_t stat)
{
	struct os_lun *q;

	ENTER("qla2100_abort_lun_queue");
	DEBUG5(printk("Abort queue single %2d:%2d:%2d\n", ha->host_no, t, l));

	q = (struct os_lun *) GET_LU_Q(ha, t, l);
	if (q != NULL) {
		struct list_head *list, *temp;
		unsigned long flags;
	
		/* abort all commands on LUN queue. */
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &q->cmd) {
			srb_t *sp;
	
			sp = list_entry(list, srb_t, list);
			__del_from_cmd_queue(q, sp);
			sp->cmd->result = stat;
			__add_to_done_queue(ha, sp);
		}
		spin_unlock_irqrestore(&ha->list_lock, flags);
	
	}
	LEAVE("qla2100_abort_lun_queue");
}

/****************************************************************************/
/*                QLogic ISP2100 Hardware Support Functions.                */
/****************************************************************************/

/*
* qla2100_initialize_adapter
*      Initialize board.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*
* This function sleeps
*/
uint8_t
qla2100_initialize_adapter(struct scsi_qla_host * ha)
{
	device_reg_t *reg;
	uint8_t status;
	uint8_t isp_init = 0;
	uint8_t restart_risc = 0;
	uint8_t retry;

	ENTER("qla2100_initialize_adapter");

	/* Clear adapter flags. */
	ha->forceLip = 0;
	ha->flags.online = FALSE;
	ha->flags.disable_host_adapter = FALSE;
	ha->flags.reset_active = FALSE;
	ha->flags.watchdog_enabled = FALSE;
	ha->loop_down_timer = LOOP_DOWN_TIME;
	ha->loop_state = LOOP_DOWN;
	ha->flags.start_timer = FALSE;
	ha->flags.done_requests_needed = FALSE;
	ha->device_flags = 0;
	ha->sns_retry_cnt = 0;
	ha->device_flags = 0;
	ha->dpc_flags = 0;
	ha->sns_retry_cnt = 0;
	ha->failback_delay = 0;
	ha->dpc_wait = &qla2x00_dpc_sem;
	/* 4.11 */
	ha->flags.managment_server_logged_in = 0;

	DEBUG(printk("Configure PCI space for adapter...\n"));

	if (!(status = qla2100_pci_config(ha))) {
		reg = ha->iobase;

		qla2100_reset_chip(ha); /* LONG delay */

		/* Initialize Fibre Channel database. */
		qla2100_init_fc_db(ha);

		/* Initialize target map database. */
		qla2100_init_tgt_map(ha);

		/* Get Flash Version */
		qla2x00_get_flash_version(ha);

		if (qla2100_verbose)
			printk("scsi(%d): Configure NVRAM parameters...\n", (int) ha->host_no);

		if (ha->device_id == QLA2100_DEVICE_ID)
			qla2100_nvram_config(ha);
		else
			qla2200_nvram_config(ha);
	
		ha->retry_count = ql2xretrycount;
#if USE_PORTNAME
		ha->flags.port_name_used = 1;
#else
		ha->flags.port_name_used = 0;
#endif

		if (qla2100_verbose)
			printk("scsi(%d): Verifying loaded RISC code...\n", (int) ha->host_no);

		qla2100_set_cache_line(ha);

		/* If the user specified a device configuration on
		   * the command line then use it as the configuration.
		   * Otherwise, we scan for all devices.
		 */
		if (ql2xdevconf) {
			ha->cmdline = ql2xdevconf;
			if (!ha->flags.failover_enabled)
				qla2100_get_properties(ha, ql2xdevconf);
		}

		retry = 10;
		/*
		   * Try an configure the loop.
		 */
		do {
			DEBUG(printk("qla2100_initialize_adapter: check if firmware needs to be loaded\n"));
			/* If firmware needs to be loaded */
			if (qla2100_isp_firmware(ha)) {
				if (qla2100_verbose)
					printk("scsi(%d): Verifying chip...\n", (int) ha->host_no);

				if (!(status = qla2100_chip_diag(ha)))
					status = qla2100_setup_chip(ha);

				if (!status)
					DEBUG(printk
					      ("scsi(%d): Chip verified and RISC loaded...\n", (int) ha->host_no));
			}
			if (!status && !(status = qla2100_init_rings(ha))) {
				/* dg - 7/3/1999
				 *
				 * Wait for a successful LIP up to a maximum of (in seconds):
				 * RISC login timeout value, RISC retry count value, and port
				 * down retry value OR a minimum of 4 seconds OR If no cable,
				 * only 5 seconds.
				 */
				DEBUG(printk("qla2100_init_rings OK, call qla2100_fw_ready...\n"));
				if (!qla2100_fw_ready(ha)) {

					ha->dpc_flags &= ~(RESET_MARKER_NEEDED | COMMAND_WAIT_NEEDED);

					/* Go setup flash database devices with proper Loop ID's. */
					do {
						ha->dpc_flags &= ~LOOP_RESYNC_NEEDED;
						status = qla2100_configure_loop(ha);
					} while (!ha->loop_down_timer && (ha->dpc_flags & LOOP_RESYNC_NEEDED));
				}

				if (ha->flags.update_config_needed) {
					ha->init_cb->additional_firmware_options.connection_options =
					    ha->operating_mode;
					restart_risc = 1;
				}

				if (ha->mem_err) {
					restart_risc = 1;
				}
				isp_init = 1;

			}
		} while (restart_risc && retry--);
		if (isp_init) {
			ha->dpc_flags &= ~RESET_MARKER_NEEDED;
			qla2100_marker(ha, 0, 0, 0, MK_SYNC_ALL);

			ha->flags.online = TRUE;

			/* Enable target response to SCSI bus. */
			if (ha->flags.enable_target_mode)
				qla2100_enable_lun(ha);
		}

	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_initialize_adapter: **** FAILED ****\n");
#endif
	LEAVE("qla2100_initialize_adapter");
	return status;
}

/*
 * ISP Firmware Test
 *      Checks if present version of RISC firmware is older than
 *      driver firmware.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = firmware does not need to be loaded.
 */
static uint8_t
qla2100_isp_firmware(struct scsi_qla_host * ha)
{
	uint8_t status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2x00_isp_firmware");

	if (ha->flags.disable_risc_code_load) {
		/* Verify checksum of loaded RISC code. */
		mb[0] = MBC_VERIFY_CHECKSUM;
		mb[1] = *QLBoardTbl_fc[ha->devnum].fwstart;
		if (!(status = qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]))) {
			/* Start firmware execution. */
			mb[0] = MBC_EXECUTE_FIRMWARE;
			mb[1] = *QLBoardTbl_fc[ha->devnum].fwstart;
			qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
		}
	} else
		status = 1;

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_isp_firmware: **** Load RISC code ****\n");
#endif
	LEAVE("qla2x00_isp_firmware");
	return status;
}

/*
 * (08/05/99)
 *
 * PCI configuration
 *      Setup device PCI configuration registers.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 */
static uint8_t
qla2100_pci_config(struct scsi_qla_host * ha)
{
	uint8_t status = 1;
	uint32_t command;
#if MEMORY_MAPPED_IO
	uint32_t page_offset, base;
	uint32_t mmapbase;
#endif
	uint16_t buf_wd;

	ENTER("qla2100_pci_config");
/* 4.12 */
	/* turn on PCI master; for system BIOSes that don't turn
	   it on by default */
	pci_set_master(ha->pdev);
	pci_read_config_word(ha->pdev, PCI_REVISION_ID, &buf_wd);
	ha->revision = buf_wd;
	if (!ha->iobase) {
		/* Get command register. */
		if (pci_read_config_word(ha->pdev, PCI_COMMAND, &buf_wd) == PCIBIOS_SUCCESSFUL) {
			command = buf_wd;

			/*
			 * Set Bus Master Enable (bit-2), Memory Address Space Enable and
			 * reset any error bits.
			 */
			buf_wd &= ~0x7;
#if MEMORY_MAPPED_IO
			DEBUG(printk("qla2x00: I/O SPACE and MEMORY MAPPED IO is enabled.\n"));
			buf_wd |= PCI_COMMAND_MASTER| PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#else
			DEBUG(printk("qla2x00: I/O SPACE Enabled and MEMORY MAPPED IO is disabled.\n"));
			buf_wd |= PCI_COMMAND_MASTER | PCI_COMMAND_IO;
#endif
			if (pci_write_config_word(ha->pdev, PCI_COMMAND, buf_wd)) 
				printk(KERN_WARNING "qla2x00: Could not write config word.\n");
			
			/* Get expansion ROM address. */
			if (pci_read_config_word(ha->pdev, PCI_ROM_ADDRESS, &buf_wd) == PCIBIOS_SUCCESSFUL) {
				/* Reset expansion ROM address decode enable. */
				buf_wd &= ~PCI_ROM_ADDRESS_ENABLE;
				if (pci_write_config_word(ha->pdev, PCI_ROM_ADDRESS, buf_wd) == PCIBIOS_SUCCESSFUL) {
#if MEMORY_MAPPED_IO
					/* Get memory mapped I/O address. */
					pci_config_dword(ha->pdev, OFFSET(cfgp->mem_base_addr), &mmapbase);
					mmapbase &= PCI_BASE_ADDRESS_MEM_MASK;
					#warning ewwwwwwwwwwwwwwwwwww

					/* Find proper memory chunk for memory map I/O reg. */
					base = mmapbase & PAGE_MASK;
					page_offset = mmapbase - base;
					/* Get virtual address for I/O registers. */
					ha->mmpbase = ioremap(base, page_offset + 256);
					if (ha->mmpbase) {
						ha->mmpbase += page_offset;
						/* ha->iobase = ha->mmpbase; */
						status = 0;
					}
#else				/* MEMORY_MAPPED_IO */
					status = 0;
#endif				/* MEMORY_MAPPED_IO */
				}
			}
		}
	} else
		status = 0;

	LEAVE("qla2100_pci_config");
	return status;
}

/*
* qla2100_set_cache_line
*      Sets PCI cache line parameter.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_set_cache_line(struct scsi_qla_host * ha)
{
	int status = 0;
	uint8_t buf;
	unsigned char cache_size;

	ENTER("qla2100_set_cache_line");

	buf = 1; /* This must be broken */

	/* Set the cache line. */
	if (!ha->flags.set_cache_line_size_1)  {
		LEAVE("qla2100_set_cache_line");
		return 0;
	}

	/* taken from drivers/net/acenic.c */
        pci_read_config_byte(ha->pdev, PCI_CACHE_LINE_SIZE, &cache_size);
        cache_size <<= 2;
        if (cache_size != SMP_CACHE_BYTES) {
                printk(KERN_INFO "  PCI cache line size set incorrectly "
                       "(%i bytes) by BIOS/FW, ", cache_size);
                if (cache_size > SMP_CACHE_BYTES)
                        printk("expecting %i\n", SMP_CACHE_BYTES);
                else {
                        printk("correcting to %i\n", SMP_CACHE_BYTES);
                        pci_write_config_byte(ha->pdev, PCI_CACHE_LINE_SIZE,
                                              SMP_CACHE_BYTES >> 2);
                }
        }
                                                                                                                                                                                                                                             

	LEAVE("qla2100_set_cache_line");
	return status;
}

/*
* Chip diagnostics
*      Test chip for proper operation.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*
* Notes:
*      This function performs excessive busy waits for up to 90 seconds
*/
static uint8_t
qla2100_chip_diag(struct scsi_qla_host * ha)
{
	device_reg_t *reg = ha->iobase;
	uint16_t buf_wd;
	uint16_t data;
	uint32_t cnt;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint8_t status = 0;

	ENTER("qla2100_chip_diag");
	DEBUG3(printk("qla2100_chip_diag: testing device at %x \n", &reg->flash_address));

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, ISP_RESET);
	data = qla2100_debounce_register(&reg->ctrl_status);
	for (cnt = 6000000; cnt && (data & ISP_RESET); cnt--) {
		udelay(5);
		#warning looong delay
		data = RD_REG_WORD(&reg->ctrl_status);
		barrier();
	}

	if (cnt) {
		DEBUG3(printk("qla2100_chip_diag: reset register cleared by chip reset\n"));
		if ((ha->device_id==QLA2300_DEVICE_ID) || (ha->device_id==QLA2312_DEVICE_ID)) {
			pci_read_config_word(ha->pdev, PCI_COMMAND, &buf_wd);
			#warning this is wrong; it enables MEMORY space! 
			buf_wd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
			data = RD_REG_WORD(&reg->mailbox6);
			if ((ha->device_id == QLA2312_DEVICE_ID) || ((data & 0xff) == FPM_2310))
				/* Enable Memory Write and Invalidate. */
				buf_wd |= PCI_COMMAND_INVALIDATE;
			else
				buf_wd &= ~PCI_COMMAND_INVALIDATE;
			pci_write_config_word(ha->pdev, PCI_COMMAND, buf_wd);
		}
		/* Reset RISC processor. */
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
		/* Workaround for Qla2312 PCI parity error */
		if (ha->device_id == QLA2312_DEVICE_ID) {
			udelay(10);
		} else {
			data = qla2100_debounce_register(&reg->mailbox0);
			for (cnt = 6000000; cnt && (data == MBS_BUSY); cnt--) {
				udelay(5);
				#warning long delay 
				data = RD_REG_WORD(&reg->mailbox0);
				barrier();
			}
		}

		if (cnt) {
			/* Check product ID of chip */
			DEBUG3(printk("{{{qla2100_chip_diag: Checking product ID of chip}}}\n"););
			if (RD_REG_WORD(&reg->mailbox1) != PROD_ID_1 ||
			    (RD_REG_WORD(&reg->mailbox2) != PROD_ID_2 &&
			     RD_REG_WORD(&reg->mailbox2) != PROD_ID_2a) ||
			    RD_REG_WORD(&reg->mailbox3) != PROD_ID_3 ||
			    qla2100_debounce_register(&reg->mailbox4) != PROD_ID_4) {
				printk(KERN_WARNING "qla2x00: Wrong product ID = 0x%x,0x%x,0x%x,0x%x\n",
				       RD_REG_WORD(&reg->mailbox1),
				       RD_REG_WORD(&reg->mailbox2),
				       RD_REG_WORD(&reg->mailbox3), RD_REG_WORD(&reg->mailbox4));
				status = 1;
			} else {
				/* Now determine if we have a 2200A board */
				if ((ha->device_id == QLA2200_DEVICE_ID ||
				     ha->device_id == QLA2200A_DEVICE_ID) &&
				    RD_REG_WORD(&reg->mailbox7) == QLA2200A_RISC_ROM_VER) {
					ha->device_id = QLA2200A_DEVICE_ID;
					DEBUG3(printk("qla2100_chip_diag: Found QLA2200A chip.\n"););
				}
				DEBUG(printk("qla2100_chip_diag: Checking mailboxes.\n"););
				DEBUG3(printk("qla2100_chip_diag: Checking mailboxes.\n"););
				/* Wrap Incoming Mailboxes Test. */
				mb[0] = MBC_MAILBOX_REGISTER_TEST;
				mb[1] = 0xAAAA;
				mb[2] = 0x5555;
				mb[3] = 0xAA55;
				mb[4] = 0x55AA;
				mb[5] = 0xA5A5;
				mb[6] = 0x5A5A;
				mb[7] = 0x2525;
				if (!(status = qla2100_mailbox_command(ha,
								       BIT_7 | BIT_6 | BIT_5 | BIT_4 | BIT_3 | BIT_2 |
								       BIT_1 | BIT_0, &mb[0]))) {
					if (mb[1] != 0xAAAA || mb[2] != 0x5555 || mb[3] != 0xAA55 || mb[4] != 0x55AA)
						status = 1;
					if (mb[5] != 0xA5A5 || mb[6] != 0x5A5A || mb[7] != 0x2525)
						status = 1;
					if (status) {
						printk(KERN_WARNING "qla2x00: Failed mailbox check\n");
						DEBUG(printk("qla2100_chip_diag: *** Failed mailbox register test ***\n"););
					}
				} else {
					printk(KERN_WARNING "qla2100_chip_diag: failed mailbox send register test\n");
				}
			}
		} else
			status = 1;
	} else
		status = 1;

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_chip_diag: **** FAILED ****\n");
#endif
	LEAVE("qla2100_chip_diag");
	return status;
}

/*
* Setup chip
*      Load and start RISC firmware.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_setup_chip(struct scsi_qla_host * ha)
{
	uint16_t cnt;
	uint16_t risc_address;
	uint16_t *risc_code_address;
	long risc_code_size;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint8_t status = 0;
	int num;
#ifdef WORD_FW_LOAD
	uint16_t *ql21_risc_code_addr01;
	uint16_t ql21_risc_code_length01;
	uint8_t dump_status;
#endif

	ENTER("qla2100_setup_chip");

	/* Load RISC code. */
	risc_address = *QLBoardTbl_fc[ha->devnum].fwstart;
	risc_code_address = QLBoardTbl_fc[ha->devnum].fwcode;
	risc_code_size = (long) (*QLBoardTbl_fc[ha->devnum].fwlen & 0xffff);

	DEBUG(printk("qla2100_setup_chip:  Loading RISC code size =(0x%lx).\n", risc_code_size));
	DEBUG(ql2x_debug_print = 0;);
	num = 0;
	while (risc_code_size > 0 && !status) {
		/* for 2200A set transfer size to 128 bytes */
		if (ha->device_id == QLA2200A_DEVICE_ID)
			cnt = 128 >> 1;
		else
			cnt = REQUEST_ENTRY_SIZE * REQUEST_ENTRY_CNT >> 1;
		if (cnt > risc_code_size)
			cnt = risc_code_size;

		DEBUG5(printk("qla2100_setup_chip:loading risc segment@ addr 0x%x:bytes %d:offset 0x%x.\n", risc_code_address, cnt, risc_address));

		memcpy((void*) ha->request_ring,(void*) risc_code_address, (cnt << 1));

		wmb();
#if 1
		mb[0] = MBC_LOAD_RAM_A64;
		mb[1] = risc_address;
		mb[3] = (uint16_t) (ha->request_dma & 0xffff);
		mb[2] = (uint16_t) ((ha->request_dma >> 16) & 0xffff);
		mb[4] = cnt;
		mb[7] = (uint16_t) ( ((ha->request_dma >> 16)>>16) & 0xffff);
		mb[6] = (uint16_t) ((((ha->request_dma >> 16)>>16)>>16) & 0xffff);
		status = qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_4 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0]);
#else
		mb[0] = MBC_LOAD_RAM;
		mb[1] = risc_address;
		mb[3] = (uint16_t) (ha->request_dma & 0xffff);
		mb[2] = (uint16_t) ((ha->request_dma >> 16) & 0xffff);
		mb[4] = cnt;
		#warning 64 bit PCI
		status = qla2100_mailbox_command(ha, BIT_4 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0]);
#endif
		if (status) {
			qla2100_dump_regs(ha->host);
			printk(KERN_WARNING "Failed to load segment %d of FW\n", num);
			break;
		}
		risc_address += cnt;
		risc_code_size -= cnt;
		risc_code_address += cnt;
		num++;
	}
#ifdef WORD_FW_LOAD
	{
		int i;
		uint8_t temp;

		temp = ql2x_debug_print;
		if (ql2x_debug_print)
			ql2x_debug_print = 0;
		risc_address = *QLBoardTbl_fc[ha->devnum].fwstart;
		ql21_risc_code_addr01 = QLBoardTbl_fc[ha->devnum].fwcode;
		ql21_risc_code_length01 = (long) (*QLBoardTbl_fc[ha->devnum].fwlen & 0xffff);

		for (i = 0; i < ql21_risc_code_length01; i++) {
			mb[0] = MBC_WRITE_RAM_WORD;
			mb[1] = risc_address + i;
			mb[2] = *(ql21_risc_code_addr01 + i);

			dump_status = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);
			if (dump_status) {
				printk(KERN_WARNING "qla2x00 : firmware load failure\n");
				break;
			}

			mb[0] = MBC_READ_RAM_WORD;
			mb[1] = risc_address + i;
			mb[2] = 0;

			dump_status = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);
			if (dump_status) {
				printk(KERN_WARNING "qla2x00: RISC FW Read Failure\n");
				break;
			}
			if (mb[2] != *(ql21_risc_code_addr01 + i))
				printk(KERN_WARNING "qla2x00: RISC FW Compare ERROR @ (0x%p)\n",
				       (void *) (ql21_risc_code_addr01 + i));
		}
		ql2x_debug_print = temp;
		printk("qla2x00: RISC FW download confirmed... \n");
	}
#endif
	/* Verify checksum of loaded RISC code. */
	if (!status) {
		DEBUG(printk("qla2100_setup_chip: Verifying checksum of loaded RISC code.\n"););
		mb[0] = MBC_VERIFY_CHECKSUM;
		mb[1] = *QLBoardTbl_fc[ha->devnum].fwstart;
		if (!(status = qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]))) {
			/* Start firmware execution. */
			DEBUG(printk("qla2100_setup_chip: start firmware running.\n"););
			mb[0] = MBC_EXECUTE_FIRMWARE;
			mb[1] = *QLBoardTbl_fc[ha->devnum].fwstart;
			status = qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
		} else
			DEBUG2(printk("qla2100_setup_chip: Failed checksum.\n"););
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_setup_chip: **** FAILED ****\n");
#endif
	LEAVE("qla2100_setup_chip");
	return status;
}

/*
* qla2100_init_rings
*      Initializes firmware.
*
*      Beginning of request ring has initialization control block
*      already built by nvram config routine.
*
* Input:
*      ha                = adapter block pointer.
*      ha->request_ring  = request ring virtual address
*      ha->response_ring = response ring virtual address
*      ha->request_dma   = request ring physical address
*      ha->response_dma  = response ring physical address
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_init_rings(struct scsi_qla_host * ha)
{
	uint8_t status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int cnt;
#ifdef ISP2300
	device_reg_t *reg = ha->iobase;
#endif

	ENTER("qla2100_init_rings");
	/* Clear outstanding commands array. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS; cnt++)
		ha->outstanding_cmds[cnt] = 0;

	/* Clear RSCN queue. */
	ha->rscn_in_ptr = 0;
	ha->rscn_out_ptr = 0;

	/* Initialize firmware. */
	ha->request_ring_ptr = ha->request_ring;
	ha->req_ring_index = 0;
	ha->req_q_cnt = REQUEST_ENTRY_CNT;
	ha->response_ring_ptr = ha->response_ring;
	ha->rsp_ring_index = 0;
	mb[0] = MBC_INITIALIZE_FIRMWARE;
	mb[3] = LSW(ha->init_cb_dma);
	mb[2] = MSW(ha->init_cb_dma);
	mb[4] = 0;
	mb[5] = 0;
	mb[7] = QL21_64BITS_3RDWD(ha->init_cb_dma);
	mb[6] = QL21_64BITS_4THWD(ha->init_cb_dma);
#ifdef ISP2300
	WRT_REG_WORD(&reg->req_q_in, 0);
	WRT_REG_WORD(&reg->rsp_q_out, 0);
#endif
	DEBUG(printk("qla2100_init_rings: Issue MBC_INIT_FIRMWARE op\n"));
	status = qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_5 | BIT_4 | BIT_3 | BIT_2 | BIT_0, &mb[0]);

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_init_rings: **** FAILED ****\n");
#endif
	LEAVE("qla2100_init_rings");
	return status;
}

/*
* qla2100_fw_ready
*      Waits for firmware ready.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_fw_ready(struct scsi_qla_host * ha)
{
	uint8_t status = 0;
	uint32_t cnt, cnt1;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint16_t timeout;

	ENTER("qla2100_fw_ready");

	timeout = (ha->retry_count * ha->login_timeout) + 5;
	cnt1 = 0x350;		/* 25 secs */
	/* Wait for ISP to finish LIP */
	if (!qla2100_quiet)
		printk(KERN_INFO "scsi(%d): Waiting for LIP to complete...\n", (int) ha->host->host_no);
	if (timeout)
		cnt = 36 * timeout;
	else
		cnt = 0x700;
	for (; cnt; cnt--) {
		mb[0] = MBC_GET_FIRMWARE_STATE;
		if (!(status = qla2100_mailbox_command(ha, BIT_0, &mb[0]))) {
			if (ha->loop_down_timer || mb[1] != FSTATE_READY) {
				status = 1;
				/* Exit if no cable connected after 10 seconds. */
				if (!cnt1--)
					if (mb[1] == FSTATE_CONFIG_WAIT || mb[1] == FSTATE_LOSS_OF_SYNC) {

						break;
					}
			} else {
				DEBUG(printk("qla2100_fw_ready: F/W Ready - OK \n"));
				status = 0;	/* dg 09/15/99 */
				break;
			}
		}

		if (ha->flags.online) {
			status = 0;	/* dg 09/15/99 */
			break;
		}

		/* Delay for a while */

		udelay(10);
		DEBUG3(printk("qla2100_fw_ready: mailbox_out[1] = %x \n",mb[1]));
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_fw_ready: **** FAILED ****\n");
#endif
	LEAVE("qla2100_fw_ready");
	return status;
}

/*
*  qla2100_configure_hba
*      Setup adapter context.
*
* Input:
*      ha = adapter state pointer.
*
* Returns:
*      0 = success
*
* Context:
*      Kernel context.
*/
static uint8_t
qla2100_configure_hba(struct scsi_qla_host * ha)
{
	uint8_t rval;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint8_t connect_type[22];

	ENTER("qla2100_configure_hba");

	/* Get host addresses. */
	mb[0] = MBC_GET_ADAPTER_LOOP_ID;
	rval = qla2100_mailbox_command(ha, BIT_0, &mb[0]);
	if (!rval) {

		if (mb[6] == 4)
			return 1;

		ha->loop_id = mb[1];
	
                if (ha->device_id == QLA2100_DEVICE_ID) 
			mb[6] = 0;

		/* Get loop topology. */
		ha->min_external_loopid = SNS_FIRST_LOOP_ID;
		ha->operating_mode = LOOP;
		switch (mb[6]) {
		case 0:
			ha->current_topology = ISP_CFG_NL;
			strcpy((char *) &connect_type[0], "(Loop)");
			break;
		case 1:
			ha->current_topology = ISP_CFG_FL;
			strcpy((char *) &connect_type[0], "(FL_Port)");
			break;
		case 2:
			ha->operating_mode = P2P;
			ha->current_topology = ISP_CFG_N;
#if LOOP_ID_FROM_ONE
			ha->min_external_loopid = 1;	/* v2.19.5b3 */
#endif
			strcpy((char *) &connect_type[0], "(N_Port-to-N_Port)");
			break;
		case 3:
			ha->operating_mode = P2P;
			ha->current_topology = ISP_CFG_F;
#if LOOP_ID_FROM_ONE
			ha->min_external_loopid = 1;	/* v2.19.5b3 */
#endif
			strcpy((char *) &connect_type[0], "(F_Port)");
			break;
		default:
			ha->current_topology = ISP_CFG_NL;
			strcpy((char *) &connect_type[0], "(Loop)");
			break;
		}

		/* Save Host port and loop ID. */
		/* Reverse byte order - TT */
		/*
		   ha->port_id[2] = LSB(mb[2]);
		   ha->port_id[1] = MSB(mb[2]);
		   ha->port_id[0] = LSB(mb[3]);
		 */

		ha->d_id.b.domain = (uint8_t) mb[3];
		ha->d_id.b.area = (uint8_t) (mb[2] >> 8);
		ha->d_id.b.al_pa = (uint8_t) mb[2];

		if (!qla2100_quiet)
			printk(KERN_INFO "scsi%d: Topology - %s, Host Loop address  0x%x\n", (int) ha->host_no,
			       connect_type, ha->loop_id);
	} else
		printk(KERN_WARNING "qla2100_configure_hba: Get host loop ID  failed\n");

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (rval != 0)
		printk("qla2100_configure_hba: **** FAILED ****\n");
#endif
	LEAVE("qla2100_configure_hba");
	return rval;
}


/*
* Mailbox remote Command
*      Issue mailbox command and waits for completion.
*/
static uint8_t
qla2x00_remote_mailbox_command(struct scsi_qla_host * ha)
{
	uint8_t status;

	status = qla2100_mailbox_command(ha, ha->mc.out_mb, &ha->mc.mb[0]);
	return status;
}

/*
* Mailbox Command
*      Issue mailbox command and waits for completion.
*
* Input:
*      ha = adapter block pointer.
*      mr = mailbox registers to load.
*      mb = data pointer for mailbox registers.
*
* Output:
*      mb[MAILBOX_REGISTER_COUNT] = returned mailbox data.
*
* Returns:
*      0 = success
*      1 = failed    (mbox status != 0x4000)
*
* Note:
*      This function busy-waits for up to 30 seconds and will release
*      the io_request_lock in doubtful circumstances.
*/
uint8_t
qla2100_mailbox_command(struct scsi_qla_host * ha, uint32_t mr, uint16_t * mb)
{
	device_reg_t *reg = ha->iobase;
	uint8_t status = 0;
	uint8_t cmd;
	uint32_t cnt;
	uint16_t *optr, *iptr;
	uint16_t data, index;
#ifdef DPC_LOCK
	unsigned long cpu_flags = 0;
#endif

	ENTER("qla2100_mailbox_command");

	/* Acquire interrupt specific lock */
	QLA2100_INTR_LOCK(ha);

	DRIVER_LOCK(); 

	ha->flags.mbox_busy = TRUE;

	DEBUG3(printk("scsi%d  qla2100_mailbox_command: mbox_out[0] = %x\n", ha->host_no, *mb));
	DEBUG5(qla2100_dump_buffer((uint8_t *) mb, 8));
	DEBUG5(printk("\n"));
	DEBUG5(printk("qla2100_mailbox_command: Load MB registers = %x", mr));
	DEBUG5(qla2100_dump_buffer((uint8_t *) mb, 16));
	DEBUG5(printk("\n"));
	DEBUG5(printk("qla2100_mailbox_command: I/O address = %x \n",optr));

	optr = (uint16_t *) & reg->mailbox0;

	iptr = mb;
	cmd = mb[0];
	for (cnt = 0; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
#if defined(ISP2200)
		if (cnt == 8)
			optr = (uint16_t *) & reg->mailbox8;
#endif

		if (mr & BIT_0) {
			WRT_REG_WORD(optr, (*iptr));
		}
		mr >>= 1;
		optr++;
		iptr++;
	}

	DEBUG5(qla2100_dump_regs(ha->host));

	/* Issue set host interrupt command. */
	ha->flags.mbox_int = FALSE;
	WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);

	/* Wait for 30 seconds for command to finish.  */
	cnt = 0x3000000;	/* 30 secs */
	#warning long delay
	#warning sync function
	for (; cnt > 0 && !ha->flags.mbox_int; cnt--) {
		/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
		data = RD_REG_WORD(&reg->istatus);
		if ((data & RISC_INT))
			qla2100_isr(ha, data);
#else
		if (ha->device_id == QLA2312_DEVICE_ID) {
			/* Workaround for 2312 PCI parity error. */
			data = RD_REG_WORD(&reg->istatus);
			if ((data & RISC_INT)) {
				data = RD_REG_WORD(&reg->host_status_lo);
				qla2100_isr(ha, data);
			}
		} else {
			data = RD_REG_WORD(&reg->host_status_lo);
			if ((data & HOST_STATUS_INT))
				qla2100_isr(ha, data);
		}
#endif
		udelay(10);
		#warning long delay with io_request_lock helt
	}

	/* Check for mailbox command timeout. */
	if (!cnt) {
		DEBUG2(printk("qla2100_mailbox_command: **** Command Timeout, mailbox0 = %x ****\n", mb[0]));
		DEBUG2(qla2100_dump_regs(ha->host));

		QLA2100_DPC_LOCK(ha);
		ha->dpc_flags |= ISP_ABORT_NEEDED;
		QLA2100_DPC_UNLOCK(ha);

		qla2100_stats.mboxtout++;
		status = 1;
	} else if (ha->mailbox_out[0] != MBS_CMD_CMP) {
		qla2100_stats.mboxerr++;
		status = 1;
	}

	/* Load return mailbox registers. */
	optr = mb;
	iptr = (uint16_t *) & ha->mailbox_out[0];
	mr = MAILBOX_REGISTER_COUNT;
	while (mr--)
		*optr++ = *iptr++;

	/* Go check for any response interrupts pending. */
	ha->flags.mbox_busy = FALSE;
	if (ha->flags.online) {
#if defined(ISP2100) || defined(ISP2200)
		index = qla2100_debounce_register(&reg->mailbox5);
#else
		index = qla2100_debounce_register(&reg->rsp_q_in);
#endif
		if (ha->rsp_ring_index != index)
			qla2x00_response_pkt(ha, index);
	}

	/* Release interrupt specific lock */
	QLA2100_INTR_UNLOCK(ha);
	DRIVER_UNLOCK(); 
	if (ha->dpc_flags & ISP_ABORT_NEEDED) {
		ha->dpc_flags &= ~ISP_ABORT_NEEDED;
		if (!(ha->dpc_flags & ABORT_ISP_ACTIVE)) {
			ha->dpc_flags |= ABORT_ISP_ACTIVE;
			QLA2100_DPC_LOCK(ha);
			qla2100_abort_isp(ha); /* LONG DELAY */
			QLA2100_DPC_UNLOCK(ha);
			ha->dpc_flags &= ~ABORT_ISP_ACTIVE;
		}
		up(ha->dpc_wait);
	}

	if (!list_empty(&ha->done_queue))
		qla2100_done(ha);

	if (status) 
		DEBUG2(printk("qla2100_mailbox_command: FAILED cmd=0x%x, mb[0]=0x%x, mb[1]=0x%x, mb[2]=0x%x\n", cmd, mb[0],mb[1], mb[2]));
	
	LEAVE("qla2100_mailbox_command");
	return status;
}


/*
* qla2100_poll
*      Polls ISP for interrupts.
*
* Input:
*      ha = adapter block pointer.
*
* Assumes to be called with the io_request_lock helt.
*/
void
qla2100_poll(struct scsi_qla_host * ha)
{
	device_reg_t *reg = ha->iobase;
	uint16_t data;

	ENTER("qla2100_poll");

	/* Acquire interrupt specific lock */
	QLA2100_INTR_LOCK(ha);

	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&reg->istatus);
	if (data & RISC_INT)
		qla2100_isr(ha, data);
#else
	if (ha->device_id == QLA2312_DEVICE_ID) {
		data = RD_REG_WORD(&reg->istatus);
		if (data & RISC_INT) {
			data = RD_REG_WORD(&reg->host_status_lo);
			qla2100_isr(ha, data);
		}

	} else {
		data = RD_REG_WORD(&reg->host_status_lo);
		if (data & HOST_STATUS_INT)
			qla2100_isr(ha, data);
	}
#endif

	/* Release interrupt specific lock */
	QLA2100_INTR_UNLOCK(ha);

	if (!list_empty(&ha->done_queue))
		qla2100_done(ha);

	LEAVE("qla2100_poll");
}

/*
*  qla2100_restart_isp
*      restarts the ISP after a reset
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*
* This function can busy wait upto 90 seconds
* This function also sleeps
*/
int
qla2x00_restart_isp(struct scsi_qla_host * ha)
{
	uint8_t status = 0;

	/* If firmware needs to be loaded */
	if (qla2100_isp_firmware(ha)) {
		ha->flags.online = FALSE;
		if (!(status = qla2100_chip_diag(ha)))
			status = qla2100_setup_chip(ha);
	}
	if (!status && !(status = qla2100_init_rings(ha))) {
		ha->dpc_flags &= ~(RESET_MARKER_NEEDED | COMMAND_WAIT_NEEDED);
		if (!(status = qla2100_fw_ready(ha))) {
			DEBUG(printk("restart_isp: Start configure loop ,status = %d\n", status););
			ha->flags.online = TRUE;
			do {
				ha->dpc_flags &= ~LOOP_RESYNC_NEEDED;
				qla2100_configure_loop(ha);
			} while (!ha->loop_down_timer && (ha->dpc_flags & LOOP_RESYNC_NEEDED));
		}
		DEBUG(printk("restart_isp: Configure loop done, status = 0x%x\n", status););
	}
	return status;
}

/*
*  qla2100_abort_isp
*      Resets ISP and aborts all outstanding commands.
*
* Input:
*      ha           = adapter block pointer.
*
* Returns:
*      0 = success
* This function can busy wait upto 90 seconds
* This function sleeps
*/
static uint8_t
qla2100_abort_isp(struct scsi_qla_host * ha)
{
	uint16_t cnt;
	srb_t *sp;
	struct os_lun *q;
	uint8_t status = 0;

	ENTER("qla2100_abort_isp");

	DRIVER_LOCK();
	/* dg 9/23
	       if( !ha->flags.abort_isp_active && ha->flags.online ) {
	       ha->flags.abort_isp_active = TRUE;
	       ha->total_isp_aborts++;
	 */
	if (ha->flags.online) {
		ha->flags.online = FALSE;
		ha->dpc_flags &= ~COMMAND_WAIT_NEEDED;
		ha->dpc_flags &= ~COMMAND_WAIT_ACTIVE;
		qla2100_stats.ispAbort++;
		ha->sns_retry_cnt = 0;
		printk(KERN_INFO "qla2x00: Performing ISP error recovery - ha= %p.\n", ha);
		qla2100_reset_chip(ha); /* LONG delay */

		if (ha->loop_state != LOOP_DOWN) {
			ha->loop_state = LOOP_DOWN;
			ha->loop_down_timer = LOOP_DOWN_TIME;
		}
#ifdef FC_IP_SUPPORT
		/* Return all IP send packets */
		for (cnt = 0; cnt < MAX_SEND_PACKETS; cnt++) {
			if (ha->apActiveIpQueue[cnt] != NULL) {
				(*ha->pSendCompletionRoutine) (ha->apActiveIpQueue[cnt]);

				ha->apActiveIpQueue[cnt] = NULL;
			}
		}
#endif

		/* Requeue all commands in outstanding command list. */
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
			sp = ha->outstanding_cmds[cnt];
			if (sp) {
				ha->outstanding_cmds[cnt] = 0;
				q = sp->lun_queue;
				q->q_outcnt = 0;
				q->q_flag &= ~QLA2100_QBUSY;
				sp->flags = 0;
				/* we need to send the command back to OS */
				sp->cmd->result = DID_BUS_BUSY << 16;
				sp->cmd->host_scribble = NULL;
				add_to_done_queue(ha, sp);
			}
		}
		/* fixme(dg): Retrun the ones in the retry queue as well */

		if (ha->device_id==QLA2100_DEVICE_ID)
			qla2100_nvram_config(ha);
		else
			qla2200_nvram_config(ha);

		if (!qla2x00_restart_isp(ha)) {
			ha->dpc_flags &= ~RESET_MARKER_NEEDED;

			if (!ha->loop_down_timer)
				qla2100_marker(ha, 0, 0, 0, MK_SYNC_ALL);

			ha->flags.online = TRUE;

			/* Enable target response to SCSI bus. */
			if (ha->flags.enable_target_mode)
				qla2100_enable_lun(ha);

#ifdef FC_IP_SUPPORT
			/* Reenable IP support */
			if (ha->flags.enable_ip)
				qla2x00_ip_initialize(ha);
#endif
			/* Enable ISP interrupts. */
			qla2100_enable_intrs(ha);

			/* v2.19.5b6 Return all commands */
			qla2x00_abort_queues(ha, TRUE);

			/* Restart queues that may have been stopped. */
			/* ha->flags.abort_isp_active = FALSE; */
			/* 6/9 if( !ha->loop_down_timer ) */
			qla2x00_restart_queues(ha, TRUE);
		} else {
			printk(KERN_WARNING "qla2100: ISP error recovery failed - board disabled\n");
			qla2100_reset_adapter(ha);
			qla2x00_abort_queues(ha, FALSE);
			ha->flags.online = TRUE;
		}
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_abort_isp: **** FAILED ****\n");
#endif
	LEAVE("qla2100_abort_isp");
	return status;
}

/*
* qla2100_init_fc_db
*      Initializes Fibre Channel Device Database.
*
* Input:
*      ha = adapter block pointer.
*
* Output:
*      ha->fc_db = initialized
*/
static void
qla2100_init_fc_db(struct scsi_qla_host * ha)
{
	uint16_t cnt;

	ENTER("qla2100_init_fc_db");

	/* Initialize fc database if it is not initialized. */
	if (!ha->fc_db[0].loop_id && !ha->fc_db[1].loop_id) {
		ha->flags.updated_fc_db = FALSE;

		/* Initialize target database. */
		for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
			ha->fc_db[cnt].name[0] = 0L;
			ha->fc_db[cnt].name[1] = 0L;
			ha->fc_db[cnt].loop_id = PORT_UNUSED;
			ha->fc_db[cnt].port_login_retry_count = 8;
			ha->fc_db[cnt].flag = 0;	/* v2.19.5b3 */
		}

#if USE_FLASH_DATABASE
		/* Move flash database to driver database. */
		qla2100_get_database(ha);
#endif
	}
	LEAVE("qla2100_init_fc_db");
}

#if 0
/*
* qla2100_check_devices
*	Check devices with loop ID's.
*
* Input:
*	ha = adapter block pointer.
*
* Returns:
*	0 = success.
*   1 = retry needed
*
* Context:
*	Kernel context.
*
* fixme(dg)
*/
static uint8_t
qla2100_check_devices(struct scsi_qla_host * ha)
{
	int cnt;
	uint8_t ret = 0;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	/*
	   * Retry any devices that wasn't found but as a WWN.
	 */
	/* v2.19.05b6 */
	for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
		if ((ha->fc_db[cnt].loop_id & PORT_LOST_ID) && !(ha->fc_db[cnt].flag & DEV_OFFLINE)) {
			/*
			   * This dev was not detected but its WWN
			   * is valid. To handle the case where
			   * the switch may not be giving us the
			   * device list correctly, schedule for
			   * a login retry later if not previously
			   * done so.
			 */
			DEBUG2(printk("qla2100_sns: Port login retry - "
				      "target %d, count=%d\n", cnt, ha->fc_db[cnt].port_login_retry_count););
			    if (ha->fc_db[cnt].port_login_retry_count)
				ha->fc_db[cnt].port_login_retry_count--;

			/*
			   * If after decrement the retry count
			   * becomes 0, mark this device OFFLINE so
			   * no more retries will be done based
			   * on this device.
			 */
			if (ha->fc_db[cnt].port_login_retry_count == 0) {
				DEBUG2(printk("qla2100_sns:Port set to OFFLINE - target %d\n", cnt););
				    ha->fc_db[cnt].flag |= DEV_OFFLINE;
			}
			ret = 1;
		}
	}
	return ret;
}
#endif

/*
* qla2100_init_tgt_map
*      Initializes target map.
*
* Input:
*      ha = adapter block pointer.
*
* Output:
*      TGT_Q initialized
*/
void
qla2100_init_tgt_map(struct scsi_qla_host * ha)
{
	uint32_t t;

	ENTER("qla2100_init_tgt_map");
	for (t = 0; t < MAX_TARGETS; t++)
		TGT_Q(ha, t) = (os_tgt_t *) NULL;

	LEAVE("qla2100_init_tgt_map");
}

#ifndef FAILOVER
/*
* qla2100_map_targets
*      Setup target queues.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2100_map_targets(struct scsi_qla_host * ha)
{
	tgt_t *tgt;
	uint32_t b;
	uint32_t t;
	uint8_t status = 0;

	ENTER("qla2100_map_targets");

	b = 0;
	for (t = 0; t < MAX_FIBRE_DEVICES; t++) {
		/* if Port never been used. OR */
		/*  Device does not exist on port. */
		if (ha->fc_db[t].loop_id != PORT_UNUSED && ha->fc_db[t].loop_id != PORT_AVAILABLE) {	/* dg 10/29/99 */

			if ((tgt = TGT_Q(ha, t)) != NULL) {
				DEBUG2(printk("Target %d already allocated\n", t));
			} else
				tgt = qla2100_tgt_alloc(ha);
			DEBUG(printk
			      ("Assigning target ID %02x:%02x @ (%08x) to loop id: 0x%04x\n", b, t, tgt,
			       ha->fc_db[t].loop_id););
			    TGT_Q(ha, t) = tgt;
			tgt->loop_id = ha->fc_db[t].loop_id;
			tgt->down_timer = 0;
		}
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_map_targets: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_map_targets");
	return status;
}
#endif				/* FAILOVER */


/*
* qla2100_reset_adapter
*      Reset adapter.
*
* Input:
*      ha = adapter block pointer.
*/
static void
 qla2100_reset_adapter(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	ENTER("qla2100_reset_adapter");

	ha->flags.online = FALSE;
	qla2100_disable_intrs(ha);
	/* WRT_REG_WORD(&reg->ictrl, 0); */
	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
	WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);

	LEAVE("qla2100_reset_adapter");
}

/*
* qla2100_loop_reset
*      Issue loop reset.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
static uint8_t qla2100_loop_reset(struct scsi_qla_host * ha) {
	uint8_t status = 0;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2100_loop_reset");

	if (ha->flags.enable_lip_reset) {
		mb[0] = MBC_LIP_RESET;
		mb[1] = 0xff00;
		mb[2] = ha->loop_reset_delay;
		status |= qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);
	}
	if (ha->flags.enable_target_reset) {
		mb[0] = MBC_TARGET_RESET;
		mb[1] = ha->loop_reset_delay;
		status |= qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
	}
	if ((!ha->flags.enable_target_reset && !ha->flags.enable_lip_reset) || ha->flags.enable_lip_full_login) {
		mb[0] = MBC_LIP_FULL_LOGIN;
		status |= qla2100_mailbox_command(ha, BIT_0, &mb[0]);
	}

	/* Issue marker command. */
	qla2100_marker(ha, 0, 0, 0, MK_SYNC_ALL);

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_loop_reset: **** FAILED ****\n");
#endif
	LEAVE("qla2100_loop_reset");
	return status;
}

#if 0
/*
*
* qla2100_device_reset
*      Issue bus device reset message to the target.
*
* Input:
*      ha = adapter block pointer.
*      b  = BUS number.
*      t  = SCSI ID.
*
* Returns:
*      0 = success
*/
static uint8_t qla2100_device_reset(struct scsi_qla_host * ha, uint32_t b, uint32_t t) {
	os_tgt_t *tq;
	struct os_lun *lq;
	uint8_t status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2100_device_reset");
#ifdef FIXME
	/* fixme(dg) */
	tq = TGT_Q(ha, t);

	mb[0] = MBC_ABORT_TARGET;
	mb[1] = tgt->loop_id << 8;
	mb[2] = 1;
	status = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Issue marker command. */
	qla2100_marker(ha, b, t, 0, MK_SYNC_ID);
#endif

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_device_reset: **** FAILED ****\n");
#endif
	LEAVE("qla2100_device_reset");
	return status;
}

/*
* qla2100_abort_device
*      Issue an abort message to the device
*
* Input:
*      ha = adapter block pointer.
*      b  = BUS number.
*      t  = SCSI ID.
*      l  = SCSI LUN.
*
* Returns:
*      0 = success
*/
static uint8_t qla2100_abort_device(struct scsi_qla_host * ha, uint32_t b, uint32_t t, uint32_t l) {
	os_tgt_t *tq;
	uint8_t status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2100_abort_device");
#ifdef FIXME

	tgt = TGT_Q(ha, t);

	mb[0] = MBC_ABORT_DEVICE;
	mb[1] = tgt->loop_id << 8;
	mb[2] = l;
	status = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Issue marker command. */
	qla2100_marker(ha, b, t, l, MK_SYNC_ID_LUN);
#endif

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_abort_device: **** FAILED ****\n");
#endif
	LEAVE("qla2100_abort_device");
	return status;
}
#else
/*
 * qla2x00_device_reset
 *	Issue bus device reset message to the target.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_device_reset(struct scsi_qla_host * ha, uint16_t t) {
	uint16_t l;
	struct os_lun *lq;
	fc_port_t *fcport;
	uint8_t status = 0;

	ENTER("qla2x00_device_reset:");

	for (l = 0; l < MAX_LUNS; l++) {
		lq = LUN_Q(ha, t, l);
		if (lq == NULL)
			continue;
		fcport = lq->fclun->fcport;
		if (LOOP_RDY(fcport->ha)) {
			qla2x00_abort_device(fcport->ha, fcport->loop_id, lq->fclun->lun);
			qla2x00_marker(fcport->ha, fcport->loop_id, lq->fclun->lun, MK_SYNC_ID);
		}
	}
	LEAVE("qla2x00_device_reset: exiting normally");
	return status;
}

int qla2x00_eh_device_reset_handler(Scsi_Cmnd *cmd)
{
	struct Scsi_Host *host;
	adapter_state_t *ha;
	int status;

	host = cmd->host;
	ha = (struct scsi_qla_host *) host->hostdata;
	
	status = qla2x00_device_reset(ha, cmd->target);
	return TRUE;
}

int qla2x00_eh_bus_reset_handler(Scsi_Cmnd * cmd)
{
	struct scsi_qla_host *ha;
	uint32_t b, t, l;
	srb_t *sp;
	uint16_t data;
	struct os_lun *q;
	int result;

	ENTER("qla2100_eh_bus_reset_handler");
	COMTRACE('R');
	if (cmd == NULL) {
		printk(KERN_WARNING "(scsi?:?:?:?) Reset called with NULL Scsi_Cmnd " "pointer, failing.\n");
		return SCSI_RESET_SNOOZE;
	}
	ha = (struct scsi_qla_host *) cmd->host->hostdata;
	sp = (srb_t *) CMD_SP(cmd);


	DRIVER_LOCK();
	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&ha->iobase->istatus);
	if (!(ha->flags.in_isr) && (data & RISC_INT))
		qla2100_isr(ha, data);
#else
	data = RD_REG_WORD(&ha->iobase->host_status_lo);
	if (!(ha->flags.in_isr) && (data & HOST_STATUS_INT))
		qla2100_isr(ha, data);
#endif

	DRIVER_UNLOCK(); 
	DEBUG2(printk("qla2x00 (%d:%d:%d): Reset() device\n", (int) b, (int) t, (int) l););
	    /*
	     * Determine the suggested action that the mid-level driver wants
	     * us to perform.
	     */

	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	q = GET_LU_Q(ha, t, l);

	/*
	   *  By this point, we want to already know what we are going to do,
	   *  so we only need to perform the course of action.
	 */

	DRIVER_LOCK(); 
	result = FALSE;

	if (qla2100_verbose)
		printk(KERN_INFO "scsi(%d:%d:%d:%d): LOOP RESET ISSUED.\n", (int) ha->host_no, (int) b, (int) t,
			       (int) l);
	for (t = 0; t < ha->max_targets; t++)
		for (l = 0; l < ha->max_luns; l++)
			qla2100_abort_lun_queue(ha, b, t, l, DID_RESET);
		
	if (!ha->loop_down_timer) {
		if (qla2100_loop_reset(ha) == 0)
			result = SCSI_RESET_SUCCESS | SCSI_RESET_BUS_RESET;
		/*
		 * The reset loop routine returns all the outstanding commands back
		 * with "DID_RESET" in the status field.
		 */
		cmd->result = (int) (DID_BUS_BUSY << 16);
		(*(cmd)->scsi_done) (cmd);
	}

	/* ha->reset_start = jiffies; */

	if (!list_empty(&ha->done_queue)) 
		qla2100_done(ha);

	qla2x00_restart_queues(ha, TRUE);
	DRIVER_UNLOCK(); 
	COMTRACE('r');
	LEAVE("qla2100_eh_bus_reset_handler");
	return result;
}

int qla2x00_eh_host_reset_handler(Scsi_Cmnd *cmd)
{
	struct Scsi_Host *host;
	adapter_state_t *ha;
	int status;

	host = cmd->host;
	ha = (struct scsi_qla_host *) host->hostdata;
	
	status = qla2100_abort_isp(ha);
	return status;
}

/*
 * qla2x00_abort_device
 *	Issue abort device mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop ID of device.
 *	lun = LUN number.
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
 qla2x00_abort_device(struct scsi_qla_host * ha, uint16_t loop_id, uint16_t lun) {
	uint8_t status;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2x00_abort_device");

	mb[0] = MBC_ABORT_DEVICE;
	mb[1] = loop_id << 8;
	mb[2] = lun;
	status = qla2100_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_abort_device: **** FAILED ****\n");
#endif

	LEAVE("qla2x00_abort_device");
	return status;
}
#endif

/*
* qla2100_abort_command
*      Abort command aborts a specified IOCB.
*
* Input:
*      ha = adapter block pointer.
*      sp = SB structure pointer.
*
* Returns:
*      0 = success
*	1 = loop down or port down.
*/
static uint8_t qla2100_abort_command(struct scsi_qla_host * os_ha, srb_t * sp) {
	uint8_t status = 1;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint32_t handle, t;
	fc_port_t *fcport;
	struct os_lun *lq = sp->lun_queue;
	struct scsi_qla_host *ha;

	ENTER("qla2100_abort_command");
	ha = lq->fclun->fcport->ha;

	/* v2.19.8 */
	t = SCSI_TCN_32(sp->cmd);
	fcport = lq->fclun->fcport;
	if (ha->loop_state == LOOP_DOWN || PORT_DOWN(fcport) > 0) {
		return 1;
	}
	/* Locate handle number. */
	for (handle = 0; handle < MAX_OUTSTANDING_COMMANDS; handle++) {
		if (ha->outstanding_cmds[handle] == sp)
			break;

		mb[0] = MBC_ABORT_COMMAND;
		mb[1] = fcport->loop_id << 8;
		mb[2] = (uint16_t) handle;
		mb[3] = handle >> 16;
		mb[6] = lq->fclun->lun;
		if (!(status = qla2100_mailbox_command(ha, BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0])))
			sp->flags |= SRB_ABORT_PENDING;
	}

	if (status)
		DEBUG2(printk("qla2100_abort_command: ** FAILED ** sp=%p\n", sp););
	LEAVE("qla2100_abort_command");
	return status;
}

/*
*  Issue marker command.
*      Function issues marker IOCB.
*
* Input:
*      ha   = adapter block pointer.
*      b    = BUS number.
*      t    = SCSI ID
*      l    = SCSI LUN
*      type = marker modifier
*/
static void
qla2100_marker(struct scsi_qla_host * ha, uint32_t b, uint32_t t, uint32_t l, uint8_t type) {
	mrk_entry_t *pkt = NULL;
	os_tgt_t *tq;
	struct os_lun *lq;
	fc_port_t *fcport = NULL;

	ENTER("qla2100_marker");

	if (type != MK_SYNC_ALL) {
		if ((tq = TGT_Q(ha, t)) == NULL) 
			DEBUG2(printk("qla2100_marker: No target\n"));

		if ((lq = LUN_Q(ha, t, l)) == NULL) 
			DEBUG2(printk("qla2100_marker: No lun\n"));

		fcport = lq->fclun->fcport;
	}

	/* Get request packet. */
	if ((pkt = (mrk_entry_t *) qla2100_req_pkt(ha))) {
		pkt->entry_type = MARKER_TYPE;
		pkt->modifier = type;

		if (type == MK_SYNC_LIP)
			pkt->sequence_number = ha->lip_seq;
		else if (type != MK_SYNC_ALL) {
			pkt->lun = l;
			if (fcport)
				pkt->target = (uint8_t) fcport->loop_id;
			else
				pkt = (mrk_entry_t *) NULL;
		}

		/* Issue command to ISP */
		if (pkt)
			qla2100_isp_cmd(ha);
	}
	if (!pkt)
		DEBUG2(printk("qla2100_marker: **** FAILED ****\n"));
	LEAVE("qla2100_marker");
}

/*
 *  Issue marker command.
 *	Function issues marker IOCB.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop ID
 *	lun = LUN
 *	type = marker modifier
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
static int
qla2x00_marker(struct scsi_qla_host * ha, uint16_t loop_id, uint16_t lun, uint8_t type) {
	mrk_entry_t *pkt;

	ENTER("qla2x00_marker: entered");

	pkt = (mrk_entry_t *) qla2100_req_pkt(ha);
	if (pkt == NULL) {
		DEBUG2(printk("qla2100_marker: **** FAILED ****\n"));
		return QLA2X00_FAILED;
	}

	pkt->entry_type = MARKER_TYPE;
	pkt->modifier = type;

	if (type != MK_SYNC_ALL) {
		pkt->lun = lun;
		pkt->target = (uint8_t) loop_id;
	}

	/* Issue command to ISP */
	qla2100_isp_cmd(ha);

	LEAVE("qla2x00_marker: exiting normally");

	return QLA2X00_SUCCESS;
}


/*
* qla2100_64bit_start_scsi
*      The start SCSI is responsible for building request packets on
*      request ring and modifying ISP input pointer.
*
* Input:
*      ha = adapter block pointer.
*      sp = SB structure pointer.
*
* Returns:
*      0 = success, was able to issue command.
*/
static uint8_t qla2x00_64bit_start_scsi(srb_t * sp) {
	device_reg_t *reg;
	uint8_t status = 0;
	Scsi_Cmnd *cmd = sp->cmd;
	uint32_t cnt;
	cmd_a64_entry_t *pkt;
	uint16_t req_cnt;
	uint16_t seg_cnt;
	uint16_t cdb_len, temp;
	struct scatterlist *sg = (struct scatterlist *) NULL;
	uint32_t timeout;
	caddr_t data_ptr;
	uint32_t *dword_ptr;
	uint64_t dma_handle;
	struct scsi_qla_host *ha;
	fc_lun_t *fclun;

	ENTER("qla2100_64bit_start_scsi:");
	DEBUG(printk("64bit_start: cmd=%x sp=%x CDB=%x\n", cmd, sp, cmd->cmnd[0]););

	/* Setup device pointers. */
	fclun = sp->lun_queue->fclun;
	ha = fclun->fcport->ha;
	reg = ha->iobase;

	/* Calculate number of entries and segments required. */
	seg_cnt = 0;
	req_cnt = 1;
	if (cmd->use_sg) {
		/* 4.10 64 bit S/G Data Transfer */
		sg = (struct scatterlist *) cmd->request_buffer;
		seg_cnt = pci_map_sg(ha->pdev, sg, cmd->use_sg, scsi_to_pci_dma_dir(cmd->sc_data_direction));

		if (seg_cnt > 2) {
			req_cnt += (uint16_t) (seg_cnt - 2) / 5;
			if ((uint16_t) (seg_cnt - 2) % 5)
				req_cnt++;
		}
	} else if (cmd->request_bufflen) {	/* no S/G Data Transfer */
		DEBUG5(printk("Single data transfer (0x%x)\n", cmd->request_bufflen));
		seg_cnt = 1;
	}

	/* Acquire ring specific lock */
	QLA2100_RING_LOCK(ha);

	if ((uint16_t) (req_cnt + 2) >= ha->req_q_cnt) {
		/* Calculate number of free request entries. */
#if defined(ISP2100) || defined(ISP2200)
		cnt = RD_REG_WORD(&reg->mailbox4);
#else
		cnt = RD_REG_WORD(&reg->req_q_out);
#endif
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
	}

	/* If room for request in request ring. */
	if ((uint16_t) (req_cnt + 2) < ha->req_q_cnt) {
		/* Check for room in outstanding command list. */
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS && ha->outstanding_cmds[cnt] != 0; cnt++) ;

		if (cnt < MAX_OUTSTANDING_COMMANDS) {
			ha->outstanding_cmds[cnt] = sp;
			ha->req_q_cnt -= req_cnt;
			sp->cmd->host_scribble = (unsigned char *) (u_long) cnt;

			/*
			   * Build command packet.
			 */
			pkt = ha->request_ring_ptr;

			pkt->entry_type = COMMAND_A64_TYPE;
			pkt->entry_count = (uint8_t) req_cnt;
			pkt->sys_define = (uint8_t) ha->req_ring_index;
			pkt->control_flags = 0;
			pkt->entry_status = 0;
			pkt->handle = (uint32_t) cnt;

			/* Zero out remaining portion of packet. */
			dword_ptr = (uint32_t *) pkt + 2;
			for (cnt = 2; cnt < REQUEST_ENTRY_SIZE / 4; cnt++)
				*dword_ptr++ = 0;

                                                
			/*
			   * We subtract 5 sec. from the timeout value to insure
			   * the ISP time-out before the mid-level or the driver.
			 */
			timeout = (uint32_t) cmd->timeout_per_command / HZ;
			if (timeout > 5)
				pkt->timeout = (uint16_t) timeout - 5;
			else
				pkt->timeout = (uint16_t) timeout;
			/* Set LUN number */
			pkt->lun = fclun->lun;
	
			/* If this is part of a volume set and not the set itself, mark the lun */
			if( ! ((cmd->data_cmnd[0] == 0x26) &&
				(cmd->data_cmnd[0] == 0xA0) &&
				(cmd->data_cmnd[0] == 0xCB) ) && (fclun->fcport->flags & FC_VSA))
                               pkt->lun = (fclun->lun | 0x4000);
			

			/* Set target ID */
			pkt->target = (uint8_t) fclun->fcport->loop_id;

			/* Enable simple tag queuing if device supports it. */
			if (cmd->device->tagged_queue) {
				switch (cmd->tag) {
				case SIMPLE_QUEUE_TAG:
					pkt->control_flags = CF_SIMPLE_TAG;
					break;
				case HEAD_OF_QUEUE_TAG:
					pkt->control_flags = CF_HEAD_TAG;
					break;
				case ORDERED_QUEUE_TAG:
					pkt->control_flags = CF_ORDERED_TAG;
					break;
				default:
					pkt->control_flags = CF_SIMPLE_TAG;
				}
			} else
				pkt->control_flags = CF_SIMPLE_TAG;

			/* Load SCSI command packet. */
			cdb_len = (uint16_t) cmd->cmd_len;
			if (cdb_len > MAX_CMDSZ)
				cdb_len = MAX_CMDSZ;
			data_ptr = (uint8_t *) & cmd->cmnd;
			for (cnt = 0; cnt < cdb_len; cnt++)
				pkt->scsi_cdb[cnt] = *data_ptr++;
			pkt->byte_count = (uint32_t) cmd->request_bufflen;

			/*
			   * Load data segments.
			 */
			if (seg_cnt) {	/* If data transfer. */
				switch (cmd->data_cmnd[0]) {
				case FORMAT_UNIT:
				case WRITE_6:
				case MODE_SELECT:
				case SEND_DIAGNOSTIC:
				case WRITE_10:
				case WRITE_BUFFER:
				case WRITE_LONG:
				case WRITE_SAME:
				case MODE_SELECT_10:
				case WRITE_12:
				case WRITE_VERIFY:
				case WRITE_VERIFY_12:
				case SEND_VOLUME_TAG:
					pkt->control_flags |= BIT_6;
					break;
				default:
					if (cmd->sc_data_direction == SCSI_DATA_WRITE)
						pkt->control_flags |= BIT_6;
					 /*WRITE*/
					else
						pkt->control_flags |= BIT_5;
					 /*READ*/
					 break;
				}
				sp->dir = pkt->control_flags & (BIT_5 | BIT_6);

				/* Set total data segment count. */
				pkt->dseg_count = seg_cnt;

				/* Setup packet address segment pointer. */
				dword_ptr = (uint32_t *) & pkt->dseg_0_address;

				if (cmd->use_sg) {	/* If scatter gather */
					/* Load command entry data segments. */
					for (cnt = 0; cnt < 2 && seg_cnt; cnt++, seg_cnt--) {
						/* 4.10 64 bit */
						*dword_ptr++ = cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
						*dword_ptr++ = cpu_to_le32(pci_dma_hi32(sg_dma_address(sg)));
						*dword_ptr++ = cpu_to_le32(sg_dma_len(sg));
						sg++;
						/* DEBUG(sprintf(debug_buff,
						   "S/G Segment phys_addr=%x %x, len=0x%x\n",
						   cpu_to_le32(pci_dma_hi32(sg_dma_address(sg))),
						   cpu_to_le32(pci_dma_lo32(sg_dma_address(sg))),
						   cpu_to_le32(sg_dma_len(sg)));)
						   DEBUG(qla2100_print(debug_buff)); */
					}
					DEBUG5(printk("qla2x00_64bit_start_scsi: Scatter/gather command packet data - "););
					DEBUG5(printk("b%dt%dd%d\n",(u32)SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd)););
					DEBUG5(qla2100_dump_buffer((caddr_t) pkt, REQUEST_ENTRY_SIZE););
					/*
					   * Build continuation packets.
					 */
					while (seg_cnt > 0) {
						/* Adjust ring index. */
						ha->req_ring_index++;
						if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
							ha->req_ring_index = 0;
							ha->request_ring_ptr = ha->request_ring;
						} else
							ha->request_ring_ptr++;

						pkt = (cmd_a64_entry_t *) ha->request_ring_ptr;

						/* Zero out packet. */
						dword_ptr = (uint32_t *) pkt;
						for (cnt = 0; cnt < REQUEST_ENTRY_SIZE / 4; cnt++)
							*dword_ptr++ = 0;

						/* Load packet defaults. */
						((cont_a64_entry_t *) pkt)->entry_type = CONTINUE_A64_TYPE;
						((cont_a64_entry_t *) pkt)->entry_count = 1;
						((cont_a64_entry_t *) pkt)->sys_define = (uint8_t)
						    ha->req_ring_index;

						/* Setup packet address segment pointer. */
						dword_ptr = (uint32_t *)
						    & ((cont_a64_entry_t *) pkt)->dseg_0_address;

						/* Load continuation entry data segments. */
						for (cnt = 0; cnt < 5 && seg_cnt; cnt++, seg_cnt--) {
							/* 4.10 64 bit */
							*dword_ptr++ = cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
							*dword_ptr++ = cpu_to_le32(pci_dma_hi32(sg_dma_address(sg)));
							*dword_ptr++ = cpu_to_le32(sg_dma_len(sg));
							sg++;
							/* DEBUG(printk(
							   "S/G Segment Cont. phys_addr=%x %x, len=0x%x\n",
							   cpu_to_le32(pci_dma_hi32(sg_dma_address(sg))),
							   cpu_to_le32(pci_dma_lo32(sg_dma_address(sg))),
							   cpu_to_le32(sg_dma_len(sg)))); */
						}
						DEBUG5(printk("qla2x00_64bit_start_scsi: continuation packet data - c"););
						DEBUG5(printk("b%dt%dd%d\n", SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd)););
						DEBUG5(qla2100_dump_buffer((caddr_t) pkt, REQUEST_ENTRY_SIZE););
					}
				} else {	/* No scatter gather data transfer */
					/* 4.10 64 bit */
					dma_handle = pci_map_single(ha->pdev,
								    cmd->request_buffer,
								    cmd->request_bufflen,
								    scsi_to_pci_dma_dir(cmd->sc_data_direction));
					/* save dma_handle for pci_unmap_single */
					sp->saved_dma_handle = dma_handle;

					*dword_ptr++ = cpu_to_le32(pci_dma_lo32(dma_handle));
					*dword_ptr++ = cpu_to_le32(pci_dma_hi32(dma_handle));
					*dword_ptr   = (uint32_t) cmd->request_bufflen;
					/* DEBUG(printk(
					   "64_bit: No S/G map_single saved_dma_handle=%lx len=%x \n",dma_handle, cmd->request_bufflen)); */
					DEBUG5(printk
					    ("qla2x00_64bit_start_scsi: No scatter/gather command packet data - c");
						printk("b%dt%dd%d\n", SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd)););
					DEBUG5(qla2x00_dump_buffer((caddr_t) pkt, REQUEST_ENTRY_SIZE););
				}
			} else {	/* No data transfer */

				*dword_ptr++ = (uint32_t) 0;
				*dword_ptr++ = (uint32_t) 0;
				*dword_ptr   = (uint32_t) 0;
				DEBUG5(printk("qla2x00_64bit_start_scsi: No data, command packet data - c"););
				DEBUG5(printk("b%dt%dd%d\n", SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd)););
				DEBUG5(qla2x00_dump_buffer((caddr_t) pkt, REQUEST_ENTRY_SIZE););
			}
			DEBUG4(printk("\nqla2100_64bit_start_scsi: Wakeup RISC for pending command\n"););
			/* Adjust ring index. */
			ha->req_ring_index++;
			if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
				ha->req_ring_index = 0;
				ha->request_ring_ptr = ha->request_ring;
			} else
				ha->request_ring_ptr++;
                        ha->actthreads++;
			ha->qthreads--;

			/* Set chip new ring index. */
#if WATCH_THREADS_SIZE
			DEBUG3(printk("qla2100_64bit_start_scsi: actthreads=%d.\n", (uint32_t) ha->actthreads););
#endif
#if defined(ISP2100) || defined(ISP2200)
			temp = CACHE_FLUSH(&reg->mailbox4);
			WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
#else
			temp = CACHE_FLUSH(&reg->req_q_in);
			WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
#endif
		} else {
			status = 1;
			DEBUG2(printk("qla2x00_64bit_start_scsi: NO ROOM IN OUTSTANDING ARRAY\n"););
			DEBUG2(printk(" req_q_cnt= %x \n",(u_long) ha->req_q_cnt););
		}
	} else {
		status = 1;
		DEBUG2(printk("qla2x00_64bit_start_scsi: in-ptr=%x req_q_cnt= %x req_cnt= %x\n", (u_long) ha->req_ring_index,
			(u_long) ha->req_q_cnt , (u_long) req_cnt ););
	}

	/* Release ring specific lock */
	QLA2100_RING_UNLOCK(ha);

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2x00_64bit_start_scsi: **** FAILED ****\n");
#endif
	LEAVE("qla2x00_64bit_start_scsi: exiting normally\n");
	return status;
}


/*
* qla2x00_32bit_start_scsi
*      The start SCSI is responsible for building request packets on
*      request ring and modifying ISP input pointer.
*
*      The Qlogic firmware interface allows every queue slot to have a SCSI
*      command and up to 4 scatter/gather (SG) entries.  If we need more
*      than 4 SG entries, then continuation entries are used that can
*      hold another 7 entries each.  The start routine determines if there
*      is eought empty slots then build the combination of requests to
*      fulfill the OS request.
*
* Input:
*      ha = adapter block pointer.
*      sp = SCSI Request Block structure pointer.
*
* Returns:
*      0 = success, was able to issue command.
*/
static uint8_t qla2x00_32bit_start_scsi(srb_t * sp) {
	device_reg_t *reg;
	uint8_t status = 0;
	Scsi_Cmnd *cmd = sp->cmd;
	uint16_t cdb_len, temp;
	uint32_t cnt;
	cmd_entry_t *pkt;
	uint16_t req_cnt;
	uint16_t seg_cnt;
	struct scatterlist *sg = (struct scatterlist *) NULL;
	caddr_t data_ptr;
	uint32_t *dword_ptr;
	uint32_t timeout;
	uint64_t dma_handle;
	struct scsi_qla_host *ha;
	fc_lun_t *fclun;

	ENTER("qla2100_32bit_start_scsi");

	/* Setup device pointers. */
	fclun = sp->lun_queue->fclun;
	ha = fclun->fcport->ha;
	reg = ha->iobase;
#ifdef NEW
	/*
	   * Send marker if required.
	 */
	if (ha->marker_needed != 0) {
		if (qla2200_marker(ha, 0, 0, MK_SYNC_ALL) != 0) {
			RING_UNLOCK(ha);
			return 1;
		}
	}
#endif
	COMTRACE('S');
	DEBUG5(printk("32bit_start: ha(%d) cmd=%x sp=%x CDB=%x\n", (int) ha->instance, cmd, sp, cmd->cmnd[0]););
	/* Calculate number of entries and segments required. */
	    
	seg_cnt = 0;
	req_cnt = 1;
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		/* 4.10 32 bit S/G Data Transfer */
		seg_cnt = pci_map_sg(ha->pdev, sg, cmd->use_sg, scsi_to_pci_dma_dir(cmd->sc_data_direction));
		/*
	   	 * if greater than four sg entries then we need to allocate
		 * continuation entries
		 */
		if (seg_cnt > 2) {
			req_cnt += (uint16_t) (seg_cnt - 3) / 7;
			if ((uint16_t) (seg_cnt - 3) % 7)
				req_cnt++;
		}
		DEBUG5(printk("S/G for data transfer -num segs(%d), req blk cnt(%d)\n", seg_cnt, req_cnt));
	} else if (cmd->request_bufflen) {	/* If data transfer. *//* no S/G Data Transfer */
		DEBUG5(printk("Single data transfer (0x%x)\n", cmd->request_bufflen));
		seg_cnt = 1;
	}

	/* Acquire ring specific lock */
	QLA2100_RING_LOCK(ha);

	if ((uint16_t) (req_cnt + 2) >= ha->req_q_cnt) {
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

	DEBUG5(printk("Number of free entries = (%d)\n", ha->req_q_cnt));
	/* If room for request in request ring. */
	if ((uint16_t) (req_cnt + 2) < ha->req_q_cnt) {
		/* Check for room in outstanding command list. */
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS && (ha->outstanding_cmds[cnt] != 0); cnt++) ;

		if (cnt < MAX_OUTSTANDING_COMMANDS) {
			ha->outstanding_cmds[cnt] = sp;
			ha->req_q_cnt -= req_cnt;
			/* save the handle -- it help if we have to abort it */
			sp->cmd->host_scribble = (unsigned char *) (u_long) cnt;

			/*
			   * Build command packet.
			 */
			pkt = (cmd_entry_t *) ha->request_ring_ptr;

			pkt->entry_type = COMMAND_TYPE;
			pkt->entry_count = (uint8_t) req_cnt;
			pkt->sys_define = (uint8_t) ha->req_ring_index;
			pkt->entry_status = 0;
			pkt->control_flags = 0;
			pkt->handle = (uint32_t) cnt;

			/* Zero out remaining portion of packet. */
			dword_ptr = (uint32_t *) pkt + 2;
			for (cnt = 2; cnt < REQUEST_ENTRY_SIZE / 4; cnt++)
				*dword_ptr++ = 0;

			/*
			   * We subtract 5 sec. from the timeout value to insure
			   * the ISP time-out before the mid-level or the driver.
			 */
			timeout = (uint32_t) cmd->timeout_per_command / HZ;
			if (timeout > 5)
				pkt->timeout = (uint16_t) timeout - 5;
			else
				pkt->timeout = (uint16_t) timeout;

			/* Set LUN number */
			pkt->lun = fclun->lun;
	
			/* If this is part of a volume set and not the set itself, mark the lun */
			if( ! ((cmd->data_cmnd[0] == 0x26) &&
				(cmd->data_cmnd[0] == 0xA0) &&
				(cmd->data_cmnd[0] == 0xCB) ) && (fclun->fcport->flags & FC_VSA))
                               pkt->lun = (fclun->lun | 0x4000);
			

#if 0
			pkt->lun_l = LSB(fclun->lun);
			pkt->lun_h = MSB(fclun->lun);
#endif

			/* Set target ID */
			pkt->target = (uint8_t) fclun->fcport->loop_id;

			/* Enable simple tag queuing if device supports it. */
			if (cmd->device->tagged_queue) {
				switch (cmd->tag) {
				case SIMPLE_QUEUE_TAG:
					pkt->control_flags = CF_SIMPLE_TAG;
					break;
				case HEAD_OF_QUEUE_TAG:
					pkt->control_flags = CF_HEAD_TAG;
					break;
				case ORDERED_QUEUE_TAG:
					pkt->control_flags = CF_ORDERED_TAG;
					break;
				default:
					pkt->control_flags = CF_SIMPLE_TAG;
				}
			} else
				pkt->control_flags = CF_SIMPLE_TAG;

			/* Load SCSI command packet. */
			cdb_len = (uint16_t) cmd->cmd_len;
			if (cdb_len > MAX_CMDSZ)
				cdb_len = MAX_CMDSZ;
			data_ptr = (uint8_t *) & cmd->cmnd;
			for (cnt = 0; cnt < cdb_len; cnt++)
				pkt->scsi_cdb[cnt] = *data_ptr++;
			DEBUG4(printk("32bit_start: Hba=%02d, lun=%02d, scsi Cdb[0]=0x%x, handle=0x%x, loop id =0x%x, sp=%p\n",
				       ha->instance, pkt->lun, pkt->scsi_cdb[0], pkt->handle, pkt->target, sp););
			    pkt->byte_count = (uint32_t) cmd->request_bufflen;

			/*
			   * Load data segments.
			 */
			if (seg_cnt) {
				/* Set transfer direction (READ and WRITE) */
				/* Linux doesn't tell us                   */

				switch (cmd->data_cmnd[0]) {
				case FORMAT_UNIT:
				case WRITE_6:
				case MODE_SELECT:
				case SEND_DIAGNOSTIC:
				case WRITE_10:
				case WRITE_BUFFER:
				case WRITE_LONG:
				case WRITE_SAME:
				case MODE_SELECT_10:
				case WRITE_12:
				case WRITE_VERIFY:
				case WRITE_VERIFY_12:
				case SEND_VOLUME_TAG:
					pkt->control_flags |= BIT_6;
					break;
				default:
					if (cmd->sc_data_direction == SCSI_DATA_WRITE)
						pkt->control_flags |= BIT_6;
					 /*WRITE*/
					else
						pkt->control_flags |= BIT_5;
					 /*READ*/
					    break;
				}
				sp->dir = pkt->control_flags & (BIT_5 | BIT_6);

				/* Set total data segment count. */
				pkt->dseg_count = seg_cnt;

				/* Setup packet address segment pointer. */
				dword_ptr = (uint32_t *) & pkt->dseg_0_address;

				if (cmd->use_sg) {	/* If scatter gather */
					DEBUG5(printk("Building S/G data segments..\n"));
					DEBUG5(qla2100_dump_buffer((caddr_t) sg, 4 * 16));
					/* Load command entry data segments. */
					for (cnt = 0; cnt < 3 && seg_cnt; cnt++, seg_cnt--) {
						*dword_ptr++ = cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
						*dword_ptr++ = cpu_to_le32(sg_dma_len(sg));
						/* DEBUG(printk(
						   "S/G Segment phys_addr=0x%x, len=0x%x\n",
						   cpu_to_le32(pci_dma_lo32(sg_dma_address(sg))),
						   cpu_to_le32(sg_dma_len(sg))));*/
						sg++;
					}
 	DEBUG5(printk("qla2100_32bit_start_scsi: Scatter/gather command packet data - "););
	DEBUG5(printk("%d:%d:%d:d\n",(u_long) ha->host_no,(u_long) SCSI_BUS_32(cmd), (u_long) SCSI_TCN_32(cmd), (u_long) SCSI_LUN_32(cmd)););
	DEBUG5(qla2100_dump_buffer((uint8_t *) pkt, REQUEST_ENTRY_SIZE););
					/*
					   * Build continuation packets.
					 */
					while (seg_cnt > 0) {
						/* Adjust ring index. */
						ha->req_ring_index++;
						if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
							ha->req_ring_index = 0;
							ha->request_ring_ptr = ha->request_ring;
						} else
							ha->request_ring_ptr++;

						pkt = (cmd_entry_t *) ha->request_ring_ptr;

						/* Zero out packet. */
						dword_ptr = (uint32_t *) pkt;
						for (cnt = 0; cnt < REQUEST_ENTRY_SIZE / 4; cnt++)
							*dword_ptr++ = 0;

						/* Load packet defaults. */
						((cont_entry_t *) pkt)->entry_type = CONTINUE_TYPE;
						((cont_entry_t *) pkt)->entry_count = 1;
						((cont_entry_t *) pkt)->sys_define = (uint8_t)
						    ha->req_ring_index;

						/* Setup packet address segment pointer. */
						dword_ptr = (uint32_t *)
						    & ((cont_entry_t *) pkt)->dseg_0_address;

						/* Load continuation entry data segments. */
						for (cnt = 0; cnt < 7 && seg_cnt; cnt++, seg_cnt--) {
							/* 4.10 32 bit */
							*dword_ptr++ = cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
							*dword_ptr++ = cpu_to_le32(sg_dma_len(sg));
							sg++;
						}
	DEBUG5(printk("qla2100_32bit_start_scsi: continuation packet data - "););
	DEBUG5(printk("%d:%d:%d:d\n",(u_long) ha->host_no,(u_long) SCSI_BUS_32(cmd), (u_long) SCSI_TCN_32(cmd), (u_long) SCSI_LUN_32(cmd)););
	DEBUG5(qla2100_dump_buffer((uint8_t *) pkt, REQUEST_ENTRY_SIZE););
					}
				} else {	/* No scatter gather data transfer */

					/* 4.10 32 bit */
					dma_handle = pci_map_single(ha->pdev,
								    cmd->request_buffer,
								    cmd->request_bufflen,
								    scsi_to_pci_dma_dir(cmd->sc_data_direction));
					sp->saved_dma_handle = dma_handle;

					*dword_ptr++ = cpu_to_le32(pci_dma_lo32(dma_handle));
					*dword_ptr = (uint32_t) cmd->request_bufflen;
					DEBUG5(printk
					       ("Single Segment ap=0x%x, len=0x%x\n", cmd->request_buffer,
						cmd->request_bufflen));

	DEBUG5(printk("qla2100_32bit_start_scsi: No scatter/gather command packet data - "););
 	DEBUG5(printk("%d:%d:%d:d\n",(u_long) ha->host_no,(u_long) SCSI_BUS_32(cmd), (u_long) SCSI_TCN_32(cmd), (u_long) SCSI_LUN_32(cmd)););
	DEBUG5(qla2100_dump_buffer((uint8_t *) pkt, REQUEST_ENTRY_SIZE););
				}
			} else {	/* No data transfer */

				*dword_ptr++ = (uint32_t) 0;
				*dword_ptr = (uint32_t) 0;
	DEBUG5(printk("qla2100_32bit_start_scsi: No data, command packet data - "););

		DEBUG5(printk("%d:%d:%d:d\n",(u_long) ha->host_no,(u_long) SCSI_BUS_32(cmd), (u_long) SCSI_TCN_32(cmd), (u_long) SCSI_LUN_32(cmd)););
	DEBUG5(qla2100_dump_buffer((uint8_t *) pkt, REQUEST_ENTRY_SIZE););
			}
			/* Adjust ring index. */
			ha->req_ring_index++;
			if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
				ha->req_ring_index = 0;
				ha->request_ring_ptr = ha->request_ring;
			} else
				ha->request_ring_ptr++;
			/* Set chip new ring index. */
			DEBUG5(printk("qla2100_32bit_start_scsi: Wakeup RISC for pending command\n"));
			DEBUG5(qla2100_dump_buffer((caddr_t) pkt, 64));
			ha->qthreads--;
			sp->flags |= SRB_SENT;
			sp->u_start = jiffies;
			ha->actthreads++;
			DEBUG5(printk("%ld ", cmd->serial_number););

#if WATCH_THREADS_SIZE
			DEBUG3(printk("qla2100_32bit_start_scsi: actthreads=%d.\n", (u_long) ha->actthreads););
#endif
#if defined(ISP2100) || defined(ISP2200)

		    			    temp = CACHE_FLUSH(&reg->mailbox4);
			WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
#else
			    temp = CACHE_FLUSH(&reg->req_q_in);
			WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
#endif
		} else {
			status = 1;
			qla2100_stats.outarray_full++;

				DEBUG3(printk("qla2100_32bit_start_scsi: NO ROOM IN OUTSTANDING ARRAY\n"););
		}
	} else {
		status = 1;
#ifdef QL_DEBUG_LEVEL_8
		printk("qla2100_32bit_start_scsi: in-ptr=%x req_q_cnt=%x req_cnt=%x\n", (u_long) ha->req_ring_index,
			(u_long) ha->req_q_cnt, (u_long) req_cnt);
#endif
	}

	/* Release ring specific lock */
	QLA2100_RING_UNLOCK(ha);

#if defined(QL_DEBUG_LEVEL_8) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_32bit_start_scsi: **** FAILED ****\n");
#endif
	LEAVE("qla2100_32bit_start_scsi");
	COMTRACE('s');
	return status;
}


/*
* qla2100_req_pkt
*      Function is responsible for locking ring and
*      getting a zeroed out request packet.
*
* Input:
*      ha  = adapter block pointer.
*
* Returns:
*      0 = failed to get slot.
*/
static request_t *qla2100_req_pkt(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;
	request_t *pkt = 0;
	uint16_t cnt;
	uint32_t timer;

	ENTER("qla2100_req_pkt");

	/* Wait for 30 seconds for slot. */
	for (timer = 3000000; timer; timer--) {
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

		/* Found empty request ring slot? */
		if (ha->req_q_cnt) {
			ha->req_q_cnt--;
			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			/* Set system defined field. */
			pkt->sys_define = (uint8_t) ha->req_ring_index;

			/* Set entry count. */
			pkt->entry_count = 1;

			break;
		}

		/* Release ring specific lock */
		QLA2100_RING_UNLOCK(ha);

		udelay(2);	/* 10 */

		/* Check for pending interrupts. */
		qla2100_poll(ha);
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_req_pkt: **** FAILED ****\n");
#endif
	LEAVE("qla2100_req_pkt");
	return pkt;
}

/*
* qla2100_isp_cmd
*      Function is responsible for modifying ISP input pointer.
*      Releases ring lock.
*
* Input:
*      ha  = adapter block pointer.
*/
void
qla2100_isp_cmd(struct scsi_qla_host * ha) {
	device_reg_t *reg = ha->iobase;

	ENTER("qla2100_isp_cmd");

	DEBUG5(printk("qla2100_isp_cmd: IOCB data:\n"));
	DEBUG5(qla2100_dump_buffer((uint8_t *) ha->request_ring_ptr, REQUEST_ENTRY_SIZE));

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
#if defined(ISP2100) || defined(ISP2200)
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
#else
	WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
#endif

	/* Release ring specific lock */
	QLA2100_RING_UNLOCK(ha);

	LEAVE("qla2100_isp_cmd");
}

/*
* qla2100_enable_lun
*      Issue enable LUN entry IOCB.
*
* Input:
*      ha = adapter block pointer.
*/
static void
 qla2100_enable_lun(struct scsi_qla_host * ha) {
	elun_entry_t *pkt;

	ENTER("qla2100_enable_lun");

	/* Get request packet. */
	if ((pkt = (elun_entry_t *) qla2100_req_pkt(ha)) != NULL) {
		pkt->entry_type = ENABLE_LUN_TYPE;
		pkt->command_count = 32;
		pkt->immed_notify_count = 1;
		pkt->timeout = 0xffff;

		/* Issue command to ISP */
		qla2100_isp_cmd(ha);
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_enable_lun: **** FAILED ****\n");
#endif
	LEAVE("qla2100_enable_lun");
}

#if QL2100_TARGET_MODE_SUPPORT
/****************************************************************************/
/*                      Target Mode Support Functions.                      */
/****************************************************************************/

/*
* qla2100_notify_ack
*      Issue notify acknowledge IOCB.
*      If sequence ID is zero, acknowledgement of
*      SCSI bus reset or bus device reset is assumed.
*
* Input:
*      ha      = adapter block pointer.
*      inotify = immediate notify entry pointer.
*/
static void
 qla2100_notify_ack(struct scsi_qla_host * ha, notify_entry_t * inotify) {
	nack_entry_t *pkt;

	ENTER("qla2100_notify_ack: entered\n");

	/* Get request packet. */
	if (pkt = (nack_entry_t *) qla2100_req_pkt(ha)) {
		pkt->entry_type = NOTIFY_ACK_TYPE;
		pkt->initiator_id = inotify->initiator_id;
		pkt->target_id = inotify->target_id;

		if ((pkt->status = inotify->status) == 0xe)
			/* Reset LIP occurred. */
			pkt->flags = OF_RESET;
		else
			/* Increment Immediate Notify Resource Count. */
			pkt->flags = OF_INC_RC;

		pkt->task_flags = inotify->task_flags;
		pkt->seq_id = inotify->seq_id;

		/* Issue command to ISP */
		qla2100_isp_cmd(ha);
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_notify_ack: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_notify_ack: exiting normally\n");
}

/*
* qla2100_64bit_continue_io
*      Issue continue target I/O IOCB.
*
* Input:
*      ha   = adapter block pointer.
*      atio = atio pointer.
*      len  = total bytecount.
*      addr = physical address pointer.
*/
static void
 qla2100_64bit_continue_io(struct scsi_qla_host * ha, atio_entry_t * atio, uint32_t len, u_long * addr) {
	ctio_a64_entry_t *pkt;
	uint32_t *dword_ptr;

	ENTER("qla2100_64bit_continue_io: entered\n");

	/* Get request packet. */
	if (pkt = (ctio_a64_entry_t *) qla2100_req_pkt(ha)) {
		pkt->entry_type = CTIO_A64_TYPE;
		pkt->initiator_id = atio->initiator_id;
		pkt->exchange_id = atio->exchange_id;
		pkt->flags = atio->flags | OF_FAST_POST;
		pkt->scsi_status = atio->scsi_status;

		if (len) {
			pkt->dseg_count = 1;
			pkt->transfer_length = len;
			pkt->dseg_0_length = len;
			dword_ptr = (uint32_t *) addr;
			pkt->dseg_0_address[0] = *dword_ptr++;
			pkt->dseg_0_address[1] = *dword_ptr;
		}

		/* Issue command to ISP */
		qla2100_isp_cmd(ha);
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_64bit_continue_io: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_64bit_continue_io: exiting normally\n");
}

/*
* qla2100_32bit_continue_io
*      Issue continue target I/O IOCB.
*
* Input:
*      ha   = adapter block pointer.
*      atio = atio pointer.
*      len  = total bytecount.
*      addr = physical address pointer.
*/
static void
 qla2100_32bit_continue_io(struct scsi_qla_host * ha, atio_entry_t * atio, uint32_t len, u_long * addr) {
	ctio_entry_t *pkt;
	uint32_t *dword_ptr;

	ENTER("qla2100_continue_io: entered\n");

	/* Get request packet. */
	if (pkt = (ctio_entry_t *) qla2100_req_pkt(ha)) {
		pkt->entry_type = CONTINUE_TGT_IO_TYPE;
		pkt->initiator_id = atio->initiator_id;
		pkt->exchange_id = atio->exchange_id;
		pkt->flags = atio->flags | OF_FAST_POST;
		pkt->scsi_status = atio->scsi_status;

		if (len) {
			pkt->dseg_count = 1;
			pkt->transfer_length = len;
			pkt->dseg_0_length = len;
			dword_ptr = (uint32_t *) addr;
			pkt->dseg_0_address = *dword_ptr;
		}

		/* Issue command to ISP */
		qla2100_isp_cmd(ha);
	}
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_32bit_continue_io: **** FAILED ****\n");
	else
#endif
		LEAVE("qla2100_32bit_continue_io: exiting normally\n");
}
#endif				/* QL2100_TARGET_MODE_SUPPORT */

/****************************************************************************/
/*                        Interrupt Service Routine.                        */
/****************************************************************************/

/*
*  qla2100_isr
*      Calls I/O done on command completion.
*
* Input:
*      ha           = adapter block pointer.
*      INTR_LOCK must be already obtained.
*/
static void
qla2100_isr(struct scsi_qla_host * ha, uint16_t data) {
	device_reg_t *reg = ha->iobase;
	srb_t *sp;
	uint32_t index;
	uint16_t *iptr, *mptr;
	uint16_t mailbox[MAILBOX_REGISTER_COUNT];
	uint16_t cnt, temp1;
	uint16_t response_index = RESPONSE_ENTRY_CNT;
#ifdef ISP2300
	uint16_t temp2;
	uint8_t mailbox_int;
#endif
	uint8_t rscn_queue_index;

	ENTER("qla2100_isr: entered\n");

	/* Check for mailbox interrupt. */
#if defined(ISP2100) || defined(ISP2200)
	response_index = qla2100_debounce_register(&reg->mailbox5);
	temp1 = RD_REG_WORD(&reg->semaphore);
	if (temp1 & BIT_0) {
		temp1 = RD_REG_WORD(&reg->mailbox0);
#else
	temp2 = RD_REG_WORD(&reg->host_status_hi);
	mailbox_int = 0;
	switch (data & 0xFF) {
	case ROM_MB_CMD_COMP:
	case ROM_MB_CMD_ERROR:
	case MB_CMD_COMP:
	case MB_CMD_ERROR:
	case ASYNC_EVENT:
		mailbox_int = 1;
		temp1 = temp2;
		break;
	case FAST_SCSI_COMP:
		mailbox_int = 1;
		temp1 = MBA_SCSI_COMPLETION;
		break;
	case RESPONSE_QUEUE_INT:
		response_index = temp2;
		goto response_queue_int;
		break;
	default:
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
		goto isr_end;
		break;
	}
	if (mailbox_int) {
#endif
		if (temp1 == MBA_SCSI_COMPLETION) {
#if defined(ISP2100) || defined(ISP2200)
			mailbox[1] = RD_REG_WORD(&reg->mailbox1);
#else
			mailbox[1] = temp2;
#endif
			mailbox[2] = RD_REG_WORD(&reg->mailbox2);
		} else {
			mailbox[0] = temp1;
			DEBUG3(printk("qla2100_isr: Saving return mbx data\n"););
			    /* Get mailbox data. */
	
			mptr = &mailbox[1];
			iptr = (uint16_t *) & reg->mailbox1;
			for (cnt = 1; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
#ifdef ISP2200
				if (cnt == 8)
					iptr = (uint16_t *) & reg->mailbox8;
#endif
				if (cnt == 4 || cnt == 5)
					*mptr = qla2100_debounce_register(iptr);
				else
					*mptr = RD_REG_WORD(iptr);
				mptr++;
				iptr++;
			}
		}

		/* Release mailbox registers. */
		WRT_REG_WORD(&reg->semaphore, 0);
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);

		DEBUG5(printk("qla2100_isr: mailbox interrupt mailbox[0] = %x\n",(u_long) temp1););

		/* Handle asynchronous event */
		switch (temp1) {
		case MBA_SCSI_COMPLETION:	/* Response completion */
			DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
			if (ha->flags.online) {
				/* Get outstanding command index. */
				index = (uint32_t) (mailbox[2] << 16 | mailbox[1]);
				/* Validate handle. */
				if (index < MAX_OUTSTANDING_COMMANDS)
					sp = ha->outstanding_cmds[index];
				else
					sp = NULL;
				if (sp) {
					/* Free outstanding command slot. */
					ha->outstanding_cmds[index] = NULL;

					sp->flags &= SRB_SENT;
					/* Save ISP completion status */
					sp->cmd->result = DID_OK;
					/* v2.19.5b2 Reset port down retry on success. */
					sp->port_down_retry_count = ha->port_down_retry_count;
					sp->fo_retry_cnt = 0;

					ha->actthreads--;
					add_to_done_queue(ha,sp);
		
				} else {
					DEBUG2(printk("scsi(%d): qla2100_isr: ISP invalid handle\n", (int) ha->host_no););
					printk(KERN_WARNING "qla2x00: ISP invalid handle");
					QLA2100_DPC_LOCK(ha);
					ha->dpc_flags |= ISP_ABORT_NEEDED;
					QLA2100_DPC_UNLOCK(ha);
				}
			}
			break;
		case MBA_RESET:	/* Reset */
			DEBUG2(printk("scsi(%d) qla2100_isr: asynchronous RESET\n", (int) ha->host_no););
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= RESET_MARKER_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
			break;
		case MBA_SYSTEM_ERR:	/* System Error */
			DEBUG2(printk("qla2100_isr: ISP System Error - mbx1=%x mbx2=%x mbx3=%x\n",
				(u_long) mailbox[1], (u_long) mailbox[2], (u_long) mailbox[3]););
			printk(KERN_WARNING
			       "qla2x00: ISP System Error - mbx1=%xh, mbx2=%xh, mbx3=%xh",
			       mailbox[1], mailbox[2], mailbox[3]);
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= ISP_ABORT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
			break;
		case MBA_REQ_TRANSFER_ERR:	/* Request Transfer Error */
			printk(KERN_WARNING "qla2x00: ISP Request Transfer Error\n");
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= ISP_ABORT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
			break;
		case MBA_RSP_TRANSFER_ERR:	/* Response Transfer Error */
			printk(KERN_WARNING "qla2100: ISP Response Transfer Error\n");
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= ISP_ABORT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
			break;
		case MBA_WAKEUP_THRES:	/* Request Queue Wake-up */
			DEBUG2(printk("qla2100_isr: asynchronous WAKEUP_THRES\n"));
			break;
		case MBA_LIP_OCCURRED:	/* Loop Initialization Procedure */
			if (unlikely(!qla2100_quiet))
				printk(KERN_INFO "scsi(%d): LIP occurred.\n", (int) ha->host_no);
			/* occurred. */
			DEBUG2(printk("qla2100_isr: asynchronous MBA_LIP_OCCURRED\n"););
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags = ha->dpc_flags | COMMAND_WAIT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);

			/* Save LIP sequence. */
			ha->lip_seq = mailbox[1];
			if (ha->loop_state != LOOP_DOWN) {
				ha->loop_state = LOOP_DOWN;
				ha->loop_down_timer = LOOP_DOWN_TIME;
			}
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= COMMAND_WAIT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_LIP_OCCURRED, NULL);
			}
			ha->total_lip_cnt++;
			break;
		case MBA_LOOP_UP:
			printk(KERN_INFO "scsi(%d): LOOP UP detected\n", (int) ha->host_no);
#if 0
			if (ha->operating_mode == P2P)
				ha->min_external_loopid = 1;	/* v2.19.5b3 */
			else {
				ha->operating_mode = LOOP;
				ha->min_external_loopid = SNS_FIRST_LOOP_ID;
			}
#endif
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_LOOP_UP, NULL);
			}
			ha->loop_state = LOOP_UP;
			break;
		case MBA_LOOP_DOWN:
			printk(KERN_INFO "scsi(%d): LOOP DOWN detected\n", (int) ha->host_no);
			DEBUG2(printk("scsi(%d) qla2100_isr: asynchronous MBA_LOOP_DOWN\n", ha->host_no););
			if (ha->loop_state != LOOP_DOWN) {
				ha->loop_state = LOOP_DOWN;
				ha->loop_down_timer = LOOP_DOWN_TIME;
			}
			/* no wait 10/19/2000 */
#if 0
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= COMMAND_WAIT_NEEDED;
			QLA2100_DPC_UNLOCK(ha);
#endif
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_LOOP_DOWN, NULL);
			}
			break;
		case MBA_LIP_RESET:	/* LIP reset occurred. */
			if (!qla2100_quiet)
				printk(KERN_INFO "scsi(%d): LIP reset occurred\n", (int) ha->host_no);
			DEBUG2(printk("scsi(%d) qla2100_isr: asynchronous MBA_LIP_RESET\n", ha->host_no););
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= COMMAND_WAIT_NEEDED;
			ha->dpc_flags |= RESET_MARKER_NEEDED;
			QLA2100_DPC_UNLOCK(ha);

			ha->loop_down_timer = LOOP_DOWN_TIME;
			ha->loop_state = LOOP_DOWN;
			ha->operating_mode = LOOP;
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_LIP_RESET, NULL);
			}
			break;
		case MBA_LINK_MODE_UP:	/* Link mode up. */
			DEBUG(printk("scsi(%d): Link node is up\n", (int) ha->host_no););
			DEBUG2(  printk("qla2100_isr: asynchronous MBA_LINK_MODE_UP\n"););
			/* 9/23 ha->dpc_flags = ha->dpc_flags | COMMAND_WAIT_NEEDED; */
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= COMMAND_WAIT_NEEDED;
			ha->dpc_flags |= RESET_MARKER_NEEDED;
			QLA2100_DPC_UNLOCK(ha);

			break;
		case MBA_UPDATE_CONFIG:	/* Update Configuration. */
			printk(KERN_INFO "scsi(%d): Configuration change detected: value %d.\n", (int) ha->host_no,
			       mailbox[1]);
			ha->loop_state = LOOP_DOWN;	/* dg - 03/30 */
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= LOOP_RESYNC_NEEDED | LOCAL_LOOP_UPDATE;
			QLA2100_DPC_UNLOCK(ha);
			break;
		case MBA_PORT_UPDATE:	/* Port database update occurred. */
			DEBUG(printk("scsi(%d): Port database changed\n", (int) ha->host_no););
			    /* 
			     * dg - 06/19/01 - Mark all devices as missing,
			     * so we will login again.
			     */
			ha->flags.rscn_queue_overflow = 1;
			DEBUG2(printk("scsi%d qla2100_isr: asynchronous MBA_PORT_UPDATE\n", ha->host_no););
			ha->loop_down_timer = 0;
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= LOOP_RESYNC_NEEDED | LOCAL_LOOP_UPDATE;
			QLA2100_DPC_UNLOCK(ha);
			/* 9/23 ha->flags.loop_resync_needed = TRUE; */
			ha->loop_state = LOOP_UPDATE;
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_PORT_UPDATE, NULL);
			}
			break;
		case MBA_SCR_UPDATE:	/* State Change Registration. */
			DEBUG2(printk("scsi%d qla2100_isr: asynchronous MBA_RSCR_UPDATE\n", ha->host_no));
			DEBUG(printk("scsi(%d): RSCN database changed - 0x%x,0x%x\n", (int) ha->host_no, mailbox[1], mailbox[2]));
			
			rscn_queue_index = ha->rscn_in_ptr + 1;
			if (rscn_queue_index == MAX_RSCN_COUNT)
				rscn_queue_index = 0;
			if (rscn_queue_index != ha->rscn_out_ptr) {
				ha->rscn_queue[ha->rscn_in_ptr].format = (uint8_t) (mailbox[1] >> 8);
				ha->rscn_queue[ha->rscn_in_ptr].d_id.b.domain = (uint8_t) mailbox[1];
				ha->rscn_queue[ha->rscn_in_ptr].d_id.b.area = (uint8_t) (mailbox[2] >> 8);
				ha->rscn_queue[ha->rscn_in_ptr].d_id.b.al_pa = (uint8_t) mailbox[2];
				ha->rscn_in_ptr = (uint8_t) rscn_queue_index;
			} else {
				ha->flags.rscn_queue_overflow = 1;
			}

			ha->loop_down_timer = 0;
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= LOOP_RESYNC_NEEDED | RSCN_UPDATE;
			QLA2100_DPC_UNLOCK(ha);
			ha->loop_down_timer = 0;

#if 0
			/* 9/23 ha->device_flags |= RSCN_UPDATE;
			   ha->dpc_flags = ha->dpc_flags | COMMAND_WAIT_NEEDED;
			   ha->flags.loop_resync_needed = TRUE; */
#endif
			ha->loop_state = LOOP_UPDATE;
			if (ha->ioctl->flags & IOCTL_AEN_TRACKING_ENABLE) {
				/* Update AEN queue. */
				qla2x00_enqueue_aen(ha, MBA_RSCN_UPDATE, &mailbox[0]);
			}
			break;
		case MBA_CTIO_COMPLETION:
			DEBUG2(printk("qla2100_isr: asynchronous MBA_CTIO_COMPLETION\n"););
			break;
		default:
			if (temp1 < MBA_ASYNC_EVENT) {

				memcpy((void *)&ha->mailbox_out[0],(void *)&mailbox[0], MAILBOX_REGISTER_COUNT);
				ha->flags.mbox_int = TRUE;
				DEBUG5(printk("qla2100_isr: Returning mailbox data\n"););
			}
			break;
		}
	} else
#ifdef ISP2300
	      response_queue_int:
#endif
	{
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
		/*
		   * Response ring
		 */
		if (ha->flags.online && !ha->flags.mbox_busy) {
			if (response_index < RESPONSE_ENTRY_CNT) {
				qla2x00_response_pkt(ha, response_index);
			} else {
				/* Invalid response pointer value. */
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
				DEBUG2(printk("qla2100_isr: Response Pointer " "Error. mb5=%x.\n", response_index););
			}
		}
		else {
			DEBUG2(printk(KERN_INFO "isr: loop down or busy.\n"););
		}
	}

#ifdef ISP2300
      isr_end:
#endif

	LEAVE("qla2100_isr");
}

/*
*  qla2100_rst_aen
*      Processes asynchronous reset.
*
* Input:
*      ha  = adapter block pointer.
*/
static void
qla2100_rst_aen(struct scsi_qla_host * ha) {
#if QL2100_TARGET_MODE_SUPPORT
	notify_entry_t nentry;
#endif				/* QL2100_TARGET_MODE_SUPPORT */

	ENTER("qla2100_rst_aen");

	if (ha->flags.online && !ha->flags.reset_active && !ha->loop_down_timer && !(ha->dpc_flags & ABORT_ISP_ACTIVE)) {
		/* 10/15 ha->flags.reset_active = TRUE; */
		do {
			ha->dpc_flags &= ~RESET_MARKER_NEEDED;

			/* Issue marker command. */
			qla2100_marker(ha, 0, 0, 0, MK_SYNC_ALL);

#if QL2100_TARGET_MODE_SUPPORT
			if (!ha->loop_down_timer && !(ha->dpc_flags & RESET_MARKER_NEEDED)) {
				/* Issue notify acknowledgement command. */
				memset(&nentry, 0, sizeof(notify_entry_t));
				nentry.initiator_id = ha->id;
				/* dg 7/3/99 nentry.target_id = ha->id; */
				nentry.task_flags = BIT_13;
				qla2100_notify_ack(ha, &nentry);
			}
#endif				/* QL2100_TARGET_MODE_SUPPORT */
		} while (!ha->loop_down_timer && (ha->dpc_flags & RESET_MARKER_NEEDED));
		/* 10/15 ha->flags.reset_active = FALSE; */
	}
	LEAVE("qla2100_rst_aen");
}

#if  QLA2100_TARGET_MODE_SUPPORT
/*
*  qla2100_atio_entry
*      Processes received ISP accept target I/O entry.
*
* Input:
*      ha  = adapter block pointer.
*      pkt = entry pointer.
*/
static void
 qla2100_atio_entry(struct scsi_qla_host * ha, atio_entry_t * pkt) {
	uint64_t *a64;
	uint64_t *end_a64;
	u_long phy_addr[2];
	u_long end_addr[2];
	uint32_t len;
	uint32_t offset;
	uint8_t t;
	uint8_t *sense_ptr;

	ENTER("qla2100_atio_entry: entered\n");

	t = pkt->initiator_id;
	sense_ptr = ha->tsense + t * TARGET_SENSE_SIZE;
	a64 = (uint64_t *) & phy_addr[0];
	end_a64 = (uint64_t *) & end_addr[0];

	switch (pkt->status & ~BIT_7) {
	case 7:		/* Path invalid */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
		printk("qla2100_atio_entry: Path invalid\n");
#endif
		break;
	case 0x16:		/* Requested Capability Not Available */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
		printk("qla2100_atio_entry: Requested Capability Not Available\n");
#endif
		break;
	case 0x17:		/* Bus Device Reset Message Received */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
		printk("qla2100_atio_entry: Bus Device Reset Message Received\n");
#endif
		break;
	case 0x3D:		/* CDB Received */

		/* Check for invalid LUN */
		if (pkt->lun && pkt->cdb[0] != SS_INQUIR && pkt->cdb[0] != SS_REQSEN)
			pkt->cdb[0] = SS_TEST;

		switch (pkt->cdb[0]) {
		case SS_TEST:
			DEBUG3(printk("qla2100_atio_entry: SS_TEST\n"););
			memset(sense_ptr,0, TARGET_SENSE_SIZE);
			len = 0;
			if (pkt->lun == 0)
				pkt->scsi_status = S_GOOD;
			else {
				*sense_ptr = 0x70;
				*(sense_ptr + 2) = SD_ILLREQ;
				*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
				*(sense_ptr + 12) = SC_INVLUN;
				pkt->scsi_status = S_CKCON;
			}
			pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
			break;
		case SS_REQSEN:
			DEBUG3(printk("qla2100_atio_entry: SS_REQSEN\n"););
			phy_addr[0] = ha->tsense_dma;
			phy_addr[1] = (ha->tsense_dma>>16)>>16;
			*a64 += t * TARGET_SENSE_SIZE;
			if (pkt->cdb[4] > TARGET_SENSE_SIZE)
				len = TARGET_SENSE_SIZE;
			else
				len = pkt->cdb[4];
			pkt->scsi_status = S_GOOD;
			pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_IN);
			break;
		case SS_INQUIR:
			DEBUG3(printk("qla2100_atio_entry: SS_INQUIR\n"););
			memset(sense_ptr,0, TARGET_SENSE_SIZE);
			phy_addr[0] = ha->tbuf_dma;
			phy_addr[1] = (ha->tbuf_dma>>16)>>16;
			*a64 += TARGET_INQ_OFFSET;

			if (pkt->lun == 0) {
				ha->tbuf->inq.id_type = ID_PROCESOR;
				ha->tbuf->inq.id_pqual = ID_QOK;
			} else {
				ha->tbuf->inq.id_type = ID_NODEV;
				ha->tbuf->inq.id_pqual = ID_QNOLU;
			}

			if (pkt->cdb[4] > sizeof(struct ident))
				len = sizeof(struct ident);
			else
				len = pkt->cdb[4];
			pkt->scsi_status = S_GOOD;
			pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_IN);
			break;
		case SM_WRDB:
			memset(sense_ptr,0, TARGET_SENSE_SIZE);
			offset = pkt->cdb[5];
			offset |= pkt->cdb[4] << 8;
			offset |= pkt->cdb[3] << 16;
			len = pkt->cdb[8];
			len |= pkt->cdb[7] << 8;
			len |= pkt->cdb[6] << 16;
			end_addr[0] = phy_addr[0] = ha->tbuf_dma&0xffffffff;
			end_addr[1] = phy_addr[1] = (ha->tbuf_dma>>16)>>16;
			*end_a64 += TARGET_DATA_OFFSET + TARGET_DATA_SIZE;
			switch (pkt->cdb[1] & 7) {
			case RW_BUF_HDATA:
				DEBUG3(printk("qla2100_atio_entry: SM_WRDB, RW_BUF_HDATA\n"););
				if (len > TARGET_DATA_SIZE + 4) {
					DEBUG2(printk("qla2100_atio_entry: SM_WRDB, length > buffer size\n"););
					*sense_ptr = 0x70;
					*(sense_ptr + 2) = SD_ILLREQ;
					*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
					*(sense_ptr + 12) = SC_ILLCDB;
					pkt->scsi_status = S_CKCON;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
					len = 0;
				} else if (len) {
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_OUT);
				} else {
					DEBUG2(printk("qla2100_atio_entry: SM_WRDB, zero length\n"););
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				}

				break;
			case RW_BUF_DATA:
				DEBUG3(printk("qla2100_atio_entry: SM_WRDB, RW_BUF_DATA\n"););
				*a64 += offset + TARGET_DATA_OFFSET;
				if (pkt->cdb[2] != 0 || *a64 >= *end_a64 || *a64 + len > *end_a64) {
					DEBUG2(printk("qla2100_atio_entry: SM_WRDB, RW_BUF_DATA BAD\n"););
					DEBUG2(printk("buf_id= %x, offset= %x, length=%x\n",
						(u_long) pkt->cdb[2], (u_long) offset, (u_long) len););
					*sense_ptr = 0x70;
					*(sense_ptr + 2) = SD_ILLREQ;
					*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
					*(sense_ptr + 12) = SC_ILLCDB;
					len = 0;
					pkt->scsi_status = S_CKCON;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				} else if (len) {
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_OUT);
				} else {
					DEBUG2(printk("qla2100_atio_entry: SM_WRDB, zero length\n"););
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				}
				break;
			default:
				DEBUG2(printk("qla2100_atio_entry: SM_WRDB unknown mode\n"););
				*sense_ptr = 0x70;
				*(sense_ptr + 2) = SD_ILLREQ;
				*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
				*(sense_ptr + 12) = SC_ILLCDB;
				len = 0;
				pkt->scsi_status = S_CKCON;
				pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				break;
			}
			break;
		case SM_RDDB:
			memset(sense_ptr,0, TARGET_SENSE_SIZE);
			offset = pkt->cdb[5];
			offset |= pkt->cdb[4] << 8;
			offset |= pkt->cdb[3] << 16;
			len = pkt->cdb[8];
			len |= pkt->cdb[7] << 8;
			len |= pkt->cdb[6] << 16;
			end_addr[0] = phy_addr[0] = ha->tbuf_dma&0xffffffff;
			end_addr[1] = phy_addr[1] = (ha->tbuf_dma>>16)>>16;
			*end_a64 += TARGET_DATA_OFFSET + TARGET_DATA_SIZE;
			switch (pkt->cdb[1] & 7) {
			case RW_BUF_HDATA:
				DEBUG3(printk("qla2100_atio_entry: SM_RDDB, RW_BUF_HDATA\n"););
				if (len) {
					ha->tbuf->hdr[0] = 0;
					ha->tbuf->hdr[1] = (uint8_t) (TARGET_DATA_SIZE >> 16);
					ha->tbuf->hdr[2] = (uint8_t) (TARGET_DATA_SIZE >> 8);
					ha->tbuf->hdr[3] = (uint8_t) TARGET_DATA_SIZE;
					if (len > TARGET_DATA_SIZE + 4)
						len = TARGET_DATA_SIZE + 4;
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_IN);
				} else {
					DEBUG2(printk("qla2100_atio_entry: SM_RDDB, zero length\n"););
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				}
				break;
			case RW_BUF_DATA:
				DEBUG3(printk("qla2100_atio_entry: SM_RDDB, RW_BUF_DATA\n"););
				*a64 += offset + TARGET_DATA_OFFSET;
				if (pkt->cdb[2] != 0 || *a64 >= *end_a64) {
					DEBUG2(printk("qla2100_atio_entry: SM_RDDB, RW_BUF_DATA BAD\n"););
					DEBUG2(printk("buf_id=%x, offset=%x\n", (u_long) pkt->cdb[2], (u_long) offset););
					*sense_ptr = 0x70;
					*(sense_ptr + 2) = SD_ILLREQ;
					*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
					*(sense_ptr + 12) = SC_ILLCDB;
					len = 0;
					pkt->scsi_status = S_CKCON;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				} else {
					if (*a64 + len > *end_a64)
						len = *end_a64 - *a64;
					if (len) {
						pkt->scsi_status = S_GOOD;
						pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_IN);
					} else {
						DEBUG2(printk("qla2100_atio_entry: SM_RDDB, zero length\n"););
						pkt->scsi_status = S_GOOD;
						pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
					}
				}
				break;
			case RW_BUF_DESC:
				DEBUG3(printk("qla2100_atio_entry: SM_RDDB, RW_BUF_DESC\n"););
				if (len) {
					if (len > 4)
						len = 4;

					ha->tbuf->hdr[0] = 0;
					if (pkt->cdb[2] != 0) {
						ha->tbuf->hdr[1] = 0;
						ha->tbuf->hdr[2] = 0;
						ha->tbuf->hdr[3] = 0;
					} else {
						ha->tbuf->hdr[1] = (uint8_t) (TARGET_DATA_SIZE >> 16);
						ha->tbuf->hdr[2] = (uint8_t) (TARGET_DATA_SIZE >> 8);
						ha->tbuf->hdr[3] = (uint8_t) TARGET_DATA_SIZE;
					}
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_DATA_IN);
				} else {
					DEBUG2(printk("qla2100_atio_entry: SM_RDDB, zero length\n"););
					pkt->scsi_status = S_GOOD;
					pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				}
				break;
			default:
				DEBUG2(printk("qla2100_atio_entry: SM_RDDB unknown mode\n"););
				*sense_ptr = 0x70;
				*(sense_ptr + 2) = SD_ILLREQ;
				*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
				*(sense_ptr + 12) = SC_ILLCDB;
				len = 0;
				pkt->scsi_status = S_CKCON;
				pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
				break;
			}
			break;
		default:
			DEBUG2(printk("qla2100_atio_entry: Unknown SCSI command\n"););
			DEBUG2(qla2100_dump_buffer((uint8_t *) & pkt->cdb[0], MAX_CMDSZ););
			memset(sense_ptr,0, TARGET_SENSE_SIZE);
			*sense_ptr = 0x70;
			*(sense_ptr + 2) = SD_ILLREQ;
			*(sense_ptr + 7) = TARGET_SENSE_SIZE - 8;
			*(sense_ptr + 12) = SC_INVOPCODE;
			len = 0;
			pkt->scsi_status = S_CKCON;
			pkt->flags = (uint16_t) (OF_SSTS | OF_INC_RC | OF_NO_DATA);
			break;
		}
		if (ha->flags.enable_64bit_addressing)
			qla2100_64bit_continue_io(ha, pkt, len, &phy_addr);
		else
			qla2100_32bit_continue_io(ha, pkt, len, &phy_addr);
		break;
	default:
		break;
	}

	LEAVE("qla2100_atio_entry: exiting normally\n");
}
#endif				/* QLA2100_TARGET_MODE_SUPPORT */

/* This gets called from the isr when ip-over-fc is done */
static void
qla2x00_response_pkt(struct scsi_qla_host * ha, uint16_t index) {
	device_reg_t *reg = ha->iobase;
	response_t *pkt;

	ENTER("qla2x00_response_pkt");

	while (ha->rsp_ring_index != index) {
		pkt = ha->response_ring_ptr;

		DEBUG5(printk("qla2x00_response_pkt: ha->rsp_ring_index=%d "
			      "index=%d.\n", (u_long) ha->rsp_ring_index, (u_long) index););
		DEBUG5(printk("qla2x00_response_pkt: response packet data:"););
		DEBUG5(qla2100_dump_buffer((uint8_t *) pkt, RESPONSE_ENTRY_SIZE););
#ifdef FC_IP_SUPPORT
		    /* Handle IP send completion */
		if (pkt->entry_type == ET_IP_COMMAND_64) {
			uint32_t lTagVal;
			SEND_CB *pSendCB;

			/* Set packet pointer from queue entry handle */
			if ((lTagVal = pkt->handle) < MAX_SEND_PACKETS) {
				if ((pSendCB = (SEND_CB *) ha->apActiveIpQueue[lTagVal]) != NULL) {
					ha->apActiveIpQueue[lTagVal] = NULL;

					/* Return send packet to IP driver */
					(*ha->pSendCompletionRoutine) (pSendCB);
				} else {
					/* Invalid handle from RISC, reset RISC firmware */
					printk(KERN_WARNING "qla2x00: bad IP send handle %x\n", lTagVal);
					QLA2100_DPC_LOCK(ha);
					ha->dpc_flags |= ISP_ABORT_NEEDED;
					QLA2100_DPC_UNLOCK(ha);
				}
			} else {
				/* Invalid handle from RISC, reset RISC firmware */
				printk(KERN_WARNING "qla2x00: bad IP send handle %x\n", lTagVal);
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
			}

			/* Adjust ring index. */
			ha->rsp_ring_index++;
			if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
				ha->rsp_ring_index = 0;
				ha->response_ring_ptr = ha->response_ring;
			} else
				ha->response_ring_ptr++;
#if defined(ISP2100) || defined(ISP2200)
			WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
#else
			WRT_REG_WORD(&reg->rsp_q_out, ha->rsp_ring_index);
#endif

			continue;
		}

		/* Handle IP receive packet */
		else if (pkt->entry_type == ET_IP_RECEIVE) {
			PIP_RECEIVE_ENTRY pIpReceiveEntry = (PIP_RECEIVE_ENTRY) pkt;
			PBUFFER_CB pBufferCB, pNextBufferCB;
			uint32_t lTagVal;
			uint32_t lPacketSize;
			uint16_t wBufferCount;
			uint32_t lReceiveBufferSize;

			/* If split buffer, set header size for 1st buffer */
			if (pIpReceiveEntry->wCompletionStatus & IP_REC_STATUS_SPLIT_BUFFER)
				lReceiveBufferSize = ha->wHeaderSize;
			else
				lReceiveBufferSize = ha->lReceiveBufferSize;

			if ((lTagVal = pIpReceiveEntry->waBufferHandle[0]) >= ha->wReceiveBufferCount) {
				/* Invalid handle from RISC, reset RISC firmware */
				printk(KERN_WARNING "qla2x00: bad IP buffer handle %x\n", lTagVal);
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
				goto InvalidIpHandle;
			}
			pBufferCB = &ha->pReceiveBufferCBs[lTagVal];

			if (!(pBufferCB->lFlags & BCB_FLAGS_RISC_OWNS_BUFFER)) {
				/* Invalid handle from RISC, reset RISC firmware */
				printk(KERN_WARNING "qla2x00: bad IP buffer handle %x\n", lTagVal);
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
				goto InvalidIpHandle;
			}

			/* Set buffer belongs to driver now */
			pBufferCB->lFlags &= ~BCB_FLAGS_RISC_OWNS_BUFFER;

			lPacketSize = pIpReceiveEntry->wSequenseLength;
			pBufferCB->lPacketSize = lPacketSize;
			pNextBufferCB = pBufferCB;

			for (wBufferCount = 1;; wBufferCount++) {
				if (lPacketSize > lReceiveBufferSize) {
					pNextBufferCB->lBufferSize = lReceiveBufferSize;
					lPacketSize -= lReceiveBufferSize;
					/* If split buffer, only use header size on 1st buffer */
					lReceiveBufferSize = ha->lReceiveBufferSize;

					if ((lTagVal = pIpReceiveEntry->waBufferHandle[wBufferCount]) >=
					    ha->wReceiveBufferCount) {
						/* Invalid handle from RISC, reset RISC firmware */
						printk(KERN_WARNING "qla2x00: bad IP buffer handle %x\n", lTagVal);
						QLA2100_DPC_LOCK(ha);
						ha->dpc_flags |= ISP_ABORT_NEEDED;
						QLA2100_DPC_UNLOCK(ha);
						goto InvalidIpHandle;
					}
					pNextBufferCB->pNextBufferCB = &ha->pReceiveBufferCBs[lTagVal];;
					pNextBufferCB = pNextBufferCB->pNextBufferCB;

					if (!(pNextBufferCB->lFlags & BCB_FLAGS_RISC_OWNS_BUFFER)) {
						/* Invalid handle from RISC, reset RISC firmware */
						printk(KERN_WARNING "qla2x00: bad IP buffer handle %x\n", lTagVal);
						QLA2100_DPC_LOCK(ha);
						ha->dpc_flags |= ISP_ABORT_NEEDED;
						QLA2100_DPC_UNLOCK(ha);
						goto InvalidIpHandle;
					}

					/* Set buffer belongs to driver now */
					pNextBufferCB->lFlags &= ~BCB_FLAGS_RISC_OWNS_BUFFER;
				} else {
					pNextBufferCB->lBufferSize = lPacketSize;
					pNextBufferCB->pNextBufferCB = NULL;
					break;
				}
			}
			/* Check for incoming ARP packet with matching IP address */
			if (pIpReceiveEntry->wServiceClass == 0) {
				PPACKET_HEADER pPacket = (PPACKET_HEADER) pBufferCB->pBuffer;
				PIP_DEVICE_BLOCK pIpDevice;
				uint8_t acPortId[3];

				/* Scan list of IP devices to see if login needed */
				for (pIpDevice = ha->pIpDeviceTop; pIpDevice != NULL;
				     pIpDevice = pIpDevice->pNextIpDevice) {
					if (*(uint16_t *) (&pIpDevice->acWorldWideName[2]) ==
					    pPacket->sNetworkHeader.wSourceAddrHigh &&
					    *(uint32_t *) (&pIpDevice->acWorldWideName[4]) ==
					    pPacket->sNetworkHeader.lSourceAddrLow) {
						/* Device already in IP list, skip login */
						goto SkipDeviceLogin;
					}
				}

				/* Device not in list, need to do login */
				acPortId[0] = pIpReceiveEntry->cS_IDHigh;
				acPortId[1] = (uint8_t) (pIpReceiveEntry->wS_IDLow >> 8);
				acPortId[2] = (uint8_t) pIpReceiveEntry->wS_IDLow;

				/* Make sure its not a local device */
				if (acPortId[0] == ha->port_id[0] && acPortId[1] == ha->port_id[1]) {
					goto SkipDeviceLogin;
				}

				if (qla2x00_add_new_ip_device(ha, PUBLIC_LOOP_DEVICE,
							      acPortId,
							      (uint8_t *) & pPacket->sNetworkHeader.wSourceNAA,
							      TRUE) == QL_STATUS_FATAL_ERROR) {
					/* Fatal error, reinitialize */
					QLA2100_DPC_LOCK(ha);
					ha->dpc_flags |= ISP_ABORT_NEEDED;
					QLA2100_DPC_UNLOCK(ha);
				}
			}
		      SkipDeviceLogin:
			/* Pass received packet to IP driver */
			pBufferCB->wBufferCount = wBufferCount;
			(*ha->pReturnReceivePacketsRoutine)
			    (ha->pReturnReceivePacketsContext, pBufferCB);

			/* Keep track of RISC buffer pointer (for IP reinit) */
			ha->wIpBufferOut += wBufferCount;
			if (ha->wIpBufferOut >= IP_BUFFER_QUEUE_DEPTH)
				ha->wIpBufferOut -= IP_BUFFER_QUEUE_DEPTH;
		      InvalidIpHandle:
			/* Adjust ring index. */
			ha->rsp_ring_index++;
			if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
				ha->rsp_ring_index = 0;
				ha->response_ring_ptr = ha->response_ring;
			} else
				ha->response_ring_ptr++;
#if defined(ISP2100) || defined(ISP2200)
			WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
#else
			WRT_REG_WORD(&reg->rsp_q_out, ha->rsp_ring_index);
#endif

			continue;
		}

		/* Handle IP FARP request */
		else if (pkt->entry_type == ET_IP_FARP_REQUEST) {
			PIP_FARP_REQUEST_ENTRY pIpFarpRequestEntry;
			uint8_t acPortId[3];
			uint8_t acPortName[8];

			pIpFarpRequestEntry = (PIP_FARP_REQUEST_ENTRY) pkt;
			acPortId[0] = pIpFarpRequestEntry->cRequesterPortIdHigh;
			acPortId[1] = (uint8_t) (pIpFarpRequestEntry->wRequesterPortIdLow >> 8);
			acPortId[2] = (uint8_t) pIpFarpRequestEntry->wRequesterPortIdLow;
			acPortName[0] = pIpFarpRequestEntry->acRequesterPortName[7];
			acPortName[1] = pIpFarpRequestEntry->acRequesterPortName[6];
			acPortName[2] = pIpFarpRequestEntry->acRequesterPortName[5];
			acPortName[3] = pIpFarpRequestEntry->acRequesterPortName[4];
			acPortName[4] = pIpFarpRequestEntry->acRequesterPortName[3];
			acPortName[5] = pIpFarpRequestEntry->acRequesterPortName[2];
			acPortName[6] = pIpFarpRequestEntry->acRequesterPortName[1];
			acPortName[7] = pIpFarpRequestEntry->acRequesterPortName[0];
			/* Login and add device to IP database */
			if (qla2x00_add_new_ip_device(ha, PUBLIC_LOOP_DEVICE,
						      acPortId, acPortName, TRUE) == QL_STATUS_FATAL_ERROR) {
				/* Fatal error, reinitialize */
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags |= ISP_ABORT_NEEDED;
				QLA2100_DPC_UNLOCK(ha);
			}

			/* Adjust ring index. */
			ha->rsp_ring_index++;
			if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
				ha->rsp_ring_index = 0;
				ha->response_ring_ptr = ha->response_ring;
			} else
				ha->response_ring_ptr++;
#if defined(ISP2100) || defined(ISP2200)
			WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
#else
			WRT_REG_WORD(&reg->rsp_q_out, ha->rsp_ring_index);
#endif
			continue;
		}
#endif				/* FC_IP_SUPPORT */

		ha->actthreads--;
		if (pkt->entry_status != 0) {
			DEBUG3(printk(KERN_INFO "qla2x00_response_pkt: process error entry.\n"););
			    qla2x00_error_entry(ha, pkt);
		} else {
			DEBUG3(printk(KERN_INFO "qla2x00_response_pkt: process response entry.\n"););

			    switch (pkt->entry_type) {
			case STATUS_TYPE:
				qla2x00_status_entry(ha, (sts_entry_t *) pkt);
				break;
/*
			case MS_IOCB_TYPE:
				qla2x00_ms_entry(ha, (ms_iocb_entry_t *)pkt);
				break;
*/
			default:
				/* Type Not Supported. */
				DEBUG2(printk(KERN_WARNING
					      "qla2x00_response_pkt: received unknown "
					      "response pkt type %x entry status=%x.\n",
					      pkt->entry_type, pkt->entry_status););
				    break;
			}
		}
		/* Adjust ring index. */
		ha->rsp_ring_index++;
		if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
			ha->rsp_ring_index = 0;
			ha->response_ring_ptr = ha->response_ring;
		} else {
			ha->response_ring_ptr++;
		}

#if defined(ISP2100) || defined(ISP2200)
		WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
#else
		WRT_REG_WORD(&reg->rsp_q_out, ha->rsp_ring_index);
#endif
	}			/* while (ha->rsp_ring_index != index) */

	DEBUG3(printk("qla2x00_response_pkt: exiting.\n"););
}

/*
*  qla2x00_status_entry
*      Processes received ISP status entry.
*
* Input:
*      ha           = adapter block pointer.
*      pkt          = entry pointer.
*/
static void
 qla2x00_status_entry(struct scsi_qla_host * ha, sts_entry_t * pkt) {
	uint32_t b, t, l;
	uint8_t sense_sz = 0;
	srb_t *sp, *sp1;
	struct os_lun *lq, *lq1;
#if 0
	os_tgt_t *tq, *tq1;
#else
	os_tgt_t *tq;
#endif

	Scsi_Cmnd *cp;
	unsigned long flags = 0;
	fc_port_t *fcport;
	struct list_head *list, *temp;

	ENTER("qla2100_status_entry");

	/* Validate handle. */
	if (pkt->handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[pkt->handle];
	else
		sp = 0;

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[pkt->handle] = 0;
		cp = sp->cmd;
		sp->ccode = pkt->comp_status;
		sp->scode = pkt->scsi_status;
		sp->flags &= ~SRB_SENT;

		/* Generate LU queue on cntrl, target, LUN */
		b = SCSI_BUS_32(cp);
		t = SCSI_TCN_32(cp);
		l = SCSI_LUN_32(cp);
		tq = sp->tgt_queue;
		lq = sp->lun_queue;

		/* Target busy */
		if (pkt->scsi_status & SS_BUSY_CONDITION) {
			DEBUG2(printk("qla2100_status_entry: SCSI busy status, scsi("););
			DEBUG2(printk("%d:%d:%d:%d)\n",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l););
			sp->retry_count--;
			if ((uint8_t) pkt->scsi_status == SS_RESERVE_CONFLICT) {
				cp->result = (int) (DID_OK << 16) | SS_RESERVE_CONFLICT;
			} else {
				cp->result = (int) (DID_BUS_BUSY << 16) | (pkt->scsi_status & 0xff);
			}
		} else if (ha->loop_down_timer || ha->loop_state != LOOP_READY) {
			DEBUG2(printk("scsi(%02d:%02d:%02d:%02d): Loop down - serial_number = %d, handle=0x%x\n",
			       (int) ha->host_no, (int) b, (int) t, (int) l, (int) sp->cmd->serial_number, pkt->handle););
			cp->result = (int) (DID_BUS_BUSY << 16);
		} else if (sp->port_down_retry_count > 1 &&
			   (pkt->comp_status == CS_INCOMPLETE ||
			    pkt->comp_status == CS_ABORTED ||
			    pkt->comp_status == CS_PORT_UNAVAILABLE ||
			    pkt->comp_status == CS_PORT_LOGGED_OUT ||
			    pkt->comp_status == CS_PORT_CONFIG_CHG || pkt->comp_status == CS_PORT_BUSY)) {
			/* if the port is unavaliable and we haven't exceeded the port down count */
			/* then send command back to the mid-level. */
			DEBUG2(printk("scsi(%d:%d:%d:%d)",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l););
			DEBUG(printk(": Port Down Retry count=%d, pid=%d, compl status=0x%x, q_flag=0x%x\n",
				      sp->port_down_retry_count, sp->cmd->serial_number, pkt->comp_status, lq->q_flag););
			sp->port_down_retry_count--;

			/* dg 08/17/99
			   * Force the SCSI layer to keep retrying until our
			   * port_down_retry_count expire.
			 */
			/* v2.19.14 */
			sp->cmd->result = DID_BUS_BUSY << 16;

			/* Acquire target queue lock */
			TGT_LOCK(tq);

			if (!(lq->q_flag & LUN_QUEUE_SUSPENDED)) {
				lq->q_flag = lq->q_flag | LUN_QUEUE_SUSPENDED;
				/* Decrement port down count. */
				if (ha->flags.link_down_error_enable) {
					if (tq->port_down_retry_count != 0)
						tq->port_down_retry_count--;
				}

				/* Decrement port down count on all pending commands. */
				/* and return them back to OS.                        */
				/* fixme(dg) */
				spin_lock_irqsave(&ha->list_lock, flags);
				list_for_each_safe(list, temp, &lq->cmd) {
					sp1 = list_entry(list, srb_t, list);

#if 0				/* requests are in the same Q */
					tq1 = sp1->tgt_queue;
					lq1 = sp1->lun_queue;

					if (ha->flags.link_down_error_enable) {
						if (tq1->port_down_retry_count != 0)
							tq1->port_down_retry_count--;
					}
#else
					lq1 = lq;
#endif
					/* Remove srb from LUN queue. */
					__del_from_cmd_queue(lq1,sp1);

					DEBUG2(printk("scsi(%d:%d:%d:%d)",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l););
					DEBUG(printk(": Q-Port Down Retry count=%d, pid=%d, compl status=0x%x, q_flag=0x%x\n",
						sp->port_down_retry_count,
						sp->cmd->serial_number, pkt->comp_status, lq->q_flag););

					/* Set ending status. */
					sp1->cmd->result = DID_BUS_BUSY << 16;
					/* SP get freed */
					sp1->cmd->host_scribble = NULL;
					__add_to_done_queue(ha, sp1);

				}
				spin_unlock_irqrestore(&ha->list_lock, flags);

				fcport = lq->fclun->fcport;
				if (PORT_DOWN(fcport) == 0)
					PORT_DOWN(fcport) = PORT_RETRY_TIME;
#if 0
				if (ha->queue_restart_timer == 0)
					ha->queue_restart_timer = PORT_RETRY_TIME;
#endif

				if (PORT_DOWN_RETRY(fcport) == 0) {
					PORT_DOWN_RETRY(fcport) = ha->port_down_retry_count * PORT_RETRY_TIME;
				}
			}

			/* Release target queue lock */
			TGT_UNLOCK(tq);

			DEBUG3(printk("scsi(%d:%d:%d:%d)",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l););
			DEBUG(printk(": Complete Port Down Retries=%d, pid=%d, compl status=0x%x, q_flag=0x%x\n",
				      sp->port_down_retry_count, sp->cmd->serial_number, pkt->comp_status, lq->q_flag););

			/* Set ending status. */

			/* Release LU queue specific lock */
		} else {
#ifdef QL_DEBUG_LEVEL_2
			if (pkt->comp_status) {
				printk("qla2100_status_entry: Compl error = %x, scsi(", (u_long) pkt->comp_status);
				printk("%d:%d:%d:%d)",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l);
				printk(", retry count= %d, pid= %x \n", ((u_long) sp->port_down_retry_count,
						(u_long) cp->serial_number);
			}
#endif
			/* Set ISP completion status and arget status byte. */
			cp->result = qla2100_return_status(ha, pkt, cp);

			memset(cp->sense_buffer, 0, sizeof(cp->sense_buffer));

			DEBUG3(printk("qla2100_status_entry: pre-check: scsi_status (0x%x)\n", (u_long) pkt->scsi_status););

			if (pkt->scsi_status & SS_CHECK_CONDITION) {
				/* Mid-level always zero sense buffer before giving it to us */
				if (pkt->scsi_status & SS_SENSE_LEN_VALID) {
					if (pkt->req_sense_length < sizeof(cp->sense_buffer))
						sense_sz = pkt->req_sense_length;
					else
						sense_sz = sizeof(cp->sense_buffer) - 1;

					memcpy( cp->sense_buffer, &pkt->req_sense_data, sense_sz);
					qla2x00_check_sense(cp, lq);
				}
				DEBUG2(printk("qla2100_status_entry: Check condition Sense data, scsi("););
				DEBUG2(printk("%d:%d:%d:%d)\n",(u_long) ha->host_no, (u_long) b, (u_long) t, (u_long) l););
				if (sense_sz)
					DEBUG2(qla2100_dump_buffer(cp->sense_buffer, sense_sz););
			}
		}
		/* Place command on done queue. */
		add_to_done_queue(ha, sp);
	} else {
		printk(KERN_WARNING "qla2x00: Status Entry invalid handle");

		QLA2100_DPC_LOCK(ha);
		ha->dpc_flags |= ISP_ABORT_NEEDED;
		QLA2100_DPC_UNLOCK(ha);
		up(ha->dpc_wait);
	}
	LEAVE("qla2100_status_entry");
}

/*
*  qla2x00_error_entry
*      Processes error entry.
*
* Input:
*      ha           = adapter block pointer.
*      pkt          = entry pointer.
*/
static void
 qla2x00_error_entry(struct scsi_qla_host * ha, response_t * pkt) {
	srb_t *sp;
#ifdef DPC_LOCK
	unsigned long cpu_flags = 0;
#endif

	ENTER("qla2100_error_entry");

#ifdef QL_DEBUG_LEVEL_2
	if (pkt->entry_status & BIT_5)
		printk("qla2100_error_entry: Invalid Entry Order\n");
	else if (pkt->entry_status & BIT_4)
		printk("qla2100_error_entry: Invalid Entry Count\n");
	else if (pkt->entry_status & BIT_3)
		printk("qla2100_error_entry: Invalid Entry Parameter\n");
	else if (pkt->entry_status & BIT_2)
		printk("qla2100_error_entry: Invalid Entry Type\n");
	else if (pkt->entry_status & BIT_1)
		printk("qla2100_error_entry: Busy\n");
	else
		printk("qla2100_error_entry: UNKNOWN flag error\n");
#endif

	/* Validate handle. */
	if (pkt->handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[pkt->handle];
	else
		sp = 0;

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[pkt->handle] = 0;

		sp->flags &= ~SRB_SENT;
		/* Bad payload or header */
		if (pkt->entry_status & (BIT_5 + BIT_4 + BIT_3 + BIT_2)) {
			/* Bad payload or header, set error status. */
			sp->cmd->result = (int) DID_ERROR << 16;

		} else if (pkt->entry_status & BIT_1 && sp->retry_count) {	/* FULL flag */
			sp->retry_count--;
			sp->cmd->result = (int) DID_BUS_BUSY << 16;
		} else {
			/* Set error status. */
			sp->cmd->result = (int) DID_ERROR << 16;
		}
		/* Place command on done queue. */
		add_to_done_queue(ha, sp);
	} else if (pkt->entry_type == COMMAND_A64_TYPE || pkt->entry_type == COMMAND_TYPE) {
		printk(KERN_WARNING "qla2x00: Error Entry invalid handle");
		QLA2100_DPC_LOCK(ha);
		ha->dpc_flags |= ISP_ABORT_NEEDED;
		QLA2100_DPC_UNLOCK(ha);
		up(ha->dpc_wait);
	}
	LEAVE("qla2100_error_entry");
}

#if 0
/*
*  qla2x00_restart_watchdog_queues
*      Restart device queues.
*
* Input:
*      ha = adapter block pointer.
*/
static void
 qla2100_restart_watchdog_queue(struct scsi_qla_host * ha) {
	srb_t *sp;
	struct list_head *list, *temp;
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->retry_queue) {
		sp = list_entry(list, srb_t, list);
		/* when time expire return request back to OS as BUSY */
		__del_from_retry_queue(ha, sp);
		sp->cmd->result = DID_BUS_BUSY << 16;
		sp->cmd->host_scribble = NULL;
		__add_to_done_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);
}
#endif

/*
 *  qla2x00_restart_queues
 *	Restart device queues.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
void
 qla2x00_restart_queues(struct scsi_qla_host * ha, uint8_t flush) {
	os_tgt_t *tq;
	struct os_lun *lq;
	uint16_t t, l;
	srb_t *sp;
	int cnt = 0;
	struct list_head *list, *temp;
	unsigned long flags = 0;

	ENTER("qla2x00_restart_queues");

	ha->flags.restart_queues_needed = FALSE;
	/*
	 * start all queues working again.
	 */
	for (t = 0; t < ha->max_targets; t++) {
		/* Test for target. */
		if ((tq = TGT_Q(ha, t)) == NULL)
			continue;

		for (l = 0; l < ha->max_luns; l++) {
			lq = LUN_Q(ha, t, l);
			if (lq == NULL)
				continue;

			/* Acquire target queue lock. */
			TGT_LOCK(tq);

			lq->q_flag = lq->q_flag & ~LUN_QUEUE_SUSPENDED;

			if (!list_empty(&lq->cmd))
				qla2x00_next(ha, tq, lq);
			else
				/* Release target queue lock */
				TGT_UNLOCK(tq);
		}
	}

	/*
	 * Clear out our retry queue
	 */
	if (flush) {
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &ha->retry_queue) {
			sp = list_entry(list, srb_t, list);
			/* when time expire return request back to OS as BUSY */
			__del_from_retry_queue(ha, sp);
			sp->cmd->result = DID_BUS_BUSY << 16;
			sp->cmd->host_scribble = NULL;
			__add_to_done_queue(ha, sp);
		}
		spin_unlock_irqrestore(&ha->list_lock, flags);
 		cnt = qla2100_done(ha);
		DEBUG2(printk("qla2100_restart_queues: callback %d commands.\n", cnt););
	}
	LEAVE("qla2x00_restart_queues");
}

/*
 *  qla2x00_abort_queues
 *	Abort all commands on queues on device
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
static void
 qla2x00_abort_queues(struct scsi_qla_host * ha, uint8_t doneqflg) {

	os_tgt_t *tq;
	struct os_lun *lq;
	uint32_t t, l;
	srb_t *sp;
	struct list_head *list, *temp;
	unsigned long flags;

	ENTER("qla2100_abort_queues");

	ha->flags.abort_queue_needed = FALSE;
	/* Return all commands device queues. */
	for (t = 0; t < ha->max_targets; t++) {
		/* Test for target. */
		if ((tq = TGT_Q(ha, t)) == NULL)
			continue;

		for (l = 0; l < MAX_LUNS; l++) {
			lq = LUN_Q(ha, t, l);
			if (lq == NULL)
				continue;

			/* Acquire target queue lock. */
			TGT_LOCK(tq);

			/*
			 * Set BUS BUSY error status for
			 * all commands on LUN queue.
			 */
			spin_lock_irqsave(&ha->list_lock, flags);
			list_for_each_safe(list, temp, &lq->cmd) {
				sp = list_entry(list, srb_t, list);

				if (sp->flags & SRB_ABORT)
					continue;

				/* Remove srb from LUN queue. */
				__del_from_cmd_queue(lq, sp);

				/* Set ending status. */
				sp->cmd->result = DID_BUS_BUSY << 16;
				if (doneqflg) {
					sp->cmd->host_scribble = NULL;
				}
				__add_to_done_queue(ha, sp);			}
			spin_unlock_irqrestore(&ha->list_lock, flags);
			/* Release target queue lock */
			TGT_UNLOCK(tq);
		}
	}
	/* Abort one command at a time for system. */
	if (!doneqflg)
		qla2100_done(ha);

	LEAVE("qla2100_abort_queues");
}

#if MPIO_SUPPORT

/*
 *  qla2x00_failover_cleanup
 *	Cleanup queues after a failover.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
static void
 qla2x00_failover_cleanup(struct scsi_qla_host * os_ha, os_tgt_t * tq, struct os_lun * lq, srb_t * sp) {

#if 0
	srb_t *tsp;
	ql_list_link_t *link1;
	struct os_lun *orig_lq;
	struct scsi_qla_host *ha;


	/*
	   * Sync up failover count on the varies queues.
	 */
	TGT_LOCK(tq);
	for (link1 = lq->cmd.first; link1 != NULL; link1 = link1->next) {
		tsp = link1->base_address;
		tsp->fo_retry_cnt = sp->fo_retry_cnt + 1;
	}

	/*
	   * the retry queue.
	 */
	ha = tq->ha;
	for (tsp = ha->retry_q_first; (tsp); tsp = sp_next) {
		sp_next = tsp->s_next;
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = sp->fo_retry_cnt + 1;
	}

	/*
	 * the all done queues - we don't know which have our requests.
	 */
	for (ha = qla2100_hostlist; (ha != NULL); ha = ha->next) {
		for (tsp = ha->done_q_first; (tsp); tsp = sp_next) {
			sp_next = tsp->s_next;
			orig_lq = tsp->lun_queue;
			if (orig_lq == lq)
				tsp->fo_retry_cnt = sp->fo_retry_cnt + 1;
		}
	}

	TGT_UNLOCK(tq);
#endif
	sp->cmd->result = DID_BUS_BUSY << 16;
	sp->cmd->host_scribble = NULL;
	/* turn-off all failover flags */
	sp->flags = sp->flags & ~(SRB_RETRY | SRB_FAILOVER | SRB_FO_CANCEL);
}

/*
 *  qla2x00_process_failover
 *	Process any command on the failover queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
static void
 qla2x00_process_failover(struct scsi_qla_host * ha) {

	os_tgt_t *tq;
	struct os_lun *lq;
	srb_t *sp;

	fc_port_t *fcport;
	struct list_head *list, *temp;
	unsigned long flags;

	DEBUG(printk("qla2x00_process_failover - for hba %d\n", (int) ha->instance););
	    /*
	     * Process all the commands in the failover
	     * queue. Attempt to failover then either
	     * complete the command as is
	     * or requeue for retry.
	     */
	    /* Prevent or allow acceptance of new I/O requests. */
	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(list, srb_t, list);

		tq = sp->tgt_queue;
		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;

		/* Remove srb from failover queue. */
		__del_from_failover_queue(ha, sp);

		DEBUG2(printk("qla2x00_process_failover: pid %ld\n", sp->cmd->serial_number););
		    /* Select an alternate path */
		    /* v5.21b16  - path has already been change by a previous request */
#if 0
		DEBUG2(printk("qla2x00_failover: ha=%d, pid=%d, fc cur_path=%d, sp cur_path=%d\n",
				   (int) ha->instance, sp->cmd->serial_number, fcport->cur_path, sp->cur_path););
		if (sp->cur_path != fcport->cur_path) {
#else
		    /*
		     * if path changed after the bad path was detected
		     * then no need of changing paths again.
		     */
		if (sp->fclun != lq->fclun ||
			((sp->err_id == 1 || sp->err_id == 3) && fcport->loop_id != FC_NO_LOOP_ID)) {
#endif
			qla2x00_failover_cleanup(ha, tq, lq, sp);
		} else if (qla2x00_cfg_failover(ha, lq->fclun, tq, sp) == NULL) {
			/*
			 * We ran out of paths, so just
			 * post the status which is already
			 * set in the cmd.
			 */
			printk(KERN_INFO "qla2x00_process_failover: Ran out of paths - pid %ld\n",
			       sp->cmd->serial_number);
			sp->fstate = 4;
		} else {
			sp->fstate = 5;

			qla2x00_failover_cleanup(ha, tq, lq, sp);

			lq->q_flag &= ~QLA2100_QSUSP;
		}
		__add_to_done_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	qla2x00_restart_queues(ha, TRUE);
	qla2100_done(ha);
	DEBUG(printk("qla2x00_process_failover - done"));
}
#endif

#if 0
/*
 * qla2x00_bus_reset
 *	Issue loop reset.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
 qla2x00_bus_reset(struct scsi_qla_host * ha) {
	fc_port_t *fcport;
	int rval = QLA2X00_SUCCESS;

	ENTER("qla2x00_bus_reset: entered");

	if (ha->flags & ENABLE_LIP_RESET) {
		rval = qla2x00_lip_reset(ha);

		/* Issue marker command. */
		qla2x00_marker(ha, 0, 0, MK_SYNC_ALL);
	}

	if (rval == QLA2X00_SUCCESS && ha->flags & ENABLE_TARGET_RESET) {
		for (fcport = ha->fcport; fcport != NULL; fcport = fcport->next) {
			qla2x00_target_reset(fcport->ha, fcport->loop_id);
			qla2x00_marker(fcport->ha, fcport->loop_id, 0, MK_SYNC_ID);
		}
	}

	if (rval == QLA2X00_SUCCESS &&
	    (!(ha->flags & (ENABLE_TARGET_RESET | ENABLE_LIP_RESET)) || ha->flags & ENABLE_LIP_FULL_LOGIN)) {
		rval = qla2x00_full_login_lip(ha);

		/* Issue marker command. */
		qla2x00_marker(ha, 0, 0, MK_SYNC_ALL);
	}

	if (rval != QLA2X00_SUCCESS) {
		 /*EMPTY*/ QL_PRINT_2_3("qla2x00_bus_reset(", ha->instance, QDBG_DEC_NUM, QDBG_NNL);
		QL_PRINT_2_3("): failed = ", rval, QDBG_HEX_NUM, QDBG_NL);
	} else {
		 /*EMPTY*/ LEAVE("qla2x00_bus_reset: exiting normally");
	}

	return rval;
}

/*
 *  qla2x00_srb_abort
 *	Aborts SRB
 *
 * Input:
 *	arg = srb pointer.
 *	os_stat = status to be returned to OS.
 *
 * Context:
 *	Kernel context.
 */
void
 qla2x00_srb_abort(void *arg, int os_stat) {
	os_tgt_t *tq;
	struct os_lun *lq;
	srb_t *sp = (srb_t *) arg;
	struct scsi_qla_host *ha;

	 ENTER("qla2x00_srb_abort");

	/* Acquire target queue lock. */
	 lq = sp->lun_queue;
	 tq = sp->tgt_queue;
	 TGT_LOCK(tq);

	/* Remove srb from LUN queue. */
	 qla2x00_remove_link(&lq->cmd, &sp->s_cmd);
	 lq->q_incnt--;

	/* Release target queue lock. */
	 TGT_UNLOCK(tq);

	/* Set ending status. */
	 ha = tq->ha;
	 sp->cmd->result = os_stat << 16;
	 sp->cmd->host_scribble = NULL;
	 add_to_done_queue(ha, sp);

	/* Call done routine to handle completions. */
	 sp->cmd.next = NULL;
	 qla2x00_done(&sp->cmd);

	 LEAVE("qla2x00_srb_abort: exiting normally");
}
#endif
/*
 *  qla2100_loop_resync
 *      Resync with fibre channel devices.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success
 *
 * This function sleeps
 */ 

static uint8_t
qla2100_loop_resync(struct scsi_qla_host * ha) {
	uint8_t status;
#ifdef DPC_LOCK
	unsigned long cpu_flags = 0;
#endif

	ENTER("qla2100_loop_resync");

	ha->loop_state = LOOP_UPDATE;
	if (ha->flags.online) {
		if (!(status = qla2100_fw_ready(ha))) {
			do {
				/* v2.19.05b6 */
				ha->loop_state = LOOP_UPDATE;

				/* Issue marker command. */
				qla2100_marker(ha, 0, 0, 0, MK_SYNC_ALL);

				/* Remap devices on Loop. */
				QLA2100_DPC_LOCK(ha);
				ha->dpc_flags &= ~LOOP_RESYNC_NEEDED;
				QLA2100_DPC_UNLOCK(ha);

				qla2100_configure_loop(ha);

			} while (!ha->loop_down_timer && (ha->dpc_flags & LOOP_RESYNC_NEEDED));
		}
		/* v2.19 - we don't want to call this if we are already
		 * in the loop resync code
		 */
		qla2x00_restart_queues(ha, TRUE);
	} else
		status = 0;

	/* Restart queues that may have been stopped. */
	/* 04/10 if( !ha->loop_down_timer ) {
	   qla2x00_restart_queues(ha,TRUE);
	   } */
	/* v2.19 */

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("qla2100_loop_resync: **** FAILED ****\n");
	else
		LEAVE("qla2100_loop_resync");
#endif
	return status;
}

/*
 * qla2100_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
uint16_t qla2100_debounce_register(volatile uint16_t * addr) {
	volatile uint16_t ret;
	volatile uint16_t ret2;

	do {
		ret = RD_REG_WORD(addr);
		barrier();
		ret2 = RD_REG_WORD(addr);
	} while (ret != ret2);

	return ret;
}

/* qla2100_cmd_wait
 *	Stall driver until all outstanding commands are returned.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Return;
 *  0 -- Done
 *  1 -- continue;
 *
 * Context:
 *	Kernel context.
 */
static void
 qla2100_cmd_wait(struct scsi_qla_host * ha) {

	uint32_t cnt;
	uint16_t index;
#ifdef DPC_LOCK
	unsigned long cpu_flags = 0;
#endif
	srb_t *psrb;
	fc_port_t *fcport;
	unsigned long time1;

	ENTER("qla2100_cmd_wait:");

	/* Wait for all outstanding commands to be returned. */
	cnt = 30;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		if (ha->outstanding_cmds[index] == NULL) {
			continue;
		} else if (cnt != 0) {
			/*
			 * If the command is for a fabric device and
			 * device is NOT dead, continue to wait.
			 */
			psrb = ha->outstanding_cmds[index];
			fcport = psrb->lun_queue->fclun->fcport;
			if ((fcport->flags & FC_FABRIC_DEVICE) && (fcport->state != FC_DEVICE_DEAD)) {
				continue;
			}

			/* Wait 1 second. */

			current->state = TASK_UNINTERRUPTIBLE;
			time1 = schedule_timeout(HZ);

			if ((ha->dpc_flags & COMMAND_WAIT_NEEDED) && (ha->dpc_flags & LOOP_RESYNC_NEEDED)) {
				DEBUG3(printk("qla2x00_cmd_wait(%ld): pending loop resync\n", ha->instance));
				/*
				 * No need to proceed. Let the next cmd_wait
				 * kick in and pick up the rest.
				 */
				break;
			}

			index = 0;
			cnt--;
		} else {
			DEBUG2(printk("qla2x00_cmd_wait(%ld): timeout\n", ha->instance););
			break;
		}
	}

	LEAVE("qla2100_cmd_wait");
}

/*
 * qla2100_reset_chip
 *      Reset ISP chip.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
 qla2100_reset_chip(struct scsi_qla_host * ha) {
	uint32_t cnt;
	device_reg_t *reg = ha->iobase;

	ENTER("qla2100_reset_chip");

	/* Disable ISP interrupts. */
	qla2100_disable_intrs(ha);
	/* WRT_REG_WORD(&reg->ictrl, 0); */

#if 1
	/* Pause RISC. */
	WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
	if (ha->device_id == QLA2312_DEVICE_ID)
		udelay(10);
	else {
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->host_cmd) & HC_RISC_PAUSE) != 0)
				break;
			else
				udelay(100);
		}
	}

	/* Select FPM registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0x20);

	/* FPM Soft Reset. */
	WRT_REG_WORD(&reg->fpm_diag_config, 0x100);
#ifdef ISP2300
	WRT_REG_WORD(&reg->fpm_diag_config, 0x0);	/* Toggle Fpm Reset */
#endif

	/* Select frame buffer registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0x10);

	/* Reset frame buffer FIFOs. */
	WRT_REG_WORD(&reg->fb_cmd, 0xa000);

	/* Select RISC module registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0);

	/* Reset RISC module. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);

	/* Reset ISP semaphore. */
	WRT_REG_WORD(&reg->semaphore, 0);

	/* Release RISC module. */
	WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);

	if (ha->device_id == QLA2312_DEVICE_ID)
		udelay(10);
	else {
		/* Wait for RISC to recover from reset. */
		for (cnt = 0; cnt < 30000; cnt++) {
			if (RD_REG_WORD(&reg->mailbox0) != MBS_BUSY)
				break;
			else
				udelay(100);
		}
	}

	/* Disable RISC pause on FPM parity error. */
	WRT_REG_WORD(&reg->host_cmd, HC_DISABLE_PARITY_PAUSE);
#else
	/* Insure mailbox registers are free. */
	WRT_REG_WORD(&reg->semaphore, 0);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);

	/* clear mailbox busy */
	ha->flags.mbox_busy = FALSE;

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, ISP_RESET);

	/*
	   * Delay after reset, for chip to recover.
	   * Otherwise causes system PANIC
	 */
	mdelay(2);

	for (cnt = 30000; cnt; cnt--) {
		if (!(RD_REG_WORD(&reg->ctrl_status) & ISP_RESET))
			break;
		udelay(100);
	}

	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
	WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
	for (cnt = 30000; cnt; cnt--) {
		if (RD_REG_WORD(&reg->mailbox0) != MBS_BUSY)
			break;
		udelay(100);
	}
#endif

	LEAVE("qla2100_reset_chip");
}

/*
 * qla2x00_get_port_database
 *	Issue enhanced get port database mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	dev = structure pointer.
 *	opt = mailbox 1 option byte.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t qla2x00_get_port_database(struct scsi_qla_host * ha, fcdev_t * dev, uint8_t opt) {
	uint8_t rval = 0;
	port_database_t *pd;
	dma_addr_t phys_address;	/* Physical address. */
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2x00_get_port_database:");
/* 4.10 */
	pd = pci_alloc_consistent(ha->pdev, PORT_DATABASE_SIZE, &phys_address);

	if (pd == NULL) {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - get_port_database", (int) ha->host_no);
		ha->mem_err++;
		return 2;
	}
	memset(pd, 0, PORT_DATABASE_SIZE);

	mb[0] = MBC_GET_PORT_DATABASE;
	mb[1] = dev->loop_id << 8 | opt;
	mb[2] = MSW(phys_address);
	mb[3] = LSW(phys_address);
	mb[6] = QL21_64BITS_4THWD(phys_address);
	mb[7] = QL21_64BITS_3RDWD(phys_address);
	if (!qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0])) {

		/* Get d_id of device. */
		/*
		   dev->d_id.b.al_pa = pd->port_id[2];
		   dev->d_id.b.area = pd->port_id[3];
		   dev->d_id.b.domain = pd->port_id[0];
		   dev->d_id.b.rsvd_1 = 0;
		 */

		/* Get initiator status of device. */
		pd->prli_svc_param_word_3[0] & BIT_5 ? (dev->flag |= DEV_INITIATOR) : (dev->flag &= ~DEV_INITIATOR);
	} else {
		printk(KERN_WARNING "qla2x00_get_port_database: failed");
		rval = 1;
	}
	pci_free_consistent(ha->pdev, PORT_DATABASE_SIZE, pd, phys_address);

	LEAVE("qla2x00_get_port_database:");

	return rval;
}

/*
 * This routine will wait for fabric devices for
 * the reset delay.
 *
 * This functions sleeps
 */
void qla2100_check_fabric_devices(struct scsi_qla_host * ha) {
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	mb[0] = MBC_GET_FIRMWARE_STATE;
	qla2100_mailbox_command(ha, BIT_0, &mb[0]);
	if (ha->dpc_flags & LOOP_RESYNC_NEEDED) {
		ha->dpc_flags &= ~LOOP_RESYNC_NEEDED;
		if (!(ha->dpc_flags & LOOP_RESYNC_ACTIVE)) {
			ha->dpc_flags |= LOOP_RESYNC_ACTIVE;
			qla2100_loop_resync(ha);
			ha->dpc_flags &= ~LOOP_RESYNC_ACTIVE;
		}
	}
}

/*
 * qla2100_extend_timeout
 *      This routine will extend the timeout to the specified value.
 *
 * Input:
 *      cmd = SCSI command structure
 *
 * Returns:
 *      None.
 */
static void qla2100_extend_timeout(Scsi_Cmnd * cmd, int timeout) {
	if (cmd->eh_timeout.function) {
		del_timer(&cmd->eh_timeout);
		cmd->eh_timeout.expires = jiffies + timeout;
		add_timer(&cmd->eh_timeout);
		cmd->done_late = 0;
	}
}
/*
* qla2100_display_fc_names
*      This routine will the node names of the different devices found
*      after port inquiry.
*
* Input:
*      cmd = SCSI command structure
*
* Returns:
*      None.
*/ static void qla2100_display_fc_names(struct scsi_qla_host * ha) {
	uint16_t index;

	/* Display the node name for adapter */
	printk(KERN_INFO
	       "scsi-qla%d-adapter-node=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
	       (int) ha->instance,
	       ha->init_cb->node_name[0],
	       ha->init_cb->node_name[1],
	       ha->init_cb->node_name[2],
	       ha->init_cb->node_name[3],
	       ha->init_cb->node_name[4],
	       ha->init_cb->node_name[5], ha->init_cb->node_name[6], ha->init_cb->node_name[7]);

	/* display the port name for adapter */
	printk(KERN_INFO
	       "scsi-qla%d-adapter-port=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
	       (int) ha->instance,
	       ha->init_cb->port_name[0],
	       ha->init_cb->port_name[1],
	       ha->init_cb->port_name[2],
	       ha->init_cb->port_name[3],
	       ha->init_cb->port_name[4],
	       ha->init_cb->port_name[5], ha->init_cb->port_name[6], ha->init_cb->port_name[7]);

	/* Print out device port names */
	for (index = 0; index < MAX_FIBRE_DEVICES; index++) {
		if (ha->fc_db[index].loop_id == PORT_UNUSED)
			continue;

		printk(KERN_INFO "scsi-qla%d-target-%d=%02x%02x%02x%02x%02x%02x%02x%02x;\n", (int) ha->instance, index,
#if USE_PORTNAME
		       ha->fc_db[index].wwn[0],
		       ha->fc_db[index].wwn[1],
		       ha->fc_db[index].wwn[2],
		       ha->fc_db[index].wwn[3],
		       ha->fc_db[index].wwn[4],
		       ha->fc_db[index].wwn[5], ha->fc_db[index].wwn[6], ha->fc_db[index].wwn[7]);
#else
		       ha->fc_db[index].name[0],
		       ha->fc_db[index].name[1],
		       ha->fc_db[index].name[2],
		       ha->fc_db[index].name[3],
		       ha->fc_db[index].name[4],
		       ha->fc_db[index].name[5], ha->fc_db[index].name[6], ha->fc_db[index].name[7]);
#endif
	}
}

/*
 * qla2100_find_propname
 *	Get property in database.
 *
 * Input:
 *	ha = adapter structure pointer.
 *      db = pointer to database
 *      propstr = pointer to dest array for string
 *	propname = name of property to search for.
 *	siz = size of property
 *
 * Returns:
 *	0 = no property
 *      size = index of property
 *
 * Context:
 *	Kernel context.
 */
static uint8_t qla2100_find_propname(struct scsi_qla_host * ha, char *propname, char *propstr, char *db, int siz) {
	char *cp;

	/* find the specified string */
	if (db) {
		/* find the property name */
		DEBUG(printk("qla2100_find_propname: Searching for propname={%s}\n", propname););
		if ((cp = strstr(db, propname)) != NULL) {
			while ((*cp) && *cp != '=')
				cp++;
			if (*cp) {
				strncpy(propstr, cp, siz + 1);
				propstr[siz + 1] = '\0';
				DEBUG(printk("qla2100_find_propname: found property = {%s} \n", propstr););
				return siz;	/* match */
			}
		}
		DEBUG(printk("qla2100_find_propname: ** property not found\n"););
	}
	return 0;
}

/*
 * qla2100_get_prop_16chars
 *	Get an 8-byte property value for the specified property name by
 *      converting from the property string found in the configuration file.
 *      The resulting converted value is in big endian format (MSB at byte0).
 *
 * Input:
 *	ha = adapter state pointer.
 *	propname = property name pointer.
 *	propval  = pointer to location for the converted property val.
 *      db = pointer to database
 *
 * Returns:
 *	0 = value returned successfully.
 *
 * Context:
 *	Kernel context.
 */
static int
 qla2100_get_prop_16chars(struct scsi_qla_host * ha, char *propname, char *propval, char *db) {
	char *propstr;
	int i, k;
	int rval;
	uint8_t nval;
	uint8_t *pchar;
	uint8_t *ret_byte;
	uint8_t *tmp_byte;
	uint8_t *retval = (uint8_t *) propval;
	uint8_t tmpval[8] = {
	0, 0, 0, 0, 0, 0, 0, 0};
	uint16_t max_byte_cnt = 8;	/* 16 chars = 8 bytes */
	uint16_t max_strlen = 16;
	char buf[LINESIZE];

	rval = qla2100_find_propname(ha, propname, buf, db, max_strlen);

	propstr = &buf[0];
	if (*propstr == '=')
		propstr++;	/* ignore equal sign */

	if (rval == 0) {
		return 1;
	}

	/* Convert string to numbers. */

	pchar = (uint8_t *) propstr;
	tmp_byte = (uint8_t *) tmpval;

	rval = 0;
	for (i = 0; i < max_strlen; i++) {
		/*
		 * Check for invalid character, two at a time,
		 * then convert them starting with first byte.
		 */

		if ((pchar[i] >= '0') && (pchar[i] <= '9')) {
			nval = pchar[i] - '0';
		} else if ((pchar[i] >= 'A') && (pchar[i] <= 'F')) {
			nval = pchar[i] - 'A' + 10;
		} else if ((pchar[i] >= 'a') && (pchar[i] <= 'f')) {
			nval = pchar[i] - 'a' + 10;
		} else {
			/* invalid character */
			rval = 1;
			break;
		}

		if (i & BIT_0) {
			*tmp_byte = *tmp_byte | nval;
			tmp_byte++;
		} else {
			*tmp_byte = *tmp_byte | nval << 4;
		}
	}

	if (rval != 0) {
		/* Encountered invalid character. */
		return rval;
	}

	/* Copy over the converted value. */

	ret_byte = retval;
	tmp_byte = tmpval;

	i = max_byte_cnt;
	k = 0;
	while (i--) {
		*ret_byte++ = *tmp_byte++;
	}

	/* big endian retval[0]; */
	return 0;
}

/*
* qla2100_get_properties
*	Find all properties for the specified adapeter in
*      command line.
*
* Input:
*	ha = adapter block pointer.
*	cmdline = pointer to command line string
*
* Context:
*	Kernel context.
*/
static void
 qla2100_get_properties(struct scsi_qla_host * ha, char *cmdline) {
	char propbuf[LINESIZE];
	int tmp_rval;
	uint16_t tgt;
	uint8_t tmp_name[8];

	/* Adapter FC node names. */
	 sprintf(propbuf, "scsi-qla%d-adapter-node", (int) ha->instance);
	 qla2100_get_prop_16chars(ha, propbuf, (caddr_t) (&ha->init_cb->node_name), cmdline);

	 sprintf(propbuf, "scsi-qla%d-adapter-port", (int) ha->instance);


	/* DG 04/07 check portname of adapter */
	 qla2100_get_prop_16chars(ha, propbuf, (caddr_t) (tmp_name), cmdline);
	if (memcmp(&ha->init_cb->port_name[0], &tmp_name[0], 8) != 0) {
		/*
		 * Adapter port name is WWN, and cannot be changed.
		 * Inform users of the mismatch, then just continue driver
		 * loading using the original adapter port name in NVRAM.
		 */
		printk(KERN_WARNING "qla2x00: qla%ld found mismatch in adapter port names.\n", ha->instance);
		printk(KERN_INFO
		       "       qla%ld port name found in NVRAM -> %02x%02x%02x%02x%02x%02x%02x%02x\n",
		       	ha->instance,
		       	ha->init_cb->port_name[0], ha->init_cb->port_name[1],
		       	ha->init_cb->port_name[2], ha->init_cb->port_name[3],
		   	ha->init_cb->port_name[4], ha->init_cb->port_name[5], 
			ha->init_cb->port_name[6], ha->init_cb->port_name[7]);
		printk(KERN_INFO
		       "      qla%ld port name found on command line -> %02x%02x%02x%02x%02x%02x%02x%02x\n",
		       ha->instance,
		       tmp_name[0],
		       tmp_name[1], tmp_name[2], tmp_name[3], tmp_name[4], tmp_name[5], tmp_name[6], tmp_name[7]);
		printk(KERN_INFO "      Using port name from NVRAM.\n");
	}

	/* FC name for devices */
	for (tgt = 0; tgt < MAX_FIBRE_DEVICES; tgt++) {
		sprintf(propbuf, "scsi-qla%d-target-%d", (int) ha->instance, tgt);

		tmp_rval = qla2100_get_prop_16chars(ha, propbuf, tmp_name, cmdline);
		if (tmp_rval == 0) {
			/* Got a name for this ID. */

			/* Save to appropriate fields. */
#if  USE_PORTNAME		/* updated for ioctl merge */
			memcpy( ha->fc_db[tgt].wwn,tmp_name, 8);
#else
			memcpy( ha->fc_db[tgt].name,tmp_name, 8);
#endif
			ha->fc_db[tgt].loop_id = PORT_AVAILABLE;
			ha->fc_db[tgt].flag = 0;	/* v2.19.05b3 */
			ha->fc_db[tgt].flag |= DEV_CONFIGURED;
			DEBUG(printk("Target %d - configured by user: ", tgt););
			    DEBUG(printk("scsi-target=\"%02x%02x%02x%02x%02x%02x%02x%02x\"\n", tmp_name[0], tmp_name[1], tmp_name[2], tmp_name[3], tmp_name[4], tmp_name[5], tmp_name[6], tmp_name[7]););	/*ioctl support change */
		}
	}
}

/*
 * qla2100_update_fc_database
 *      This routine updates the device data in the database.
 *
 * Input:
 *      ha = adapter block pointer.
 *      device = device data pointer.
 *
 * Returns:
 *      0 = success, if device found or added to database.
 *      BIT_0 = error
 *      BIT_1 = database was full and device was not configured.
 */
static uint8_t qla2100_update_fc_database(struct scsi_qla_host * ha, fcdev_t * device, uint8_t enable_slot_reuse) {
	uint16_t cnt, i;

	DEBUG(printk("qla2x00: Found device - nodename=%02x%02x%02x%02x%02x%02x%02x%02x, "
		     "portname=%02x%02x%02x%02x%02x%02x%02x%02x, port Id=%06x, "
		     "loop id=%04x\n",
		     device->name[0], device->name[1],
		     device->name[2], device->name[3],
		     device->name[4], device->name[5],
		     device->name[6], device->name[7],
		     device->wwn[0], device->wwn[1],
		     device->wwn[2], device->wwn[3],
		     device->wwn[4], device->wwn[5], device->wwn[6], device->wwn[7], device->d_id.b24,
		     device->loop_id););

	    /* Look for device in database. */
	    for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
#if USE_PORTNAME
		if (memcmp(device->wwn, ha->fc_db[cnt].wwn, 8) == 0) {
#else
		if (memcmp(device->name, ha->fc_db[cnt].name, 8) == 0) {
#endif
			DEBUG(printk("qla2x00: Reusing slot %d for device "
				     "%02x%02x%02x%02x%02x%02x%02x%02x\n",
				     cnt,
				     device->wwn[0], device->wwn[1],
				     device->wwn[2], device->wwn[3],
				     device->wwn[4], device->wwn[5], device->wwn[6], device->wwn[7]););
			    if (device->flag == DEV_PUBLIC) {
				ha->fc_db[cnt].flag |= DEV_PUBLIC;
			} else {
				if (ha->fc_db[cnt].flag & DEV_PUBLIC) {
					ha->fc_db[cnt].flag &= ~DEV_PUBLIC;
					ha->fabricid[ha->fc_db[cnt].loop_id].in_use = FALSE;
				}
			}
			ha->fc_db[cnt].loop_id = device->loop_id;
			ha->fc_db[cnt].d_id.b24 = device->d_id.b24;
			return 0;
		}
	}

	/* Find a empty slot and add device into database. */
	for (i = 0; i < MAX_FIBRE_DEVICES; i++)
		if ((ha->fc_db[i].loop_id == PORT_UNUSED) || (ha->fc_db[i].loop_id == PORT_NEED_MAP)) {
			DEBUG(printk("qla2x00: New slot %d for device "
				     "%02x%02x%02x%02x%02x%02x%02x%02x\n",
				     i,
				     device->wwn[0], device->wwn[1],
				     device->wwn[2], device->wwn[3],
				     device->wwn[4], device->wwn[5], device->wwn[6], device->wwn[7]););
			    memcpy( ha->fc_db[i].name,device->name, 8);
			memcpy( ha->fc_db[i].wwn,device->wwn, 8);
			ha->fc_db[i].loop_id = device->loop_id;
			ha->fc_db[i].d_id.b24 = device->d_id.b24;
			if (device->flag == DEV_PUBLIC)
				ha->fc_db[i].flag |= DEV_PUBLIC;
			ha->flags.updated_fc_db = TRUE;
			return 0;
		}

	if (enable_slot_reuse) {
		for (i = 0; i < MAX_FIBRE_DEVICES; i++)
			if (ha->fc_db[i].loop_id == PORT_AVAILABLE) {
				DEBUG(printk("qla2x00: Assigned slot %d reuse for device "
					     "%02x%02x%02x%02x%02x%02x%02x%02x\n",
					     i, device->wwn[0], device->wwn[1],
					     device->wwn[2], device->wwn[3],
					     device->wwn[4], device->wwn[5], device->wwn[6], device->wwn[7]););
				    memcpy( ha->fc_db[i].name,device->name, 8);
				memcpy( ha->fc_db[i].wwn,device->wwn, 8);
				ha->fc_db[i].loop_id = device->loop_id;
				ha->fc_db[i].d_id.b24 = device->d_id.b24;
				if (device->flag == DEV_PUBLIC)
					ha->fc_db[i].flag |= DEV_PUBLIC;
				ha->flags.updated_fc_db = TRUE;
				return 0;
			}
	}
	return BIT_1;
}

/*
 * qla2100_device_resync
 *	Marks devices in the database that needs resynchronization.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
static void
 qla2100_device_resync(struct scsi_qla_host * ha) {
	uint16_t index;
	uint32_t mask;
	rscn_t dev;

	ENTER("qlc2200_device_resync: entered\n");

	while (ha->rscn_out_ptr != ha->rscn_in_ptr || ha->flags.rscn_queue_overflow) {
		QLA2100_INTR_LOCK(ha);
		memcpy( (void *) &dev,(void *) &ha->rscn_queue[ha->rscn_out_ptr], sizeof(rscn_t));
		DEBUG(printk("qla%ld: device_resync: rscn_queue[%d], portID=%06x\n",
			     ha->instance, ha->rscn_out_ptr, ha->rscn_queue[ha->rscn_out_ptr].d_id.b24););
		
		ha->rscn_out_ptr++;
		if (ha->rscn_out_ptr == MAX_RSCN_COUNT)
			ha->rscn_out_ptr = 0;
		/* Queue overflow, set switch default case. */
		if (ha->flags.rscn_queue_overflow) {
			DEBUG(printk("device_resync: rscn overflow\n"););
			    dev.format = 3;
			ha->flags.rscn_queue_overflow = 0;
		}

		switch (dev.format) {
		case 0:
			mask = 0xffffff;
			break;
		case 1:
			mask = 0xffff00;
			break;
		case 2:
			mask = 0xff0000;
			break;
		default:
			mask = 0x0;
			dev.d_id.b24 = 0;
			ha->rscn_out_ptr = ha->rscn_in_ptr;
			break;
		}
		QLA2100_INTR_UNLOCK(ha);

		for (index = 0; index < MAX_FIBRE_DEVICES; index++) {
			if (ha->fc_db[index].flag & DEV_PUBLIC && (ha->fc_db[index].d_id.b24 & mask) == dev.d_id.b24) {
				if (ha->fc_db[index].loop_id <= LAST_SNS_LOOP_ID) {
					ha->fc_db[index].loop_id |= PORT_LOST_ID;
					DEBUG(printk("qla%ld: RSCN: port lost @ slot %d %06x\n",
						     ha->instance, index, ha->fc_db[index].d_id.b24););
				}
			}
		}
	}

	LEAVE("qlc2200_device_resync: exiting normally\n");
}

/*
 * qla2100_configure_fabric
 *      Setup SNS devices with loop ID's.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 *      BIT_0 = error
 *      BIT_1 = database was full and device was not configured.
 */
#define MAX_PUBLIC_LOOP_IDS LAST_SNS_LOOP_ID + 1

static uint8_t qla2100_configure_fabric(struct scsi_qla_host * ha, uint8_t enable_slot_reuse) {
	uint8_t rval = 0;
	uint8_t rval1;
	uint8_t local_flags = 0;
	sns_data_t *sns;
	static uint16_t mb[MAILBOX_REGISTER_COUNT];
	fcdev_t dev;
	uint16_t i, index;
	dma_addr_t phys_address = 0;
	uint16_t new_dev_cnt;
	static struct new_dev new_dev_list[MAX_FIBRE_DEVICES];
#if 0
	unsigned long cpu_flags = 0;
#endif

	ENTER("qla2x00_configure_fabric: entered\n");
	DEBUG(printk("scsi%ld: qla2x00_configure_fabric: hba=%p\n", ha->host_no, ha););

	/* If FL port exists, then SNS is present */
	mb[0] = MBC_GET_PORT_NAME;
	mb[1] = SNS_FL_PORT << 8;
	mb[2] = 0;
	mb[3] = 0;
	mb[6] = 0;
	mb[7] = 0;
	rval1 = qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
	if (rval1 || (mb[2] == 0 && mb[3] == 0 && mb[6] == 0 && mb[7] == 0)) {
		DEBUG2(printk("qla2100_configure_fabric: MBC_GET_PORT_NAME Failed, No FL Port\n"););
		return 0;
	}

	/* Get adapter port ID. */
	mb[0] = MBC_GET_ADAPTER_LOOP_ID;
	qla2100_mailbox_command(ha, BIT_0, &mb[0]);
	ha->d_id.b.domain = (uint8_t) mb[3];
	ha->d_id.b.area = (uint8_t) (mb[2] >> 8);
	ha->d_id.b.al_pa = (uint8_t) mb[2];

/* 4.10 */
	sns = pci_alloc_consistent(ha->pdev, sizeof(sns_data_t), &phys_address);

	if (sns == NULL) {
		printk(KERN_WARNING "qla(%d): Memory Allocation failed - sns", (int) ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}
	memset(sns,0, sizeof(sns_data_t));

	/* Mark devices that need re-synchronization. */
	qla2100_device_resync(ha);

	do {
		rval = qla2100_find_all_fabric_devs(ha, sns, phys_address, new_dev_list, &new_dev_cnt, &local_flags);
		if (rval != 0)
			break;

		/*
		 * Logout all previous devices not currently in database
		 * and mark them available.
		 */
		for (index = 0; index < MAX_FIBRE_DEVICES && !ha->loop_down_timer
		     && !(ha->dpc_flags & LOOP_RESYNC_NEEDED); index++) {
			if (ha->fc_db[index].loop_id & PORT_LOST_ID && (ha->fc_db[index].flag & DEV_PUBLIC)) {
				qla2100_fabric_logout(ha, ha->fc_db[index].loop_id & 0xff);
				local_flags |= LOGOUT_PERFORMED;
			}
		}
		/*
		 * Wait for all remaining IO's to finish if there was logout.
		 */
		if (local_flags & LOGOUT_PERFORMED) {
			local_flags &= ~LOGOUT_PERFORMED;
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags |= COMMAND_WAIT_ACTIVE;
			QLA2100_DPC_UNLOCK(ha);
			qla2100_cmd_wait(ha);
			QLA2100_DPC_LOCK(ha);
			ha->dpc_flags &= ~COMMAND_WAIT_ACTIVE;
			QLA2100_DPC_UNLOCK(ha);
		}

		/*
		 * Scan through our database and login entries already
		 * in our database.
		 */
		for (index = 0; index < MAX_FIBRE_DEVICES && !ha->loop_down_timer
		     && !(ha->dpc_flags & LOOP_RESYNC_NEEDED); index++) {
			if (!(ha->fc_db[index].loop_id & PORT_LOGIN_NEEDED))
				continue;
			ha->fc_db[index].loop_id &= ~PORT_LOGIN_NEEDED;
			if (ha->fc_db[index].loop_id <= LAST_SNS_LOOP_ID) {
				/* loop_id reusable */
				dev.loop_id = ha->fc_db[index].loop_id & 0xff;
			} else {
				for (i = ha->min_external_loopid; i < MAX_PUBLIC_LOOP_IDS; i++) {
					if (!ha->fabricid[i].in_use) {
						ha->fabricid[i].in_use = TRUE;
						dev.loop_id = i;
						break;
					}
				}
				if (i == MAX_PUBLIC_LOOP_IDS)
					break;
			}
			dev.d_id.b24 = ha->fc_db[index].d_id.b24;
			/* login and update database */
			if (qla2100_fabric_login(ha, &dev) == 0)
				ha->fc_db[index].loop_id = dev.loop_id;
		}

		/*
		 * Scan through new device list and login and add to our
		 * database.
		 */
		for (index = 0; index < new_dev_cnt && !ha->loop_down_timer
		     && !(ha->dpc_flags & LOOP_RESYNC_NEEDED); index++) {
			memcpy( (void *) &dev,(void *) &new_dev_list[index], sizeof(struct new_dev));
			dev.flag = DEV_PUBLIC;
			for (i = ha->min_external_loopid; i < MAX_PUBLIC_LOOP_IDS; i++) {
				if (!ha->fabricid[i].in_use) {
					ha->fabricid[i].in_use = TRUE;
					dev.loop_id = i;
					break;
				}
			}
			if (i == MAX_PUBLIC_LOOP_IDS)
				break;
			if (qla2100_fabric_login(ha, &dev) == 0) {
				if ((rval = qla2100_update_fc_database(ha, &dev, enable_slot_reuse))) {
					qla2100_fabric_logout(ha, dev.loop_id);
					ha->fabricid[i].in_use = FALSE;
					break;
				}
			}
		}
	} while (0);
	pci_free_consistent(ha->pdev, sizeof(sns_data_t), sns, phys_address);

	if (rval != 0) 
		DEBUG2(printk("qla2100_configure_fabric: exit, rval = %d\n", (uint32_t) rval););
	
	return rval;
}

/*
 * qla2100_find_all_fabric_devs
 *	Go through GAN list to find all fabric devices.  Will perform
 *	necessary logout of previously existed devices that have changed
 *	and save new devices in a new device list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dev = database device entry pointer.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = error.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
    qla2100_find_all_fabric_devs(struct scsi_qla_host * ha, sns_data_t * sns,
				 dma_addr_t phys_addr, struct new_dev *new_dev_list, uint16_t * new_dev_cnt,
				 uint8_t * flags) {
	fcdev_t first_dev, dev;
	uint8_t rval = 0;
	uint16_t i;
	uint16_t index;
	uint16_t new_cnt;
	uint16_t public_count;
	uint16_t initiator;

	ENTER("qla2100_find_all_fabric_devs: entered\n");
	DEBUG(printk("scsi(%d): qla2100_find_all_fabric_devs\n", (int) ha->host_no););
	 
	ha->max_public_loop_ids = MAX_PUBLIC_LOOP_IDS;

	/*
	 * Loop getting devices from switch.
	 * Issue GAN to find all devices out there.
	 * Logout the devices that were in our database but changed
	 * port ID.
	 */
	/* Calculate the max number of public ports */
	public_count = ha->max_public_loop_ids - ha->min_external_loopid + 2;

	/* Set start port ID scan at adapter ID. */
	dev.d_id.b24 = 0;
	first_dev.d_id.b24 = 0;

	new_cnt = 0;		/* new device count */

	for (i = 0; i < public_count && !ha->loop_down_timer && !(ha->dpc_flags & LOOP_RESYNC_NEEDED); i++) {
		/* Send GAN to the switch */
		rval = 0;
		if (qla2100_gan(ha, sns, phys_addr, &dev)) {
			rval = rval | BIT_0;
			break;
		}

		/* If wrap on switch device list, exit. */
		if (dev.d_id.b24 == first_dev.d_id.b24)
			 break;

		DEBUG(printk("qla%ld: found fabric(%d) - port Id=%06x\n", ha->instance, i, dev.d_id.b24););
		    if (first_dev.d_id.b24 == 0)
			first_dev.d_id.b24 = dev.d_id.b24;

		/* If port type not equal to N or NL port, skip it. */
		if (sns->p.rsp[16] != 1 && sns->p.rsp[16] != 2) 
			continue;	/* needed for McData switch */
		

		/* Bypass if host adapter. */
		if (dev.d_id.b24 == ha->d_id.b24)
			continue;

		/* Bypass reserved domain fields. */
		if ((dev.d_id.b.domain & 0xf0) == 0xf0)
			continue;

		/* Bypass if same domain and area of adapter. */
		if ((dev.d_id.b24 & 0xffff00) == (ha->d_id.b24 & 0xffff00))
			continue;

		/* Bypass if initiator */
		initiator = FALSE;
		for (index = 0; index < ha->next_host_slot; index++)
#if USE_PORTNAME
			if (memcmp(dev.wwn, ha->host_db[index].wwn, 8) == 0) {
#else
			if (memcmp(dev.name, ha->host_db[index].name, 8) == 0) {
#endif
				initiator = TRUE;
				DEBUG(printk
				      ("qla%ld: host entry - nodename %02x%02x%02x%02x%02x%02x%02x%02x port Id=%06x\n",
				       ha->instance, dev.name[0], dev.name[1], dev.name[2], dev.name[3], dev.name[4],
				       dev.name[5], dev.name[6], dev.name[7], dev.d_id.b24););
				    break;
			}
		if (initiator)
			continue;

		/* Locate matching device in database. */
		for (index = 0; index < MAX_FIBRE_DEVICES; index++) {
#if USE_PORTNAME
			if (memcmp(dev.wwn, ha->fc_db[index].wwn, 8) == 0) {
#else
			if (memcmp(dev.name, ha->fc_db[index].name, 8) == 0) {
#endif
				DEBUG3(printk
				       ("qla%ld: find_fabric [%d]: found match in db, flags= 0x%x, loop= 0x%04x, port=%06x\n",
					ha->instance, i, ha->fc_db[index].flag, ha->fc_db[index].loop_id,
					ha->fc_db[index].d_id.b24));
				if (!(ha->fc_db[index].flag & DEV_PUBLIC)) {
					/*
					 * This was in our database as a local
					 * device. Here we assume this device
					 * either has changed location so
					 * configure_local_loop has already
					 * done necessary clean up, or it's
					 * saved here due to persistent name
					 * binding. We'll just add it in
					 * as a fabric device.
					 */
					/* Copy port id and name fields. */
					ha->fc_db[index].flag |= DEV_PUBLIC;
					ha->fc_db[index].d_id.b24 = dev.d_id.b24;
					ha->fc_db[index].loop_id |= PORT_LOGIN_NEEDED;
					break;
				}
				/* This was in our database as a fabric device. */
				if ((ha->fc_db[index].d_id.b24 == dev.d_id.b24) &&
				    (ha->fc_db[index].loop_id <= LAST_SNS_LOOP_ID))
					/* Device didn't change */
					break;

				if (ha->fc_db[index].loop_id == PORT_AVAILABLE) {
					ha->fc_db[index].flag |= DEV_PUBLIC;
					ha->fc_db[index].d_id.b24 = dev.d_id.b24;
					ha->fc_db[index].loop_id |= PORT_LOGIN_NEEDED;
					break;
				}

				/*
				 * Port ID changed or device was
				 * marked to be updated; logout and
				 * mark it for relogin later.
				 */
				qla2100_fabric_logout(ha, ha->fc_db[index].loop_id & 0xff);
				ha->fc_db[index].flag |= DEV_PUBLIC;
				ha->fc_db[index].d_id.b24 = dev.d_id.b24;
				ha->fc_db[index].loop_id |= PORT_LOGIN_NEEDED;
				ha->fc_db[index].loop_id &= ~PORT_LOST_ID;
				*flags |= LOGOUT_PERFORMED;
				break;
			}
		}

		if (index == MAX_FIBRE_DEVICES) {
			/*
			 * Did not find a match in our database.
			 * This is a new device.
			 */
			memcpy( (void *) &new_dev_list[new_cnt],(void *) &dev, sizeof(struct new_dev));
			new_cnt++;
		}
	}

	*new_dev_cnt = new_cnt;

	if (new_cnt > 0)
		ha->device_flags |= DFLG_FABRIC_DEVICES;

	DEBUG3(printk("qla2100_find_all_fabric_devs: exit, rval = %d\n", (uint32_t) rval););
	return rval;
}

/*
 * qla2100_gan
 *	Issue Get All Next (GAN) Simple Name Server (SNS) command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	dev = FC device type pointer.
 *
 * Returns:
 *	qla2100 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t qla2100_gan(struct scsi_qla_host * ha, sns_data_t * sns, dma_addr_t phys_addr, fcdev_t * dev) {
	uint8_t rval;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2100_gan: entered\n");
	/* Get port ID for device on SNS. */
	memset((caddr_t) sns,0, sizeof(sns_data_t));
	sns->p.req.hdr.buffer_length = 304;
	sns->p.req.hdr.buffer_address[0] = LS_64BITS(phys_addr);
	sns->p.req.hdr.buffer_address[1] = MS_64BITS(phys_addr);
	sns->p.req.hdr.subcommand_length = 6;
	sns->p.req.subcommand = 0x100;	/* GA_NXT */
	sns->p.req.param[6] = dev->d_id.b.al_pa;
	sns->p.req.param[7] = dev->d_id.b.area;
	sns->p.req.param[8] = dev->d_id.b.domain;

	mb[0] = MBC_SEND_SNS_COMMAND;
	mb[1] = 14;
	mb[3] = LSW(phys_addr);
	mb[2] = MSW(phys_addr);
	mb[7] = QL21_64BITS_3RDWD(phys_addr);
	mb[6] = QL21_64BITS_4THWD(phys_addr);
	rval = BIT_0;
	if (!qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0])) {
		if (sns->p.rsp[8] == 0x80 && sns->p.rsp[9] == 0x2) {
			dev->d_id.b.al_pa = sns->p.rsp[19];
			dev->d_id.b.area = sns->p.rsp[18];
			dev->d_id.b.domain = sns->p.rsp[17];
			dev->flag = DEV_PUBLIC;

			/* Save FC name */
			memcpy( dev->name,&sns->p.rsp[284], 8);

			/* Extract portname */
			memcpy( dev->wwn,&sns->p.rsp[20], 8);

			DEBUG3(printk("qla2x00: gan entry - portname %02x%02x%02x%02x%02x%02x%02x%02x port Id=%06x\n",
				      sns->p.rsp[20],
				      sns->p.rsp[21],
				      sns->p.rsp[22],
				      sns->p.rsp[23],
				      sns->p.rsp[24], sns->p.rsp[25], sns->p.rsp[26], sns->p.rsp[27], dev->d_id.b24););
			rval = 0;
		}
	} else {
		DEBUG2(printk("qla2100_gan: Failed Performing a GAN mb0=0x%x, mb1=0x%x rsp=%02x%02x.\n",
			      mb[0], mb[1], sns->p.rsp[8], sns->p.rsp[9]););
	}

	if (rval != 0)
		DEBUG2(printk("qla2x00_gan: exit, rval = %d\n", rval););
	return rval;
}

/*
 * qla2100_fabric_login
 *	Issue fabric login command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	device = pointer to FC device type structure.
 *
 * Returns:
 *      0 - Login successfully
 *      1 - Login failed
 *      2 - Initiator device
 */
static uint8_t qla2100_fabric_login(struct scsi_qla_host * ha, fcdev_t * device) {
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	for (;;) {
		DEBUG(printk("Trying Fabric Login w/loop id= 0x%04x for "
			     " port %06x\n", device->loop_id, device->d_id.b24););
		mb[0] = MBC_LOGIN_FABRIC_PORT;
		mb[1] = device->loop_id << 8 | 0x01;
		mb[2] = device->d_id.b.domain;
		mb[3] = device->d_id.b.area << 8 | device->d_id.b.al_pa;
		qla2100_mailbox_command(ha, BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0]);
		if (mb[0] == 0x4007) {
			ha->fabricid[device->loop_id].in_use = FALSE;
			device->loop_id = mb[1];
			DEBUG(printk("Fabric Login: port in use - next loop id= 0x%04x, "
				     " port Id=%06x\n", device->loop_id, device->d_id.b24););
			if (device->loop_id <= LAST_SNS_LOOP_ID)
				ha->fabricid[device->loop_id].in_use = TRUE;
			else
				return 1;
		} else if (mb[0] == 0x4000) {
			if (mb[1] & 0x0001) {
				/* If it's an initiator, save node name and ignore it */
				if (ha->next_host_slot < 8) {
					memcpy( ha->host_db[ha->next_host_slot].wwn,device->wwn, 8);
					memcpy( ha->host_db[ha->next_host_slot].name,device->name, 8);
					ha->host_db[ha->next_host_slot].d_id.b24 = device->d_id.b24;
					ha->next_host_slot++;
				} else {
					printk(KERN_INFO "qla%ld: Host table full.\n", ha->instance);
				}
				return 2;
			}

			/* This is target capable device */
			qla2x00_get_port_database(ha, device, 0);
			DEBUG(printk("Fabric Login OK @ loop id= 0x%04x, port Id=%06x\n",
				     device->loop_id, device->d_id.b24););
			    return 0;

		} else if (mb[0] == 0x4008) {
			if (device->loop_id++ <= LAST_SNS_LOOP_ID)
				ha->fabricid[device->loop_id].in_use = TRUE;
			else
				return 1;
		} else {
			return 1;
		}
	}
}

/*
 * qla2100_fabric_logout
 *	Issue logout fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 */
uint8_t qla2100_fabric_logout(struct scsi_qla_host * ha, uint16_t loop_id) {
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	mb[0] = MBC_LOGOUT_FABRIC_PORT;
	mb[1] = loop_id << 8;
	qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
	DEBUG(printk("Fabric Logout @ loop id= 0x%04x\n", loop_id););
	return 0;
}

/*
 * qla2100_configure_loop
 *      Updates Fibre Channel Device Database with what is actually on loop.
 *
 * Input:
 *      ha                = adapter block pointer.
 *
 * Output:
 *      ha->fc_db = updated
 *
 * Returns:
 *      0 = success.
 *      1 = error.
 *      2 = database was full and device was not configured.
 *
 * This function sleeps.
 */
static int qla2100_configure_loop(struct scsi_qla_host * ha) {
	uint8_t rval = 0;
	uint8_t rval1 = 0;
	uint8_t enable_slot_reuse = FALSE;
	uint16_t cnt;
	uint32_t flags;
#ifdef DPC_LOCK
	unsigned long cpu_flags = 0;
#endif

	ENTER("qla2x00_configure_loop: entered\n");
	DEBUG(printk("scsi %ld: qla2100_configure_loop:\n", ha->host_no););

	    /* Get Initiator ID */
	if (qla2100_configure_hba(ha)) 
		return 1;

	QLA2100_DPC_LOCK(ha);
	flags = ha->dpc_flags;
	ha->dpc_flags &= ~(LOCAL_LOOP_UPDATE | RSCN_UPDATE);
	QLA2100_DPC_UNLOCK(ha);
	DEBUG(printk("qla2100_configure_loop: dpc flags =0x%x\n", flags););

	ha->mem_err = 0;

	/* Determine what we need to do */
	if (ha->current_topology == ISP_CFG_FL && (flags & LOCAL_LOOP_UPDATE)) {
		ha->flags.rscn_queue_overflow = TRUE;
		flags = flags | RSCN_UPDATE;
	} else if (ha->current_topology == ISP_CFG_F && (flags & LOCAL_LOOP_UPDATE)) {
		ha->flags.rscn_queue_overflow = TRUE;
		flags = flags | RSCN_UPDATE;
		flags = flags & ~LOCAL_LOOP_UPDATE;
	} else if (!ha->flags.online || (ha->dpc_flags & ABORT_ISP_ACTIVE)) {
		ha->flags.rscn_queue_overflow = TRUE;
		flags = flags | LOCAL_LOOP_UPDATE | RSCN_UPDATE;
	}

	do {
		if (flags & LOCAL_LOOP_UPDATE)
			rval = rval | qla2100_configure_local_loop(ha, enable_slot_reuse);

		/* if (!(rval & BIT_0) && flags & RSCN_UPDATE) */
		if (flags & RSCN_UPDATE) {
			rval1 = qla2100_configure_fabric(ha, enable_slot_reuse);
			if ((rval1 & BIT_0) && ha->sns_retry_cnt < 8) {
				ha->sns_retry_cnt++;
				ha->dpc_flags = ha->dpc_flags | flags;
				ha->device_flags |= LOGIN_RETRY_NEEDED;
			}
		}
		/* If devices not configured first time try reusing slots. */
		if (enable_slot_reuse == FALSE && (rval & BIT_1))
			enable_slot_reuse = TRUE;
		else
			enable_slot_reuse = FALSE;

		/* Isolate error status. */
		if (rval & BIT_0) {
			rval = 1;
		} else
			rval = 0;
	} while (enable_slot_reuse == TRUE && rval == 0);

	if (!ha->loop_down_timer && !(ha->dpc_flags & LOOP_RESYNC_NEEDED)) {
		/* Mark devices that are not present as DEV_ABSENCE */
		for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
			if (ha->fc_db[cnt].loop_id & PORT_LOST_ID) {
				ha->fc_db[cnt].flag |= DEV_ABSENCE;
			} else {
				/* device returned */
				if (ha->fc_db[cnt].loop_id <= LAST_SNS_LOOP_ID && ha->fc_db[cnt].flag & DEV_ABSENCE) {
					ha->fc_db[cnt].flag &= ~DEV_ABSENCE;
					ha->fc_db[cnt].flag |= DEV_RETURN;
					ha->fc_db[cnt].port_login_retry_count = 8;

				}
			}
			if ((flags & RSCN_UPDATE) && 0 &&	/* 5.21b7 */
			    (ha->fc_db[cnt].flag & DEV_ABSENCE)) {
				/*
				 * This dev was not detected but its WWN
				 * is valid. To handle the case where
				 * the switch may not be giving us the
				 * device list correctly, schedule for
				 * a login retry later if not previously
				 * done so.
				 */
				if (ha->fc_db[cnt].port_login_retry_count) {
					DEBUG(printk("qla2100_sns:Port login retry - slot=%d, loop id 0x%x, count=%d\n",
						     cnt, ha->fc_db[cnt].loop_id,
						     ha->fc_db[cnt].port_login_retry_count););
					    ha->fc_db[cnt].port_login_retry_count--;
					ha->dpc_flags = ha->dpc_flags | flags;
					ha->device_flags |= LOGIN_RETRY_NEEDED;
				}

			}
		}
		rval1 = qla2x00_build_fcport_list(ha);
		if (((rval1 & BIT_0) || ha->mem_err != 0)
		    && ha->sns_retry_cnt < 8) {
			ha->sns_retry_cnt++;
			ha->dpc_flags = ha->dpc_flags | flags;
			ha->device_flags |= LOGIN_RETRY_NEEDED;
		}

		if (!ha->flags.failover_enabled)
			qla2x00_config_os(ha);  /* <-- This sleeps */
		/* If we found all devices then go ready */
		if (!(ha->device_flags & LOGIN_RETRY_NEEDED)) {
			ha->loop_state = LOOP_READY;
#if	MPIO_SUPPORT
			if (qla2100_failover) {
				DEBUG(printk("qla2100_update_fc_db(%ld): schedule FAILBACK EVENT\n", ha->host_no););

						    ha->dpc_flags |= (FAILOVER_EVENT_NEEDED | COMMAND_WAIT_NEEDED);
				ha->failover_type = MP_NOTIFY_LOOP_UP;
				ha->failback_delay = failbackTime;
			}
#endif
			for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++)
				ha->fc_db[cnt].port_login_retry_count = 8;
			DEBUG(printk("qla2100_update_fc_db(%ld): LOOP READY\n", ha->host_no););
		}
	} else
		rval = 1;

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (rval)
		printk("qla2x00_configure_loop: **** FAILED ****\n");
	else
		LEAVE("qla2x00_configure_loop: exiting normally\n");
#endif
	return rval;
}

#if 0
/*
 * qla2x00_lun_abort
 *	Aborts all commands pending/running on LUN.
 *
 * Input:
 *	ha = adapter block pointer.
 *	tq = SCSI target queue pointer.
 *	lq = SCSI LUN queue pointer.
 *
 * Context:
 *	Kernel context.
 */
static void
 qla2x00_lun_abort(struct scsi_qla_host * ha, os_tgt_t * tq, struct os_lun * lq) {
	ql_list_link_t *link;
	srb_t *sp;
	uint16_t index;

	ENTER("qla2x00_lun_abort:");

	/* Abort all outstanding cmds on LUN */
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		sp = ha->outstanding_cmds[index];
		if (sp != 0 && sp->lun_queue == lq)
			qla2100_abort_command(sp);
	}

	/* Acquire target queue lock */
	TGT_LOCK(tq);

	/* All commands on LUN queue. */
	for (link = lq->cmd.first; link != NULL; link = link->next) {
		sp = link->base_address;

#if 0
		/* fixme(dg) convert for Solrias to Linux */
		sp->pkt->pkt_reason = CMD_ABORTED;
		sp->pkt->pkt_statistics = sp->pkt->pkt_statistics | STAT_ABORTED;
		sp->flags = sp->flags & ~(SRB_ISP_STARTED | SRB_ISP_COMPLETED);
#endif
	}

	link = lq->cmd.first;
	sp = link->base_address;
	lq->cmd.first = NULL;
	lq->cmd.last = NULL;

	lq->q_outcnt = 0;

	/* Release target queue lock */
	TGT_UNLOCK(tq);

	/* Call done routine to handle completion. */
	if (sp != NULL)
		qla2100_done(ha, &sp->cmd);

	LEAVE("qla2x00_lun_abort");
}
#endif

/*
 * qla2x00_config_os
 *	Setup OS target and LUN structures.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Context:
 *	Kernel context.
 *	This function sleeps
 */
static void
 qla2x00_config_os(struct scsi_qla_host * ha) {
	fc_port_t *fcport;
	fc_lun_t *fclun;
	struct os_lun *lq;
	uint16_t t, l;

	ENTER("qla2x00_config_os");
	for (fcport = ha->fcport; fcport != NULL; fcport = fcport->next) {
		/* Allocate target */
		if (fcport->loop_id == FC_NO_LOOP_ID)
			continue;

		/* Bind fcport to target number. */
		if ((t = qla2x00_fcport_bind(ha, fcport)) == MAX_TARGETS)
			continue;

		/* Allocate LUNs */
		for (fclun = fcport->fclun; fclun != NULL; fclun = fclun->next) {
			l = fclun->lun;	/* Must not exceed MAX_LUN */
			if ((lq = qla2x00_lun_alloc(ha, t, l)) == NULL)
				continue;
			lq->fclun = fclun;
		}
	}
	LEAVE("qla2x00_config_os: exiting normally");

}

/*
 * qla2x00_fcport_bind
 *	Locates a target number for FC port.
 *
 * Input:
 *	ha = adapter state pointer.
 *	fcport = FC port structure pointer.
 *
 * Returns:
 *	target number
 *
 * Context:
 *	Kernel context.
 *	This function sleeps
 */
static uint16_t qla2x00_fcport_bind(struct scsi_qla_host * ha, fc_port_t * fcport) {
	char *name;
	uint16_t t;
	os_tgt_t *tq;

	ENTER("qla2x00_fcport_bind");

	if (ha->flags.port_name_used)
		name = &fcport->port_name[0];
	else
		name = &fcport->node_name[0];

	/* Check for persistent binding. */
	for (t = 0; t < MAX_TARGETS; t++) {
		if ((tq = TGT_Q(ha, t)) == NULL)
			continue;
		if (memcmp(name, tq->fc_name, WWN_SIZE) == 0)
			break;
	}

	if (t == MAX_TARGETS) {
		unsigned int id;
		/* Check if target/loop ID available. */
		id = fcport->loop_id;
		if (TGT_Q(ha, id) != NULL) {
			/* Locate first free target for device. */
			for (t = 0; t < MAX_TARGETS; t++) {
				if (TGT_Q(ha, t) == NULL)
					break;
			}
		}
	}

	if (t != MAX_TARGETS && qla2x00_tgt_alloc(ha, t, name) == NULL)
		t = MAX_TARGETS;

	if (t == MAX_TARGETS) {
		 /*EMPTY*/ printk(KERN_WARNING "qla2x00_fcport_bind: **** FAILED ****");
	} else {
		 /*EMPTY*/ 
	 	DEBUG(tq = TGT_Q(ha, t););
		DEBUG(printk("Assigning target ID %02x @ %p to loop id: 0x%04x\n", t, tq, fcport->loop_id));
		LEAVE("qla2x00_fcport_bind: exiting normally");
	}

	return t;
}

/*
 * qla2x00_build_fcport_list
 *	Updates device on list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 *	This function sleeps.
 */
static int qla2x00_build_fcport_list(struct scsi_qla_host * ha) {
	fcdev_t *dev;
	int found = 0;
	int cnt, i;
	fc_port_t *fcport;

	ENTER("qla2x00_build_fcport_list");
	for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
		dev = &ha->fc_db[cnt];

		/* Skip if zero port name */
		if (qla2x00_is_wwn_zero((uint8_t *) & dev->wwn[0])) {
			continue;
		}
		/* Check for matching device in port list. */
		found = 0;
		for (i = 0, fcport = ha->fcport; fcport != NULL; fcport = fcport->next, i++) {

			if (memcmp(dev->wwn, fcport->port_name, WWN_SIZE) == 0 &&
			    memcmp(dev->name, fcport->node_name, WWN_SIZE) == 0) {

				DEBUG(printk("qla2x00_buildfcportlist: Found matching "
					     " port %06x, device flags= 0x%x\n", dev->d_id.b24, dev->flag););
				    /* if device found is missing then mark it */
				    if (dev->flag & DEV_ABSENCE) {
					DEBUG(printk("qla2100_buildfcportlist: Port "
						     "missing ---  (port_name) -> "
						     "%02x%02x%02x%02x%02x%02x%02x%02x, "
						     "loop id = 0x%04x\n",
						     fcport->port_name[0],
						     fcport->port_name[1],
						     fcport->port_name[2],
						     fcport->port_name[3],
						     fcport->port_name[4],
						     fcport->port_name[5],
						     fcport->port_name[6], fcport->port_name[7], fcport->loop_id););
					    fcport->loop_id = FC_NO_LOOP_ID;
					found++;
					break;
				}

				/* if device was missing but returned */
				if (fcport->loop_id == FC_NO_LOOP_ID ||
				    !(dev->flag & DEV_PUBLIC) || fcport->state != FC_ONLINE) {
					DEBUG(printk("qla2100_buildfcportlist: Port "
						     "returned +++  (port_name) -> "
						     "%02x%02x%02x%02x%02x%02x%02x%02x, "
						     "loop id = 0x%04x\n",
						     fcport->port_name[0],
						     fcport->port_name[1],
						     fcport->port_name[2],
						     fcport->port_name[3],
						     fcport->port_name[4],
						     fcport->port_name[5],
						     fcport->port_name[6], fcport->port_name[7], fcport->loop_id););
					    fcport->loop_id = dev->loop_id;
					PORT_DOWN_RETRY(fcport) = 0;
					break;
				}

				DEBUG(printk("qla2100_buildfcportlist: Match -"
					     "fcport[%d] = fc_db[%d] (ignored) -> "
					     "%02x%02x%02x%02x%02x%02x%02x%02x, "
					     "loop id = 0x%04x\n",
					     i, cnt,
					     fcport->port_name[0],
					     fcport->port_name[1],
					     fcport->port_name[2],
					     fcport->port_name[3],
					     fcport->port_name[4],
					     fcport->port_name[5],
					     fcport->port_name[6], fcport->port_name[7], fcport->loop_id););
				    found++;
				break;
			}
		}
		if (found)
			continue;

		/* Add device to port list. */
		if (fcport == NULL) {
			fcport = (fc_port_t *) kmalloc(sizeof(fc_port_t),GFP_ATOMIC);

			if (fcport == NULL)
				break;

			memset(fcport, 0, sizeof(fc_port_t));

			/* copy fields into fcport */
			memcpy( &fcport->port_name[0], &dev->wwn[0], WWN_SIZE);
			memcpy( &fcport->node_name[0], &dev->name[0], WWN_SIZE);

			if (dev->flag & DEV_ABSENCE) {
				DEBUG(printk("qla2100_buildfcportlist: Port missing --- "
					     "(port_name) -> %02x%02x%02x%02x%02x%02x"
					     "%02x%02x, loop id = 0x%04x\n",
					     fcport->port_name[0],
					     fcport->port_name[1],
					     fcport->port_name[2],
					     fcport->port_name[3],
					     fcport->port_name[4],
					     fcport->port_name[5],
					     fcport->port_name[6], fcport->port_name[7], fcport->loop_id););
				fcport->loop_id = FC_NO_LOOP_ID;
			} else {
				fcport->loop_id = dev->loop_id;
			}

			fcport->d_id = dev->d_id;

			DEBUG(printk("qla2100_buildfcportlist: New Device +++ "
				      "(port_name) -> %02x%02x%02x%02x%02x%02x%02x%02x, "
				      "loop id = 0x%04x\n",
				      fcport->port_name[0],
				      fcport->port_name[1],
				      fcport->port_name[2],
				      fcport->port_name[3],
				      fcport->port_name[4],
				      fcport->port_name[5],
				      fcport->port_name[6], fcport->port_name[7], fcport->loop_id););

			    /* flags */
			if (dev->flag & DEV_PUBLIC)
				fcport->flags |= FC_FABRIC_DEVICE;

			if (dev->flag & DEV_INITIATOR)
				fcport->flags |= FC_INITIATOR_DEVICE;

			if (!qla2100_failover)
				qla2100_get_lun_mask_from_config(ha, fcport, dev->loop_id, 0);

			fcport->next = ha->fcport;
			ha->fcport = fcport;
		} else {
			fcport->loop_id = dev->loop_id;
		}
		if (qla2x00_update_fcport(ha, fcport, cnt)) {
			return BIT_0;
		}

		if (dev->flag & DEV_ABSENCE)
			fcport->state = FC_DEVICE_LOST;
	}
	LEAVE("qla2x00_build_fcport_list: exiting normally");
	return 0;
}

/*
 * qla2x00_update_fcport
 *	Updates device on list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	0  - Success
 *  	!0 - Error
 *
 * Context:
 *	Kernel context.
 *	This function sleeps.
 */
static int
qla2x00_update_fcport(struct scsi_qla_host * ha, fc_port_t * fcport, int index) {
	ENTER("qla2x00_update_fcport");
	DEBUG4(printk("qla2x00_update_fcport: entered, loop_id = %d ", fcport->loop_id););

	fcport->port_login_retry_count = 8;
	fcport->state = FC_ONLINE;
	fcport->ha = ha;

	/* Do LUN discovery. */
	return qla2x00_lun_discovery(ha, fcport, index);

}
/*
 * qla2x00_lun_discovery
 *	Issue SCSI inquiry command for LUN discovery.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = FC port structure pointer.
 *
 * Return:
 *	0  - Success
 * 	!0 - error
 *
 * Context:
 *	Kernel context.
 *	This function sleeps.
 */ 
 
 static int
 qla2x00_lun_discovery(struct scsi_qla_host * ha, fc_port_t * fcport, int index) {
	inq_cmd_rsp_t *pkt;
	int rval;
	uint16_t lun;
	fc_lun_t *fclun;
	dma_addr_t phys_address = 0;
	int disconnected;
	int retry;
	fcdev_t dev;

	ENTER("qla2x00_lun_discovery:");

/* 4.10 */
	pkt = pci_alloc_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), &phys_address);
	if (pkt == NULL) {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - INQ", (int) ha->host_no);
		ha->mem_err++;
		return -ENOMEM;
	}

	for (lun = 0; lun < MAX_FIBRE_LUNS; lun++) {
		retry = 2;
		do {
			memset(pkt,0, sizeof(inq_cmd_rsp_t));
			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = lun;
			pkt->p.cmd.target = (uint8_t) fcport->loop_id;
			pkt->p.cmd.control_flags = CF_READ | CF_SIMPLE_TAG;
			pkt->p.cmd.scsi_cdb[0] = 0x12;
			pkt->p.cmd.scsi_cdb[4] = INQ_DATA_SIZE;
			pkt->p.cmd.dseg_count = 1;
			pkt->p.cmd.timeout = 10;
			pkt->p.cmd.byte_count = cpu_to_le32(INQ_DATA_SIZE);
			pkt->p.cmd.dseg_0_address[0] = 
				cpu_to_le32(pci_dma_lo32(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_address[1] = 
				cpu_to_le32(pci_dma_hi32(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_length = cpu_to_le32(INQ_DATA_SIZE);

			DEBUG4(printk("lun_discovery: Lun Inquiry - fcport= %p, lun (%d)\n", fcport, lun););
		
			rval = qla2x00_issue_iocb(ha,  pkt, phys_address, sizeof(inq_cmd_rsp_t));

			DEBUG4(printk(
				"lun_discovery: lun (%d) inquiry - inq[0]= 0x%x, comp status 0x%x, scsi status 0x%x, rval=%d\n",
				lun, pkt->inq[0], pkt->p.rsp.comp_status, pkt->p.rsp.scsi_status, rval););

			/* if port not logged in then try and login */
			if (lun == 0 && pkt->p.rsp.comp_status == CS_PORT_LOGGED_OUT) {
				memset(&dev, 0, sizeof(dev));
				dev.d_id.b24 = ha->fc_db[index].d_id.b24;
				/* login and update database */
				if (qla2100_fabric_login(ha, &dev) == 0)
					ha->fc_db[index].loop_id = dev.loop_id;
			}
		} while ((rval != QLA2X00_SUCCESS || pkt->p.rsp.comp_status != CS_COMPLETE) && retry--);

		if (rval != QLA2X00_SUCCESS ||
		    pkt->p.rsp.comp_status != CS_COMPLETE || (pkt->p.rsp.scsi_status & SS_CHECK_CONDITION)
		    ) {
			DEBUG(printk(
			       "lun_discovery: Failed lun inquiry - inq[0]= 0x%x, comp status 0x%x, scsi status 0x%x\n",
			       pkt->inq[0], pkt->p.rsp.comp_status, pkt->p.rsp.scsi_status););
			    break;
		}

		disconnected = 0;
		if (pkt->inq[0] == 0 || pkt->inq[0] == 0xc)
			fcport->flags = fcport->flags & ~FC_TAPE_DEVICE;
		else if (pkt->inq[0] == 1)
			fcport->flags = fcport->flags | FC_TAPE_DEVICE;
		else if (pkt->inq[0] == 0x20 || pkt->inq[0] == 0x7f)
			disconnected++;
		else
			continue;

		/* Allocate LUN if not already allocated. */
		for (fclun = fcport->fclun; fclun != NULL; fclun = fclun->next) {
			if (fclun->lun == lun)
				break;
		}
		if (fclun != NULL)
			continue;

		fcport->lun_cnt++;

		fclun = (fc_lun_t *) kmalloc(sizeof(fc_lun_t),GFP_ATOMIC);
		if (fclun != NULL) {
			/* Setup LUN structure. */
			DEBUG4(printk("lun_discovery: Allocated fclun %p, disconnected %d\n", fclun, disconnected););
			    fclun->fcport = fcport;
			fclun->lun = lun;
			if (disconnected)
				fclun->flags = FC_DISCON_LUN;
			fclun->next = fcport->fclun;
			fcport->fclun = fclun;
		} else {
			printk(KERN_WARNING "scsi(%d): Memory Allocation failed - FCLUN", (int) ha->host_no);
			ha->mem_err++;
			return BIT_0;
		}
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), pkt, phys_address);

	LEAVE("qla2x00_lun_discovery: exiting normally");
	return 0;
}

/*
 * qla2x00_issue_iocb
 *	Issue IOCB using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	size = size of buffer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
 qla2x00_issue_iocb(struct scsi_qla_host * ha, void* buffer, dma_addr_t phys_address, size_t size) {
	int rval;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla2x00_issue_iocb: started");

	mb[0] = MBC_IOCB_EXECUTE;
	mb[1] = 0;
	mb[2] = MSW(phys_address);
	mb[3] = LSW(phys_address);
	mb[6] = QL21_64BITS_4THWD(phys_address);
	mb[7] = QL21_64BITS_3RDWD(phys_address);
	rval = qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0]);

	if (rval != QLA2X00_SUCCESS) {
		 /*EMPTY*/ DEBUG(printk("qla2x00_issue_iocb(%ld): failed rval 0x%x", ha->host_no, rval););
	} else {
		 /*EMPTY*/ LEAVE("qla2x00_issue_iocb: exiting normally");
	}

	return rval;
}

/*
 * qla2100_configure_local_loop
 *	Updates Fibre Channel Device Database with local loop devices.
 *
 * Input:
 *	ha = adapter block pointer.
 *	enable_slot_reuse = allows the use of PORT_AVAILABLE slots.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = error.
 *	BIT_1 = database was full and a device was not configured.
 */
static uint8_t qla2100_configure_local_loop(struct scsi_qla_host * ha, uint8_t enable_slot_reuse) {
	uint8_t status = 0;
	uint8_t rval;
	uint8_t update_status = 0;
	uint16_t index, size;
	dma_addr_t phys_address = 0;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	fcdev_t device;
	port_list_entry_t *gn_list, *port_entry;
	uint16_t localdevices = 0;
	uint8_t *cp;

	DEBUG3(printk("qla2100_configure_local_loop: entered\n"););

/* 4.10 */
	gn_list = pci_alloc_consistent(ha->pdev, sizeof(GN_LIST_LENGTH), &phys_address);

	if (gn_list) {
		memset(gn_list,0, sizeof(GN_LIST_LENGTH));
		if (!ha->loop_down_timer && !(ha->dpc_flags & LOOP_RESYNC_NEEDED)) {
			/* Mark all local devices PORT_LOST_ID first */
			for (index = 0; index < MAX_FIBRE_DEVICES; index++) {
				if (ha->fc_db[index].loop_id <=
				    LAST_SNS_LOOP_ID && !(ha->fc_db[index].flag & DEV_PUBLIC)) {

					DEBUG(printk("qla%ld: qla2x00_configure_local_loop: port lost @ slot %d %06x\n",
						     ha->instance, index, ha->fc_db[index].d_id.b24););
					ha->fc_db[index].loop_id |= PORT_LOST_ID;
				}
			}

			/* Get port name list. */
			mb[0] = MBC_GET_PORT_LIST;
			mb[1] = BIT_0;
			mb[3] = LSW(phys_address);
			mb[2] = MSW(phys_address);
			mb[7] = QL21_64BITS_3RDWD(phys_address);
			mb[6] = QL21_64BITS_4THWD(phys_address);
			if (!qla2100_mailbox_command(ha, BIT_7 | BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0, &mb[0])) {
				port_entry = gn_list;
				size = mb[1];

				/* dg: 10/29/99 for an empty list */
				if (size / sizeof(port_list_entry_t) != 0) {
					for (size = mb[1];
					     size >= sizeof(port_list_entry_t);
					     size -= sizeof(port_list_entry_t), port_entry++) {

						/* Skip the known ports. */
						if ((port_entry->loop_id ==
						     SNS_FL_PORT) ||
						    (port_entry->loop_id ==
						     FABRIC_CONTROLLER) ||
						    (port_entry->loop_id == SIMPLE_NAME_SERVER)) {
							continue;
						}

						device.loop_id = port_entry->loop_id;

						/* Get port name */
						mb[0] = MBC_GET_PORT_NAME;
						mb[1] = device.loop_id << 8;
						rval = qla2100_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
						if (!rval && (mb[2] != 0 || mb[3] != 0 || mb[6] != 0 || mb[7] != 0)) {

							if (mb[0] != MBS_COMMAND_COMPLETE) {
								printk(KERN_INFO
								       "qla2100_configure_local_loop: GET PORT NAME - bad status.\n");
								status = BIT_0;
								break;
							}

							mb[7] = (mb[7] & 0xff) << 8 | mb[7] >> 8;
							mb[6] = (mb[6] & 0xff) << 8 | mb[6] >> 8;
							mb[3] = (mb[3] & 0xff) << 8 | mb[3] >> 8;
							mb[2] = (mb[2] & 0xff) << 8 | mb[2] >> 8;
							memcpy( &device.wwn[0],&mb[7], 2);
							memcpy( &device.wwn[2],&mb[6], 2);
							memcpy( &device.wwn[4],&mb[3], 2);
							memcpy( &device.wwn[6],&mb[2], 2);

							cp = (uint8_t *)
							    & device.wwn[0];
							BIG_ENDIAN_64(*cp);

							DEBUG3(printk
							       ("qla%ld configure_local: found portname -> %02x%02x%02x%02x%02x%02x%02x%02x\n",
								ha->host_no, cp[0], cp[1], cp[2], cp[3], cp[4], cp[5],
								cp[6], cp[7]););

						} else {
							if (mb[0] == MBS_FATAL_ERROR) {
								printk(KERN_INFO
								       "qla2100_configure_local_loop: GET PORT NAME - fatal mb error.\n");
								DEBUG(printk
								      ("qla2100_configure_local_loop: GET PORT NAME mb0=0x%x, mb1=0x%x\n",
								       mb[0], mb[1]););

								    status = BIT_0;
								break;
							}
							continue;
						}

/*
		device.name[0] = port_entry->name[1];
		device.name[1] = port_entry->name[0];
		device.name[2] = port_entry->name[3];
		device.name[3] = port_entry->name[2];
		device.name[4] = port_entry->name[5];
		device.name[5] = port_entry->name[4];
		device.name[6] = port_entry->name[7];
		device.name[7] = port_entry->name[6];
*/
						memcpy( &device.name[0],&port_entry->name[0], WWN_SIZE);

						cp = (uint8_t *) & device.name[0];
						BIG_ENDIAN_64(*cp);
						DEBUG3(printk
						       ("qla%ld configure_local: found nodename -> %02x%02x%02x%02x%02x%02x%02x%02x\n",
							ha->host_no, cp[0], cp[1], cp[2], cp[3], cp[4], cp[5], cp[6],
							cp[7]););

						    device.flag = 0;
						device.d_id.b24 = 0;
						update_status =
						    qla2100_update_fc_database(ha, &device, enable_slot_reuse);
						if (update_status)
							localdevices++;
						status = status | update_status;
					}
				}
			} else
				status = BIT_0;
		}
		pci_free_consistent(ha->pdev, sizeof(GN_LIST_LENGTH), gn_list, phys_address);

	} else {
		printk(KERN_WARNING "scsi(%d): Memory Allocation failed - port_list", (int) ha->host_no);
		ha->mem_err++;
		DEBUG2(printk("qla2100_configure_local_loop: Failed to allocate memory, No local loop\n"););
		status = BIT_0;
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status & BIT_0)
		printk("qla2100_configure_local_loop: *** FAILED ***\n");
	else
		LEAVE("qla2100_configure_local_loop: exiting normally\n");
#endif
	if (localdevices > 0) {
		ha->device_flags |= DFLG_LOCAL_DEVICES;
		ha->device_flags &= ~DFLG_RETRY_LOCAL_DEVICES;
	}

	return status;
}

#ifdef NEW_SP
srb_t *qla2100_allocate_sp_from_ha(struct scsi_qla_host * ha, Scsi_Cmnd * cmd) {
	int i, cnt;
	srb_t *sp;

	/* ENTER("qla2100_allocate_sp_from_ha"); */
	i = ha->srb_cnt;
	cnt = 0;
	do {
		/* handle warp-around */
		i = (i == (MAX_CMDS_PER_LUN - 1)) ? 0 : i;
		sp = &ha->srb_pool[i];
		if (sp->used == 0) {
			sp->used = 0x81;
			ha->srb_cnt = i;
			DEBUG4(printk("qla2100_allocate_sp: Used HA pool cmd=%p, sp=%p, index= %d\n", cmd, sp, i));
			break;
		}
		cnt++;
		i++;
	} while (cnt < MAX_CMDS_PER_LUN);
	/* if no SP left in ha pool then give up */
	if (cnt == MAX_CMDS_PER_LUN)
		sp = NULL;

	CMD_SP(cmd) = (void *) sp;

	/* LEAVE("qla2100_allocate_sp_from_ha"); */
	return sp;
}

/*
 * Get a sp for the specified lun.  The lun
 * structure has a list of possible sp pointers.
 */
srb_t *qla2100_allocate_sp(struct scsi_qla_host * ha, Scsi_Cmnd * cmd) {
	uint32_t t, l;
	srb_t *sp;
	struct os_lun *q;
	int i, cnt;

	/* ENTER("qla2100_allocate_sp"); */
	DEBUG4(printk("qla2100_allocate_sp: Enter cmd=%p\n", cmd););

	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	if ((q = GET_LU_Q(ha, t, l)) == NULL) {
		/* if we don't have a q pointer then use the ha pool */
		sp = qla2100_allocate_sp_from_ha(ha, cmd);
	} else {
		/* find a used srb ptr */
		i = q->srb_cnt;
		cnt = 0;
		do {
			/* handle warp-around */
			i = (i == (MAX_CMDS_PER_LUN - 1)) ? 0 : i;
			/* sp = &q->srb_pool[i]; */
			sp = q->srb_pool[i];
			if (sp == NULL) {
				sp = kmalloc(sizeof(srb_t), GFP_ATOMIC);
				
				if (sp != NULL) {
					DEBUG2(printk("Alloc Srb @ %p \n", sp););
					memset(sp,0,sizeof(srb_t));
					q->srb_pool[i] = sp;
				} else {
					 /*EMPTY*/
					    printk(KERN_WARNING "scsi(%d): Memory Allocation failed - srb",
						   (int) ha->host_no);
					ha->mem_err++;
					return sp;
				}
			}
			if (sp->used == 0) {
				sp->used = 1;
				q->srb_cnt = i;
				DEBUG4(printk("qla2100_allocate_sp: Used LUN pool cmd=%p, sp=%p, index= %d\n",
					cmd, sp, i););
				break;
			}
			cnt++;
			i++;
		} while (cnt < MAX_CMDS_PER_LUN);

		if (cnt == MAX_CMDS_PER_LUN)
			sp = NULL;
		CMD_SP(cmd) = (void *) sp;
	}
	DEBUG4(printk("qla2100_allocate_sp: Exit %p \n", sp));
	/* LEAVE("qla2100_allocate_sp"); */ 
	return sp;
}

void qla2100_free_sp(Scsi_Cmnd * cmd, srb_t * sp) {
	/* ENTER("qla2100_free_sp"); */
	if ((sp->used)) {
		DEBUG4(printk("qla2100: Freed sp=%p\n", sp););
		CMD_SP(cmd) = NULL;
		sp->used = 0;
	} else
		printk(KERN_INFO "qla2100_free_sp: Couldn't free sp %p it is already freed\n", sp);
	/* LEAVE("qla2100_free_sp"); */
}
#endif

/*
 * qla2x00_tgt_alloc
 *	Allocate and initialize target queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *	name = 8 byte node or port name.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 *	This function sleeps
 */
os_tgt_t *qla2x00_tgt_alloc(struct scsi_qla_host * ha, uint16_t t, uint8_t * name) {
	os_tgt_t *tq;

	ENTER("qla2x00_tgt_alloc: entered");

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS) {
		DEBUG2(printk("qla2x00_tgt_alloc: **** FAILED exiting ****"));
		return NULL;
	}

	tq = TGT_Q(ha, t);
	if (tq == NULL) {
		tq = (os_tgt_t *) kmalloc(sizeof(os_tgt_t),GFP_ATOMIC);
		if (tq != NULL) {
			memset((caddr_t) tq,0, sizeof(os_tgt_t));
			/* lock to protect the LUN queue. */
			DEBUG(printk("Alloc Target @ %08x \n", tq););

			tq->flags = TGT_TAGGED_QUEUE;
			tq->ha = ha;

			TGT_Q(ha, t) = tq;
		} else {
			printk(KERN_WARNING "scsi(%d): Failed to allocate target\n", (int) ha->host_no);
			ha->mem_err++;
		}
	}

	if (tq != NULL) {
		/* Save fcport name for persistent binding. */
		memcpy( (void *) tq->fc_name,(void *) name, 8);

		tq->port_down_retry_count = ha->port_down_retry_count;
#if 0
		tq->down_timer = 0;
#endif

		LEAVE("qla2x00_tgt_alloc: exiting normally");
	} else {
		 /*EMPTY*/ DEBUG2(printk("qla2x00_tgt_alloc: **** FAILED exiting ****"););
	}

	return tq;
}

/*
 * qla2x00_tgt_free
 *	Frees target and LUN queues.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
void
 qla2x00_tgt_free(struct scsi_qla_host * ha, uint16_t t) {
	os_tgt_t *tq;
	uint16_t l;

	/* ENTER("qla2x00_tgt_free: entered"); */

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS) {
		DEBUG2(printk("qla2x00_tgt_free: **** FAILED exiting ****"););
		return;
	}

	tq = TGT_Q(ha, t);
	if (tq != NULL) {
		TGT_Q(ha, t) = NULL;
		DEBUG(printk("Dealloc target @ %08x -- deleted\n", tq););

		    /* Free LUN structures. */
		    for (l = 0; l < MAX_LUNS; l++)
			qla2x00_lun_free(ha, t, l);

		kfree(tq);
	}

	/* LEAVE("qla2x00_tgt_free: exiting normally"); */

	return;
}

/*
 * qla2x00_lun_alloc
 *	Allocate and initialize LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *	l = LUN number.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 */
struct os_lun *qla2x00_lun_alloc(struct scsi_qla_host * ha, uint16_t t, uint16_t l) {
	struct os_lun *lq;

	ENTER("qla2x00_lun_alloc: entered");

	/*
	 * If SCSI addressing OK, allocate LUN queue.
	 */
	if (t >= MAX_TARGETS || l >= MAX_LUNS || TGT_Q(ha, t) == NULL) {
		DEBUG2(printk(
			"qla2x00_lun_alloc: tgt=%d, tgt_q= %p,lun=%d,instance=%ld **** FAILED exiting ****\n", t,
			TGT_Q(ha, t), l, ha->instance););
		    return NULL;
	}

	lq = LUN_Q(ha, t, l);
	if (lq == NULL) {
		lq = (struct os_lun *) kmalloc(sizeof(struct os_lun),GFP_ATOMIC);
		if (lq != NULL) {
			DEBUG2(printk("Alloc Lun @ %08x \n", lq););
			memset((caddr_t) lq,0, sizeof(struct os_lun));
	
			INIT_LIST_HEAD(&lq->cmd);
	
			LUN_Q(ha, t, l) = lq;
		} else {
			 /*EMPTY*/ DEBUG2(printk("qla2x00_lun_alloc: Failed to allocate lun %d ***\n", l););
			    printk(KERN_WARNING "scsi(%d): Memory Allocation failed - FCLUN", (int) ha->host_no);
			ha->mem_err++;
		}
	}

	if (lq == NULL) {
		 /*EMPTY*/ DEBUG2(printk("qla2x00_lun_alloc: **** FAILED exiting ****\n"););
	} else {
		 /*EMPTY*/ LEAVE("qla2x00_lun_alloc: exiting normally");
	}

	return lq;
}

/*
 * qla2x00_lun_free
 *	Frees LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
static void
 qla2x00_lun_free(struct scsi_qla_host * ha, uint16_t t, uint16_t l) {
	struct os_lun *lq;
	srb_t *sp;
	int i;

	/* ENTER("qla2x00_lun_free: entered"); */

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS || l >= MAX_LUNS) {
		DEBUG2(printk("qla2x00_lun_free: **** FAILED exiting ****"););
		return;
	}

	if (TGT_Q(ha, t) != NULL && (lq = LUN_Q(ha, t, l)) != NULL) {
		LUN_Q(ha, t, l) = NULL;
		for (i = 0; i < MAX_CMDS_PER_LUN; i++) {
			sp = lq->srb_pool[i];
			lq->srb_pool[i] = NULL;
			if (sp != NULL)
				kfree(sp);
		}
		kfree(lq);
		DEBUG3(printk("Dealloc lun @ %08x -- deleted\n", lq););
	}

	/* LEAVE("qla2x00_lun_free: exiting normally"); */

	return;
}


/*
 * qla2x00_next
 *	Retrieve and process next job in the LUN queue.
 *
 * Input:
 *	tq = SCSI target queue pointer.
 *	lq = SCSI LUN queue pointer.
 *	TGT_LOCK must be already obtained.
 *
 * Output:
 *	Releases TGT_LOCK upon exit.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
void
 qla2x00_next(struct scsi_qla_host *old_ha, os_tgt_t * tq, struct os_lun * lq) {
	struct scsi_qla_host *ha;
	fc_port_t *fcport;
	srb_t *sp;
	int rval;
	unsigned long flags;

	ENTER("qla2x00_next"); 

	spin_lock_irqsave(&old_ha->list_lock, flags);
	while (!list_empty(&lq->cmd)) {
		sp = list_entry(lq->cmd.next, srb_t, list);
		fcport = lq->fclun->fcport;
		ha = fcport->ha;
		/* Check if command can be started, exit if not. */
		if (
			   /* tq->flags & TGT_BUSY || */
			   (lq->q_flag & (LUN_BUSY | LUN_QUEUE_SUSPENDED)) ||
			   /* sp->flags & SRB_ABORT || */
			   LOOP_TRANSITION(ha)) {
			break;
		}

		__del_from_cmd_queue(lq, sp);

		tq->outcnt++;
		lq->q_outcnt++;
		if (lq->q_outcnt >= ha->hiwat) {
			lq->q_flag = lq->q_flag | LUN_BUSY;
			lq->q_bsycnt++;
		}

		/* Release target queue lock */
		TGT_UNLOCK(tq);

		spin_unlock_irqrestore(&old_ha->list_lock, flags);

		if (ha->flags.enable_64bit_addressing)
			rval = qla2x00_64bit_start_scsi(sp);
		else
			rval = qla2x00_32bit_start_scsi(sp);

		spin_lock_irqsave(&old_ha->list_lock, flags);

		/* Acquire target queue lock */
		TGT_LOCK(tq);

		if (rval != QLA2X00_SUCCESS) {
			/* Place request back on top of device queue */
			/* add to the top of queue */
			__add_to_cmd_queue_head(lq,sp);
			if (lq->q_outcnt != 0)
				lq->q_outcnt--;
			if (lq->q_outcnt < ha->hiwat)
				lq->q_flag = lq->q_flag & ~LUN_BUSY;

			/* Restart queue later. */
			if (lq->q_outcnt == 0 && lq->q_incnt > 0 && ha->queue_restart_timer == 0)
				ha->queue_restart_timer = PORT_RETRY_TIME;

			break;
		}
	}
	spin_unlock_irqrestore(&old_ha->list_lock, flags);

	/* Release target queue lock */
	TGT_UNLOCK(tq);

	 LEAVE("qla2x00_next");
}

/*
 * qla2x00_is_wwn_zero
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
static int
 qla2x00_is_wwn_zero(uint8_t * nn) {
	int cnt;

	/* Check for zero node name */
	for (cnt = 0; cnt < WWN_SIZE; cnt++, nn++) {
		if (*nn != 0)
			break;
	}
	/* if zero return TRUE */
	return cnt==WWN_SIZE;
}

/*
*  qla2100_flush_lun_queue
*      Abort all commands on a device queues.
*
* Input:
*      ha = adapter block pointer.
*/
#if 0
static void qla2100_flush_lun_queue() {
}
#endif

/*
 * qla2100_get_lun_mask_from_config
 * Get lun mask from the configuration parameters.
 *
 * Input:
 * ha  -- Host adapter
 * tgt  -- target/device number
 * port -- pointer to port
 */
void
 qla2100_get_lun_mask_from_config(struct scsi_qla_host * ha, fc_port_t * port, uint16_t tgt, uint16_t dev_no) {
	char propbuf[54];	/* size of search string */
	int rval, lun;

	lun_bit_mask_t lun_mask, *mask_ptr = &lun_mask;

	/* Get "target-N-device-N-lun-mask" as a 256 bit lun_mask */
	sprintf(propbuf, "scsi-qla%02ld-target-%03d-device-%03d-lun-disabled", ha->instance, tgt, dev_no);
	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *) & lun_mask, sizeof(lun_mask));
	if (rval != -1 && (rval == sizeof(lun_mask))) {
		/* Iterate through the array and swap bits around */
		DEBUG(printk("qla2100_get_lun_mask_from_config : lun mask for port %p\n", port););
		    DEBUG(qla2100_dump_buffer((uint8_t *) & port->lun_mask, sizeof(lun_bit_mask_t)););
		    for (lun = MAX_FIBRE_LUNS - 1; lun >= 0; lun--) {
			if (EXT_IS_LUN_BIT_SET(mask_ptr, lun))
				EXT_SET_LUN_BIT((&port->lun_mask), lun);

			else
				EXT_CLR_LUN_BIT((&port->lun_mask), lun);
		}
	}
}

/*
 * qla2x00_bstr_to_hex
 *	Convert hex byte string to number.
 *
 * Input:
 *	s = byte string pointer.
 *	bp = byte pointer for number.
 *	size = number of bytes.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
static int
 qla2x00_bstr_to_hex(char *s, uint8_t * bp, int size) {
	int cnt;
	uint8_t n;

	 ENTER("qla2x00_bstr_to_hex");
	for (cnt = 0; *s != '\0' && cnt / 2 < size; cnt++) {
		if (*s >= 'A' && *s <= 'F') {
			n = (*s++ - 'A') + 10;
		} else if (*s >= 'a' && *s <= 'f') {
			n = (*s++ - 'a') + 10;
		} else if (*s >= '0' && *s <= '9') {
			n = *s++ - '0';
		} else {
			cnt = 0;
			break;
		}

		if (cnt & BIT_0)
			*bp++ |= n;
		else
			*bp = n << 4;
	}
	/* fixme(dg) Need to swap data little endian */
	LEAVE("qla2x00_bstr_to_hex");

	return (cnt / 2);
}

/*
 * qla2100_get_prop_xstr
 *      Get a string property value for the specified property name and
 *      convert from the property string found in the configuration file,
 *      which are ASCII characters representing nibbles, 2 characters represent
 *      the hexdecimal value for a byte in the byte array.
 *      The byte array is initialized to zero.
 *      The resulting converted value is in big endian format (MSB at byte0).
 *
 * Input:
 *      ha = adapter state pointer.
 *      propname = property name pointer.
 *      propval  = pointer where to store converted property val.
 *      size = max or expected size of 'propval' array.
 *
 * Returns:
 *      0 = empty value string or invalid character in string
 *      >0 = count of characters converted
 *      -1 = property not found
 *
 * Context:
 *      Kernel context.
 */
int
 qla2x00_get_prop_xstr(struct scsi_qla_host * ha, char *propname, uint8_t * propval, int size) {
	char *propstr;
	int rval = -1;
	char buf[LINESIZE];

	 ENTER("qla2x00_get_prop_xstr");

	/* Get the requested property string */
	rval = qla2100_find_propname(ha, propname, buf, ha->cmdline, size * 2);
	DEBUG3(printk("get_prop_xstr: Ret rval from find propname = %d\n", rval););
	propstr = &buf[0];
	if (*propstr == '=')
		 propstr++;	/* ignore equal sign */

	if (rval == 0) {	/* not found */
		LEAVE("qla2x00_get_prop_xstr");
		return -1;
	}

	rval = qla2x00_bstr_to_hex(propstr, (uint8_t *) propval, size);
	if (rval == 0) {
		/* Invalid character in value string */
		printk(KERN_INFO "qla2x00_get_prop_xstr: %s Invalid hex string for property", propname);
		printk(KERN_INFO " Invalid string - %s ", propstr);
	}
	LEAVE("qla2x00_get_prop_xstr");
	return rval;
}

/*
 * qla2x00_chg_endian
 *	Change endianess of byte array.
 *
 * Input:
 *	buf = array pointer.
 *	size = size of array in bytes.
 *
 * Context:
 *	Kernel context.
 */
void qla2x00_chg_endian(uint8_t buf[], size_t size) 
{
	uint8_t byte;
	size_t cnt1;
	size_t cnt;

	cnt1 = size - 1;
	for (cnt = 0; cnt < size / 2; cnt++) {
		byte = buf[cnt1];
		buf[cnt1] = buf[cnt];
		buf[cnt] = byte;
		cnt1--;
	}
}

/*
* Declarations for load module
*/
static Scsi_Host_Template driver_template = QLA2100_LINUX_TEMPLATE;
#include "scsi_module.c"

#ifdef QL_DEBUG_ROUTINES
/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

/*
*  Out NULL terminated string to COM port.
*/
static void
 qla2100_print(int8_t * s) {
	printk("%s", s);
}

#ifdef QL_DEBUG_ROUTINES
static void
 qla2100_dump_buffer(uint8_t * b, uint32_t size) {
	uint32_t cnt;
	uint8_t c;

	printk(" 0   1   2   3   4   5   6   7   8   9   Ah  Bh  Ch  Dh  Eh  Fh\n");
	printk("---------------------------------------------------------------\n");

	for (cnt = 0; cnt < size;) {
		c = *b++;
		printk("%2x",(uint32_t) c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else if (c < 10)
			printk("  ");
		else
			printk(" ");
	}
	if (cnt % 16)
		printk("\n");
}
#endif

/**************************************************************************
*   ql2100_print_scsi_cmd
*
**************************************************************************/
void qla2100_print_scsi_cmd(Scsi_Cmnd * cmd) {
	struct scsi_qla_host *ha;
	struct Scsi_Host *host = cmd->host;
	srb_t *sp;
	struct os_lun *lq;
	fc_port_t *fcport;

	int i;
	ha = (struct scsi_qla_host *) host->hostdata;

	ql2x_debug_print = 1;
	sp = (srb_t *) CMD_SP(cmd);
	printk("SCSI Command @= 0x%p, Handle=0x%08lx\n", cmd, (u_long) CMD_HANDLE(cmd));
	printk("  chan=%d, target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
		cmd->channel, cmd->target, cmd->lun, cmd->cmd_len);
	printk(" CDB = ");
	for (i = 0; i < cmd->cmd_len; i++) {
		printk("0x%02x ", cmd->cmnd[i]);
	}
	printk("\n  seg_cnt =%d, retries=%d, serial_number_at_timeout=0x%lx\n", cmd->use_sg,
		cmd->retries, cmd->serial_number_at_timeout);
	printk("  request buffer=0x%p, request buffer len=0x%x\n", cmd->request_buffer,
		cmd->request_bufflen);
	printk("  tag=%d, flags=0x%x, transfersize=0x%x \n", cmd->tag, cmd->flags, cmd->transfersize);
	printk("  serial_number=%d, SP=0x%x\n", (int) cmd->serial_number, (int) CMD_SP(cmd));
	if (sp) {
		printk("  sp flags=0x%lx, wdgtime=%d\n", sp->flags, sp->wdg_time);
		printk("  r_start=0x%lx, u_start=0x%lx, state=%d\n", sp->r_start, sp->u_start,
			sp->state);

		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;
		printk("  state=%d, fo retry=%d, loopid =%d, port path=%d\n", sp->fstate,
			sp->fo_retry_cnt, fcport->loop_id, fcport->cur_path);
	}
}

void qla2100_print_q_info(struct os_lun * q) {
	printk("Queue info: queue in =%d, queue out= %d, flags=0x%lx\n", q->q_incnt, q->q_outcnt, q->q_flag);
}

#ifdef QL_DEBUG_ROUTINES
/*
 * qla2x00_dump_buffer
 *	 Prints string plus buffer.
 *
 * Input:
 *	 string  = Null terminated string (no newline at end).
 *	 buffer  = buffer address.
 *	 wd_size = word size 8, 16, 32 or 64 bits
 *	 count   = number of words.
 */
void
 qla2x00_dump_buffer(char *string, uint8_t * buffer, uint8_t wd_size, uint32_t count) {
	uint32_t cnt;
	uint16_t *buf16;
	uint32_t *buf32;
	uint64_t *buf64;

	if (ql2x_debug_print != TRUE)
		 return;

	if (strcmp(string, "") != 0) 
		printk("%s\n",string);

	switch (wd_size) {
	case 8:
		printk(" 0    1    2    3    4    5    6    7    8    9    Ah   Bh   Ch   Dh   Eh   Fh\n");
		printk("------------------------------------------------------------------------------\n");

		for (cnt = 1; cnt <= count; cnt++, buffer++) {
			printk("%2x",*buffer);
			if (cnt % 16 == 0)
				printk("\n");
			else if (*buffer < 10)
				printk("   ");
			else
				printk("  ");
		}
		if (cnt % 16 != 0)
			printk("\n");
		break;
	case 16:
		printk("   0      2      4      6      8      Ah     Ch     Eh\n");
		printk("------------------------------------------------------\n");

		buf16 = (uint16_t *) buffer;
		for (cnt = 1; cnt <= count; cnt++, buf16++) {
			printk("%4x"*buf16);

			if (cnt % 8 == 0)
				printk("\n");
			else if (*buf16 < 10)
				printk("   ");
			else
				printk("  ");
		}
		if (cnt % 8 != 0)
			qla2100_putc('\n');
		break;
	case 32:
		printk("       0          4          8          Ch\n");
		printk("------------------------------------------\n");

		buf32 = (uint32_t *) buffer;
		for (cnt = 1; cnt <= count; cnt++, buf32++) {
			printk("%8x", *buf32);

			if (cnt % 4 == 0)
				printk("\n");
			else if (*buf32 < 10)
				printk("   ");
			else
				printk("  ");
		}
		if (cnt % 4 != 0)
			printk("\n");
		break;
	case 64:
		printk("               0                  4\n");
		printk("-----------------------------------\n");

		buf64 = (uint64_t *) buffer;
		for (cnt = 1; cnt <= count; cnt++, buf64++) {
			printk("%16x",*buf64);

			if (cnt % 2 == 0)
				printk("\n");
			else if (*buf64 < 10)
				printk("   ");
			else
				printk("  ");
		}
		if (cnt % 2 != 0)
			printk("\n");
		break;
	default:
		break;
	}
}
#endif

#endif

/**************************************************************************
*   ql2100_dump_regs
*
**************************************************************************/
static void qla2100_dump_regs(struct Scsi_Host *host) {
	printk("Mailbox registers:\n");
	printk("qla2x00 : mbox 0 0x%04x \n", inw(host->io_port + 0x10));
	printk("qla2x00 : mbox 1 0x%04x \n", inw(host->io_port + 0x12));
	printk("qla2x00 : mbox 2 0x%04x \n", inw(host->io_port + 0x14));
	printk("qla2x00 : mbox 3 0x%04x \n", inw(host->io_port + 0x16));
	printk("qla2x00 : mbox 4 0x%04x \n", inw(host->io_port + 0x18));
	printk("qla2x00 : mbox 5 0x%04x \n", inw(host->io_port + 0x1a));
}

#if  STOP_ON_ERROR
/**************************************************************************
*   ql2100_panic
*
**************************************************************************/
static void qla2100_panic(char *cp, struct Scsi_Host *host) {
	struct scsi_qla_host *ha;
	long *fp;

	ha = (struct scsi_qla_host *) host->hostdata;
	DEBUG2(ql2x_debug_print = 1;);
	printk("qla2100 - PANIC:  %s\n", cp);
	printk("Current time=0x%lx\n", jiffies);
	printk("Number of pending commands =0x%lx\n", ha->actthreads);
	printk("Number of queued commands =0x%lx\n", ha->qthreads);
	printk("Number of free entries = (%d)\n", ha->req_q_cnt);
	printk("Request Queue @ 0x%lx, Response Queue @ 0x%lx\n", ha->request_dma, ha->response_dma);
	printk("Request In Ptr %d\n", ha->req_ring_index);
	fp = (long *) &ha->flags;
	printk("HA flags =0x%lx\n", *fp);
	ql2100_dump_requests(ha);
	qla2100_dump_regs(host);
	cli();
	for (;;) {
		QLA2100_DELAY(2);
		barrier();
		cpu_relax();
	}
	sti();
}
#endif
/**************************************************************************
*   ql2100_dump_requests
*
**************************************************************************/
void
 ql2100_dump_requests(struct scsi_qla_host * ha) {

	Scsi_Cmnd *cp;
	srb_t *sp;
	int i;
	printk("Outstanding Commands on controller:\n");
	for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
		if ((sp = ha->outstanding_cmds[i]) == NULL)
			continue;
		if ((cp = sp->cmd) == NULL)
			continue;
		printk("(%d): Pid=%d, sp flags=0x%lx, cmd=0x%p\n", i, (int) sp->cmd->serial_number,
			(long) sp->flags, CMD_SP(sp->cmd));
	}

}

/**************************************************************************
*   qla2100_setup
*
*   Handle Linux boot parameters. This routine allows for assigning a value
*   to a parameter with a ';' between the parameter and the value.
*   ie. qla2x00=arg0;arg1;...;argN;<properties .... properties>  OR
*   via the command line.
*   ie. qla2x00 ql2xopts=arg0;arg1;...;argN;<properties .... properties>
**************************************************************************/
void qla2100_setup(char *s, int *dummy) {
	char *cp, *np;
	char *slots[MAXARGS];
	char **argv = &slots[0];
	char buf[LINESIZE];
	int argc, opts;

	/*
	 * Determine if we have any properties.
	 */
	 cp = s;
	 opts = 1;
	while (*cp && (np = qla2100_get_line(cp, buf)) != NULL) {
		if (strncmp("scsi-qla", buf, 8) == 0) {
			DEBUG(printk("qla2100: devconf=%s\n", cp););
			ql2xdevconf = cp;
			(opts > 0) ? opts-- : 0;
			break;
		}
		opts++;
		cp = np;
	}
	/*
	   * Parse the args before the properties
	 */
	if (opts) {
		opts = (opts > MAXARGS - 1) ? MAXARGS - 1 : opts;
		argc = qla2100_get_tokens(s, argv, opts);
		while (argc > 0) {
			cp = *argv;
			DEBUG(printk("scsi: found cmd arg =[%s]\n", cp););
			if (strcmp(cp, "verbose") == 0) {
				DEBUG(printk("qla2100: verbose\n"));
				qla2100_verbose++;
			} 
			if (strcmp(cp, "quiet") == 0) {
				qla2100_quiet = 1;
			} 
			if (strcmp(cp, "reinit_on_loopdown") == 0) {
				qla2100_reinit++;
				DEBUG(printk("qla2100: reinit_on_loopdown\n"););
			}
			argc--, argv++;
		}
	}

}

/********************** qla2100_get_line *********************
* qla2100_get_line
* Copy a substring from the specified string. The substring
* consists of any number of chars seperated by white spaces (i.e. spaces)
* and ending with a newline '\n' or a semicolon ';'.
*
* Enter:
* str - orig string
* line - substring
*
* Returns:
*   cp - pointer to next string
*     or
*   null - End of string
*************************************************************/
static char *qla2100_get_line(char *str, char *line) {
	register char *cp = str;
	register char *sp = line;

	/* skip preceeding spaces */
	while (*cp && *cp == ' ')
		++cp;
	while ((*cp) && *cp != '\n' && *cp != ';')	/* end of line */
		*sp++ = *cp++;

	*sp = '\0';
	 DEBUG5(printk("qla2100_get_line: %s\n", line););
	if ((*cp)) {
		cp++;
		return cp;
	}
	return NULL;
}

/**************************** get_tokens *********************
* Parse command line into argv1, argv2, ... argvX
* Arguments are seperated by white spaces and colons and end
* with a NULL.
*************************************************************/
static int qla2100_get_tokens(char *line, char **argv, int maxargs) {
	register char *cp = line;
	int count = 0;

	while (*cp && count < maxargs) {
		/* skip preceeding spaces */
		while ((*cp) && *cp == ' ')
			++cp;
		/* symbol starts here */
		argv[count++] = cp;
		/* skip symbols */
		while ((*cp) && !(*cp == ' ' || *cp == ';' || *cp == ':'))
			cp++;
		/* replace comma or space with a null */
		if ((*cp) && (*cp == ' ') && argv[count - 1] != cp)
			*cp++ = '\0';
	}
	return count;
}

#ifdef FC_IP_SUPPORT
	    /* Include routines for supporting IP */
#include "qla2100ip.c"
#endif				/* FC_IP_SUPPORT */

#if MPIO_SUPPORT
/*
 * Declarations for failover
 */

#include "qla_def.h"
#include "qlfo.h"
#include "qla_cfg.h"
#include "qla_dbg.h"
#include "qla_gbl.h"

#if 0
/*
 * qla2x00_cfg_persistent_binding
 *	Get driver configuration file persistent binding.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 *	This function sleeps
 */
void
qla2x00_cfg_persistent_binding(adapter_state_t * ha)
{
	char prop_name[80];
	char *prop_str;
	int tmp_rval;
	uint8_t name[8];
	uint16_t tgt;
	char *cmdline = ha->cmdline;
	uint8_t tmp_name[8];
	uint8_t tmp_name1[8];

	QL_PRINT_3("qla2x00_cfg_persistent_binding: entered", 0, QDBG_NO_NUM, QDBG_NL);

	/* FC name for devices */
	for (tgt = 0; tgt < MAX_FIBRE_DEVICES; tgt++) {
		sprintf(propbuf, "scsi-qla%02d-target-%03d", (int) ha->instance, tgt);
		tmp_rval = qla2100_get_prop_16chars(ha, propbuf, tmp_name, cmdline);
		if (tmp_rval == 0) {
			/* Got a name for this ID. */
#if  USE_PORTNAME		/* updated for ioctl merge */
			memcpy( ha->fc_db[tgt].wwn,tmp_name, 8);
#else
			memcpy( ha->fc_db[tgt].name,tmp_name, 8);
#endif

			if (qla2x00_bstr_to_hex(prop_str, &name[0], 8) != 8)
				continue;

			qla2x00_tgt_alloc(ha, tgt, name); 
			DEBUG(printk("Target %d - configured by user: ", tgt));
			DEBUG(printk("scsi-target=\"%02x%02x%02x%02x"
					 "%02x%02x%02x\"\n",
					 tmp_name[0], tmp_name[1],
					 tmp_name[2], tmp_name[3], tmp_name[4], tmp_name[5], tmp_name[6], tmp_name[7]));
		}
	}

	QL_PRINT_3("qla2x00_cfg_persistent_binding: exiting normally", 0, QDBG_NO_NUM, QDBG_NL);
}
#endif


/*
 * qla2x00_cfg_display_devices
 *      This routine will the node names of the different devices found
 *      after port inquiry.
 *
 * Input:
 *
 * Returns:
 *      None.
 */
void
qla2x00_cfg_display_devices(void)
{
	mp_host_t *host;
	int id;
	mp_device_t *dp;
	mp_path_t *path;
	mp_path_list_t *path_list;
	int cnt, i, dev_no;
	int instance;
	lun_bit_mask_t lun_mask;
	int mask_set;
	uint8_t l;

	for (host = mp_hosts_base; (host); host = host->next) {

		instance = (int) host->instance;
		/* Display the node name for adapter */
		printk(KERN_INFO "scsi-qla%02x-adapter-port=%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
		       instance, host->portname[0], host->portname[1], host->portname[2],
		       host->portname[3], host->portname[4], host->portname[5], host->portname[6], host->portname[7]);

		for (id = 0; id < MAX_MP_DEVICES; id++) {
			if ((dp = host->mp_devs[id]) == NULL)
				continue;

			path_list = dp->path_list;

			if ((path = path_list->last) != NULL) {
				/* Print out device port names */
				path = path->next;	/* first path */
				for (dev_no = 0, cnt = 0; cnt < path_list->path_cnt; path = path->next, cnt++) {
					/* skip others if not our host */
					if (host != path->host)
						continue;
					printk(KERN_INFO
					       "scsi-qla%02x-tgt-%03x-di-%02d-node=%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
					       instance, id, path->id, dp->nodename[0],
					       dp->nodename[1], dp->nodename[2], dp->nodename[3],
					       dp->nodename[4], dp->nodename[5], dp->nodename[6], dp->nodename[7]);

					/* port_name */
					printk(KERN_INFO
					       "scsi-qla%02x-tgt-%03x-di-%02x-port=%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
					       instance, id, path->id, path->portname[0], path->portname[1], 
					       path->portname[2],path->portname[3], path->portname[4],
					       path->portname[5], path->portname[6], path->portname[7]);

					/* control byte */

					printk(KERN_INFO "scsi-qla%02x-tgt-%03x-di-%02x-control=%02x\\;\n",
					       instance, id, path->id, path->mp_byte);

					/* Build preferred bit mask for this path */
					memset((char *) &lun_mask,0, sizeof(lun_mask));
					mask_set = 0;
					for (i = 0; i < MAX_LUNS; i++) {
						l = (uint8_t) (i & 0xFF);
						if (path_list->current_path[l] == path->id) {
							EXT_SET_LUN_BIT((&lun_mask), l);
							mask_set++;
						}
					}
					if (mask_set) {
						printk(KERN_INFO
						       "scsi-qla%02x-tgt-%03x-di-%02x-preferred=%08x%08x%08x%08x%08x%08x%08x%08x\\;\n",
						       instance, id, path->id,
						       *((uint32_t *) & lun_mask.mask[28]),
						       *((uint32_t *) & lun_mask.mask[24]),
						       *((uint32_t *) & lun_mask.mask[20]),
						       *((uint32_t *) & lun_mask.mask[16]),
						       *((uint32_t *) & lun_mask.mask[12]),
						       *((uint32_t *) & lun_mask.mask[8]),
						       *((uint32_t *) & lun_mask.mask[4]),
						       *((uint32_t *) & lun_mask.mask[0]));
					}
					/* Build disable bit mask for this path */
					mask_set = 0;
					for (i = 0; i < MAX_LUNS; i++) {
						l = (uint8_t) (i & 0xFF);
						if (!(path->lun_data.data[l] & LUN_DATA_ENABLED)) {
							mask_set++;
						}
					}
					if (mask_set) {
						printk(KERN_INFO
						       "scsi-qla%02x-tgt-%03x-di-%02x-lun-disable=%08x%08x%08x%08x%08x%08x%08x%08x\\;\n",
						       instance, id, path->id,
						       *((uint32_t *) & lun_mask.mask[28]),
						       *((uint32_t *) & lun_mask.mask[24]),
						       *((uint32_t *) & lun_mask.mask[20]),
						       *((uint32_t *) & lun_mask.mask[16]),
						       *((uint32_t *) & lun_mask.mask[12]),
						       *((uint32_t *) & lun_mask.mask[8]),
						       *((uint32_t *) & lun_mask.mask[4]),
						       *((uint32_t *) & lun_mask.mask[0]));
					}
					dev_no++;
				}

			}
		}
	}
}

#if 0
int
qla2x00_cfg_build_range(mp_path_t * path, uint8_t * buf, int siz, uint8_t mask)
{
	int i;
	int max, min;
	int colonflg = FALSE;
	int len = 0;

	max = -1;
	min = 0;
	for (i = 0; i < MAX_LUNS; i++) {
		if ((path->lun_data.data[i] & mask)) {
			max = i;
		} else {
			if (colonflg && max >= min) {
				len += sprintf(&buf[len], ":");
				if (len > siz)
					return len;
				colonflg = FALSE;
			}
			if (max > min) {
				len += sprintf(&buf[len], "%02x-%02x", min, max);
				if (len > siz)
					return len;
				colonflg = TRUE;
			} else if (max == min) {
				len += sprintf(&buf[len], "%02x", max);
				if (len > siz)
					return len;
				colonflg = TRUE;
			}
			min = i + 1;
			max = i;
		}
	}
	DEBUG4(printk("build_range: return len =%d\n", len);
	    );
	    return (len);
}
#endif

#if 0
/*
 * qla2x00_cfg_proc_display_devices
 *      This routine will the node names of the different devices found
 *      after port inquiry.
 *
 * Input:
 *
 * Returns:
 *      None.
 */
int
qla2x00_cfg_proc_display_devices(adapter_state_t * ha)
{
	mp_host_t *host;
	int id;
	mp_device_t *dp;
	mp_path_t *path;
	mp_path_list_t *path_list;
	int cnt, i;
	int instance;
	lun_bit_mask_t lun_mask;
	int mask_set;
	uint8_t l;
	fc_port_t *port;
	int len = 0;

	for (host = mp_hosts_base; (host); host = host->next) {

		if (host->ha != ha)
			continue;

		instance = (int) host->instance;

		/* Display the node name for adapter */
		len += sprintf(PROC_BUF,
			       "scsi-qla%02d-adapter-node=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
			       instance, host->nodename[0], host->nodename[1],
			       host->nodename[2], host->nodename[3], 
			       host->nodename[4], host->nodename[5], host->nodename[6], host->nodename[7]);

		for (id = 0; id < MAX_MP_DEVICES; id++) {
			if ((dp = host->mp_devs[id]) == NULL)
				continue;

			path_list = dp->path_list;

			if ((path = path_list->last) != NULL) {
				/* Print out device port names */
				path = path->next;	/* first path */
				for (cnt = 0; cnt < path_list->path_cnt; path = path->next, cnt++) {
					/* skip others if not our host */
					if (host != path->host)
						continue;
					len += sprintf(PROC_BUF,
						       "scsi-qla%02d-target-%03d-path-%03d-node=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
						       instance, id, path->id, dp->nodename[0],
						       dp->nodename[1], dp->nodename[2],
						       dp->nodename[3], dp->nodename[4],
						       dp->nodename[5], dp->nodename[6], dp->nodename[7]);

					/* port_name */
					len += sprintf(PROC_BUF,
						       "scsi-qla%02d-target-%03d-path-%03d-port=%02x%02x%02x%02x%02x%02x%02x%02x;\n",
						       instance, id, path->id, path->portname[0],
						       path->portname[1], path->portname[2],
						       path->portname[3], path->portname[4],
						       path->portname[5], path->portname[6], path->portname[7]);

					if (path_list->visible == path->id) {
						len +=
						    sprintf(PROC_BUF,
							    "scsi-qla%02d-target-%03d-path-%03d-visible=%02x;\n",
							    instance, id, path->id, path->id);
					}

					len += sprintf(PROC_BUF, "scsi-qla%02d-target-%03d-path-%03d-control=%02x;\n",
						       instance, id, path->id, path->mp_byte);

					/* Build preferred bit mask for this path */
					memset((char *) &lun_mask,0, sizeof(lun_mask));
					mask_set = 0;
					for (i = 0; i < MAX_LUNS_PER_DEVICE; i++) {
						l = (uint8_t) (i & 0xFF);
						if (path_list->current_path[l] == path->id) {
							EXT_SET_LUN_BIT((&lun_mask), l);
							mask_set++;
						}
					}
					if (mask_set && EXT_DEF_MAX_LUNS <= 256) {
						len += sprintf(PROC_BUF,
							       "scsi-qla%02d-target-%03d-path-%03d-preferred=%08x%08x%08x%08x%08x%08x%08x%08x;\n",
							       instance, id, path->id,
							       *((uint32_t *) & lun_mask.mask[0]),
							       *((uint32_t *) & lun_mask.mask[4]),
							       *((uint32_t *) & lun_mask.mask[8]),
							       *((uint32_t *) & lun_mask.mask[12]),
							       *((uint32_t *) & lun_mask.mask[16]),
							       *((uint32_t *) & lun_mask.mask[20]),
							       *((uint32_t *) & lun_mask.mask[24]),
							       *((uint32_t *) & lun_mask.mask[28]));
					}

					len += sprintf(PROC_BUF,
						       "scsi-qla%02d-target-%03d-path-%03d-lun-enable=%08x%08x%08x%08x%08x%08x%08x%08x;\n",
						       instance, id, path->id,
						       *((uint32_t *) & path->lun_data.data[0]),
						       *((uint32_t *) & path->lun_data.data[4]),
						       *((uint32_t *) & path->lun_data.data[8]),
						       *((uint32_t *) & path->lun_data.data[12]),
						       *((uint32_t *) & path->lun_data.data[16]),
						       *((uint32_t *) & path->lun_data.data[20]),
						       *((uint32_t *) & path->lun_data.data[24]),
						       *((uint32_t *) & path->lun_data.data[28]));

				}	/* for */
			}
		}
	}
	return (len);
}
#endif


/*
 * qla2x00_get_hba
 *	Searches the hba structure chain for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	inst = adapter instance number.
 *
 * Returns:
 *	Return value is a pointer to the adapter structure or
 *      NULL if instance not found.
 *
 * Context:
 *	Kernel context.
 */
scsi_qla_host_t *
qla2x00_get_hba(int instance)
{
	scsi_qla_host_t * hbap;

	hbap = (scsi_qla_host_t *) qla2100_hostlist;

	while (hbap != NULL) {
		if (hbap->instance == instance) {
			break;
		}
		hbap = (scsi_qla_host_t *) hbap->next;
	}
	return (hbap);
}



/*
 * qla2x00_fo_count_retries
 *	Increment the retry counter for the command.
 *      Set or reset the SRB_RETRY flag.
 *
 * Input:
 *	sp = Pointer to command.
 *
 * Returns:
 *	TRUE or FALSE.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_fo_count_retries(struct scsi_qla_host * ha, srb_t * sp)
{
	uint8_t retry = TRUE;
	os_lun_t *lq;
	os_tgt_t *tq;
	struct scsi_qla_host *orig_ha;
	unsigned long flags;

	if (++sp->fo_retry_cnt > qla_fo_params.MaxRetriesPerIo) {
		/* no more failovers for this request */
		retry = FALSE;
		sp->fo_retry_cnt = 0;
		DEBUG(sp->fstate = 1);
		printk(KERN_INFO "qla2x00: no more failovers for request - pid= %ld\n", sp->cmd->serial_number);
	} else {

		/*
		   * We haven't exceeded the max retries for this
		   * request, check max retries this path
		 */
		if ((sp->fo_retry_cnt % qla_fo_params.MaxRetriesPerPath) == 0) {
			DEBUG(printk(
			       " qla2x00_fo_count_retries: Schedule FAILOVER - ha=%d, pid =%d, fo retry= %d \n",
			       ha->instance, sp->cmd->serial_number, sp->fo_retry_cnt));
			    /*
			     * Note: we don't want it to timeout, so
			     * it is recycling on the retry queue and
			     * the fialover queue.
			     */
			sp->flags = sp->flags | SRB_FAILOVER;
			lq = sp->lun_queue;
			tq = sp->tgt_queue;
			lq->q_flag |= QLA2100_QSUSP;

			/* We can get a path error on any ha, but always 
			 * queue failover on originating ha. This will allow
			 * us to syncronized the requests for a given lun.
			 */
			orig_ha = tq->ha;

			/* Now queue it on to be failover */
	
			spin_lock_irqsave(&orig_ha->list_lock, flags);
			list_add_tail(&sp->list,&orig_ha->failover_queue);		
			orig_ha->dpc_flags |= FAILOVER_NEEDED;
			spin_unlock_irqrestore(&orig_ha->list_lock, flags);
			qla2100_extend_timeout(sp->cmd, 60 * HZ);
			DEBUG(sp->fstate = 2);
		}
	}
	return (retry);
}

/*
 * qla2x00_fo_check
 *	This function is called from the done routine to see if
 *  the SRB requires a failover.
 *
 *	This function examines the available os returned status and
 *  if meets condition, the command(srb) is placed ont the failover
 *  queue for processing.
 *
 * Input:
 *	sp  = Pointer to the SCSI Request Block
 *
 * Output:
 *      sp->flags SRB_RETRY bit id command is to
 *      be retried otherwise bit is reset.
 *
 * Returns:
 *      None.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
uint8_t
qla2x00_fo_check(struct scsi_qla_host * ha, srb_t * sp)
{
	uint8_t retry = FALSE;
	int host_status;
#if  DEBUG_QLA2100
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif

	/* ENTER("qla2x00_fo_check"); */
	/* we failover on selction timeouts only */
	host_status = sp->cmd->result >> 16;
	if (host_status == DID_NO_CONNECT) {
		if (qla2x00_fo_count_retries(ha, sp)) {
			/* Force a retry  on this request, it will
			   * cause the LINUX timer to get reset, while we
			   * we are processing the failover.
			 */
			sp->cmd->result = DID_BUS_BUSY << 16;
			retry = TRUE;
		}
		DEBUG(printk("qla2x00_fo_check: pid= %d sp %p retry flag = %d, host status (%s)\n\r",
		       sp->cmd->serial_number, sp, retry, reason[host_status]));
	}
	/* LEAVE("qla2x00_fo_check"); */
	return (retry);
}




#endif
#if  APIDEV
/****************************************************************************/
/* Create character driver "HbaApiDev" w dynamically allocated major number */
/* and create "/proc/scsi/qla2x00/HbaApiNode" as the device node associated */
/* with the major number.                                                   */
/****************************************************************************/

#define APIDEV_NODE  "HbaApiNode"
#define APIDEV_NAME  "HbaApiDev"

static int apidev_major = 0;
static struct Scsi_Host *apidev_host = 0;

static int apidev_open(struct inode *inode, struct file *file) {
	printk(KERN_INFO "qla2100_apidev: open MAJOR number = %d, MINOR number = %d\n", MAJOR(inode->i_rdev),
	       MINOR(inode->i_rdev));
	return 0;
}
static int apidev_close(struct inode *inode, struct file *file) {
	printk(KERN_INFO "qla2100_apidev: closed\n");
	return 0;
}
static int apidev_ioctl(struct inode *inode, struct file *fp, unsigned int cmd, unsigned long arg) {
	Scsi_Device fake_scsi_device;
	fake_scsi_device.host = apidev_host;
	return qla2x00_ioctl(&fake_scsi_device, (int) cmd, (void *) arg);
} 

static struct file_operations apidev_fops = {
	ioctl: apidev_ioctl, 
	open: apidev_open, 
	release:apidev_close
};

static int apidev_init(struct Scsi_Host *host) {
	if (apidev_host)
		return 0;
	if (0 > (apidev_major = register_chrdev(0, APIDEV_NAME, &apidev_fops))) {
		DEBUG(printk("qla2100_apidev: register_chrdev rc=%d\n", apidev_major););
		return apidev_major;
	}
	apidev_host = host;
	DEBUG(printk("qla2x00: Created /proc/scsi/qla2x00/%s major=%d\n", APIDEV_NODE, apidev_major););
	    proc_mknod(APIDEV_NODE, 0777 + S_IFCHR, host->hostt->proc_dir, (kdev_t) MKDEV(apidev_major, 0));
	return 0;
}

static int apidev_cleanup() {
	if (!apidev_host)
		return 0;
	unregister_chrdev(apidev_major, APIDEV_NAME);
	remove_proc_entry(APIDEV_NODE, apidev_host->hostt->proc_dir);
	apidev_host = 0;
	return 0;
}
#endif				/* APIDEV */


/*
* Overrides for Emacs so that we almost follow Linus's tabbing style.
* Emacs will notice this stuff at the end of the file and automatically
* adjust the settings for this buffer only.  This must remain at the end
* of the file.
* ---------------------------------------------------------------------------
* Local variables:
* c-indent-level: 2
* c-brace-imaginary-offset: 0
* c-brace-offset: -2
* c-argdecl-indent: 2
* c-label-offset: -2
* c-continued-statement-offset: 2
* c-continued-brace-offset: 0
* indent-tabs-mode: nil
* tab-width: 8
* End:
*/
