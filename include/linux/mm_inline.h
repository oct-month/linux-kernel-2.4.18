#ifndef _LINUX_MM_INLINE_H
#define _LINUX_MM_INLINE_H

#include <linux/mm.h>

/*
 * These inline functions tend to need bits and pieces of all the
 * other VM include files, meaning they cannot be defined inside
 * one of the other VM include files.
 *
 * The include file mess really needs to be cleaned up...
 */

static inline void add_page_to_active_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageActive(page);
	list_add(&page->lru, &zone->active_list);
	zone->active_pages++;
	nr_active_pages++;
}

static inline void add_page_to_inactive_dirty_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageInactiveDirty(page);
	list_add(&page->lru, &zone->inactive_dirty_list);
	zone->inactive_dirty_pages++;
	nr_inactive_dirty_pages++;
}

static inline void add_page_to_inactive_clean_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageInactiveClean(page);
	list_add(&page->lru, &zone->inactive_clean_list);
	zone->inactive_clean_pages++;
	nr_inactive_clean_pages++;
}

static inline void del_page_from_active_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	list_del(&page->lru);
	ClearPageActive(page);
	nr_active_pages--;
	zone->active_pages--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_inactive_dirty_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	list_del(&page->lru);
	ClearPageInactiveDirty(page);
	nr_inactive_dirty_pages--;
	zone->inactive_dirty_pages--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_inactive_clean_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	list_del(&page->lru);
	ClearPageInactiveClean(page);
	zone->inactive_clean_pages--;
	nr_inactive_clean_pages--;
	DEBUG_LRU_PAGE(page);
}

/*
 * Inline functions to control some balancing in the VM.
 *
 * Note that we do both global and per-zone balancing, with
 * most of the balancing done globally.
 */
#define	PLENTY_FACTOR	2
#define	ALL_ZONES	NULL
#define	ANY_ZONE	(struct zone_struct *)(~0UL)
#define INACTIVE_FACTOR	5

#define	VM_MIN	0
#define	VM_LOW	1
#define	VM_HIGH	2
#define VM_PLENTY 3
static inline int zone_free_limit(struct zone_struct * zone, int limit)
{
	int free, target, delta;

	/* This is really nasty, but GCC should completely optimise it away. */
	if (limit == VM_MIN)
		target = zone->pages_min;
	else if (limit == VM_LOW)
		target = zone->pages_low;
	else if (limit == VM_HIGH)
		target = zone->pages_high;
	else
		target = zone->pages_high * PLENTY_FACTOR;

	free = zone->free_pages + zone->inactive_clean_pages;
	delta = target - free;

	return delta;
}

static inline int free_limit(struct zone_struct * zone, int limit)
{
	int shortage = 0, local;

	if (zone == ALL_ZONES) {
		for_each_zone(zone)
			shortage += zone_free_limit(zone, limit);
	} else if (zone == ANY_ZONE) {
		for_each_zone(zone) {
			local = zone_free_limit(zone, limit);
			shortage += max(local, 0);
		}
	} else {
		shortage = zone_free_limit(zone, limit);
	}

	return shortage;
}

/**
 * free_min - test for critically low amount of free pages
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if we have a serious shortage of free and
 * clean pages, zero or negative if there is no serious shortage.
 */
static inline int free_min(struct zone_struct * zone)
{
	return free_limit(zone, VM_MIN);
}

/**
 * free_low - test for low amount of free pages
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if we have a shortage of free and
 * clean pages, zero or negative if there is no shortage.
 */
static inline int free_low(struct zone_struct * zone)
{
	return free_limit(zone, VM_LOW);
}

/**
 * free_high - test if amount of free pages is less than ideal
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if the number of free and clean
 * pages is below kswapd's target, zero or negative if we
 * have more than enough free and clean pages.
 */
static inline int free_high(struct zone_struct * zone)
{
	return free_limit(zone, VM_HIGH);
}

/**
 * free_plenty - test if enough pages are freed
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if the number of free + clean pages
 * in a zone is not yet excessive and kswapd is still allowed to
 * free pages here, a negative value if kswapd should leave the
 * zone alone.
 */
static inline int free_plenty(struct zone_struct * zone)
{
	return free_limit(zone, VM_PLENTY);
}

/*
 * The inactive page target is the free target + 20% of (active + inactive)
 * pages. 
 */
static inline int zone_inactive_limit(struct zone_struct * zone, int limit)
{
	int inactive, target, inactive_base;

	inactive_base = zone->active_pages + zone->inactive_dirty_pages;
	inactive_base /= INACTIVE_FACTOR;

	/* GCC should optimise this away completely. */
	if (limit == VM_MIN)
		target = zone->pages_high + inactive_base / 2;
	else if (limit == VM_LOW)
		target = zone->pages_high + inactive_base;
	else
		target = zone->pages_high + inactive_base * 2;

	inactive = zone->free_pages + zone->inactive_clean_pages;
	inactive += zone->inactive_dirty_pages;

	return target - inactive;
}

static inline int inactive_limit(struct zone_struct * zone, int limit)
{
	int shortage = 0, local;

	if (zone == ALL_ZONES) {
		for_each_zone(zone)
			shortage += zone_inactive_limit(zone, limit);
	} else if (zone == ANY_ZONE) {
		for_each_zone(zone) {
			local = zone_inactive_limit(zone, limit);
			shortage += max(local, 0);
		}
	} else {
		shortage = zone_inactive_limit(zone, limit);
	}

	return shortage;
}

/**
 * inactive_min - test for serious shortage of (free + inactive clean) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have no serious shortage of (free + inactive clean) pages
 */
static inline int inactive_min(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_MIN);
}

/**
 * inactive_low - test for shortage of (free + inactive clean) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have no shortage of (free + inactive clean) pages
 */
static inline int inactive_low(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_LOW);
}

/**
 * inactive_high - less than ideal amount of (free + inactive) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have more than enough (free + inactive) pages
 */
static inline int inactive_high(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_HIGH);
}

/*
 * inactive_target - number of inactive pages we ought to have.
 */
static inline int inactive_target(void)
{
	int target;

	target = nr_active_pages + nr_inactive_dirty_pages
			+ nr_inactive_clean_pages;

	target /= INACTIVE_FACTOR;

	return target;
}

#endif /* _LINUX_MM_INLINE_H */
