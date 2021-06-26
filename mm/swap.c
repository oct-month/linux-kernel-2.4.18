/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the opereation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * linux/Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swapctl.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/mm_inline.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/pgtable.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

pager_daemon_t pager_daemon = {
	512,	/* base number for calculating the number of tries */
	SWAP_CLUSTER_MAX,	/* minimum number of tries */
	8,	/* do swap I/O in clusters of this size */
};

/**
 * (de)activate_page - move pages from/to active and inactive lists
 * @page: the page we want to move
 * @nolock - are we already holding the pagemap_lru_lock?
 *
 * Deactivate_page will move an active page to the right
 * inactive list, while activate_page will move a page back
 * from one of the inactive lists to the active list. If
 * called on a page which is not on any of the lists, the
 * page is left alone.
 */
void deactivate_page_nolock(struct page * page)
{
	/*
	 * Don't touch it if it's not on the active list.
	 * (some pages aren't on any list at all)
	 */
	ClearPageReferenced(page);
	page->age = 0;
	if (PageActive(page)) {
		del_page_from_active_list(page);
		add_page_to_inactive_dirty_list(page);
	}
}	

void deactivate_page(struct page * page)
{
	spin_lock(&pagemap_lru_lock);
	deactivate_page_nolock(page);
	spin_unlock(&pagemap_lru_lock);
}

/**
 * drop_page - like deactivate_page, but try inactive_clean list
 * @page: the page to drop
 *
 * Try to move a page to the inactive_clean list, this succeeds if the
 * page is clean and not in use by anybody. If the page cannot be placed
 * on the inactive_clean list it is placed on the inactive_dirty list
 * instead.
 *
 * Note: this function gets called with the pagemap_lru_lock held.
 */
void drop_page(struct page * page)
{
	if (!TryLockPage(page)) {
		if (page->mapping && page->buffers) {
			page_cache_get(page);
			spin_unlock(&pagemap_lru_lock);
			try_to_release_page(page, GFP_NOIO);
			spin_lock(&pagemap_lru_lock);
			page_cache_release(page);
		}
		UnlockPage(page);
	}

	/* Make sure the page really is reclaimable. */
	pte_chain_lock(page);
	if (!page->mapping || PageDirty(page) || page->pte_chain ||
			page->buffers || page_count(page) > 1)
		deactivate_page_nolock(page);

	else if (page_count(page) == 1) {
		ClearPageReferenced(page);
		page->age = 0;
		if (PageActive(page)) {
			del_page_from_active_list(page);
			add_page_to_inactive_clean_list(page);
		} else if (PageInactiveDirty(page)) {
			del_page_from_inactive_dirty_list(page);
			add_page_to_inactive_clean_list(page);
		}
	}
	pte_chain_unlock(page);
}

/*
 * Move an inactive page to the active list.
 */
void activate_page_nolock(struct page * page)
{
	if (PageInactiveDirty(page)) {
		del_page_from_inactive_dirty_list(page);
		add_page_to_active_list(page);
	} else if (PageInactiveClean(page)) {
		del_page_from_inactive_clean_list(page);
		add_page_to_active_list(page);
	}

	/* Make sure the page gets a fair chance at staying active. */
	page->age = max((int)page->age, PAGE_AGE_START);
}

void activate_page(struct page * page)
{
	spin_lock(&pagemap_lru_lock);
	activate_page_nolock(page);
	spin_unlock(&pagemap_lru_lock);
}

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
void lru_cache_add(struct page * page)
{
	if (!PageLRU(page)) {
		spin_lock(&pagemap_lru_lock);
		SetPageLRU(page);
		add_page_to_active_list(page);
		spin_unlock(&pagemap_lru_lock);
	}
}

/**
 * __lru_cache_del: remove a page from the page lists
 * @page: the page to add
 *
 * This function is for when the caller already holds
 * the pagemap_lru_lock.
 */
void __lru_cache_del(struct page * page)
{
	if (PageActive(page)) {
		del_page_from_active_list(page);
	} else if (PageInactiveDirty(page)) {
		del_page_from_inactive_dirty_list(page);
	} else if (PageInactiveClean(page)) {
		del_page_from_inactive_clean_list(page);
	}
	ClearPageLRU(page);
}

/**
 * lru_cache_del: remove a page from the page lists
 * @page: the page to remove
 */
void lru_cache_del(struct page * page)
{
	spin_lock(&pagemap_lru_lock);
	__lru_cache_del(page);
	spin_unlock(&pagemap_lru_lock);
}

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = num_physpages >> (20 - PAGE_SHIFT);

	/* Use a smaller cluster for small-memory machines */
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	/*
	 * Right now other parts of the system means that we
	 * _really_ don't want to cluster much more
	 */
}
