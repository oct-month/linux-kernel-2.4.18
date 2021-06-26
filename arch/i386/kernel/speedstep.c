/*
 *  $Id: speedstep.c,v 1.34 2002/07/07 12:29:13 db Exp $
 *
 *	(C) 2001  Dave Jones, Arjan van de ven.
 *	(C) 2002  Dominik Brodowski <devel@brodo.de>
 *
 * 	Licensed under the terms of the GNU GPL License version 2.
 *  Based upon reverse engineered information, and on Intel documentation
 *  for chipsets ICH2-M and ICH3-M.
 *
 *      Many thanks to Ducrot Bruno for finding and fixing the last
 *  "missing link" for ICH2-M/ICH3-M support, and to Thomas Winkler 
 *  for extensive testing.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 *
 *	Version $Id: speedstep.c,v 1.34 2002/07/07 12:29:13 db Exp $
 */


/*********************************************************************
 *                        SPEEDSTEP - DEFINITIONS                    *
 *********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>


/* speedstep_chipset:
 *   It is necessary to know which chipset is used. As accesses to 
 * this device occur at various places in this module, we need a 
 * static struct pci_dev * pointing to that device.
 */
static unsigned int                     speedstep_chipset;
static struct pci_dev                   *speedstep_chipset_dev;

#define SPEEDSTEP_CHIPSET_ICH2M         0x00000002
#define SPEEDSTEP_CHIPSET_ICH3M         0x00000003
//#define SPEEDSTEP_CHIPSET_PIIX4         0x00000004


/* speedstep_processor
 */
static unsigned int                     speedstep_processor;

#define SPEEDSTEP_PROCESSOR_PIII_C      0x00000001  /* Coppermine core */
#define SPEEDSTEP_PROCESSOR_PIII_T      0x00000002  /* Tulatin core */
#define SPEEDSTEP_PROCESSOR_P4M         0x00000003  /* P4-M with 100 MHz FSB */


/* speedstep_[low,high]_freq
 *   There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static unsigned int                     speedstep_low_freq;
static unsigned int                     speedstep_high_freq;


/* DEBUG
 *   Undefine it if you do not want verbose debug output
 */
#define SPEEDSTEP_DEBUG

#ifdef SPEEDSTEP_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0);
#endif



/*********************************************************************
 *                    LOW LEVEL CHIPSET INTERFACE                    *
 *********************************************************************/

/**
 * speedstep_get_frequency - read the current SpeedStep state
 * @freq: current processor frequency in kHz
 *
 *   Tries to read the SpeedStep state. Returns -EIO when there has been
 * trouble to read the status or write to the control register, -EINVAL
 * on an unsupported chipset, and zero on success.
 */
static int speedstep_get_frequency (unsigned int *freq)
{
	u32             pmbase;
	u8              value;

	if (!speedstep_chipset_dev || !freq)
		return -EINVAL;

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
		/* get PMBASE */
		pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
		if (!(pmbase & 0x01))
			return -EIO;

		pmbase &= 0xFFFFFFFE;
		if (!pmbase) 
			return -EIO;

		/* read state */
		local_irq_disable();
		value = inb(pmbase + 0x50);
		local_irq_enable();

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		*freq = (value & 0x01) ? speedstep_low_freq : \
			speedstep_high_freq;
		return 0;

	}

	printk (KERN_ERR "cpufreq: setting CPU frequency on this chipset unsupported.\n");
	return -EINVAL;
}


/**
 * speedstep_set_frequency - set the SpeedStep state
 * @freq: new processor frequency in kHz
 *
 *   Tries to change the SpeedStep state. 
 */
void speedstep_set_frequency (unsigned int freq)
{
	u32             pmbase;
	u8	        pm2_blk;
	u8              value;
	unsigned long   flags;

	if (!speedstep_chipset_dev || !freq) {
		printk(KERN_ERR "cpufreq: unknown chipset or state\n");
		return;
	}

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
		/* get PMBASE */
		pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
		if (!(pmbase & 0x01))
			return;

		pmbase &= 0xFFFFFFFE;
		if (!pmbase)
			return;

		/* read state */
		local_irq_disable();
		value = inb(pmbase + 0x50);
		local_irq_enable();

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		/* write new state, but only if indeed a transition 
		 * is necessary */
		if (freq == ((value & 0x01) ? speedstep_low_freq : \
			      speedstep_high_freq))
			return;

		value = (freq == speedstep_high_freq) ? 0x00 : 0x01;

		dprintk(KERN_DEBUG "cpufreq: writing 0x%x to pmbase 0x%x + 0x50\n", value, pmbase);

		/* Disable IRQs */
		local_irq_save(flags);
		local_irq_disable();

		/* Disable bus master arbitration */
		pm2_blk = inb(pmbase + 0x20);
		pm2_blk |= 0x01;
		outb(pm2_blk, (pmbase + 0x20));

		/* Actual transition */
		outb(value, (pmbase + 0x50));

		/* Restore bus master arbitration */
		pm2_blk &= 0xfe;
		outb(pm2_blk, (pmbase + 0x20));

		/* Enable IRQs */
		local_irq_enable();
		local_irq_restore(flags);

		/* check if transition was sucessful */
		local_irq_disable();
		value = inb(pmbase + 0x50);
		local_irq_enable();

		dprintk(KERN_DEBUG "cpufreq: read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

		if (freq == ((value & 0x01) ? speedstep_low_freq : \
			    speedstep_high_freq)) {
			dprintk (KERN_INFO "cpufreq: change to %u MHz succeded\n", (freq / 1000));
			return;
		}

		printk (KERN_ERR "cpufreq: change failed - I/O error\n");
		return;
	}

	printk (KERN_ERR "cpufreq: setting CPU frequency on this chipset unsupported.\n");
	return;
}


/**
 * speedstep_activate - activate SpeedStep control in the chipset
 *
 *   Tries to activate the SpeedStep status and control registers.
 * Returns -EINVAL on an unsupported chipset, and zero on success.
 */
static int speedstep_activate (void)
{
	if (!speedstep_chipset_dev)
		return -EINVAL;

	switch (speedstep_chipset) {
	case SPEEDSTEP_CHIPSET_ICH2M:
	case SPEEDSTEP_CHIPSET_ICH3M:
	{
		u16             value = 0;

		pci_read_config_word(speedstep_chipset_dev, 
				     0x00A0, &value);
		if (!(value & 0x08)) {
			value |= 0x08;
			dprintk(KERN_DEBUG "cpufreq: activating SpeedStep (TM) registers\n");
			pci_write_config_word(speedstep_chipset_dev, 
					      0x00A0, value);
		}

		return 0;
	}
/*	case SPEEDSTEP_CHIPSET_PIIX4:
	{
		printk (KERN_ERR "cpufreq: SpeedStep (TM) on PIIX4 not yet supported.\n");
		return -EINVAL;
	}*/
	}

	printk (KERN_ERR "cpufreq: SpeedStep (TM) on this chipset unsupported.\n");
	return -EINVAL;
}


/**
 * speedstep_detect_chipset - detect the Southbridge which contains SpeedStep logic
 *
 *   Detects PIIX4, ICH2-M and ICH3-M so far. The pci_dev points to 
 * the LPC bridge / PM module which contains all power-management 
 * functions. Returns the SPEEDSTEP_CHIPSET_-number for the detected
 * chipset, or zero on failure.
 */
static unsigned int speedstep_detect_chipset (void)
{
	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801CA_12, 
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return SPEEDSTEP_CHIPSET_ICH3M;


	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801BA_10,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return SPEEDSTEP_CHIPSET_ICH2M;

	speedstep_chipset_dev = pci_find_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82371AB_3,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
//	if (speedstep_chipset_dev)
//		return SPEEDSTEP_CHIPSET_PIIX4;

	return 0;
}



/*********************************************************************
 *                   LOW LEVEL PROCESSOR INTERFACE                   *
 *********************************************************************/


/**
 * pentium6_get_fsb - read the FSB on Intel PIII
 *
 *   Returns the Front Side Bus speed of a Pentium III processor (in MHz).
 */
static inline unsigned int pentium6_get_fsb (void)
{
	/* PIII(-M) FSB settings: see table b1-b of 24547206.pdf */
	struct {
		unsigned int value;     /* Front Side Bus speed in MHz */
		u8 bitmap;              /* power on configuration bits [18: 19]
					   (in MSR 0x2a) */
	} msr_decode_fsb [] = {
		{  66, 0x0 },
		{ 100, 0x2 },
		{ 133, 0x1 },
		{   0, 0xff}
	};
	u32     msr_lo, msr_hi;
	int     i = 0;

	rdmsr(MSR_IA32_EBL_CR_POWERON, msr_lo, msr_hi);

	msr_lo &= 0x00c0000;
	msr_lo >>= 18;

	while (msr_lo != msr_decode_fsb[i].bitmap) {
		if (msr_decode_fsb[i].bitmap == 0xff)
			return -EINVAL;
		i++;
	}

	return msr_decode_fsb[i].value;
}


/**
 * pentium6_get_ratio - read the frequency multiplier on Intel PIII
 *
 *   Detects the current processor frequency multiplier ratio. 
 * Returns 10 times the value to properly manage .5 settings.
 */
static inline unsigned int pentium6_get_ratio(void)
{
        /* Intel processor frequency multipliers:
	 *   See table 14 of p3_ds.pdf and table 22 of 29834003.pdf
	 */
	struct {
		unsigned int ratio;	/* Frequency Multiplier (x10) */
		u8 bitmap;	        /* power on configuration bits
					   [27, 25:22] (in MSR 0x2a) */
	} msr_decode_mult [] = {
		{ 30, 0x01 },
		{ 35, 0x05 },
		{ 40, 0x02 },
		{ 45, 0x06 },
		{ 50, 0x00 },
		{ 55, 0x04 },
		{ 60, 0x0b },
		{ 65, 0x0f },
		{ 70, 0x09 },
		{ 75, 0x0d },
		{ 80, 0x0a },
		{ 85, 0x26 },
		{ 90, 0x20 },
		{ 100, 0x2b },
		{ 0, 0xff }     /* error or unknown value */
	};
	u32     msr_lo, msr_hi;
	int     i = 0;
	struct  cpuinfo_x86 *c = cpu_data;

	rdmsr(MSR_IA32_EBL_CR_POWERON, msr_lo, msr_hi);

	/* decode value */
	if ((c->x86_model == 0x08) && (c->x86_mask == 0x01)) 
                /* different on early Coppermine PIII */
		msr_lo &= 0x03c00000;
	else
		msr_lo &= 0x0bc00000;

	msr_lo >>= 22;
	while (msr_lo != msr_decode_mult[i].bitmap) {
		if (msr_decode_mult[i].bitmap == 0xff)
				return -EINVAL;
			i++;
		}

	return msr_decode_mult[i].ratio;
}


/**
 * pentium4_get_frequency - get the core frequency for P4-Ms
 *
 *   Should return the core frequency for P4-Ms. 
 */
static inline unsigned int pentium4_get_frequency(void)
{
	u32 msr_lo, msr_hi;

	rdmsr(0x2c, msr_lo, msr_hi);

	dprintk(KERN_DEBUG "cpufreq: P4 - MSR_EBC_FREQUENCY_ID: 0x%x 0x%x\n", msr_lo, msr_hi);

	/* Don't trust unseen values yet, except in the MHz field 
	 */
	if (msr_hi || ((msr_lo & 0x00FFFFFF) != 0x300511)) {
		printk(KERN_INFO "cpufreq: Due to incomplete documentation, please send a mail to devel@brodo.de\n");
		printk(KERN_INFO "with a dmesg of a boot while on ac-power, and one of a boot on battery-power.\n");
		printk(KERN_INFO "Thanks in advance.\n");
		return 0;
	}

	/* It seems that the frequency is equal to the 
	 * value in bits 24:31 (in 100 MHz).
	 */
	msr_lo >>= 24;
	return (msr_lo * 100);
}


/** 
 * speedstep_detect_processor - detect Intel SpeedStep-capable processors.
 *
 *   Returns the SPEEDSTEP_PROCESSOR_-number for the detected chipset, 
 * or zero on failure.
 */
static unsigned int speedstep_detect_processor (void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32                     ebx;
	int                     check = 0;

	if ((c->x86_vendor != X86_VENDOR_INTEL) || ((c->x86 != 6) && (c->x86 != 0xF)))
		return 0;

	if (c->x86 == 0xF) {
		/* Intel Pentium 4 Mobile P4-M */
		if (c->x86_model != 2)
			return 0;

		if (c->x86_mask != 4)
			return 0;     /* all those seem to support Enhanced
					 SpeedStep */

		return SPEEDSTEP_PROCESSOR_P4M;
	}

	switch (c->x86_model) {
	case 0x0B: /* Intel PIII [Tulatin] */
		/* cpuid_ebx(1) is 0x04 for desktop PIII, 
		                   0x06 for mobile PIII-M */
		ebx = cpuid_ebx(0x00000001);

		ebx &= 0x000000FF;
		if (ebx != 0x06)
			return 0;

		/* So far all PIII-M processors support SpeedStep. See
		 * Intel's 24540628.pdf of March 2002 
		 */

		return SPEEDSTEP_PROCESSOR_PIII_T;

	case 0x08: /* Intel PIII [Coppermine] */
		/* all mobile PIII Coppermines have FSB 100 MHz */
		if (pentium6_get_fsb() != 100)
			return 0;

		/* Unfortunatey, no information exists on how to check
		 * whether the processor is capable of SpeedStep. On
		 * processors that don't support it, doing such
 		 * transitions might be harmful. So the user has to
		 * override this safety abort.
		 */

/* ---> */	check = 1; /* remove this line to enable SpeedStep. */
			   /* See the comment above on why this check 
			    * is necessary - Sorry for the inconvenience!
			    */

		if (check) {
			printk(KERN_INFO "cpufreq: Intel PIII (Coppermine) detected. If you are sure this is a\n");
			printk(KERN_INFO "cpufreq: SpeedStep capable processor, please remove line %u in\n", (__LINE__ - 7));
			printk(KERN_INFO "cpufreq: linux/arch/i386/kernel/cpufreq/speedstep.c.\n");
			return 0;
		}

		return SPEEDSTEP_PROCESSOR_PIII_C;

	default:
		return 0;
	}
}



/*********************************************************************
 *                        HIGH LEVEL FUNCTIONS                       *
 *********************************************************************/


/**
 * speedstep_detect_speeds - detects low and high CPU frequencies.
 *
 *   Detects the low and high CPU frequencies in kHz. Returns 0 on
 * success or -EINVAL / -EIO on problems. 
 */
static int speedstep_detect_speeds (void)
{
	switch (speedstep_processor) {
	case SPEEDSTEP_PROCESSOR_PIII_C:
	case SPEEDSTEP_PROCESSOR_PIII_T:
	{
		unsigned int    state;
		unsigned int    fsb;
		int             i = 0;
		int             result;

		fsb = pentium6_get_fsb();

		for (i=0; i<2; i++) {
			/* read the current state */
			result = speedstep_get_frequency(&state);
			if (result)
				return result;

			/* save the correct value, and switch to other */
			if (state == speedstep_low_freq) {
				speedstep_low_freq = 
					pentium6_get_ratio() * fsb * 100;
				speedstep_set_frequency(speedstep_high_freq);
			} else {
				speedstep_high_freq = 
					pentium6_get_ratio() * fsb * 100;
				speedstep_set_frequency(speedstep_low_freq);
			}
		}

		if (!speedstep_low_freq || !speedstep_high_freq)
			return -EIO;
		else
			return 0;
	}
	case SPEEDSTEP_PROCESSOR_P4M:
	{
		unsigned int    state;
		int             i = 0;
		int             result;

		for (i=0; i<2; i++) {
			/* read the current state */
			result = speedstep_get_frequency(&state);
			if (result)
				return result;

			/* save the correct value, and switch to other */
			if (state == speedstep_low_freq) {
				speedstep_low_freq = 
					pentium4_get_frequency() * 1000;
				speedstep_set_frequency(speedstep_high_freq);
			} else {
				speedstep_high_freq = 
					pentium4_get_frequency() * 1000;
				speedstep_set_frequency(speedstep_low_freq);
			}
		}

		if (!speedstep_low_freq || !speedstep_high_freq)
			return -EIO;
		else
			return 0;
	}
	}
	return -EINVAL;
}


/**
 * speedstep_validate_frequency - validates the CPU frequency to be set
 * @kHz: suggested new CPU frequency
 *
 *   Makes sure the CPU frequency to be set is valid.
 */
unsigned int speedstep_validate_frequency(unsigned int kHz)
{
	if (((int) (kHz  - speedstep_low_freq)) < 
	    ((int) (speedstep_high_freq - kHz)))
		return speedstep_low_freq;
	else 
		return speedstep_high_freq;
}


/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	int                     result;
	unsigned int            speed;
	struct cpufreq_driver   driver;

	printk(KERN_INFO "cpufreq: Intel(R) SpeedStep(TM) support $Revision: 1.34 $\n");

	/* detect processor */
	speedstep_processor = speedstep_detect_processor();

	/* detect chipset */
	speedstep_chipset = speedstep_detect_chipset();
	if ((!speedstep_chipset) || (!speedstep_processor)) {
		printk(KERN_INFO "cpufreq: Intel(R) SpeedStep(TM) for this %s not (yet) available.\n", speedstep_processor ? "chipset" : "processor");
		return -ENODEV;
	}

	/* startup values, correct ones will be detected later */
	speedstep_low_freq = 1;
	speedstep_high_freq = 2;

	/* define method to be used */
	dprintk(KERN_DEBUG "cpufreq: chipset 0x%x - processor 0x%x\n", 
	       speedstep_chipset, speedstep_processor);

	/* activate speedstep support */
	result = speedstep_activate();
	if (result) {
		speedstep_chipset = 0;
		return result;
	}

	/* detect low and high frequency */
	result = speedstep_detect_speeds();
	if (result) {
		speedstep_chipset = 0;
		return result;
	}

	/* get current speed setting */
	result = speedstep_get_frequency(&speed);
	if (result) {
		speedstep_chipset = 0;
		return result;
	}

	dprintk(KERN_INFO "cpufreq: currently at %s speed setting - %i MHz\n", 
	       (speed == speedstep_low_freq) ? "low" : "high",
	       (speed / 1000));

	/* initialization of main "cpufreq" code*/
	driver.freq.min = speedstep_low_freq;
	driver.freq.max = speedstep_high_freq;
	driver.freq.cur = speed;
	driver.validate = &speedstep_validate_frequency;
	driver.setspeed = &speedstep_set_frequency;

	result = cpufreq_register(driver);
	if (result) {
		speedstep_chipset = 0;
		return result;
	}

	return 0;
}


/**
 * speedstep_exit - unregisters SpeedStep support
 *
 *   Unregisters SpeedStep support.
 */
static void __exit speedstep_exit(void)
{
	if (speedstep_chipset)
		cpufreq_unregister();
}



MODULE_AUTHOR ("Dave Jones <davej@suse.de>, Dominik Brodowski <devel@brodo.de>");
MODULE_DESCRIPTION ("Speedstep driver for Intel mobile processors.");
MODULE_LICENSE ("GPL");
module_init(speedstep_init);
module_exit(speedstep_exit);
