/* cache.c: AFS local cache management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/buffer_head.h>
#include "cell.h"
#include "cmservice.h"
#include "fsclient.h"
#include "cache.h"
#include "volume.h"
#include "vnode.h"
#include "internal.h"

static LIST_HEAD(afs_cache_list);
static DECLARE_MUTEX(afs_cache_list_sem);

static int afs_cache_read_sig(afs_cache_t *cache);

/*****************************************************************************/
/*
 * stat a cache device to find its device numbers
 */
static int afs_cache_get_kdev(const char *cachename, kdev_t *_kdev, struct file **_bdfile)
{
	struct nameidata nd;
	struct inode *inode;
	struct file *bdfile;
	int ret;

	/* look up the cache device file */
	if (!cachename)
		return -EINVAL;

	ret = path_lookup(cachename,LOOKUP_FOLLOW,&nd);
	if (ret)
		return ret;

	/* check it's a block device file */
	inode = nd.dentry->d_inode;
	ret = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode)) {
		path_release(&nd);
		return ret;
	}

	/* open a file for it */
	bdfile = dentry_open(nd.dentry,nd.mnt,O_RDWR);
	if (IS_ERR(bdfile))
		return ret;

	*_kdev = inode->i_rdev;
	*_bdfile = bdfile;
	return 0;
} /* end afs_cache_get_kdev() */

/*****************************************************************************/
/*
 * open a cache device
 */
int afs_cache_open(const char *cachename, afs_cache_t **_cache)
{
	struct list_head *_p;
	afs_cache_t *cache, *ncache;
	kdev_t dev;
	int ret = 0;

	_enter("{%s}",cachename);

	BUG();

	/* pre-allocate a cache record */
	ret = -ENOMEM;
	ncache = kmalloc(sizeof(*ncache),GFP_KERNEL);
	if (!ncache) {
		_leave(" = %d [lookup failed]",ret);
		return ret;
	}
	memset(ncache,0,sizeof(*ncache));

	atomic_set(&ncache->usage,1);
	INIT_LIST_HEAD(&ncache->link);
	init_rwsem(&ncache->sem);

	/* lookup the block device */
	ret = afs_cache_get_kdev(cachename,&dev,&ncache->bdfile);
	if (ret<0) {
		kfree(ncache);
		_leave(" = %d [lookup failed]",ret);
		return ret;
	}

	ncache->dev = dev;

	/* see if we've already got the cache open */
	cache = NULL;
	down(&afs_cache_list_sem);

	list_for_each(_p,&afs_cache_list) {
		cache = list_entry(_p,afs_cache_t,link);
		if (kdev_same(cache->dev,dev))
			goto found;
	}
	goto not_found;

	/* we already have the cache open */
 found:
	kdebug("kAFS re-using cache block dev %s",kdevname(dev));
	filp_close(cache->bdfile,NULL);
	kfree(ncache);
	ncache = NULL;
	afs_get_cache(cache);
	goto success;

	/* we don't already have the cache open */
 not_found:
	kdebug("kAFS using cache block dev %s",kdevname(dev));
	cache = ncache;
	ncache = NULL;

	/* grab a handle to the block device */
	ret = -ENOMEM;
	cache->bdev = bdget(kdev_t_to_nr(dev));
	if (!cache->bdev)
		goto out;

	/* open the block device node */
	ret = blkdev_get(cache->bdev,FMODE_READ|FMODE_WRITE,0,BDEV_RAW);
	if (ret)
		goto out;

	/* quick insanity check */
	check_disk_change(cache->dev);
	ret = -EACCES;
	if (is_read_only(cache->dev))
		goto out;

	/* mark it as mine */
	ret = bd_claim(cache->bdev,cache);
	if (ret)
		goto out;

	/* check it */
	ret = afs_cache_read_sig(cache);
	if (ret<0)
		goto out_unclaim;

	list_add_tail(&cache->link,&afs_cache_list);

 success:
	*_cache = cache;
	up(&afs_cache_list_sem);
	_leave(" = 0 (%p{%x})",cache->bdev,kdev_t_to_nr(cache->dev));
	return 0;

 out_unclaim:
	bd_release(cache->bdev);
 out:
	if (cache->bdfile)
		filp_close(cache->bdfile,NULL);
	if (cache->bdev) {
		blkdev_put(cache->bdev,BDEV_RAW);
		cache->bdev = NULL;
	}

	kfree(cache);

	up(&afs_cache_list_sem);
	_leave(" = %d",ret);
	return ret;

} /* end afs_cache_open() */

/*****************************************************************************/
/*
 * release a cache device
 */
void afs_put_cache(afs_cache_t *cache)
{
	_enter("%p{u=%d}",cache,atomic_read(&cache->usage));

	down(&afs_cache_list_sem);

	if (!atomic_dec_and_test(&cache->usage))
		cache = NULL;
	else
		list_del(&cache->link);

	up(&afs_cache_list_sem);

	/* if that was the last ref, then release the kernel resources */
	if (cache) {
		kdebug("kAFS releasing cache block dev %s",kdevname(cache->dev));
		filp_close(cache->bdfile,NULL);
		bd_release(cache->bdev);
		blkdev_put(cache->bdev,BDEV_RAW);
		kfree(cache);
	}

	_leave("");
} /* end afs_put_cache() */

/*****************************************************************************/
/*
 * read the cache signature block from the cache device
 */
static int afs_cache_read_sig(afs_cache_t *cache)
{
	struct afs_cache_super_block *csb;
	struct buffer_head *bh;

	bh = __bread(cache->bdev,0,PAGE_CACHE_SIZE);
	if (!bh)
		return -EIO;

	csb = (struct afs_cache_super_block*) bh->b_data;

	/* validate the cache superblock */
	if (memcmp(csb->magic,AFS_CACHE_SUPER_MAGIC,sizeof(csb->magic))!=0) {
		printk("kAFS cache magic string doesn't match\n");
		return -EINVAL;
	}
	if (csb->endian!=AFS_CACHE_SUPER_ENDIAN) {
		printk("kAFS endian spec doesn't match (%hx not %hx)\n",
		       csb->endian,AFS_CACHE_SUPER_ENDIAN);
		return -EINVAL;
	}
	if (csb->version!=AFS_CACHE_SUPER_VERSION) {
		printk("kAFS version doesn't match (%u not %u)\n",
		       csb->version,AFS_CACHE_SUPER_VERSION);
		return -EINVAL;
	}

	/* copy the layout into the cache management structure */
	memcpy(&cache->layout,csb,sizeof(cache->layout));

	brelse(bh);
	return 0;
} /* end afs_cache_read_sig() */

/*****************************************************************************/
/*
 * update part of one page in the cache
 * - the caller must hold any required protective locks
 * - based on rw_swap_page_base()
 */
static int afs_cache_update_region(afs_cache_t *cache, afs_cache_bix_t bix,
				   unsigned off, size_t size, void *buf)
{
	mm_segment_t oldfs;
	loff_t pos;
	int ret;

	_enter("%s,%u,%u,%u,",kdevname(cache->dev),bix,off,size);

	pos = bix*cache->layout.bsize + off;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = generic_file_write(cache->bdfile,buf,size,&pos);
	set_fs(oldfs);

	if (ret>0)
		ret = 0;

	_leave(" = %d",ret);
	return ret;
} /* end afs_cache_update_region() */

/*****************************************************************************/
/*
 * look up cell information in the cache
 * - mkafscache preloads /etc/sysconfig/kafs/cell-serv-db into the cache
 */
int afs_cache_lookup_cell(afs_cache_t *cache,
			  afs_cell_t *cell)
{
	struct afs_cache_cell_block *ccells;
	struct afs_cache_cell *ccell;
	struct buffer_head *bh;
	afs_cache_cellix_t cix, stop, rem;
	afs_cache_bix_t bix;
	int loop;

	_enter("%s,%s",kdevname(cache->dev),cell->name);

	BUG();

	rem = cache->layout.ncells;

	for (bix=cache->layout.off_cell_cache; bix<cache->layout.off_volume_bitmap; bix++) {
		/* read the next block */
		bh = __bread(cache->bdev,bix,PAGE_CACHE_SIZE);
		if (!bh) {
			kleave(" = -EIO (block %u)",bix);
			return -EIO;
		}

		ccells = (struct afs_cache_cell_block*) bh->b_data;

		/* and scan it */
		stop = min((size_t)rem,
			   sizeof(struct afs_cache_cell_block)/sizeof(struct afs_cache_cell));
		rem -= stop;

		for (cix=0; cix<stop; cix++) {
			ccell = &ccells->entries[cix];
			if (strncmp(cell->name,ccell->name,sizeof(ccell->name))==0)
				goto found;
		}

		brelse(bh);
	}

	_leave(" = -ENOENT");
	return -ENOENT;

 found:
	/* found the cell record - copy out the details */
	bix -= cache->layout.off_cell_cache;
	cell->cache_ix = cix;
	cell->cache_ix += bix * sizeof(struct afs_cache_cell_block)/sizeof(struct afs_cache_cell);

	memcpy(cell->vl_addrs,ccell->servers,sizeof(cell->vl_addrs));

	for (loop=0; loop<sizeof(cell->vl_addrs)/sizeof(cell->vl_addrs[0]); loop++)
		if (!cell->vl_addrs[loop].s_addr)
			break;
	cell->vl_naddrs = loop;

	brelse(bh);
	_leave(" = 0 (bix=%u cix=%u ccix=%u)",bix,cix,cell->cache_ix);
	return 0;

} /* end afs_cache_lookup_cell() */

/*****************************************************************************/
/*
 * search for a volume location record in the cache
 */
int afs_cache_lookup_vlocation(afs_vlocation_t *vlocation)
{
	BUG();

#if 0
	struct afs_cache_volume_block *cvols;
	struct afs_cache_volume *cvol;
	struct buffer_head *bh;
	afs_cache_bix_t bix;
	unsigned rem, stop, ix;

	_enter("%s,{v=%s cix=%u}",
	       kdevname(vlocation->cache->dev),vlocation->vldb.name,vlocation->vldb.cell_ix);

	rem = vlocation->cache->layout.nvols;

	for (bix=vlocation->cache->layout.off_volume_cache;
	     bix<vlocation->cache->layout.off_vnode_bitmap;
	     bix++
	     ) {
		/* read the next block */
		bh = __bread(vlocation->cache->bdev,bix,PAGE_CACHE_SIZE);
		if (!bh) {
			kleave(" = -EIO (block %u)",bix);
			return -EIO;
		}

		cvols = (struct afs_cache_volume_block*) bh->b_data;

		/* and scan it */
		stop = min((size_t)rem,sizeof(*cvols)/sizeof(*cvol));
		rem -= stop;

		for (ix=0; ix<stop; ix++) {
			cvol = &cvols->entries[ix];
			if (cvol->name[0])
				_debug("FOUND[%u.%u]: cell %u vol '%s' %08x",
				       bix,ix,cvol->cell_ix,cvol->name,cvol->vid[0]);
			if (cvol->cell_ix==vlocation->vldb.cell_ix &&
			    memcmp(vlocation->vldb.name,cvol->name,sizeof(cvol->name))==0) {
				goto found;
			}
		}

		brelse(bh);
	}

	_leave(" = %d",-ENOENT);
	return -ENOENT;

 found:
	/* found the cell record */
	memcpy(&vlocation->vldb,cvol,sizeof(*cvol));
	brelse(bh);

	/* note the volume ID */
	bix -= vlocation->cache->layout.off_volume_cache;
	vlocation->vix.index = (ix + bix * (sizeof(*cvols)/sizeof(*cvol))) << 2;

	_leave(" = 0 (bix=%u ix=%u vix=%hu)",bix,ix,vlocation->vix.index);
#endif
	return 0;

} /* end afs_cache_lookup_vlocation() */

/*****************************************************************************/
/*
 * search for a volume location record in the cache, and if one's not available then reap the
 * eldest not currently in use
 */
int afs_cache_update_vlocation(afs_vlocation_t *vlocation)
{
	BUG();

#if 0
	struct afs_cache_volume_block *cvols;
	struct afs_cache_volume *cvol;
	struct buffer_head *bh;
	afs_cache_bix_t bix;
	unsigned rem, stop, ix, candidate, tmp;
	time_t cand_age;
	int ret;


	_enter("%s,{v=%s cix=%u}",
	       kdevname(vlocation->cache->dev),vlocation->vldb.name,vlocation->vldb.cell_ix);

	candidate = UINT_MAX;
	cand_age = ULONG_MAX;
	rem = vlocation->cache->layout.nvols;

	for (bix=vlocation->cache->layout.off_volume_cache;
	     bix<vlocation->cache->layout.off_vnode_bitmap;
	     bix++
	     ) {
		/* read the next block */
		bh = __bread(vlocation->cache->bdev,bix,PAGE_CACHE_SIZE);
		if (!bh) {
			kleave(" = -EIO (block %u)",bix);
			return -EIO;
		}

		cvols = (struct afs_cache_volume_block*) bh->b_data;

		/* and scan it */
		stop = min((size_t)rem,sizeof(*cvols)/sizeof(*cvol));
		rem -= stop;

		for (ix=0; ix<stop; ix++) {
			cvol = &cvols->entries[ix];
			if (cvol->name[0])
				_debug("FOUND[%u.%u]: cell %u vol '%s' %08x",
				       bix,ix,cvol->cell_ix,cvol->name,cvol->vid[0]);
			if (cvol->cell_ix==vlocation->vldb.cell_ix &&
			    memcmp(vlocation->vldb.name,cvol->name,sizeof(cvol->name))==0) {
				goto found;
			}

			if (candidate!=UINT_MAX && cvol->ctime<cand_age) {
				/* TODO: don't recycle volumes currently in use */
				cand_age = cvol->ctime;
				candidate = bix - vlocation->cache->layout.off_volume_cache;
				candidate = ix + candidate * sizeof(*cvols)/sizeof(*cvol);
			}
		}

		brelse(bh);
	}

	/* TODO: recycle old entry if no spare slots available */
	if (vlocation->cache->layout.nvols>=vlocation->cache->layout.maxvols)
		BUG();

	/* insert new entry */
	ix = vlocation->vix.index = vlocation->cache->layout.nvols++;
	tmp = (sizeof(*cvols)/sizeof(*cvol));
	bix = ix / tmp + vlocation->cache->layout.off_volume_cache;
	ix %= tmp;

	kdebug("INSERT (bix=%u ix=%u)",bix,ix);
	ret = afs_cache_update_region(vlocation->cache,
				      bix,
				      ix*sizeof(*cvol),
				      sizeof(*cvol),
				      &vlocation->vldb);
	if (ret<0)
		goto out;

	/* update the superblock */
	ret = afs_cache_update_region(vlocation->cache,
				      0,0,
				      sizeof(vlocation->cache->layout),
				      &vlocation->cache->layout);

	/* TODO: handle failure by winding back cache->layout.nvols */

 out:
	_leave(" = %d (bix=%u ix=%u vix=%hu)",ret,bix,ix,vlocation->vix.index);
	return ret;

 found:
	brelse(bh);

	/* update the on-disk cache with the latest news */
	_debug("UPDATE (bix=%u ix=%u)",bix,ix);
	ret = afs_cache_update_region(vlocation->cache,
				      bix,
				      ix*sizeof(*cvol),
				      sizeof(*cvol),
				      &vlocation->vldb);
	if (ret<0)
		goto out;

	/* found the cell record - note the volume ID */
	bix -= vlocation->cache->layout.off_volume_cache;
	vlocation->vix.index = (ix + bix * (sizeof(*cvols)/sizeof(*cvol))) << 2;

	_leave(" = 0 (bix=%u ix=%u vix=%hu)",bix,ix,vlocation->vix.index);
#endif
	return 0;

} /* end afs_cache_update_vlocation() */

/*****************************************************************************/
/*
 * search for a vnode record in the cache, and if one's not available then reap the
 * eldest not currently in use
 */
int afs_cache_lookup_vnode(afs_volume_t *volume, afs_vnode_t *vnode)
{
	BUG();

#if 0
	struct afs_cache_vnode_index_block *cindexb;
	struct afs_cache_vnode_index cindex;
	struct buffer_head *bh;
	afs_cache_bix_t bix;
	unsigned rem, stop, ix, candidate, tmp;
	time_t cand_age;
	int ret;

	_enter("{cix=%u vix=%u},{%u,%u,%u}",
	       volume->cix,volume->vix.index,vnode->fid.vid,vnode->fid.vnode,vnode->fid.unique);

	candidate = UINT_MAX;
	cand_age = ULONG_MAX;
	rem = volume->cache->layout.nvnodes;

	for (bix=volume->cache->layout.off_vnode_index;
	     bix<volume->cache->layout.off_vnode_cache;
	     bix++
	     ) {
		/* read the next block */
		bh = __bread(volume->cache->bdev,bix,PAGE_CACHE_SIZE);
		if (!bh) {
			kleave(" = -EIO (block %u)",bix);
			return -EIO;
		}

		cindexb = (struct afs_cache_vnode_index_block*) bh->b_data;

		/* and scan it */
		stop = min((size_t)rem,AFS_CACHE_VNODE_INDEX_PER_BLOCK);
		rem -= stop;

		for (ix=0; ix<stop; ix++) {
			memcpy(&cindex,&cindexb->index[ix],sizeof(cindex));

#if 0
			if (cindex.vnode>0)
				kdebug("FOUND[%u.%u]: vix %u vnode %u",
				       bix,ix,cindex.volume_ix.index,cindex.vnode);
#endif

			if (cindex.vnode==vnode->fid.vnode &&
			    cindex.volume_ix.index==volume->vix.index)
				goto found;

			if (candidate!=UINT_MAX && cindex.atime<cand_age) {
				/* TODO: don't recycle volumes currently in use */
				cand_age = cindex.atime;
				candidate = bix - volume->cache->layout.off_vnode_index;
				candidate = ix + candidate * AFS_CACHE_VNODE_INDEX_PER_BLOCK;
			}
		}

		brelse(bh);
	}

	/* TODO: recycle old entry if no spare slots available */
	if (volume->cache->layout.nvnodes>=volume->cache->layout.maxvnodes)
		BUG();

	/* append new entry */
	vnode->nix = volume->cache->layout.nvnodes++;

	cindex.vnode = vnode->fid.vnode;
	cindex.atime = xtime.tv_sec;
	cindex.volume_ix = volume->vix;

	ix = vnode->nix;
	tmp = AFS_CACHE_VNODE_INDEX_PER_BLOCK;
	bix = ix / tmp + volume->cache->layout.off_vnode_index;
	ix %= tmp;

	_debug("CACHE APPEND VNODE %u (bix=%u ix=%u)",vnode->nix,bix,ix);
	ret = afs_cache_update_region(volume->cache,
				      bix,
				      ix*sizeof(cindex),
				      sizeof(cindex),
				      &cindex);
	if (ret<0)
		goto out;

	/* update the superblock */
	ret = afs_cache_update_region(volume->cache,
				      0,0,
				      sizeof(volume->cache->layout),
				      &volume->cache->layout);

	/* TODO: handle failure by winding back cache->layout.nvnodes */

 out:
	_leave(" = %d (bix=%u ix=%u nix=%u)",ret,bix,ix,vnode->nix);
	return ret;

 found:
	brelse(bh);

	cindex.atime = xtime.tv_sec;

	/* update the on-disk cache with the latest news */
	_debug("UPDATE (bix=%u ix=%u)",bix,ix);
	ret = afs_cache_update_region(volume->cache,
				      bix,
				      ix*sizeof(cindex),
				      sizeof(cindex),
				      &cindex);
	if (ret<0)
		goto out;

	/* found the cell record - note the volume ID */
	bix -= volume->cache->layout.off_vnode_index;
	vnode->nix = ix + bix * AFS_CACHE_VNODE_INDEX_PER_BLOCK;

	_leave(" = 0 (bix=%u ix=%u nix=%u)",bix,ix,vnode->nix);
#endif
	return 0;

} /* end afs_cache_lookup_vnode() */
