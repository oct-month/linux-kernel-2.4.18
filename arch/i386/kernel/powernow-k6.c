/*
 *  $Id: powernow-k6.c,v 1.18 2002/06/12 14:37:27 db Exp $
 *	This file is part of Powertweak Linux (http://www.powertweak.org)
 *	and is shared with the Linux Kernel module.
 *
 *	(C) 2000, 2001  Dave Jones, Arjan van de Ven, Janne Pänkälä.
 *
 * 	Licensed under the terms of the GNU GPL License version 2.
 *
 *	Version $Id: powernow-k6.c,v 1.18 2002/06/12 14:37:27 db Exp $
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>

static unsigned long cpu_khz=350000;

/* Clock ratio multiplied by 10 */
static int clock_ratio[8] = {
	45,  /* 000 -> 4.5x */
	50,  /* 001 -> 5.0x */
	40,  /* 010 -> 4.0x */
	55,  /* 011 -> 5.5x */
	20,  /* 100 -> 2.0x */
	30,  /* 101 -> 3.0x */
	60,  /* 110 -> 6.0x */
	35   /* 111 -> 3.5x */
};

static unsigned int clock_bogomips[8];

static unsigned int busfreq;
unsigned int minfreq,maxfreq;

void set_cpu_frequency_K6(unsigned int Mhz)
{
	int i;
	unsigned int best=200; /* safe initial values */
	unsigned int besti=4;
	unsigned long outvalue=0,invalue=0;
	unsigned long msrval;

	/* Find out which bit-pattern we want */

	for (i=0;i<8;i++) {
		unsigned int newclock;
		newclock = (clock_ratio[i]*busfreq/10);
		if ((newclock > best) && (newclock <= (Mhz+1))) {
			/* +1 is for compensating rounding errors */
			best = newclock;
			besti = i;
		}
	}

	/* "besti" now contains the bitpattern of the new multiplier.
	   we now need to transform it to the BVC format, see AMD#23446 */

	outvalue = (1<<12) | (1<<10) | (1<<9) | (besti<<5);

	msrval = 0xFFF1;  	/* FIXME!! we should check if 0xfff0 is available */
	wrmsr(0xC0000086,msrval,0); /* enable the PowerNow port */
	invalue=inl(0xfff8);
	invalue = invalue & 15;
	outvalue = outvalue | invalue;
	outl(outvalue ,0xFFF8);
	msrval = 0xFFF0;
	wrmsr(0xC0000086,msrval,0); /* disable it again */

	/* now adjust bogomips */
	if (!clock_bogomips[besti]) {
		/*scale_bogomips(clock_ratio[besti]);*/
		clock_bogomips[besti] = loops_per_jiffy;
	} else {
		loops_per_jiffy = clock_bogomips[besti];
	}
}

static int get_cpu_multiplier(void)
{
	unsigned long invalue=0,msrval;
	

	msrval = 0xFFF1;  	/* FIXME!! we should check if 0xfff0 is available */
	wrmsr(0xC0000086, msrval,0 ); /* enable the PowerNow port */
	invalue=inl(0xfff8);
	msrval = 0xFFF0;
	wrmsr(0xC0000086, msrval,0 ); /* disable it again */

	printk("Clock ratio is %i\n",clock_ratio[(invalue >> 5)&7]);
	return clock_ratio[(invalue >> 5)&7];
}

int PowerNow_k6plus_init(void)
{	
	struct cpuinfo_x86 *c = cpu_data;

	if ((c->x86_vendor != X86_VENDOR_AMD) || (c->x86 != 5) ||
		((c->x86_model != 12) && (c->x86_model != 13)))
		return -ENODEV;

	busfreq = cpu_khz / get_cpu_multiplier() / 100;
	//cpufreq_init(0, 0, 0);
	return 0;
}

void PowerNow_k6plus_exit(void)
{
}

MODULE_AUTHOR ("Arjan van de Ven <arjanv@redhat.com>, Dave Jones <davej@suse.de>");
MODULE_DESCRIPTION ("Longhaul driver for AMD K6-2+ / K6-3+ processors.");

MODULE_LICENSE ("GPL");
module_init(PowerNow_k6plus_init);
module_exit(PowerNow_k6plus_exit);
