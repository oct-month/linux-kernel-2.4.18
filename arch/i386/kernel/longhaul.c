/*
 *  $Id: longhaul.c,v 1.52 2002/06/26 15:33:29 db Exp $
 *
 *	(C) 2001  Dave Jones. <davej@suse.de>
 *	(C) 2002  Padraig Brady. <padraig@antefacto.com>
 *
 * 	Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by VIA.
 *
 *  VIA have currently 3 different versions of Longhaul.
 *
 *  +---------------------+----------+---------------------------------+
 *  | Marketing name      | Codename | longhaul version / features.    |
 *  +---------------------+----------+---------------------------------+
 *  |  Samuel/CyrixIII    |   C5A    | v1 : multipliers only           |
 *  |  Samuel2/C3         | C3E/C5B  | v1 : multiplier only            |
 *  |  Ezra               |   C5C    | v2 : multipliers & voltage      |
 *  |  Ezra-T             | C5M/C5N  | v3 : multipliers, voltage & FSB |
 *  +---------------------+----------+---------------------------------+
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
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

#define DEBUG

#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0);
#endif

static int numscales=16, numvscales;
static int minvid, maxvid;
static int can_scale_voltage;
static int can_scale_fsb;
static int vrmrev;


/* Module parameters */
static int dont_scale_voltage;
static int dont_scale_fsb;
static int current_fsb;
static int favour_fast_fsb;

#define __hlt()     __asm__ __volatile__("hlt": : :"memory")

/*
 * Clock ratio tables.
 * The eblcr ones specify the ratio read from the CPU.
 * The clock_ratio ones specify what to write to the CPU.
 */

/* VIA C3 Samuel 1  & Samuel 2 (stepping 0)*/
static int __initdata longhaul1_clock_ratio[16] = {
	-1, /* 0000 -> RESERVED */
	30, /* 0001 ->  3.0x */
	40, /* 0010 ->  4.0x */
	-1, /* 0011 -> RESERVED */
	-1, /* 0100 -> RESERVED */
	35, /* 0101 ->  3.5x */
	45, /* 0110 ->  4.5x */
	55, /* 0111 ->  5.5x */
	60, /* 1000 ->  6.0x */
	70, /* 1001 ->  7.0x */
	80, /* 1010 ->  8.0x */
	50, /* 1011 ->  5.0x */
	65, /* 1100 ->  6.5x */
	75, /* 1101 ->  7.5x */
	-1, /* 1110 -> RESERVED */
	-1, /* 1111 -> RESERVED */
};

static int __initdata samuel1_eblcr[16] = {
	50, /* 0000 -> RESERVED */
	30, /* 0001 ->  3.0x */
	40, /* 0010 ->  4.0x */
	-1, /* 0011 -> RESERVED */
	55, /* 0100 ->  5.5x */
	35, /* 0101 ->  3.5x */
	45, /* 0110 ->  4.5x */
	-1, /* 0111 -> RESERVED */
	-1, /* 1000 -> RESERVED */
	70, /* 1001 ->  7.0x */
	80, /* 1010 ->  8.0x */
	60, /* 1011 ->  6.0x */
	-1, /* 1100 -> RESERVED */
	75, /* 1101 ->  7.5x */
	-1, /* 1110 -> RESERVED */
	65, /* 1111 ->  6.5x */
};

/* VIA C3 Samuel2 Stepping 1->15 & VIA C3 Ezra */
static int __initdata longhaul2_clock_ratio[16] = {
	100, /* 0000 -> 10.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	90,  /* 0011 ->  9.0x */
	95,  /* 0100 ->  9.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	55,  /* 0111 ->  5.5x */
	60,  /* 1000 ->  6.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	50,  /* 1011 ->  5.0x */
	65,  /* 1100 ->  6.5x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	120, /* 1111 -> 12.0x */
};

static int __initdata samuel2_eblcr[16] = {
	90,  /* 0000 ->  9.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	110, /* 0111 -> 11.0x */
	90,  /* 1000 ->  9.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	130, /* 1110 -> 13.0x */
	65,  /* 1111 ->  6.5x */
};

static int __initdata ezra_eblcr[16] = {
	90,  /* 0000 ->  9.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	95,  /* 0111 ->  9.5x */
	50,  /* 1000 ->  5.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	65,  /* 1111 ->  6.5x */
};

/* VIA C5M. */
static int __initdata longhaul3_clock_ratio[32] = {
	100, /* 0000 -> 10.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	90,  /* 0011 ->  9.0x */
	95,  /* 0100 ->  9.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	55,  /* 0111 ->  5.5x */
	60,  /* 1000 ->  6.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	50,  /* 1011 ->  5.0x */
	65,  /* 1100 ->  6.5x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	120, /* 1111 ->  12.0x */

	-1,  /* 0000 -> RESERVED (10.0x) */
	110, /* 0001 -> 11.0x */
	120, /* 0010 -> 12.0x */
	-1,  /* 0011 -> RESERVED (9.0x)*/
	105, /* 0100 -> 10.5x */
	115, /* 0101 -> 11.5x */
	125, /* 0110 -> 12.5x */
	135, /* 0111 -> 13.5x */
	140, /* 1000 -> 14.0x */
	150, /* 1001 -> 15.0x */
	160, /* 1010 -> 16.0x */
	130, /* 1011 -> 13.0x */
	145, /* 1100 -> 14.5x */
	155, /* 1101 -> 15.5x */
	-1,  /* 1110 -> RESERVED (13.0x) */
	-1,  /* 1111 -> RESERVED (12.0x) */
};

static int __initdata c5m_eblcr[32] = {
	90,  /* 0000 ->  9.0x */
	30,  /* 0001 ->  3.0x */
	40,  /* 0010 ->  4.0x */
	100, /* 0011 -> 10.0x */
	55,  /* 0100 ->  5.5x */
	35,  /* 0101 ->  3.5x */
	45,  /* 0110 ->  4.5x */
	95,  /* 0111 ->  9.5x */
	50,  /* 1000 ->  5.0x */
	70,  /* 1001 ->  7.0x */
	80,  /* 1010 ->  8.0x */
	60,  /* 1011 ->  6.0x */
	120, /* 1100 -> 12.0x */
	75,  /* 1101 ->  7.5x */
	85,  /* 1110 ->  8.5x */
	65,  /* 1111 ->  6.5x */

	-1,  /* 0000 -> RESERVED (9.0x) */
	110, /* 0001 -> 11.0x */
	120, /* 0010 -> 12.0x */
	-1,  /* 0011 -> RESERVED (10.0x)*/
	135, /* 0100 -> 13.5x */
	115, /* 0101 -> 11.5x */
	125, /* 0110 -> 12.5x */
	105, /* 0111 -> 10.5x */
	130, /* 1000 -> 13.0x */
	150, /* 1001 -> 15.0x */
	160, /* 1010 -> 16.0x */
	140, /* 1011 -> 14.0x */
	-1,  /* 1100 -> RESERVED (12.0x) */
	155, /* 1101 -> 15.5x */
	-1,  /* 1110 -> RESERVED (13.0x) */
	145, /* 1111 -> 14.5x */
};

/* fsb values as defined in CPU */
static unsigned int eblcr_fsb_table[] = { 66, 133, 100, -1 };
/* fsb values to favour low fsb speed (lower power) */
static unsigned int power_fsb_table[] = { 66, 100, 133, -1 };
/* fsb values to favour high fsb speed (for e.g. if lowering CPU 
   freq because of heat, but want to maintain highest performance possible) */
static unsigned int perf_fsb_table[] = { 133, 100, 66, -1 };
static unsigned int *fsb_search_table;

/* Voltage scales. Div by 1000 to get actual voltage. */
static int __initdata vrm85scales[32] = {
	1250, 1200, 1150, 1100, 1050, 1800, 1750, 1700,
	1650, 1600, 1550, 1500, 1450, 1400, 1350, 1300,
	1275, 1225, 1175, 1125, 1075, 1825, 1775, 1725,
	1675, 1625, 1575, 1525, 1475, 1425, 1375, 1325,
};

static int __initdata mobilevrmscales[32] = {
	2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
	1600, 1550, 1500, 1450, 1500, 1350, 1300, -1,
	1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
	1075, 1050, 1025, 1000, 975, 950, 925, -1,
};

/* Clock ratios multiplied by 10 */
static int clock_ratio[32];
static int eblcr_table[32];
static int voltage_table[32];
static int highest_speed, lowest_speed;
static int longhaul; /* version. */

/*
 * All integer math.
 * Input:
 *   ratio is multiplier*10, for e.g. 15 = 1.5, 12=1.2
 * Output:
 *   return value is in units of fsb input parameter
 */
static unsigned int get_clock_from_ratio(int ratio, unsigned fsb)
{
	unsigned int clock=0;

	if (ratio > 0 && fsb != 0) {
		clock =  (ratio/10)*fsb;
		if (ratio%10)
		    clock += (fsb/(10/(ratio%10)));
	}
	return clock;
}

static unsigned int longhaul_validatespeed_fsb(unsigned int freq, unsigned fsb, unsigned int oldbest)
{
	int i;
	unsigned int newclock;
	unsigned int best=0;

	best = oldbest;

	/* Find closest MHz match. */
	for (i=0; i<numscales; i++) {
		if (clock_ratio[i]==-1)	/* skip RESERVED entries */
			continue;

		newclock = get_clock_from_ratio(clock_ratio[i], fsb);

		if ((newclock > best) && (newclock <= (freq+1)))
			if ((newclock>=lowest_speed) && (newclock<=highest_speed))
				best = newclock;
	}
	return best;
}

/* Determine nearest speed possible for the desired fsb speed */
static unsigned int longhaul_validatespeed (unsigned int freq)
{
	unsigned int best=0;

	if (freq<lowest_speed)
		freq=lowest_speed;
	if (freq>highest_speed)
		freq=highest_speed;

	if (can_scale_fsb==1) {
		int fsb_index;
		for (fsb_index=0; fsb_search_table[fsb_index]!=-1; fsb_index++)
			best = longhaul_validatespeed_fsb (freq, fsb_search_table[fsb_index], best);
	} else {
		/* Can only scale multiplier. */
		best = longhaul_validatespeed_fsb (freq, current_fsb, best);
	}

	return best;
}

/*
 * longhaul_set_cpu_frequency()
 * This gets passed a MHz value that has been preprocessed
 * with longhaul_validatespeed().
 *
 * Note this function is only called if a new frequency has been selected.
 */

static void longhaul_set_cpu_frequency (unsigned int Mhz)
{
	int index;
	unsigned int bestfsb=-1;
	unsigned long lo, hi;
	int bits=-1;
	int revkey;
	int vidindex, i;

	/* Find out which mult bit-pattern & FSB we want */
	for (index=0; index<numscales; index++) {
		if (clock_ratio[index]==-1)	/* skip RESERVED entries */
			continue;

		if (can_scale_fsb==1) {
			int fsb_index;
			for (fsb_index=0; fsb_search_table[fsb_index]!=-1; fsb_index++) {
				if (get_clock_from_ratio(clock_ratio[index], fsb_search_table[fsb_index]) == Mhz) {
					bestfsb=fsb_search_table[fsb_index];
					break;
				}
			}
		} else {
			/* Can't scale FSB. */
			bestfsb = current_fsb;
			if (get_clock_from_ratio(clock_ratio[index], bestfsb) == Mhz)
				break;
		}
	}

	if (clock_ratio[index]==-1)
		return;

	dprintk (KERN_INFO "longhaul: New FSB:%d Mult(x10):%d\n",
				bestfsb, clock_ratio[index]);

	bits = index;
	/* "bits" contains the bitpattern of the new multiplier.
	   we now need to transform it to the desired format. */

	switch (longhaul) {
	case 1:
		rdmsr (0x1147, lo, hi);
		revkey = (lo & 0xf)<<4; /* Rev key. */
		lo &= ~(1<<23|1<<24|1<<25|1<<26);
		lo |= (1<<19);		/* Enable software clock multiplier */
		lo |= (bits<<23);	/* desired multiplier */
		lo |= revkey;
		wrmsr (0x1147, lo, hi);

		__hlt();

		/* Disable software clock multiplier */
		rdmsr (0x1147, lo, hi);
		lo &= ~(1<<19);
		lo |= revkey;
		wrmsr (0x1147, lo, hi);
		break;

	case 2:
		rdmsr (0x110a, lo, hi);
		revkey = (lo & 0xf)<<4;	/* Rev key. */
		lo &= 0xfff0bf0f;	/* reset [19:16,14](bus ratio) and [7:4](rev key) to 0 */
		lo |= (bits<<16);
		lo |= (1<<8);	/* EnableSoftBusRatio */
		lo |= revkey;

		if (can_scale_voltage) {
			if (can_scale_fsb==1) {
				dprintk (KERN_INFO "longhaul: Voltage scaling + FSB scaling not done yet.\n");
				goto bad_voltage;
                        } else {
                                /* PB: TODO fix this up */
				vidindex = (((highest_speed-lowest_speed) / (bestfsb/2)) -
						((highest_speed-Mhz) / (bestfsb/2)));
			}
			for (i=0;i<32;i++) {
				dprintk (KERN_INFO "VID hunting. Looking for %d, found %d\n",
						minvid+(vidindex*25), voltage_table[i]);
				if (voltage_table[i]==(minvid + (vidindex * 25)))
					break;
			}
			if (i==32)
				goto bad_voltage;

			dprintk (KERN_INFO "longhaul: Desired vid index=%d\n", i);
#if 0
			lo &= 0xfe0fffff;/* reset [24:20](voltage) to 0 */
			lo |= (i<<20);   /* set voltage */
			lo |= (1<<9);    /* EnableSoftVID */
#endif
		}

bad_voltage:
		wrmsr (0x110a, lo, hi);
		__hlt();

		rdmsr (0x110a, lo, hi);
		lo &= ~(1<<8);
		if (can_scale_voltage) {
			lo &= ~(1<<9);
		}
		lo |= revkey;
		wrmsr (0x110a, lo, hi);
		break;

	case 3:
		rdmsr (0x110a, lo, hi);
		revkey = (lo & 0xf)<<4;	/* Rev key. */
		lo &= 0xfff0bf0f;	/* reset longhaul[19:16,14] to 0 */
		lo |= (bits<<16);
		lo |= (1<<8);	/* EnableSoftBusRatio */
		lo |= revkey;

		/* Set FSB */
		if (can_scale_fsb==1) {
			lo &= ~(1<<28|1<<29);
			switch (bestfsb) {
				case 66:	lo |= (1<<28|1<<29); /* 11 */
							break;
				case 100:	lo |= 1<<28;	/* 01 */
							break;
				case 133:	break;	/* 00*/
			}
		}
		wrmsr (0x110a, lo, hi);
		__hlt();

		rdmsr (0x110a, lo, hi);
		lo &= ~(1<<8);
		lo |= revkey;
		wrmsr (0x110a, lo, hi);
		break;
	}
}

static int __init longhaul_get_cpu_fsb (void)
{
	unsigned long invalue=0,lo, hi;

	if (current_fsb == 0) {
		rdmsr (0x2a, lo, hi);
		invalue = (lo & (1<<18|1<<19)) >>18;
		return eblcr_fsb_table[invalue];
	} else {
		return current_fsb;
	}
}


static int __init longhaul_get_cpu_mult (void)
{
	unsigned long invalue=0,lo, hi;

	rdmsr (0x2a, lo, hi);
	invalue = (lo & (1<<22|1<<23|1<<24|1<<25)) >>22;
	if (longhaul==3) {
		if (lo & (1<<27))
			invalue+=16;
	}
	return eblcr_table[invalue];
}


static void __init longhaul_get_ranges (void)
{
	unsigned long lo, hi, invalue;
	unsigned int minmult=0, maxmult=0, minfsb=0, maxfsb=0;
	unsigned int multipliers[32]= {
		50,30,40,100,55,35,45,95,90,70,80,60,120,75,85,65,
		-1,110,120,-1,135,115,125,105,130,150,160,140,-1,155,-1,145 };
	unsigned int fsb_table[4] = { 133, 100, -1, 66 };

	switch (longhaul) {
	case 1:
		//FIXME: fill in from datasheet
		break;

	case 2 ... 3:
		rdmsr (0x110a, lo, hi);

		invalue = (hi & (1<<0|1<<1|1<<2|1<<3));
		if (hi & (1<<11))
			invalue += 16;
		maxmult=multipliers[invalue];

		#if 0 /* This is MaxMhz @ Min Voltage. Ignore for now */
                    invalue = (hi & (1<<16|1<<17|1<<18|1<<19)) >> 16;
                    if (hi & (1<<27))
                            invalue += 16;
                    minmult = multipliers[invalue];
                #else
                    minmult = 3; /* as per spec */
                #endif

		if (can_scale_fsb==1) {
			invalue = (hi & (1<<9|1<<10)) >> 9;
			maxfsb = fsb_table[invalue];

			invalue = (hi & (1<<25|1<<26)) >> 25;
			minfsb = fsb_table[invalue];

			dprintk (KERN_INFO "longhaul: Min FSB=%d Max FSB=%d\n",
				minfsb, maxfsb);
		} else {
			minfsb = maxfsb = current_fsb;
		}
		break;
	}

	highest_speed = get_clock_from_ratio(maxmult, maxfsb);
	lowest_speed = get_clock_from_ratio(minmult, minfsb);

	dprintk (KERN_INFO "longhaul: MinMult(x10)=%d MaxMult(x10)=%d\n",
		minmult, maxmult);
	dprintk (KERN_INFO "longhaul: Lowestspeed=%d Highestspeed=%d\n",
		lowest_speed, highest_speed);
}


static void __init longhaul_setup_voltagescaling (unsigned long lo, unsigned long hi)
{
	int revkey;

	can_scale_voltage = 1;

	minvid = (hi & (1<<20|1<<21|1<<22|1<<23|1<<24)) >> 20; /* 56:52 */
	maxvid = (hi & (1<<4|1<<5|1<<6|1<<7|1<<8)) >> 4;       /* 40:36 */
	vrmrev = (lo & (1<<15))>>15;

	if (vrmrev==0) {
		dprintk (KERN_INFO "longhaul: VRM 8.5 : ");
		memcpy (voltage_table, vrm85scales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/25;
	} else {
		dprintk (KERN_INFO "longhaul: Mobile VRM : ");
		memcpy (voltage_table, mobilevrmscales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/5;
	}

	/* Current voltage isn't readable at first, so we need to
	   set it to a known value. The spec says to use maxvid */
	revkey = (lo & 0xf)<<4;	/* Rev key. */
	lo &= 0xfe0fff0f;	/* Mask unneeded bits */
	lo |= (1<<9);		/* EnableSoftVID */
	lo |= revkey;		/* Reinsert key */
	lo |= maxvid << 20;
	wrmsr (0x110a, lo, hi);
	minvid = voltage_table[minvid];
	maxvid = voltage_table[maxvid];
	dprintk ("Min VID=%d.%03d Max VID=%d.%03d, %d possible voltage scales\n",
		maxvid/1000, maxvid%1000, minvid/1000, minvid%1000, numvscales);
}


static int __init longhaul_init (void)
{
	struct cpuinfo_x86 *c = cpu_data;
	unsigned int currentspeed;
	static int currentmult;
	unsigned long lo, hi;
	int ret;
	struct cpufreq_driver driver;

	if ((c->x86_vendor != X86_VENDOR_CENTAUR) || (c->x86 !=6) )
		return -ENODEV;

	switch (c->x86_model) {
	case 6:		/* VIA C3 Samuel C5A */
		return -ENODEV; // See line 499
		longhaul=1;
		memcpy (clock_ratio, longhaul1_clock_ratio, sizeof(longhaul1_clock_ratio));
		memcpy (eblcr_table, samuel1_eblcr, sizeof(samuel1_eblcr));
		break;

	case 7:		/* C5B / C5C */
		switch (c->x86_mask) {
		case 0:
			return -ENODEV; // See line 499                
			longhaul=1;
			memcpy (clock_ratio, longhaul1_clock_ratio, sizeof(longhaul1_clock_ratio));
			memcpy (eblcr_table, samuel2_eblcr, sizeof(samuel2_eblcr));
			break;
		case 1 ... 15:
			longhaul=2;
			memcpy (clock_ratio, longhaul2_clock_ratio, sizeof(longhaul2_clock_ratio));
			memcpy (eblcr_table, ezra_eblcr, sizeof(ezra_eblcr));
			break;
		}
		break;

	case 8:		/* C5M/C5N */
		return -ENODEV; // Waiting on updated docs from VIA before this is usable
		longhaul=3;
		numscales=32;
		memcpy (clock_ratio, longhaul3_clock_ratio, sizeof(longhaul3_clock_ratio));
		memcpy (eblcr_table, c5m_eblcr, sizeof(c5m_eblcr));
		break;

	default:
		printk (KERN_INFO "longhaul: Unknown VIA CPU. Contact davej@suse.de\n");
		return -ENODEV;
	}

	printk (KERN_INFO "longhaul: VIA CPU detected. Longhaul version %d supported\n", longhaul);

	if (favour_fast_fsb==1)
		fsb_search_table = perf_fsb_table;
	else
		fsb_search_table = power_fsb_table;

	current_fsb = longhaul_get_cpu_fsb();
	currentmult = longhaul_get_cpu_mult();
	currentspeed = get_clock_from_ratio(currentmult, current_fsb);

	dprintk (KERN_INFO "longhaul: CPU currently at %dMHz (%d x %d.%d)\n",
		currentspeed, current_fsb, currentmult/10, currentmult%10);

	if (longhaul==2 || longhaul==3) {
		rdmsr (0x110a, lo, hi);
		if ((lo & (1<<0)) && (dont_scale_voltage==0))
			longhaul_setup_voltagescaling (lo, hi);

		if ((lo & (1<<1)) && (dont_scale_fsb==0) && (current_fsb==0))
			can_scale_fsb = 1;
	}

	longhaul_get_ranges();

	driver.freq.min = (unsigned int) lowest_speed * 1000;
	driver.freq.max = (unsigned int) highest_speed * 1000;
	driver.freq.cur = currentspeed * 1000;
	driver.validate = &longhaul_validatespeed;
	driver.setspeed = &longhaul_set_cpu_frequency;

	ret = cpufreq_register(driver);

	if (ret) {
		longhaul = 0;
		return ret;
	}

	return 0;
}


static void __exit longhaul_exit (void)
{
	if (longhaul)
		cpufreq_unregister();
}

MODULE_PARM (dont_scale_fsb, "i");
MODULE_PARM (dont_scale_voltage, "i");
MODULE_PARM (current_fsb, "i");
MODULE_PARM (favour_fast_fsb, "i");

MODULE_AUTHOR ("Dave Jones <davej@suse.de>");
MODULE_DESCRIPTION ("Longhaul driver for VIA Cyrix processors.");
MODULE_LICENSE ("GPL");

module_init(longhaul_init);
module_exit(longhaul_exit);

