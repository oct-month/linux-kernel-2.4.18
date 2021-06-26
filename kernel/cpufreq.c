/*
 *  linux/kernel/cpufreq.c
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 Dominik Brodowski <devel@brodo.de>
 *
 *  $Id: cpufreq.c,v 1.29 2002/06/28 16:43:17 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * CPU speed changing core functionality.  We provide the following
 * services to the system:
 *  - notifier lists to inform other code of the freq change both
 *    before and after the freq change.
 *  - the ability to change the freq speed
 *
 * ** You'll need to add CTL_CPU = 10 to include/linux/sysctl.h if
 * it's not already present.
 *
 * When this appears in the kernel, the sysctl enums will move to
 * include/linux/sysctl.h
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/sysctl.h>

#include <asm/semaphore.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>	/* requires system.h */


/*
 * This list is for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 */
static struct notifier_block    *cpufreq_notifier_list;
static DECLARE_MUTEX            (cpufreq_notifier_sem);

/*
 * This is internal information about the actual transition
 * driver.
 */
static struct cpufreq_driver    cpufreq_driver;
static DECLARE_MUTEX            (cpufreq_driver_sem);

/*
 * Some data for the CPUFreq core - loops_per_jiffy / frequency values at boot
 */
static unsigned long            cpufreq_ref_loops;
static unsigned int             cpufreq_ref_freq;



/**
 * scale - "old * mult / div" calculation for large values (32-bit-arch safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 * Needed for loops_per_jiffy calculation.  We do it this way to
 * avoid math overflow on 32-bit machines.  Maybe we should make
 * this architecture dependent?  If you have a better way of doing
 * this, please replace!
 *
 *    new = old * mult / div
 */
static unsigned long scale(unsigned long old, u_int div, u_int mult)
{
	unsigned long low_part, high_part;

	high_part  = old / div;
	low_part   = (old % div) / 100;
	high_part *= mult;
	low_part   = low_part * mult / div;

	return high_part + low_part * 100;
}


/**
 * cpufreq_setup - cpufreq command line parameter parsing
 *
 * cpufreq command line parameter.  Use:
 *  cpufreq=59000-221000
 * to set the CPU frequency to 59 to 221MHz.
 */
static int __init cpufreq_setup(char *str)
{
	unsigned int min, max;

	min = 0;
	max = simple_strtoul(str, &str, 0);
	if (*str == '-') {
		min = max;
		max = simple_strtoul(str + 1, NULL, 0);
	}

	down(&cpufreq_driver_sem);
	cpufreq_driver.freq.max = max;
	cpufreq_driver.freq.min = min;
	up(&cpufreq_driver_sem);

	return 1;
}
__setup("cpufreq=", cpufreq_setup);


/**
 * adjust_jiffies - adjust the system "loops_per_jiffy"
 *
 * This function alters the system "loops_per_jiffy" for the clock
 * speed change.  We ignore CPUFREQ_DRIVER.MINMAX here.
 */
static void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	if ((val == CPUFREQ_PRECHANGE  && ci->cur < ci->new) ||
	    (val == CPUFREQ_POSTCHANGE && ci->cur > ci->new))
	    	loops_per_jiffy = scale(cpufreq_ref_loops, cpufreq_ref_freq,
					ci->new);
}



/*********************************************************************
 *                      NOTIFIER LIST INTERFACE                      *
 *********************************************************************/


/**
 *	cpufreq_register_notifier - register a driver with cpufreq
 *	@nb: notifier function to register
 *
 *	Add a driver to the list of drivers that which to be notified about
 *	CPU clock rate changes. The driver will be called three times on
 *	clock change.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_register.
 */
int cpufreq_register_notifier(struct notifier_block *nb)
{
	int ret;

	down(&cpufreq_notifier_sem);
	ret = notifier_chain_register(&cpufreq_notifier_list, nb);
	up(&cpufreq_notifier_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_register_notifier);


/**
 *	cpufreq_unregister_notifier - unregister a driver with cpufreq
 *	@nb: notifier block to be unregistered
 *
 *	Remove a driver from the CPU frequency notifier lists.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_unregister.
 */
int cpufreq_unregister_notifier(struct notifier_block *nb)
{
	int ret;

	down(&cpufreq_notifier_sem);
	ret = notifier_chain_unregister(&cpufreq_notifier_list, nb);
	up(&cpufreq_notifier_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_unregister_notifier);



/*********************************************************************
 *                     GET / SET PROCESSOR SPEED                     *
 *********************************************************************/

/**
 *	cpu_setfreq - change the CPU clock frequency.
 *	@freq: frequency (in kHz) at which we should run.
 *
 *	Set the CPU clock frequency, informing all registered users of
 *	the change. We bound the frequency according to the cpufreq
 *	command line parameter, information obtained from the cpufreq 
 *      driver, and the parameters the registered users will allow.
 *
 *	This function must be called from process context.
 *
 *	We return 0 if successful, -EINVAL if no CPUFreq architecture
 *      driver is registered, and -ENXIO if the driver is invalid.
 */
int cpufreq_set(unsigned int freq)
{
	struct cpufreq_freqs cpufreq;
	int ret;

	if (in_interrupt())
		panic("cpufreq_set() called from interrupt context!");

	down(&cpufreq_driver_sem);
	down(&cpufreq_notifier_sem);

	if (!cpufreq_driver.initialised) {
		ret = -EINVAL;
		goto out;
	}

	ret = -ENXIO;
	if (!cpufreq_driver.setspeed || !cpufreq_driver.validate)
		goto out;

	/*
	 * Don't allow the CPU to be clocked over the limit.
	 */
	cpufreq.min = cpufreq_driver.freq.min;
	cpufreq.max = cpufreq_driver.freq.max;
	cpufreq.cur = cpufreq_driver.freq.cur;
	cpufreq.new = freq;

	/*
	 * Find out what the registered devices will currently tolerate,
	 * and limit the requested clock rate to these values.  Drivers
	 * must not rely on the 'new' value - it is only a guide.
	 */
	notifier_call_chain(&cpufreq_notifier_list, CPUFREQ_MINMAX, &cpufreq);

	if (freq < cpufreq.min)
		freq = cpufreq.min;
	if (freq > cpufreq.max)
		freq = cpufreq.max;

	/*
	 * Ask the CPU specific code to validate the speed.  If the speed
	 * is not acceptable, make it acceptable.  Current policy is to
	 * round the frequency down to the value the processor actually
	 * supports.
	 */
	freq = cpufreq_driver.validate(freq);

	if (cpufreq_driver.freq.cur != freq) {
		cpufreq.cur = cpufreq_driver.freq.cur;
		cpufreq.new = freq;

		notifier_call_chain(&cpufreq_notifier_list, CPUFREQ_PRECHANGE,
				    &cpufreq);

		adjust_jiffies(CPUFREQ_PRECHANGE, &cpufreq);

		/*
		 * Actually set the CPU frequency.
		 */
		
		cpufreq_driver.setspeed(freq);
		
		cpufreq_driver.freq.cur = freq;
		adjust_jiffies(CPUFREQ_POSTCHANGE, &cpufreq);

		notifier_call_chain(&cpufreq_notifier_list, CPUFREQ_POSTCHANGE,
				    &cpufreq);

		ret = 0;
	}

 out:
	up(&cpufreq_notifier_sem);
	up(&cpufreq_driver_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_set);


/**
 *	cpufreq_setmax - set the CPUs to maximum frequency
 *
 *	Sets the CPUs to maximum frequency.
 */
int cpufreq_setmax(void)
{
	return cpufreq_set(cpufreq_driver.freq.max);
}
EXPORT_SYMBOL_GPL(cpufreq_setmax);


/**
 *	cpufreq_get - get the CPU frequency in kHz (zero means failure)
 *
 *	Returns the CPU frequency in kHz or zero on failure.
 */
unsigned int cpufreq_get(void)
{
	if (!cpufreq_driver.initialised)
		return 0;
	return cpufreq_driver.freq.cur;
}
EXPORT_SYMBOL(cpufreq_get);


#ifdef CONFIG_PM
/**
 *	cpufreq_restore - restore the CPU clock frequency after resume
 *
 *	Restore the CPU clock frequency so that our idea of the current
 *	frequency reflects the actual hardware.
 */
int cpufreq_restore(void)
{
	int ret;

	if (in_interrupt())
		panic("cpufreq_restore() called from interrupt context!");

	down(&cpufreq_driver_sem);
	if (!cpufreq_driver.initialised) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}
	ret = -ENXIO;
	if (cpufreq_driver.setspeed) {
		cpufreq_driver.setspeed(cpufreq_driver.freq.cur);
		ret = 0;
	}

	up(&cpufreq_driver_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_restore);
#endif



/*********************************************************************
 *                         SYSCTL INTERFACE                          *
 *********************************************************************/

#ifdef CONFIG_SYSCTL

struct ctl_table_header *cpufreq_sysctl_table;

static int
cpufreq_procctl(ctl_table *ctl, int write, struct file *filp,
		void *buffer, size_t *lenp)
{
	char buf[16], *p;
	int len, left = *lenp;

	if (!left || (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		unsigned int freq;

		len = left;
		if (left > sizeof(buf))
			left = sizeof(buf);
		if (copy_from_user(buf, buffer, left))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';

		freq = simple_strtoul(buf, &p, 0);
		cpufreq_set(freq);
	} else {
		len = sprintf(buf, "%d\n", cpufreq_get());
		if (len > left)
			len = left;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
	}

	*lenp = len;
	filp->f_pos += len;
	return 0;
}

static int
cpufreq_sysctl(ctl_table *table, int *name, int nlen,
	       void *oldval, size_t *oldlenp,
	       void *newval, size_t newlen, void **context)
{

	if (oldval && oldlenp) {
		size_t oldlen;

		if (get_user(oldlen, oldlenp))
			return -EFAULT;

		if (oldlen != sizeof(unsigned int))
			return -EINVAL;

		if (put_user(cpufreq_get(), (unsigned int *)oldval) ||
		    put_user(sizeof(unsigned int), oldlenp))
			return -EFAULT;
	}
	if (newval && newlen) {
		unsigned int freq;

		if (newlen != sizeof(unsigned int))
			return -EINVAL;

		if (get_user(freq, (unsigned int *)newval))
			return -EFAULT;

		cpufreq_set(freq);
	}
	return 1;
}

enum {
	CPU_NR_FREQ_MAX = 1,
	CPU_NR_FREQ_MIN = 2,
	CPU_NR_FREQ = 3
};

static ctl_table ctl_cpu_vars[4] = {
	{
		ctl_name:	CPU_NR_FREQ_MAX,
		procname:	"speed-max",
		data:		&cpufreq_driver.freq.max,
		maxlen:		sizeof(cpufreq_driver.freq.max),
		mode:		0444,
		proc_handler:	proc_dointvec,
	},
	{
		ctl_name:	CPU_NR_FREQ_MIN,
		procname:	"speed-min",
		data:		&cpufreq_driver.freq.min,
		maxlen:		sizeof(cpufreq_driver.freq.min),
		mode:		0444,
		proc_handler:	proc_dointvec,
	},
	{
		ctl_name:	CPU_NR_FREQ,
		procname:	"speed",
		mode:		0644,
		proc_handler:	cpufreq_procctl,
		strategy:	cpufreq_sysctl,
	},
	{
		ctl_name:	0,
	}
};

enum {
	CPU_NR = 1,
};

static ctl_table ctl_cpu_nr[2] = {
	{
		ctl_name:	CPU_NR,
#ifndef CONFIG_SMP
		procname:	"0",
#else
		procname:       "all",
#endif
		mode:		0555,
		child:		ctl_cpu_vars,
	},
	{
		ctl_name:	0,
	}
};

static ctl_table ctl_cpu[2] = {
	{
		ctl_name:	CTL_CPU,
		procname:	"cpu",
		mode:		0555,
		child:		ctl_cpu_nr,
	},
	{
		ctl_name:	0,
	}
};

static inline void cpufreq_sysctl_register(void)
{
	cpufreq_sysctl_table = register_sysctl_table(ctl_cpu, 0);
}
static inline void cpufreq_sysctl_unregister(void)
{
	unregister_sysctl_table(cpufreq_sysctl_table);
}

#else
#define cpufreq_sysctl_register()
#define cpufreq_sysctl_unregister()
#endif




/*********************************************************************
 *               REGISTER / UNREGISTER CPUFREQ DRIVER                *
 *********************************************************************/


/**
 * cpufreq_register - register a CPU Frequency driver
 * @driver_data: A struct cpufreq_driver containing the values submitted by the CPU Frequency driver.
 *
 * driver_data should contain the following elements: 
 * freq.min is the minimum frequency the CPU / the CPUs can be set to 
 * (optional), freq.max is the maximum frequency (optional), freq.cur 
 * is the current frequency, validate points to a function returning 
 * the closest available CPU frequency, and setspeed points to a 
 * function performing the actual transition.
 *
 * All other variables are currently ignored.
 *
 *
 *   Registers a CPU Frequency driver to this core code. This code 
 * returns zero on success, -EBUSY when another driver got here first
 * (and isn't unregistered in the meantime). 
 *
 */
int cpufreq_register(struct cpufreq_driver driver_data)
{
	if (cpufreq_driver.initialised)
		return -EBUSY;

	down(&cpufreq_driver_sem);

	/*
	 * If the user doesn't tell us the maximum frequency,
	 * or if it is invalid, use the values determined 
	 * by the cpufreq-arch-specific initialization functions.
	 * The validatespeed code is responsible for limiting
	 * this further.
	 */

	if (driver_data.freq.max && 
	   (!cpufreq_driver.freq.max || 
	   (cpufreq_driver.freq.max > driver_data.freq.max)))
		cpufreq_driver.freq.max = driver_data.freq.max;
	if (driver_data.freq.min && 
	   (!cpufreq_driver.freq.min || 
	   (cpufreq_driver.freq.min < driver_data.freq.min)))
		cpufreq_driver.freq.min = driver_data.freq.min;

	if (!cpufreq_driver.freq.max)
		cpufreq_driver.freq.max = driver_data.freq.cur;

	cpufreq_driver.freq.cur = driver_data.freq.cur;
	cpufreq_driver.validate = driver_data.validate;
	cpufreq_driver.setspeed = driver_data.setspeed;

	printk(KERN_INFO "CPU clock: %d.%03d MHz (%d.%03d-%d.%03d MHz)\n",
		cpufreq_driver.freq.cur / 1000, cpufreq_driver.freq.cur % 1000,
		cpufreq_driver.freq.min / 1000, cpufreq_driver.freq.min % 1000,
		cpufreq_driver.freq.max / 1000, cpufreq_driver.freq.max % 1000);

	cpufreq_ref_loops = loops_per_jiffy;
	cpufreq_ref_freq = cpufreq_driver.freq.cur;
	cpufreq_driver.initialised = 1;

	cpufreq_sysctl_register();

	up(&cpufreq_driver_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_register);


/**
 * cpufreq_unregister:
 *
 *    Unregister the current CPUFreq driver. Only call this if you have 
 * the right to do so, i.e. if you have succeeded in initialising before!
 * Returns zero if successful, and -EINVAL if the cpufreq_driver is
 * currently not initialised.
 */
int cpufreq_unregister(void)
{
	down(&cpufreq_driver_sem);

	if (!cpufreq_driver.initialised) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	cpufreq_driver.validate = NULL;
	cpufreq_driver.setspeed = NULL;
	cpufreq_driver.initialised = 0;

	cpufreq_sysctl_unregister();

	up(&cpufreq_driver_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister);

