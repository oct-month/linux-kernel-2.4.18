/*
 *	Pentium 4/Xeon CPU on demand clock modulation/speed scaling
 *	(C) 2002 Zwane Mwaikambo <zwane@commfireservices.com>
 *	(C) 2002 Tora T. Engstad
 *	(C) 2002 Arjan van de Ven <arjanv@redhat.com>
 *	All Rights Reserved
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      The author(s) of this software shall not be held liable for damages
 *      of any nature resulting due to the use of this software. This
 *      software is provided AS-IS with no warranties.
 *	
 *	Date		Errata			Description
 *	20020525	N44, O17	12.5% or 25% DC causes lockup
 *
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>

#include <asm/processor.h> 
#include <asm/msr.h>

/* i'll be submitting a patch to lkml for this */
#ifndef MSR_IA32_THERM_CONTROL
#define MSR_IA32_THERM_CONTROL	0x19a
#endif
#ifndef MSR_IA32_THERM_STATUS
#define MSR_IA32_THERM_STATUS	0x19c
#endif

#define PFX	"cpufreq: "

/*
 * Duty Cycle (3bits), note DC_DISABLE is not specified in
 * intel docs i just use it to mean disable
 */
enum {
	DC_RESV, DC_DFLT, DC_25PT, DC_38PT, DC_50PT,
	DC_64PT, DC_75PT, DC_88PT, DC_DISABLE
};

static int cycle_table[8][2] = {
	{13,	DC_DFLT },
	{25,	DC_25PT },
	{38,	DC_38PT },
	{50,	DC_50PT },
	{64,	DC_64PT },
	{75,	DC_75PT },
	{88,	DC_88PT },
	{100,	DC_DISABLE} };

static int has_N44_O17_errata;
static int stock_freq;
MODULE_PARM(stock_freq, "i");

static unsigned int cpufreq_p4_initialised;

static int cpufreq_p4_validatedc(unsigned int percent, unsigned int *pct)
{
	u32 l, h;
	int dc = DC_DISABLE, cpu = smp_processor_id();
	int i;

	/* FYI: Thermal monitor takes precedence and does a 50% DC modulation
	 * so we'll just return the thermal throttled settings to keep everybody
	 * happy. returning -EBUSY would have been better.
	 *
	 * I disagree; if we want to set the percentage LOWER than the thermal throttle
	 * we should allow that so that it takes effect once the thermal throttle situation
	 * ends -- Arjan
	 */
	 
	rdmsr(MSR_IA32_THERM_STATUS, l, h);
	if (l & 0x01)  {
		printk(KERN_INFO PFX "CPU#%d currently thermal throttled\n", cpu);
		if (percent > 50)
			goto done;
		dc = DC_50PT;
		*pct = 50;
		goto done;
	}

	dc = DC_DFLT;
	*pct = 13;
	
	
	/* look up the closest (but lower) duty cycle in the table */
	for (i=0; i<8; i++) 
		if (percent <= cycle_table[i][0]) {
			dc = cycle_table[i][1];
			*pct = cycle_table[i][0];
			break;
		}


	if (has_N44_O17_errata && (dc == DC_25PT || dc == DC_DFLT)) {
		dc = DC_38PT;
		*pct = 38;
	}

done:
	return dc;
}

static int cpufreq_p4_setdc(unsigned int percent)
{
	u32 l, h;
	int pct, dc, cpu = smp_processor_id();

	dc = cpufreq_p4_validatedc(percent, &pct);
	rdmsr(MSR_IA32_THERM_CONTROL, l, h);

	if (dc == DC_DISABLE) {
		printk(KERN_INFO PFX "CPU#%d disabling modulation\n", cpu);
		wrmsr(MSR_IA32_THERM_CONTROL, l & ~(1<<4), h);
	} else {
		printk(KERN_INFO PFX "CPU#%d setting duty cycle to %d%%\n", cpu, pct);
		/* bits 63 - 5	: reserved 
		 * bit  4	: enable/disable
		 * bits 3-1	: duty cycle
		 * bit  0	: reserved
		 */
		l = (l & ~14);
		l = l | (1<<4) | ((dc & 0x7)<<1);
		wrmsr(MSR_IA32_THERM_CONTROL, l, h);
	}

	return 0;
}

static unsigned int cpufreq_p4_validate_speed(unsigned int khz)
{
	unsigned int percent, valid_percent, valid_khz;
	
	percent = (khz * 100) / stock_freq;
	cpufreq_p4_validatedc(percent, &valid_percent);
	valid_khz = (stock_freq * valid_percent) / 100;

	return valid_khz;
}

static void cpufreq_p4_set_cpuspeed(unsigned int khz)
{
	unsigned int percent;

	percent = (khz * 100) / stock_freq;
	cpufreq_p4_setdc(percent);
}

int __init cpufreq_p4_init(void)
{	
	u32 l, h;
	struct cpuinfo_x86 *c = cpu_data;
	int cpu = smp_processor_id();
	int cpuid;
	int ret;
	struct cpufreq_driver driver;

	/*
	 * THERM_CONTROL is architectural for IA32 now, so 
	 * we can rely on the capability checks
	 */
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return -ENODEV;

	if (!test_bit(X86_FEATURE_ACPI, c->x86_capability) ||
		!test_bit(X86_FEATURE_ACC, c->x86_capability))
		return -ENODEV;

	/* Errata workarounds */
	cpuid = (c->x86 << 8) | (c->x86_model << 4) | c->x86_mask;
	switch (cpuid) {
		case 0x0f07:
		case 0x0f0a:
		case 0x0f11:
		case 0x0f12:
			has_N44_O17_errata = 1;
		default:
			break;
	}

	printk(KERN_INFO PFX "CPU#%d P4/Xeon(TM) CPU On-Demand Clock Modulation available\n", cpu);

	stock_freq = cpu_khz;	/* ugg :( */
	
	if (!stock_freq)
		return -ENODEV;

	driver.freq.cur=cpu_khz;
	driver.freq.min=cpu_khz/10;
	driver.freq.max=stock_freq;
	driver.validate=&cpufreq_p4_validate_speed;
	driver.setspeed=&cpufreq_p4_set_cpuspeed;

	ret = cpufreq_register(driver);
	if (ret) 
		return ret;

	cpufreq_p4_initialised = 1;

	return 0;
}


void __exit cpufreq_p4_exit(void)
{
	u32 l, h;

	if (cpufreq_p4_initialised) {
		cpufreq_unregister();
		/* return back to a non modulated state */
		rdmsr(MSR_IA32_THERM_CONTROL, l, h);
		wrmsr(MSR_IA32_THERM_CONTROL, l & ~(1<<4), h);
		cpufreq_p4_initialised = 0;
	}
}

MODULE_AUTHOR ("Zwane Mwaikambo <zwane@commfireservices.com>");
MODULE_DESCRIPTION ("cpufreq driver for Pentium(TM) 4/Xeon(TM)");
MODULE_LICENSE ("GPL");

module_init(cpufreq_p4_init);
module_exit(cpufreq_p4_exit);

