/*
 *  $Id: powernow-k7.c,v 1.7 2002/06/12 14:37:27 db Exp $
 *	(C) 2002 Dave Jones
 *
 * 	Licensed under the terms of the GNU GPL License version 2.
 *
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

static int can_scale_mult;
static int can_scale_voltage;

/*
 * When picking a new multiplier, we must work around
 * the horrible #14 errata. If ACPI is enabled, we must
 * avoid using half multipliers, or we may hang on
 * exiting C2 state. If no ACPI is enabled, this isn't a problem.
 */


int PowerNow_K7_init(void)
{	
	struct cpuinfo_x86 *c = cpu_data;
//	int currentspeed = 0;
	u32 eax, ebx, ecx, edx;

	if ((c->x86_vendor != X86_VENDOR_AMD) || (c->x86 != 6))
		return -ENODEV;

	/* Do we have PowerNow magic ? */
	if (cpuid_eax(0x80000000) < 0x80000007)
		return -ENODEV;

	cpuid (0x80000007, &eax, &ebx, &ecx, &edx);
	if (edx & 1<<1)
		can_scale_mult = 1;
	if (edx & 1<<2)
		can_scale_voltage = 1;

	if (!(edx & (1<<1 | 1<<2)))
		return -ENODEV;

	//currentspeed = DetermineSpeed();
	//cpufreq_init(currentspeed, 0, 0);
	return 0;
}


void PowerNow_K7_exit(void)
{
}

MODULE_PARM (dont_scale_fsb, "i");
MODULE_PARM (dont_scale_voltage, "i");

MODULE_AUTHOR ("Dave Jones <davej@suse.de>");
MODULE_DESCRIPTION ("PowerNow driver for mobile AMD K7 processors.");
MODULE_LICENSE ("GPL");

module_init(PowerNow_K7_init);
module_exit(PowerNow_K7_exit);
