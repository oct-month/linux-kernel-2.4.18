/******************************************************************************
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
 ******************************************************************************/

/*
 * San/Device Management Failover Ioctl Header
 * File is created to adhere to Solaris requirement using 8-space tabs.
 *
 * !!!!! PLEASE DO NOT REMOVE THE TABS !!!!!
 * !!!!! PLEASE NO SINGLE LINE COMMENTS: // !!!!!
 * !!!!! PLEASE NO MORE THAN 80 CHARS PER LINE !!!!!
 *
 * Revision History:
 *
 * Rev. 0.00	August 8, 2000
 * WTR	- Created.
 *
 * Rev. 0.01	August 8, 2000
 * WTR	- Made size of HbaInstance fields consistant as UINT8.
 *        Made command codes as 300 upward to be consistant with definitions
 *        in ExIoct.h.
 */

#ifndef _SDM_FO_H
#define _SDM_FO_H

#include "ExIoct.h"


#define SDM_DEF_MAX_DEVICES		16
#define SDM_DEF_MAX_PATHS_PER_TARGET	4
#define SDM_DEF_MAX_TARGETS_PER_DEVICE	4
#define SDM_DEF_MAX_PATHS_PER_DEVICE	\
    (SDM_DEF_MAX_PATHS_PER_TARGET * SDM_DEF_MAX_TARGETS_PER_DEVICE)
#define SDM_FO_MAX_LUNS_PER_DEVICE	MAX_LUNS_OS

#define SDM_FO_MAX_PATHS		\
    (SDM_DEF_MAX_PATHS_PER_DEVICE * SDM_DEF_MAX_DEVICES)
#define SDM_FO_MAX_ADAPTERS		8
#define SDM_FO_ADAPTER_ALL		0xFF

#define SDM_CC_FO_GET_PARAMS		300
#define SDM_CC_FO_SET_PARAMS		301
#define SDM_CC_FO_GET_PATHS		302
#define SDM_CC_FO_SET_CURRENT_PATH	303
#define SDM_CC_FO_GET_HBA_STAT		304
#define SDM_CC_FO_RESET_HBA_STAT	305


/* Systemwide failover parameters. */

typedef struct _SDM_FO_PARAMS
{
    UINT8       MaxTargetsPerDevice;             
    UINT8       MaxPathsPerTarget;             
    UINT8       MaxPathsPerDevice;	/* Max paths to any single device. */
    UINT8       MaxRetriesPerPath;	/* Max retries on a path before */
    					/* failover. */
    UINT8       MaxRetriesPerIo;	/* Max retries per i/o request. */
    UINT8       Reserved1;                     
    UINT32      Flags;			/* Control flags. */
    UINT32      Reserved2[4];                  

    /* Failover notify parameters. */

    UINT8       FailoverNotifyType;	/* Type of notification. */
    UINT8       FailoverNotifyCdbLength;/* Length of notification CDB. */
    UINT8       FailoverNotifyCdb[16];	/* CDB if notification by CDB. */
    UINT32      Reserved3;                     

} SDM_FO_PARAMS, *PSDM_FO_PARAMS, SysFoParams_t, *SysFoParams_p;

extern SysFoParams_t qla_fo_params;

typedef struct _SDM_FO_GET_PATHS
{
    UINT8  HbaInstance;
    SDM_SCSI_ADDR HbaAddr;		/* Lun field is ignored */	
    UINT32  Reserved[5];

} SDM_FO_GET_PATHS, *PSDM_FO_GET_PATHS;


typedef struct _SDM_FO_PATH_ENTRY
{
    BOOLEAN Reserved1; 
    BOOLEAN Visible;		/* Path is visible path. */
    UINT16  Reserved2; 
    UINT8  HbaInstance;
    UINT8   PortName[SDM_DEF_WWN_NAME_SIZE];

    UINT16  Reserved3;
    UINT32  Reserved[3];

} SDM_FO_PATH_ENTRY, *PSDM_FO_PATH_ENTRY;


typedef struct _SDM_FO_PATHS_INFO
{
    /* These first fields in the output buffer are specifically the
     * same as the fields in the input buffer.  This is because the
     * same system buffer holds both, and this allows us to reference
     * the input buffer parameters while filling the output buffer. */

    UINT8  HbaInstance;
    SDM_SCSI_ADDR HbaAddr;
    UINT32  Reserved[5];

    UINT8   PathCount;		/* Number of Paths in PathEntry array */
    UINT8   Reserved3;
    UINT8   VisiblePathIndex;	/* Which index has BOOLEAN "visible" flag set */
    UINT8   Reserved4;

	/* Current Path Index for each Lun */
    UINT8   CurrentPathIndex[SDM_FO_MAX_LUNS_PER_DEVICE];

    SDM_FO_PATH_ENTRY   PathEntry[SDM_FO_MAX_PATHS];

    UINT32   Reserved5[4];

} SDM_FO_PATHS_INFO, *PSDM_FO_PATHS_INFO;

typedef struct _SDM_FO_SET_CURRENT_PATH
{
    UINT8  HbaInstance;

    SDM_SCSI_ADDR HbaAddr;

    UINT8   NewCurrentPathIndex;	/* Path index to make current path. */
    UINT8   FailoverType;		/* Reason for failover. */

    UINT32  Reserved[3];

} SDM_FO_SET_CURRENT_PATH, *PSDM_FO_SET_CURRENT_PATH;

typedef union _SDM_FO_PATHS {
	SDM_FO_GET_PATHS input;
	SDM_FO_SET_CURRENT_PATH set;
	SDM_FO_PATHS_INFO info;
} SDM_FO_PATHS;


typedef struct  _SDM_FO_HBA_STAT_INPUT
{
    /* The first field in the input buffer is specifically the
     * same as the field in the output buffer.  This is because the
     * same system buffer holds both, and this allows us to reference
     * the input buffer parameters while filling the output buffer. */

    UINT8       HbaInstance;		/* Port number or ADAPTER_ALL. */
    UINT8       Reserved1[3];
    UINT32      Reserved2[7];

} SDM_FO_HBA_STAT_INPUT, *PSDM_FO_HBA_STAT_INPUT;


typedef struct _SDM_FO_HBA_STAT_ENTRY
{
    UINT8       HbaInstance; 
    UINT8       Reserved1[3]; 
    UINT32      Reserved2;

    UINT64      IosRequested; /* IOs requested on this adapter. */
    UINT64      BytesRequested;		/* Bytes requested on this adapter. */
    UINT64      IosExecuted; /* IOs executed on this adapter. */
    UINT64      BytesExecuted;		/* Bytes executed on this adapter. */

    UINT32      Reserved3[22];

} SDM_FO_HBA_STAT_ENTRY, *PSDM_FO_HBA_STAT_ENTRY;


typedef struct _SDM_FO_HBA_STAT_INFO
{
    /* The first fields in the output buffer is specifically the
     * same as the field in the input buffer.  This is because the
     * same system buffer holds both, and this allows us to reference
     * the input buffer parameters while filling the output buffer. */

    UINT8       HbaInstance; /* Port number or ADAPTER_ALL. */
    UINT8       HbaCount; /* Count of adapters returned. */
    UINT8       Reserved1[2];
    UINT32      Reserved2[7];

    SDM_FO_HBA_STAT_ENTRY StatEntry[SDM_FO_MAX_ADAPTERS];

} SDM_FO_HBA_STAT_INFO, *PSDM_FO_HBA_STAT_INFO;

typedef union _SDM_FO_HAB_STAT {
	SDM_FO_HBA_STAT_INPUT input;
	SDM_FO_HBA_STAT_INFO info;

} SDM_FO_HBA_STAT;

#endif	/* ifndef _SDM_FO_H */
