/* cache.h: AFS local cache management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_CACHE_H
#define _LINUX_AFS_CACHE_H

#include <linux/fs.h>
#include "cache-layout.h"

#ifdef __KERNEL__

/*****************************************************************************/
/*
 * AFS cache management record
 */
struct afs_cache
{
	atomic_t			usage;		/* usage count */
	struct list_head		link;		/* link in cache list */
	kdev_t				dev;		/* device numbers */
	struct block_device		*bdev;		/* block device */
	struct file			*bdfile;	/* file attached to block device */
	struct rw_semaphore		sem;		/* access semaphore */
	struct afs_cache_super_block	layout;		/* layout description */
};

extern int afs_cache_open(const char *name, afs_cache_t **_cache);

#define afs_get_cache(C) do { atomic_inc(&(C)->usage); } while(0)

extern void afs_put_cache(afs_cache_t *cache);

extern int afs_cache_lookup_cell(afs_cache_t *cache, afs_cell_t *cell);
extern int afs_cache_lookup_vlocation(afs_vlocation_t *vlocation);
extern int afs_cache_update_vlocation(afs_vlocation_t *vlocation);
extern int afs_cache_lookup_vnode(afs_volume_t *volume, afs_vnode_t *vnode);

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_CACHE_H */
