/* @(#)scsi_target_ctl.c 1.3 */
/*
 * SCSI Target Mode "stub" control program for Linux.
 *
 * Copyright (c) 2001 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * PMB #825
 * 5214-F Diamond Hts Blvd
 * San Francisco, CA, 94131
 * mjacob@feral.com
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/major.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include "scsi_target.h"

const char scp[] = "/proc/scsi/scsi_target_minor";
const char sct[] = "/dev/scsi_target";
const char usage[] =
    "usage: %s {enable|disable} hba-name-unit channel target lun\n";

int
main(int a, char **v)
{
	FILE *fp;
	sc_enable_t x;
	int fd, action, minor;

	if ((fp = fopen(scp, "r")) == NULL) {
		perror(scp);
		return (1);	
	}
	minor = 0;
	if (fscanf(fp, "%d", &minor) != 1) {
		fprintf(stderr, "could not get misc minor to use\n", minor);
		return (1);
	}
	
	(void) unlink(sct);
	(void) mknod(sct, S_IFCHR, makedev(MISC_MAJOR, minor));

	if ((fd = open(sct, 0)) < 0) {
		perror(sct);
	}

	if (a < 2) {
		fprintf(stderr, usage, v[0]);
		return (1);
	} else if (strcmp(v[1], "enable") == 0) {
		action = SC_ENABLE_LUN;
	} else if (strcmp(v[1], "disable") == 0) {
		action = SC_DISABLE_LUN;
	} else {
		fprintf(stderr, usage, v[0]);
		return (1);
	}

	/*
	 * xxx {enable|disable} hba-name-unit channel target lun
	 */
	if (a != 6) {
		fprintf(stderr, usage, v[0]);
		return (1);

	}
	memset(&x, 0, sizeof (x));
	strncpy(x.hba_name_unit, v[2], sizeof (x.hba_name_unit)-1);
	x.channel = atoi(v[3]);
	x.target = atoi(v[4]);
	x.lun = atoi(v[5]);
	if (ioctl(fd, action, &x) < 0) {
		perror(v[1]);
		return (2);
	}
	return (0);
}
