/* @(#)scsi_target.h 1.2 */
/*
 * SCSI Target Stub Driver for Linux
 * Ioctl Definitions File.
 *
 * Copyright (c) 2000 by Matthew Jacob
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
 * mjacob@feral.com
 */

#define	u_int8_t	unsigned char
#define	u_int16_t	unsigned short
#define	u_int32_t	unsigned int
#if BITS_PER_LONG == 64
#define	u_int64_t	unsigned long
#else
#define	u_int64_t	unsigned long long
#endif
#define	int8_t		char
#define	int16_t		short
#define	int32_t		int
#define	u_long		unsigned long
#define	u_int		unsigned int
#define	u_char		unsigned char

#ifndef	SCSI_GOOD
#define	SCSI_GOOD	0x0
#endif
#ifndef	SCSI_BUSY
#define	SCSI_BUSY	0x8
#endif
#ifndef	SCSI_CHECK
#define	SCSI_CHECK	0x2
#endif
#ifndef	SCSI_QFULL
#define	SCSI_QFULL	0x28
#endif

/*
 * ioctl support
 */
#define	_SI		('s' << 8)

/*
 * Set new debugging level (get previous) (int argument).
 */
#define	SC_DEBUG	(_SI | 0)

/*
 * enable Lun- still toy stuff...
 */
typedef struct {
	char	hba_name_unit[16];	/* e.g., "isp0" */
	int	channel, target, lun;	/* target is ignored for now */
} sc_enable_t;
#define	SC_ENABLE_LUN	(_SI | 1)
#define	SC_DISABLE_LUN	(_SI | 2)
