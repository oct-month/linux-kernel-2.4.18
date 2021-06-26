/*
 * Battery driver for Advent 7006 laptop and related
 *
 * Copyright (c) 2001 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Arjan van de Ven <arjanv@redhat.com>
 *
 */
         
         
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/apm_bios.h>


/* 
 * SMM area layout:
 * Byte 0 : unknown
 * Byte 1 : unknown
 * Byte 2 : status
 	bit 0/1 -> battery present
 	bit 2   -> 1 if charging
 	bit 3   -> 0 if AC power connected
 * Byte 3: unknown
 * Byte 4/5: charge in battery at last charge (LSB first)
 * Byte 6/7: maximum charge of battery
 * Byte 8/9: nominal battery voltage in milivolt
 * Byte 10 : rate of current power usage
 * Byte 11/12: current amount of power left in battery
 */


static unsigned long voltage; /* milivolt */
static unsigned long battery_capacity;

/* Get static (non transient) information about the battery */
static void get_battery_info(void)
{
	unsigned char data[16];
	void *battery_area;
	
	/* Magic SMM invocation */
	outb(0xd1, 0xb2);
	battery_area = ioremap(0x20000000-4096+0xc04, 16);
	if (battery_area)
		memcpy(data, battery_area, 16);
	else
		memset(data, 0, 16);
	iounmap(battery_area);
	
	battery_capacity = data[7]*255 + data[6];
 	voltage = data[9] * 255 + data[8];	
}

static void gericom_battery_hook(unsigned short *status, unsigned short *flag,
   unsigned short *ac, int *percentage, int *time)
{
	unsigned char data[16];
	void *battery_area;
	
	int power_left;
	
	/* Magic SMM invocation */
	outb(0xd2, 0xb2);
	battery_area = ioremap(0x20000000-4096+0xc04, 16);
	if (battery_area)
		memcpy(data, battery_area, 16);
	else
		memset(data, 0, 16);
	iounmap(battery_area);
	
	/* the current charge is in bytes 11 and 12 */
	power_left = data[12]*255 + data[11];
	
	/* Bit 4 of byte 3 indicates AC connected */
	*ac = 1;
	if (data[2]&8)
		*ac = 0;
	
	*status = 0;
	*flag = 0;
	
	if (battery_capacity*10/100 >= power_left) 
		*flag |= 1;
		
	/* if < 10% charge left, low power */
	if (battery_capacity*10/100 > power_left) {
		*flag |= 2;
		*status = 1;
	}		

	/* if < 5% charge left, critical power */
	if (battery_capacity*5/100 > power_left) {
		*flag |= 4;
		*status = 2;
	}
	
	/* check for charging */
	if (data[2]&4) {
		*flag |= 8;
		*status = 3;
	}

	/* check for battery absent */
	if (!(data[2]&1)) {
		*flag |= 128;
		*status = 0x4;
	}
				
	*percentage = power_left * 100 / (battery_capacity+1);
	*time = -1;
}



int gericom_init(void)
{
	unsigned char *foo;
	int i;
	int q;
	
	get_battery_info();
	apm_set_battery_hook(&gericom_battery_hook);
	printk(KERN_INFO "Gericom battery monitoring module, (C) 2002 Red Hat, Inc.\n");
	return 0;
}

void gericom_exit(void)
{
	apm_remove_battery_hook(&gericom_battery_hook);
}

module_init(gericom_init) 
module_exit(gericom_exit)
MODULE_AUTHOR("Arjan van de Ven <arjanv@redhat.com> for Red Hat, Inc.");
MODULE_DESCRIPTION("Battery status module for Gericom laptops"); 
MODULE_LICENSE("GPL");
