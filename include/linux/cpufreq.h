/*
 *  linux/include/linux/cpufreq.h
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 Dominik Brodowski <devel@brodo.de>
 *            
 *
 * $Id: cpufreq.h,v 1.9 2002/06/26 15:33:29 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

#include <linux/config.h>
#include <linux/notifier.h>


/* speed setting interface */

int cpufreq_setmax(void);
int cpufreq_set(unsigned int khz);
unsigned int cpufreq_get(void);

#ifdef CONFIG_PM
int cpufreq_restore(void);
#endif


/* notifier interface */

/*
 * The max and min frequency rates that the registered device
 * can tolerate.  Never set any element this structure directly -
 * always use cpufreq_updateminmax.
 */
struct cpufreq_freqs {
	unsigned int min;
	unsigned int max;
	unsigned int cur;
	unsigned int new;
};

static inline
void cpufreq_updateminmax(struct cpufreq_freqs *freq, 
			  unsigned int min, 
			  unsigned int max)
{
	if (freq->min < min)
		freq->min = min;
	if (freq->max > max)
		freq->max = max;
}

#define CPUFREQ_MINMAX		(0)
#define CPUFREQ_PRECHANGE	(1)
#define CPUFREQ_POSTCHANGE	(2)

int cpufreq_register_notifier(struct notifier_block *nb);
int cpufreq_unregister_notifier(struct notifier_block *nb);



/* cpufreq driver interface */

typedef unsigned int (*cpufreq_verify_t) (unsigned int kHz);
typedef void (*cpufreq_setspeed_t)        (unsigned int kHz);

struct cpufreq_driver {
	struct cpufreq_freqs    freq;
	cpufreq_verify_t        validate;
	cpufreq_setspeed_t      setspeed;
	unsigned int            initialised;
};

int cpufreq_register(struct cpufreq_driver driver_data);
int cpufreq_unregister(void);

#endif
