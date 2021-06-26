/*
 *  linux/mm/vmscan.c
 *
 *  The pageout daemon, decides which pages to evict (swap out) and
 *  does the actual work of freeing them.
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/mm_inline.h>
#include <linux/suspend.h>

#include <asm/pgalloc.h>

static void refill_freelist(void);
static void wakeup_memwaiters(void);
/*
 * The "priority" of VM scanning is how much of the queues we
 * will scan in one go. A value of 6 for DEF_PRIORITY implies
 * that we'll scan 1/64th of the queues ("queue_length >> 6")
 * during a normal aging round.
 */
#define DEF_PRIORITY (6)

static inline void age_page_up(struct page *page)
{
	page->age = min((int) (page->age + PAGE_AGE_ADV), PAGE_AGE_MAX); 
}

static inline void age_page_down(struct page *page)
{
	page->age -= min(PAGE_AGE_DECL, (int)page->age);
}

/* Must be called with page's pte_chain_lock held. */
static inline int page_mapping_inuse(struct page * page)
{
	struct address_space * mapping = page->mapping;

	/* Page is in somebody's page tables. */
	if (page->pte_chain)
		return 1;

	/* XXX: does this happen ? */
	if (!mapping)
		return 0;

	/* File is mmaped by somebody. */
	if (mapping->i_mmap || mapping->i_mmap_shared)
		return 1;

	return 0;
}

/**
 * reclaim_page - reclaims one page from the inactive_clean list
 * @zone: reclaim a page from this zone
 *
 * The pages on the inactive_clean can be instantly reclaimed.
 * The tests look impressive, but most of the time we'll grab
 * the first page of the list and exit successfully.
 */
struct page * reclaim_page(zone_t * zone)
{
	struct page * page = NULL;
	struct list_head * page_lru;
	swp_entry_t entry = {0};
	int maxscan;

	/*
	 * We need to hold the pagecache_lock around all tests to make sure
	 * reclaim_page() cannot race with find_get_page() and friends.
	 */
	spin_lock(&pagemap_lru_lock);
	spin_lock(&pagecache_lock);
	maxscan = zone->inactive_clean_pages;
	while (maxscan-- && !list_empty(&zone->inactive_clean_list)) {
		page_lru = zone->inactive_clean_list.prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (unlikely(!PageInactiveClean(page))) {
			printk("VM: reclaim_page, wrong page on list.\n");
			list_del(page_lru);
			page_zone(page)->inactive_clean_pages--;
			continue;
		}

		/* Page is being freed */
		if (unlikely(page_count(page)) == 0) {
			list_del(page_lru);
			list_add(page_lru, &zone->inactive_clean_list);
			continue;
		}

		/* Page cannot be reclaimed ?  Move to inactive_dirty list. */
		pte_chain_lock(page);
		if (unlikely(page->pte_chain || page->buffers ||
				PageReferenced(page) || PageDirty(page) ||
				page_count(page) > 1 || TryLockPage(page))) {
			del_page_from_inactive_clean_list(page);
			add_page_to_inactive_dirty_list(page);
			pte_chain_unlock(page);
			continue;
		}

		/*
		 * From here until reaching either the bottom of the loop
		 * or found_page: the pte_chain_lock is held.
		 */

		/* OK, remove the page from the caches. */
                if (PageSwapCache(page)) {
			entry.val = page->index;
			__delete_from_swap_cache(page);
			goto found_page;
		}

		if (page->mapping) {
			__remove_inode_page(page);
			goto found_page;
		}

		/* We should never ever get here. */
		printk(KERN_ERR "VM: reclaim_page, found unknown page\n");
		list_del(page_lru);
		zone->inactive_clean_pages--;
		pte_chain_unlock(page);
		UnlockPage(page);
	}
	spin_unlock(&pagecache_lock);
	spin_unlock(&pagemap_lru_lock);
	return NULL;


found_page:
	__lru_cache_del(page);
	pte_chain_unlock(page);
	spin_unlock(&pagecache_lock);
	spin_unlock(&pagemap_lru_lock);
	if (entry.val)
		swap_free(entry);
	UnlockPage(page);
	page->age = PAGE_AGE_START;
	if (page_count(page) != 1)
		printk("VM: reclaim_page, found page with count %d!\n",
				page_count(page));
	return page;
}

/**
 * page_dirty - do we need to write the data out to disk
 * @page: page to test
 *
 * Returns true if the page contains data which needs to
 * be written to disk.  Doesn't test the page tables (yet?).
 */
static inline int page_dirty(struct page *page)
{
	struct buffer_head *tmp, *bh;

	if (PageDirty(page))
		return 1;

	if (page->mapping && !page->buffers)
		return 0;

	tmp = bh = page->buffers;

	do {
		if (tmp->b_state & ((1<<BH_Dirty) | (1<<BH_Lock)))
			return 1;
		tmp = tmp->b_this_page;
	} while (tmp != bh);

	return 0;
}

/**
 * page_launder_zone - clean dirty inactive pages, move to inactive_clean list
 * @zone: zone to free pages in
 * @gfp_mask: what operations we are allowed to do
 * @full_flush: full-out page flushing, if we couldn't get enough clean pages
 *
 * This function is called when we are low on free / inactive_clean
 * pages, its purpose is to refill the free/clean list as efficiently
 * as possible.
 *
 * This means we do writes asynchronously as long as possible and will
 * only sleep on IO when we don't have another option. Since writeouts
 * cause disk seeks and make read IO slower, we skip writes alltogether
 * when the amount of dirty pages is small.
 *
 * This code is heavily inspired by the FreeBSD source code. Thanks
 * go out to Matthew Dillon.
 */
int page_launder_zone(zone_t * zone, int gfp_mask, int full_flush)
{
	int maxscan, cleaned_pages, target, maxlaunder, iopages;
	struct list_head * entry, * next;

	target = free_plenty(zone);
	cleaned_pages = iopages = 0;

	/* If we can get away with it, only flush 2 MB worth of dirty pages */
	if (full_flush)
		maxlaunder = 1000000;
	else {
		maxlaunder = min_t(int, 512, zone->inactive_dirty_pages / 4);
		maxlaunder = max(maxlaunder, free_plenty(zone));
	}
	
	/* The main launder loop. */
	spin_lock(&pagemap_lru_lock);
	maxscan = zone->inactive_dirty_pages;
rescan:
	entry = zone->inactive_dirty_list.prev;
	next = entry->prev;
	while (maxscan-- && !list_empty(&zone->inactive_dirty_list) &&
			next != &zone->inactive_dirty_list) {
		struct page * page;
		
		/* Low latency reschedule point */
		if (current->need_resched) {
			spin_unlock(&pagemap_lru_lock);
			schedule();
			spin_lock(&pagemap_lru_lock);
			continue;
		}

		entry = next;
		next = entry->prev;
		page = list_entry(entry, struct page, lru);

		/* This page was removed while we looked the other way. */
		if (!PageInactiveDirty(page))
			goto rescan;

		if (cleaned_pages > target)
			break;

		/* Stop doing IO if we've laundered too many pages already. */
		if (maxlaunder < 0)
			gfp_mask &= ~(__GFP_IO|__GFP_FS);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageInactiveDirty(page)) {
			printk("VM: page_launder, wrong page on list.\n");
			list_del(entry);
			nr_inactive_dirty_pages--;
			page_zone(page)->inactive_dirty_pages--;
			continue;
		}

		/*
		 * Page is being freed, don't worry about it.
		 */
		if (unlikely(page_count(page)) == 0)
			continue;

		/*
		 * The page is locked. IO in progress?
		 * Acquire PG_locked early in order to safely
		 * access page->mapping.
		 */
		if (unlikely(TryLockPage(page)))
			continue;

		/*
		 * The page is in active use or really unfreeable. Move to
		 * the active list and adjust the page age if needed.
		 */
		pte_chain_lock(page);
		if (page_referenced(page) && page_mapping_inuse(page) &&
				!page_over_rsslimit(page)) {
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			page->age = max((int)page->age, PAGE_AGE_START);
			pte_chain_unlock(page);
			UnlockPage(page);
			continue;
		}
		pte_chain_unlock(page);

		/*
		 * Anonymous process memory without backing store. Try to
		 * allocate it some swap space here.
		 *
		 * XXX: implement swap clustering ?
		 */
		pte_chain_lock(page);
		if (page->pte_chain && !page->mapping && !page->buffers) {
			page_cache_get(page);
			pte_chain_unlock(page);
			spin_unlock(&pagemap_lru_lock);
			if (!add_to_swap(page)) {
				activate_page(page);
				UnlockPage(page);
				page_cache_release(page);
				spin_lock(&pagemap_lru_lock);
				continue;
			}
			page_cache_release(page);
			spin_lock(&pagemap_lru_lock);
			pte_chain_lock(page);
		}

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (page->pte_chain && page->mapping) {
			switch (try_to_unmap(page)) {
				case SWAP_ERROR:
				case SWAP_FAIL:
					goto page_active;
				case SWAP_AGAIN:
					pte_chain_unlock(page);
					UnlockPage(page);
					continue;
				case SWAP_SUCCESS:
					; /* try to free the page below */
			}
		}
		pte_chain_unlock(page);

		if (PageDirty(page) && page->mapping) {
			/*
			 * It is not critical here to write it only if
			 * the page is unmapped beause any direct writer
			 * like O_DIRECT would set the PG_dirty bitflag
			 * on the physical page after having successfully
			 * pinned it and after the I/O to the page is finished,
			 * so the direct writes to the page cannot get lost.
			 */
			int (*writepage)(struct page *);

			writepage = page->mapping->a_ops->writepage;
			if ((gfp_mask & __GFP_FS) && writepage) {
				ClearPageDirty(page);
				SetPageLaunder(page);
				page_cache_get(page);
				spin_unlock(&pagemap_lru_lock);

				writepage(page);
				maxlaunder--;
				page_cache_release(page);

				spin_lock(&pagemap_lru_lock);
				continue;
			} else {
				UnlockPage(page);
				list_del(entry);
				list_add(entry, &zone->inactive_dirty_list);
				continue;
			}
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we try to free
		 * the page as well.
		 */
		if (page->buffers) {
			/* To avoid freeing our page before we're done. */
			page_cache_get(page);
			
			spin_unlock(&pagemap_lru_lock);


			if (try_to_release_page(page, gfp_mask)) {
				if (!page->mapping) {
					/*
					 * We must not allow an anon page
					 * with no buffers to be visible on
					 * the LRU, so we unlock the page after
					 * taking the lru lock
					 */
					spin_lock(&pagemap_lru_lock);
					UnlockPage(page);
					__lru_cache_del(page);

					/* effectively free the page here */
					page_cache_release(page);

					cleaned_pages++;
					continue;
				} else {
					/*
					 * We freed the buffers but may have
					 * slept; undo the stuff we did before
					 * try_to_release_page and fall through
					 * to the next step.
					 * But only if the page is still on the inact. dirty 
					 * list.
					 */

					spin_lock(&pagemap_lru_lock);
					/* Check if the page was removed from the list
					 * while we looked the other way. 
					 */
					if (!PageInactiveDirty(page)) {
						page_cache_release(page);
						continue;
					}
					page_cache_release(page);
				}
			} else {
				/* failed to drop the buffers so stop here */
				UnlockPage(page);
				page_cache_release(page);
				maxlaunder--;

				spin_lock(&pagemap_lru_lock);
				continue;
			}
		}


		/*
		 * If the page is really freeable now, move it to the
		 * inactive_clean list.
		 *
		 * We re-test everything since the page could have been
		 * used by somebody else while we waited on IO above.
		 * This test is not safe from races, but only the one
		 * in reclaim_page() needs to be.
		 */
		pte_chain_lock(page);
		if (page->mapping && !PageDirty(page) && !page->pte_chain &&
				page_count(page) == 1) {
			del_page_from_inactive_dirty_list(page);
			add_page_to_inactive_clean_list(page);
			pte_chain_unlock(page);
			UnlockPage(page);
			cleaned_pages++;
		} else {
			/*
			 * OK, we don't know what to do with the page.
			 * It's no use keeping it here, so we move it to
			 * the active list.
			 */
page_active:
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			pte_chain_unlock(page);
			UnlockPage(page);
		}
	}
	spin_unlock(&pagemap_lru_lock);

	/* Return the number of pages moved to the inactive_clean list. */
	return cleaned_pages;
}

/**
 * page_launder - clean dirty inactive pages, move to inactive_clean list
 * @gfp_mask: what operations we are allowed to do
 *
 * This function iterates over all zones and calls page_launder_zone(),
 * balancing still needs to be added...
 */
int page_launder(int gfp_mask)
{
	struct zone_struct * zone;
	int freed = 0;

	/* Global balancing while we have a global shortage. */
	if (free_high(ALL_ZONES) >= 0)
		for_each_zone(zone)
			if (free_plenty(zone) >= 0)
				freed += page_launder_zone(zone, gfp_mask, 0);
	
	/* Clean up the remaining zones with a serious shortage, if any. */
	for_each_zone(zone)
		if (free_min(zone) >= 0)
			freed += page_launder_zone(zone, gfp_mask, 1);

	return freed;
}

/**
 * refill_inactive_zone - scan the active list and find pages to deactivate
 * @priority: how much are we allowed to scan
 *
 * This function will scan a portion of the active list of a zone to find
 * unused pages, those pages will then be moved to the inactive list.
 */
int refill_inactive_zone(struct zone_struct * zone, int priority)
{
	int maxscan = zone->active_pages >> priority;
	int target = inactive_high(zone);
	struct list_head * page_lru;
	int nr_deactivated = 0;
	struct page * page;

	/* Take the lock while messing with the list... */
	spin_lock(&pagemap_lru_lock);
	while (maxscan-- && !list_empty(&zone->active_list)) {
		page_lru = zone->active_list.prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (unlikely(!PageActive(page))) {
			printk("VM: refill_inactive, wrong page on list.\n");
			list_del(page_lru);
			nr_active_pages--;
			continue;
		}
		
		/* Needed to follow page->mapping */
		if (TryLockPage(page)) {
			list_del(page_lru);
			list_add(page_lru, &zone->active_list);
			continue;
		}

		/*
		 * If the object the page is in is not in use we don't
		 * bother with page aging.  If the page is touched again
		 * while on the inactive_clean list it'll be reactivated.
		 * From here until the end of the current iteration
		 * both PG_locked and the pte_chain_lock are held.
		 */
		pte_chain_lock(page);
		if (!page_mapping_inuse(page)) {
			pte_chain_unlock(page);
			UnlockPage(page);
			drop_page(page);
			continue;
		}

		/*
		 * Do aging on the pages.
		 */
		if (page_referenced(page)) {
			age_page_up(page);
		} else {
			age_page_down(page);
		}

		/* 
		 * If the page age is 'hot' and the process using the
		 * page doesn't exceed its RSS limit we keep the page.
		 * Otherwise we move it to the inactive_dirty list.
		 */
		if (page->age && !page_over_rsslimit(page)) {
			list_del(page_lru);
			list_add(page_lru, &zone->active_list);
		} else {
			deactivate_page_nolock(page);
			if (++nr_deactivated > target) {
				pte_chain_unlock(page);
				UnlockPage(page);
				goto done;
			}
		}
		pte_chain_unlock(page);
		UnlockPage(page);

		/* Low latency reschedule point */
		if (current->need_resched) {
			spin_unlock(&pagemap_lru_lock);
			schedule();
			spin_lock(&pagemap_lru_lock);
		}
	}

done:
	spin_unlock(&pagemap_lru_lock);

	return nr_deactivated;
}

/**
 * refill_inactive - checks all zones and refills the inactive list as needed
 *
 * This function tries to balance page eviction from all zones by aging
 * the pages from each zone in the same ratio until the global inactive
 * shortage is resolved. After that it does one last "clean-up" scan to
 * fix up local inactive shortages.
 */
int refill_inactive(void)
{
	int maxtry = 1 << DEF_PRIORITY;
	zone_t * zone;
	int ret = 0;

	/* Global balancing while we have a global shortage. */
	while (maxtry-- && inactive_low(ALL_ZONES) >= 0) {
		for_each_zone(zone) {
			if (inactive_high(zone) >= 0)
				ret += refill_inactive_zone(zone, DEF_PRIORITY);
		}
	}

	/* Local balancing for zones which really need it. */
	for_each_zone(zone) {
		if (inactive_min(zone) >= 0)
			ret += refill_inactive_zone(zone, 0);
	}

	return ret;
}

/**
 * background_aging - slow background aging of zones
 * @priority: priority at which to scan
 *
 * When the VM load is low or nonexistant, this function is
 * called once a second to "sort" the pages in the VM. This
 * way we know which pages to evict once a load spike happens.
 * The effects of this function are very slow, the CPU usage
 * should be minimal to nonexistant under most loads.
 */
static inline void background_aging(int priority)
{
	struct zone_struct * zone;

	for_each_zone(zone)
		if (inactive_high(zone) > 0)
			refill_inactive_zone(zone, priority);
}

/*
 * Worker function for kswapd and try_to_free_pages, we get
 * called whenever there is a shortage of free/inactive_clean
 * pages.
 *
 * This function will also move pages to the inactive list,
 * if needed.
 */
static int do_try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 0;

	/*
	 * Eat memory from filesystem page cache, buffer cache,
	 * dentry, inode and filesystem quota caches.
	 */
	ret += page_launder(gfp_mask);
	ret += shrink_dcache_memory(DEF_PRIORITY, gfp_mask);
	ret += shrink_icache_memory(1, gfp_mask);
#ifdef CONFIG_QUOTA
	ret += shrink_dqcache_memory(DEF_PRIORITY, gfp_mask);
#endif

	/*
	 * Move pages from the active list to the inactive list.
	 */
	refill_inactive();

	/* 	
	 * Reclaim unused slab cache memory.
	 */
	ret += kmem_cache_reap(gfp_mask);

	refill_freelist();

	/* Start IO when needed. */
	if (free_plenty(ALL_ZONES) > 0 || free_low(ANY_ZONE) > 0)
		run_task_queue(&tq_disk);

	/*
	 * Hmm.. Cache shrink failed - time to kill something?
	 * Mhwahahhaha! This is the part I really like. Giggle.
	 */
	if (!ret && free_min(ANY_ZONE) > 0)
		out_of_memory();

	return ret;
}

/**
 * refill_freelist - move inactive_clean pages to free list if needed
 *
 * Move some pages from the inactive_clean lists to the free
 * lists so atomic allocations have pages to work from. This
 * function really only does something when we don't have a 
 * userspace load on __alloc_pages().
 *
 * We refill the freelist in a bump from pages_min to pages_min * 2
 * in order to give the buddy allocator something to play with.
 */
static void refill_freelist(void)
{
	struct page * page;
	zone_t * zone;

	for_each_zone(zone) {
		if (!zone->size || zone->free_pages >= zone->pages_min)
			continue;

		while (zone->free_pages < zone->pages_min * 2) {
			page = reclaim_page(zone);
			if (!page)
				break;
			__free_page(page);
		}
	}
}

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process. 
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
int kswapd(void *unused)
{
	struct task_struct *tsk = current;

	daemonize();
	strcpy(tsk->comm, "kswapd");
	sigfillset(&tsk->blocked);
	
	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC | PF_KERNTHREAD;

	/*
	 * Kswapd main loop.
	 */
	for (;;) {
		static long recalc = 0;
		static long recalc_inode = 0;

		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);

		/*
		 * We try to rebalance the VM either when we have a
		 * global shortage of free pages or when one particular
		 * zone is very short on free pages.
		 */
		if (free_high(ALL_ZONES) >= 0 || free_low(ANY_ZONE) > 0)
			do_try_to_free_pages(GFP_KSWAPD);

		refill_freelist();

		/* Once a second ... */
		if (time_after(jiffies, recalc + HZ)) {
			recalc = jiffies;

			/* Do background page aging. */
			background_aging(DEF_PRIORITY);
		}

		/* Once every 5 minutes ... */
		if (time_after(jiffies, recalc_inode + 300*HZ)) {
			recalc_inode = jiffies;
			shrink_dcache_memory(6, GFP_KERNEL);
			shrink_icache_memory(6, GFP_KERNEL);
#ifdef CONFIG_QUOTA
			shrink_dqcache_memory(6, GFP_KERNEL);
#endif
		}

		wakeup_memwaiters();
	}
}

static int kswapd_overloaded;
unsigned int kswapd_minfree; /* initialized in mm/page_alloc.c */
DECLARE_WAIT_QUEUE_HEAD(kswapd_wait);
DECLARE_WAIT_QUEUE_HEAD(kswapd_done);

/**
 * wakeup_kswapd - wake up the pageout daemon
 * gfp_mask: page freeing flags
 *
 * This function wakes up kswapd and can, under heavy VM pressure,
 * put the calling task to sleep temporarily.
 */
void wakeup_kswapd(unsigned int gfp_mask)
{
	DECLARE_WAITQUEUE(wait, current);

	/* If we're in the memory freeing business ourself, don't sleep
	 * but just wake kswapd and go back to businesss.
	 */
	if (current->flags & PF_MEMALLOC) {
		wake_up_interruptible(&kswapd_wait);
		return;
	}

	/* We need all of kswapd's GFP flags, otherwise we can't sleep on it.
	 * We still wake kswapd of course.
	 */
	if ((gfp_mask & GFP_KSWAPD) != GFP_KSWAPD) {
		wake_up_interruptible(&kswapd_wait);
		return;
	}
	
	add_wait_queue(&kswapd_done, &wait);
        set_current_state(TASK_UNINTERRUPTIBLE);
        
        /* Wake kswapd .... */
        wake_up_interruptible(&kswapd_wait);
        
        /* ... and check if we need to wait on it */
	if ((free_low(ALL_ZONES) > (kswapd_minfree / 2)) && !kswapd_overloaded)
		schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&kswapd_done, &wait);
}

static void wakeup_memwaiters(void)
{
	DECLARE_WAITQUEUE(wait, current);
	        
	add_wait_queue(&kswapd_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	/* Don't let the processes waiting on memory get stuck, ever. */
	wake_up(&kswapd_done);

	/* Enough free RAM, we can easily keep up with memory demand. */
	if (free_high(ALL_ZONES) <= 0) {
		schedule_timeout(HZ);
		remove_wait_queue(&kswapd_wait, &wait);
		return;
	}
	remove_wait_queue(&kswapd_wait, &wait);

	/* OK, the VM is very loaded. Sleep instead of using all CPU. */
	kswapd_overloaded = 1;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 4);
	kswapd_overloaded = 0;
	wmb();
	return;
}

/**
 * try_to_free_pages - run the pageout code ourselves
 * gfp_mask: mask of things the pageout code is allowed to do
 *
 * When the load on the system gets higher, it can happen
 * that kswapd no longer manages to keep enough memory
 * free. In those cases user programs allocating memory
 * will call try_to_free_pages() and help the pageout code.
 * This has the effects of freeing memory and slowing down
 * the largest memory hogs a bit.
 */
int try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 1;

	gfp_mask = pf_gfp_mask(gfp_mask);
	if (gfp_mask & __GFP_WAIT) {
		unsigned long flags;
		flags = current->flags;
		current->flags |= PF_MEMALLOC;
		ret = do_try_to_free_pages(gfp_mask);
		current->flags &= (~PF_MEMALLOC) | (flags & PF_MEMALLOC);
	}

	return ret;
}

/**
 * rss_free_pages - run part of the pageout code and slow down a bit
 * @gfp_mask: mask of things the pageout code is allowed to do
 *
 * This function is called when a task is over its RSS limit and
 * has a page fault.  It's goal is to free some memory so non-hogs
 * can run faster and slow down itself when needed so it won't eat
 * the memory non-hogs can use.
 */
void rss_free_pages(unsigned int gfp_mask)
{
	long pause = 0;

	if (current->flags & PF_MEMALLOC)
		return;

	current->flags |= PF_MEMALLOC;

	do {
		page_launder(gfp_mask);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(pause);
		set_current_state(TASK_RUNNING);
		pause++;
	} while (free_high(ALL_ZONES) >= 0);

	current->flags &= ~PF_MEMALLOC;
	return;
}

static int __init kswapd_init(void)
{
	printk("Starting kswapd\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	return 0;
}

module_init(kswapd_init)
