/* cache-layout.h: AFS cache layout
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * The cache is stored on a block device and is laid out as follows:
 *
 *  0	+------------------------------------------------
 *	|
 *	|  SuperBlock
 *	|
 *  1	+------------------------------------------------
 *	|
 *	|  Cell Cache (preloaded by mkafscache)
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Volume Cache Allocation BitMap (1 page)
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Volume Cache
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Vnode Cache Allocation BitMap
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Vnode Cache Index
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Vnode Cache
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Data Cache Allocation BitMap
 *	|
 *	+------------------------------------------------
 *	|
 *	|  Data Cache
 *	|
 *  End	+------------------------------------------------
 *
 */

#ifndef _LINUX_AFS_CACHE_LAYOUT_H
#define _LINUX_AFS_CACHE_LAYOUT_H

#include "types.h"

typedef unsigned afs_cache_bix_t;
typedef unsigned short afs_cache_cellix_t;

typedef struct { unsigned short index; } afs_cache_volix_t;

/*****************************************************************************/
/*
 * cache superblock block layout
 */
struct afs_cache_super_block
{
	char			magic[10];	/* magic number */
#define AFS_CACHE_SUPER_MAGIC "kafscache"

	unsigned short		endian;		/* 0x1234 stored CPU-normal order */
#define AFS_CACHE_SUPER_ENDIAN 0x1234

	unsigned		version;	/* format version */
#define AFS_CACHE_SUPER_VERSION 1

	/* accounting */
	afs_cache_cellix_t	ncells;		/* number of cells cached */
	afs_cache_cellix_t	maxcells;	/* max number of cells cacheable */
	afs_cache_cellix_t	thiscell;	/* index of this cell in cache */
	unsigned short		nvols;		/* volume cache usage */
	unsigned short		maxvols;	/* maximum number of volumes cacheable */
	unsigned		nvnodes;	/* vnode cache usage */
	unsigned		maxvnodes;	/* maximum number of vnodes cacheable */

	/* layout */
	unsigned		bsize;			/* cache block size */
	afs_cache_bix_t		off_cell_cache;		/* block offset of cell cache */
	afs_cache_bix_t		off_volume_bitmap;	/* block offset of volume alloc bitmap */
	afs_cache_bix_t		off_volume_cache;	/* block offset of volume cache */
	afs_cache_bix_t		off_vnode_bitmap;	/* block offset of vnode alloc bitmap */
	afs_cache_bix_t		off_vnode_index;	/* block offset of vnode index */
	afs_cache_bix_t		off_vnode_cache;	/* block offset of vnode cache */
	afs_cache_bix_t		off_data_bitmap;	/* block offset of data bitmap */
	afs_cache_bix_t		off_data_cache;		/* block offset of data cache */
	afs_cache_bix_t		off_end;		/* block offset of end of cache */
};

/*****************************************************************************/
/*
 * cached cell info
 */
struct afs_cache_cell
{
	char			name[64];	/* cell name (padded with NULs) */
	struct in_addr		servers[16];	/* cached cell servers */
};

struct afs_cache_cell_block
{
	struct afs_cache_cell entries[PAGE_SIZE/sizeof(struct afs_cache_cell)];
};

/*****************************************************************************/
/*
 * cached volume info
 * - indexed by (afs_cache_volix_t/4)
 * - (afs_cache_volix_t%4) is 0 for R/W, 1 for R/O and 2 for Bak (3 is not used)
 */
struct afs_cache_volume
{
	char			name[64];	/* volume name (padded with NULs) */
	afs_volid_t		vid[3];		/* volume IDs for R/W, R/O and Bak volumes */
	unsigned char		vidmask;	/* voltype mask for vid[] */
	unsigned char		_pad[1];
	unsigned short		nservers;	/* number of entries used in servers[] */
	struct in_addr		servers[8];	/* fileserver addresses */
	unsigned char		srvtmask[8];	/* voltype masks for servers[] */
#define AFS_CACHE_VOL_STM_RW	0x01 /* server holds a R/W version of the volume */
#define AFS_CACHE_VOL_STM_RO	0x02 /* server holds a R/O version of the volume */
#define AFS_CACHE_VOL_STM_BAK	0x04 /* server holds a backup version of the volume */

	afs_cache_cellix_t	cell_ix;	/* cell cache index (MAX_UINT if unused) */
	time_t			ctime;		/* time at which cached */
};

struct afs_cache_volume_block
{
	struct afs_cache_volume entries[PAGE_SIZE/sizeof(struct afs_cache_volume)];
};

/*****************************************************************************/
/*
 * cached vnode index
 * - map on a 1:1 basis with the vnode index table
 */
struct afs_cache_vnode_index
{
	afs_vnodeid_t		vnode;		/* vnode ID */
	time_t			atime;		/* last time accessed */
	afs_cache_volix_t	volume_ix;	/* volume cache index */
} __attribute__((packed));

#define AFS_CACHE_VNODE_INDEX_PER_BLOCK ((size_t)(PAGE_SIZE/sizeof(struct afs_cache_vnode_index)))

struct afs_cache_vnode_index_block
{
	struct afs_cache_vnode_index index[AFS_CACHE_VNODE_INDEX_PER_BLOCK];
};

/*****************************************************************************/
/*
 * cached vnode rights entry
 */
struct afs_cache_rights
{
	uid_t			uid;
	unsigned		access;
	unsigned short		mode;
} __attribute__((packed));

/*****************************************************************************/
/*
 * vnode (inode) metadata cache
 * - PAGE_SIZE in size
 */
struct afs_cache_vnode_block
{
	/* file ID */
	unsigned		unique;		/* FID unique */

	/* file status */
	afs_file_type_t		type;		/* file type */
	unsigned		nlink;		/* link count */
	size_t			size;		/* file size */
	afs_dataversion_t	version;	/* current data version */
	unsigned		author;		/* author ID */
	unsigned		owner;		/* owner ID */
	unsigned		anon_access;	/* access rights for unauthenticated caller */
	unsigned short		mode;		/* UNIX mode */
	time_t			mtime;		/* last time server changed data */
	time_t			cachetime;	/* time at which cached */

	/* file contents */
	afs_cache_bix_t		pt0_bix;	/* "page table 0" block index */
	afs_cache_bix_t		pgd_bix;	/* "page directory" block index */

	/* access rights */
	size_t			nrights;	/* number of cached rights */
	struct afs_cache_rights	rights[0];	/* cached access rights buffer */
};

#define AFS_CACHE_VNODE_MAXRIGHTS \
	((PAGE_SIZE - sizeof(struct afs_cache_vnode_block)) / sizeof(struct afs_cache_rights))

/*****************************************************************************/
/*
 * vnode data "page directory" block
 * - first 1024 pages don't map through here
 * - PAGE_SIZE in size
 */
struct afs_cache_pgd_block
{
	unsigned		_unused;
	afs_cache_bix_t		pt_bix[1023];	/* "page table" block indices */
};

/*****************************************************************************/
/*
 * vnode data "page table" block
 * - PAGE_SIZE in size
 */
struct afs_cache_pt_block
{
	afs_cache_bix_t		page_bix[1024];	/* "page" block indices */
};


#endif /* _LINUX_AFS_CACHE_LAYOUT_H */
