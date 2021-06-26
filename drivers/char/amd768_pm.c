/*
 *	Native power management driver for the AMD 760MPx series
 *
 *	(c) Copyright 2002, Red Hat Inc, <alan@redhat.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/uaccess.h>

static u32 pmbase;			/* PMxx I/O base */
static struct pci_dev *amd762;

/*
 * amd768pm_init_one - look for and attempt to init PM
 */
static int __init amd768pm_init_one (struct pci_dev *dev)
{
	u32 reg;
	u8 rnen;
	int i;

	amd762 = pci_find_device(0x1022, 0x700C, NULL);
	if(amd762 == NULL)
		return -ENODEV;

	pci_read_config_dword(amd762, 0x70, &reg);
	if(!(reg & (1<<18)))
	{
		printk(KERN_INFO "AMD768_pm: enabling self refresh.\n");
		reg |= (1<<18);	/* Enable self refresh */
		pci_write_config_dword(amd762, 0x70, reg);
	}
	
	pci_read_config_dword(amd762, 0x58, &reg);
	if(reg&(1<<19))
	{
		printk(KERN_INFO "AMD768_pm: DRAM refresh enabled.\n");
		reg &= ~(1<<19); /* Disable to allow self refresh modes */
		pci_write_config_dword(amd762, 0x58, reg);
	}

	for(i=0; i<smp_num_cpus;i++)
	{
		pci_read_config_dword(amd762, 0x60 + 8*i, &reg);
		if(!(reg&(1<<17)))
		{
			printk(KERN_INFO "AMD768_pm: Enabling disconnect on CPU#%d.\n", i);
			reg |= (1<<17);
			pci_write_config_dword(amd762, 0x60 + 8*i, reg);
		}
	}

	pci_read_config_dword(dev, 0x58, &pmbase);

	pmbase &= 0x0000FF00;

	if(pmbase == 0)
	{
		printk ("AMD768_pm: power management base not set\n");
		return -ENODEV;
	}

	pci_read_config_byte(dev, 0x41, &rnen);
	if(!(rnen&(1<<7)))
	{
		printk ("AMD768_pm: enabling PMIO\n");
		rnen|=(1<<7);	/* PMIO enable */
		pci_write_config_byte(dev, 0x41, rnen);
	}
	if(!(rnen&1))
	{
		printk ("AMD768_pm: enabling W4SG");
		rnen|=1;	/* W4SG enable */
		pci_write_config_byte(dev, 0x41, rnen);
	}
	printk(KERN_INFO "AMD768_pm: AMD768 system management I/O registers at 0x%X.\n", pmbase);
	return 0;
}


/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static struct pci_device_id amd768pm_pci_tbl[] __initdata = {
	{ 0x1022, 0x7443, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, },
};
MODULE_DEVICE_TABLE (pci, amd768pm_pci_tbl);


MODULE_AUTHOR("Alan Cox, Red Hat Inc");
MODULE_DESCRIPTION("AMD 762/768 Native power management");
MODULE_LICENSE("GPL");


/*
 * amd768pm_init - initialize RNG module
 */
static int __init amd768pm_init (void)
{
	int rc;
	struct pci_dev *pdev;

	if(smp_num_cpus > 2)
	{
		printk(KERN_ERR "Only single and dual processor AMD762/768 is supported.\n");
		return -ENODEV;
	}
	pci_for_each_dev(pdev) {
		if (pci_match_device (amd768pm_pci_tbl, pdev) != NULL)
			goto match;
	}

	return -ENODEV;

match:
	rc = amd768pm_init_one (pdev);
	if (rc)
		return rc;
	return 0;
}


/*
 * amd768pm_cleanup - shutdown the AMD pm module
 */
static void __exit amd768pm_cleanup (void)
{
}


module_init (amd768pm_init);
module_exit (amd768pm_cleanup);
