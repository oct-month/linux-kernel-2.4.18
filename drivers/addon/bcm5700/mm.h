
/******************************************************************************/
/*                                                                            */
/* Broadcom BCM5700 Linux Network Driver, Copyright (c) 2000 Broadcom         */
/* Corporation.                                                               */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/******************************************************************************/

#ifndef MM_H
#define MM_H

#include <linux/config.h>
#if defined(CONFIG_SMP) && ! defined(__SMP__)
#define __SMP__
#endif
#if defined(CONFIG_MODVERSIONS) && defined(MODULE) && ! defined(MODVERSIONS)
#define MODVERSIONS
#endif

#ifndef B57UM
#define __NO_VERSION__
#endif
#include <linux/version.h>
#ifdef MODULE
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/module.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define SET_MODULE_OWNER(dev)
#define MODULE_DEVICE_TABLE(pci, pci_tbl)
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/time.h>
#if (LINUX_VERSION_CODE >= 0x020400)
#include <linux/ethtool.h>
#include <asm/uaccess.h>
#endif
#ifdef CONFIG_PROC_FS
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#endif
#ifdef NETIF_F_HW_VLAN_TX
#include <linux/if_vlan.h>
#define BCM_VLAN 1
#endif

#ifdef __BIG_ENDIAN
#define BIG_ENDIAN_HOST 1
#endif

#if (LINUX_VERSION_CODE < 0x020327)
#define __raw_readl readl
#define __raw_writel writel
#endif

#include "lm.h"
#include "queue.h"
#include "tigon3.h"

extern int MM_Packet_Desc_Size;

#define MM_PACKET_DESC_SIZE MM_Packet_Desc_Size

DECLARE_QUEUE_TYPE(UM_RX_PACKET_Q, MAX_RX_PACKET_DESC_COUNT+1);

#define MAX_MEM 16

#if (LINUX_VERSION_CODE < 0x020211)
typedef u32 dma_addr_t;
#endif

#if (LINUX_VERSION_CODE < 0x02030e)
#define net_device device
#define netif_carrier_on(dev)
#define netif_carrier_off(dev)
#endif

#if (LINUX_VERSION_CODE < 0x02032b)
#define tasklet_struct			tq_struct
#endif

typedef struct _UM_DEVICE_BLOCK {
	LM_DEVICE_BLOCK lm_dev;
	struct net_device *dev;
	struct pci_dev *pdev;
	struct net_device *next_module;
	char *name;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *pfs_entry;
	char pfs_name[32];
#endif
	void *mem_list[MAX_MEM];
	dma_addr_t dma_list[MAX_MEM];
	int mem_size_list[MAX_MEM];
	int mem_list_num;
	int index;
	int opened;
	int delayed_link_ind; /* Delay link status during initial load */
	int adapter_just_inited; /* the first few seconds after init. */
	int spurious_int;
	int timer_interval;
	int adaptive_expiry;
	int crc_counter_expiry;
	int poll_tbi_expiry;
	int tx_full;
	int tx_queued;
	int line_speed;		/* in Mbps, 0 if link is down */
	UM_RX_PACKET_Q rx_out_of_buf_q;
	int rx_out_of_buf;
	int rx_buf_repl_thresh;
	int rx_buf_repl_panic_thresh;
	int rx_buf_align;
	struct timer_list timer;
	int do_global_lock;
	spinlock_t global_lock;
	spinlock_t undi_lock;
	spinlock_t phy_lock;
	long undi_flags;
	volatile unsigned long interrupt;
	atomic_t intr_sem;
	int tasklet_pending;
	int tasklet_busy;
	struct tasklet_struct tasklet;
	struct net_device_stats stats;
#ifdef NICE_SUPPORT
	void (*nice_rx)( struct sk_buff*, void* );
	void* nice_ctx;
#endif /* NICE_SUPPORT */
#ifdef NETIF_F_HW_VLAN_TX
	struct vlan_group *vlgrp;
#endif
	int adaptive_coalesce;
	uint rx_last_cnt;
	uint tx_last_cnt;
	uint rx_curr_coalesce_frames;
	uint rx_curr_coalesce_ticks;
	uint tx_curr_coalesce_frames;
#if TIGON3_DEBUG
	uint tx_zc_count;
	uint tx_chksum_count;
	uint tx_himem_count;
	uint rx_good_chksum_count;
#endif
	uint rx_bad_chksum_count;
	uint rx_misc_errors;
} UM_DEVICE_BLOCK, *PUM_DEVICE_BLOCK;

#define MM_ACQUIRE_UNDI_LOCK(_pDevice) \
	if (!(((PUM_DEVICE_BLOCK)(_pDevice))->do_global_lock)) {	\
		long flags;						\
		spin_lock_irqsave(&((PUM_DEVICE_BLOCK)(_pDevice))->undi_lock, flags);	\
		((PUM_DEVICE_BLOCK)(_pDevice))->undi_flags = flags; \
	}

#define MM_RELEASE_UNDI_LOCK(_pDevice) \
	if (!(((PUM_DEVICE_BLOCK)(_pDevice))->do_global_lock)) {	\
		long flags = ((PUM_DEVICE_BLOCK) (_pDevice))->undi_flags; \
		spin_unlock_irqrestore(&((PUM_DEVICE_BLOCK)(_pDevice))->undi_lock, flags); \
	}

#define MM_ACQUIRE_PHY_LOCK_IN_IRQ(_pDevice) \
	if (!(((PUM_DEVICE_BLOCK)(_pDevice))->do_global_lock)) {	\
		spin_lock(&((PUM_DEVICE_BLOCK)(_pDevice))->phy_lock);	\
	}

#define MM_RELEASE_PHY_LOCK_IN_IRQ(_pDevice) \
	if (!(((PUM_DEVICE_BLOCK)(_pDevice))->do_global_lock)) {	\
		spin_unlock(&((PUM_DEVICE_BLOCK)(_pDevice))->phy_lock); \
	}

#define MM_ACQUIRE_INT_LOCK(_pDevice) \
	while (((PUM_DEVICE_BLOCK) _pDevice)->interrupt)

#define MM_RELEASE_INT_LOCK(_pDevice)

#define MM_UINT_PTR(_ptr)   ((unsigned long) (_ptr))

#define DbgPrint(fmt, arg...) printk(KERN_WARNING fmt, ##arg)
#if defined(CONFIG_X86)
#define DbgBreakPoint() __asm__("int $129")
#else
#define DbgBreakPoint()
#endif
#define MM_Wait(time) udelay(time)

#endif
