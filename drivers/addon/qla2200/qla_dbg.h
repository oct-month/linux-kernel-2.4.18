/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.2.x and 2.4.x
 * Copyright (C) 2001 Qlogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

#ifndef _QLA_DBG_H
#define	_QLA_DBG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver debug definitions in makefile.
 *
 * QL_DEBUG_LEVEL_1=0x1		Output register accesses.
 * QL_DEBUG_LEVEL_2=0x2		Output error msgs.
 * QL_DEBUG_LEVEL_3=0x4		Output function trace msgs.
 * QL_DEBUG_LEVEL_4=0x8		Output NVRAM trace msgs.
 * QL_DEBUG_LEVEL_5=0x10	Output ring trace msgs.
 * QL_DEBUG_LEVEL_6=0x20	Output WATCHDOG timer trace.
 * QL_DEBUG_LEVEL_7=0x40	Output RISC load trace msgs.
 * QL_DEBUG_LEVEL_8=0x80	Output ring staturation msgs.
 * QL_DEBUG_LEVEL_9=0x200	Output failover trace msgs.
 * QL_DEBUG_LEVEL_10=0x400	Output configuration trace msgs.
 * QL_DEBUG_LEVEL_100=0x100	Temporary msgs.
 */
 
/* #define QL_DEBUG_LEVEL_9 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_10 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_20 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_40 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_80 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_100 */      /* Output ring saturation msgs to COM1 */
/* #define QL_DEBUG_LEVEL_400  */
/* #define QL_DEBUG_LEVEL_800  */

/* #define QL_DEBUG_ROUTINES */


/*
 * Macro defines.
 */
#ifdef QL_DEBUG_LEVEL_100
#define	QL_PRINT_100(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_100(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_100(a, b, c, d)
#define	QL_DUMP_100(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_1
#define	QL_PRINT_1(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_1(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_1(a, b, c, d)
#define	QL_DUMP_1(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_2
#define	QL_PRINT_2(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_2(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_2(a, b, c, d)
#define	QL_DUMP_2(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_4
#define	QL_PRINT_3(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_3(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_3(a, b, c, d)
#define	QL_DUMP_3(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_6
#define	QL_PRINT_2_3(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_2_3(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_2_3(a, b, c, d)
#define	QL_DUMP_2_3(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_8
#define	QL_PRINT_4(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_4(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_4(a, b, c, d)
#define	QL_DUMP_4(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_10
#define	QL_PRINT_5(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_5(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_5(a, b, c, d)
#define	QL_DUMP_5(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_20
#define	QL_PRINT_6(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_6(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_6(a, b, c, d)
#define	QL_DUMP_6(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_40
#define	QL_PRINT_7(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_7(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_7(a, b, c, d)
#define	QL_DUMP_7(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_80
#define	QL_PRINT_8(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_8(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_8(a, b, c, d)
#define	QL_DUMP_8(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_200
#define	QL_PRINT_9(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_9(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_9(a, b, c, d)
#define	QL_DUMP_9(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_400
#define	QL_PRINT_10(a, b, c, d) \
       qla2100_print((char *)a)
#define	QL_DUMP_10(a, b, c, d) \
       qla2100_print((char *)a); \
	qla2100_dump_buffer((char *)b, (uint32_t)d)
#else
#define	QL_PRINT_10(a, b, c, d)
#define	QL_DUMP_10(a, b, c, d)
#endif

#ifdef QL_DEBUG_LEVEL_800
#define	QL_PRINT_13(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_13(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#define	QL_PRINT_12(a, b, c, d) \
	qla2x00_print((char *)a, (uint64_t)b, (uint8_t)c, (uint8_t)d)
#define	QL_DUMP_12(a, b, c, d) \
	qla2x00_dump_buffer((char *)a, (uint8_t *)b, (uint8_t)c, (uint32_t)d)
#else
#define	QL_PRINT_13(a, b, c, d)
#define	QL_DUMP_13(a, b, c, d)

#define	QL_PRINT_12(a, b, c, d)
#define	QL_DUMP_12(a, b, c, d)
#endif
/* Number radix. */
#define	QDBG_NO_NUM	0	/* No number. */
#define	QDBG_DEC_NUM	10	/* Decimal number. */
#define	QDBG_HEX_NUM	16	/* Hexadecimal number. */
#define	QDBG_PSTR 	QDBG_NO_NUM

/* Insert newline at end of srting. */
#define	QDBG_NL		1	/* Add newline. */
#define	QDBG_NNL	0	/* No newline. */

/*
 * Firmware Dump structure definition
 */
#define	FW_DUMP_SIZE	0x68000		/* bytes */

typedef struct fw_dump {
	uint16_t pbiu_reg[8];
	uint16_t mailbox_reg[8];
	uint16_t dma_reg[48];
	uint16_t risc_hdw_reg[16];
	uint16_t risc_gp0_reg[16];
	uint16_t risc_gp1_reg[16];
	uint16_t risc_gp2_reg[16];
	uint16_t risc_gp3_reg[16];
	uint16_t risc_gp4_reg[16];
	uint16_t risc_gp5_reg[16];
	uint16_t risc_gp6_reg[16];
	uint16_t risc_gp7_reg[16];
	uint16_t frame_buf_hdw_reg[16];
	uint16_t fpm_b0_reg[64];
	uint16_t fpm_b1_reg[64];
	uint16_t risc_ram[0xf000];
} fw_dump_t;

#ifdef QL_DEBUG_ROUTINES

#define	QL_PRINT_SAVE		uint8_t saved_print_status; \
    saved_print_status = qla2x00_debug_print
#define	QL_PRINT_SET(x)		(qla2x00_debug_print = x)
#define	QL_PRINT_RESTORE	(qla2x00_debug_print = saved_print_status)

#ifdef sparc
#define	QL_DEBUG_CONSOLE_DELAY	5	/* In clock ticks. */
#endif

#else	/* QL_DEBUG_ROUTINES not defined */
#define	QL_PRINT_SAVE
#define	QL_PRINT_SET(x)
#define	QL_PRINT_RESTORE
#endif	/* ifdef QL_DEBUG_ROUTINES */

#ifdef  __cplusplus
}
#endif  /* __cplusplus */

#endif  /* _QLA2X00_DEBUG_H */
