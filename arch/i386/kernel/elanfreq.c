/*
 * 	elanfreq: 	cpufreq driver for the AMD ELAN family
 *
 *	(c) Copyright 2002 Robert Schwebel <r.schwebel@pengutronix.de>
 *
 *	Parts of this code are (c) Sven Geggus <sven@geggus.net> 
 *
 *      All Rights Reserved. 
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version. 
 *
 *	2002-02-13: - initial revision for 2.4.18-pre9 by Robert Schwebel
 *
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>

#include <linux/cpufreq.h>

#include <linux/sched.h>
#include <linux/init.h>
#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <linux/delay.h>

#define REG_CSCIR 0x22 		/* Chip Setup and Control Index Register    */
#define REG_CSCDR 0x23		/* Chip Setup and Control Data  Register    */

#define SAFE_FREQ 33000		/* every Elan CPU can run at 33 MHz         */

static int currentspeed; 	/* current frequency in kHz                 */
static unsigned int elanfreq_initialised;

MODULE_LICENSE("GPL");

MODULE_AUTHOR(
  "Robert Schwebel <r.schwebel@pengutronix.de>, Sven Geggus <sven@geggus.net>"
);
MODULE_DESCRIPTION("cpufreq driver for AMD's Elan CPUs");

struct s_elan_multiplier {
	int clock;		/* frequency in kHz                         */
	int val40h;		/* PMU Force Mode register                  */
	int val80h;		/* CPU Clock Speed Register                 */
};

/*
 * It is important that the frequencies 
 * are listed in ascending order here!
 */
struct s_elan_multiplier elan_multiplier[] = {
	{1000,	0x02,	0x18},
	{2000,	0x02,	0x10},
	{4000,	0x02,	0x08},
	{8000,	0x00,	0x00},
	{16000,	0x00,	0x02},
	{33000,	0x00,	0x04},
	{66000,	0x01,	0x04},
	{99000,	0x01,	0x05}
};


/**
 *	elanfreq_get_cpu_frequency: determine current cpu speed
 *
 *	Finds out at which frequency the CPU of the Elan SOC runs
 *	at the moment. Frequencies from 1 to 33 MHz are generated 
 *	the normal way, 66 and 99 MHz are called "Hyperspeed Mode"
 *	and have the rest of the chip running with 33 MHz. 
 */

static unsigned int elanfreq_get_cpu_frequency(void)
{
        u8 clockspeed_reg;    /* Clock Speed Register */
	
        cli();
        outb_p(0x80,REG_CSCIR);
        clockspeed_reg = inb_p(REG_CSCDR);
        sti();

        if ((clockspeed_reg & 0xE0) == 0xE0) { return 0; }

        /* Are we in CPU clock multiplied mode (66/99 MHz)? */
        if ((clockspeed_reg & 0xE0) == 0xC0) {
                if ((clockspeed_reg & 0x01) == 0) {
			return 66000;
		} else {
			return 99000;             
		}
        }

	/* 33 MHz is not 32 MHz... */
	if ((clockspeed_reg & 0xE0)==0xA0)
		return 33000;

        return ((1<<((clockspeed_reg & 0xE0) >> 5)) * 1000);
}


/**
 *      elanfreq_set_cpu_frequency: Change the CPU core frequency
 *	@freq: frequency in kHz
 *
 *      This function takes a frequency value and changes the CPU frequency 
 *	according to this. Note that the frequency has to be checked by
 *	elanfreq_validatespeed() for correctness!
 *	
 *	There is no return value. 
 */

static void elanfreq_set_cpu_frequency (unsigned int freq) {

	int i;

	printk(KERN_INFO "elanfreq: attempting to set frequency to %i kHz\n",freq);

	/* search table entry for given Mhz value */
	for (i=0; i<(sizeof(elan_multiplier)/sizeof(struct s_elan_multiplier)); i++) 
	{
		if (elan_multiplier[i].clock==freq) break;
	}

	/* XXX Shouldn't we have a sanity check here or can we rely on 
           the frequency having been checked before ??? */

	/* 
	 * Access to the Elan's internal registers is indexed via    
	 * 0x22: Chip Setup & Control Register Index Register (CSCI) 
	 * 0x23: Chip Setup & Control Register Data  Register (CSCD) 
	 *
	 */

	/* 
	 * 0x40 is the Power Management Unit's Force Mode Register. 
	 * Bit 6 enables Hyperspeed Mode (66/100 MHz core frequency)
	 */

	cli();
	outb_p(0x40,REG_CSCIR); 	/* Disable hyperspeed mode          */
	outb_p(0x00,REG_CSCDR);
	sti();				/* wait till internal pipelines and */
	udelay(1000);			/* buffers have cleaned up          */

	cli();

	/* now, set the CPU clock speed register (0x80) */
	outb_p(0x80,REG_CSCIR);
	outb_p(elan_multiplier[i].val80h,REG_CSCDR);

	/* now, the hyperspeed bit in PMU Force Mode Register (0x40) */
	outb_p(0x40,REG_CSCIR);
	outb_p(elan_multiplier[i].val40h,REG_CSCDR);
	udelay(10000);
	sti();

	currentspeed=freq;

};


/**
 *	elanfreq_validatespeed: test if frequency is valid 
 *	@freq: frequency in kHz
 *
 *	This function checks if a given frequency in kHz is valid for the 
 *	hardware supported by the driver. It returns a "best fit" frequency
 * 	which might be different from the given one. 
 */

static unsigned int elanfreq_validatespeed (unsigned int freq)
{
	unsigned int best = 0;
	int i;

	for (i=0; i<(sizeof(elan_multiplier)/sizeof(struct s_elan_multiplier)); i++)
	{
		if (freq >= elan_multiplier[i].clock)
		{
			best = elan_multiplier[i].clock;
			
		}
	} 

	return best;
}



/*
 *	Module init and exit code
 */

static int __init elanfreq_init(void) 
{	
	struct cpuinfo_x86 *c = cpu_data;
	struct cpufreq_driver driver;
	int ret;

	/* Test if we have the right hardware */
	if ((c->x86_vendor != X86_VENDOR_AMD) ||
		(c->x86 != 4) || (c->x86_model!=10))
	{
		printk(KERN_INFO "elanfreq: error: no Elan processor found!\n");
                return -ENODEV;
	}

	driver.freq.cur = elanfreq_get_cpu_frequency();
	driver.validate = &elanfreq_validatespeed;
	driver.setspeed = &elanfreq_set_cpu_frequency;

	ret = cpufreq_register(driver);
	if (ret)
		return ret;

	elanfreq_initialised = 1,

	return 0;
}


static void __exit elanfreq_exit(void) 
{
	if (elanfreq_initialised)
		cpufreq_unregister();
}

module_init(elanfreq_init);
module_exit(elanfreq_exit);

