/* $Id: op_events.c,v 1.6 2002/03/03 01:18:18 phil_e Exp $ */
/* COPYRIGHT (C) 2000 THE VICTORIA UNIVERSITY OF MANCHESTER and John Levon
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Adapted from libpperf 0.5 by M. Patrick Goda and Michael S. Warren */

/* See IA32 Vol. 3 Appendix A */

#ifdef __KERNEL__
#include <linux/string.h>
#define strcmp(a,b) strnicmp((a),(b),strlen((b)))
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#endif

#include "op_user.h"

struct cpu_type_descr {
	const char * name;
	uint nr_counters;
};

static struct cpu_type_descr cpu_type_descrs[MAX_CPU_TYPE] = {
	{ "Pentium Pro", 2 },
	{ "PII", 2 },
	{ "PIII", 2 },
	{ "Athlon", 4 },
	{ "CPU with RTC device", 1}
};

struct op_unit_mask op_unit_masks[] = {
	/* reserved empty entry */
	{ 0, utm_mandatory, 0x00, { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }, },
	/* MESI counters */
	{ 5, utm_bitmask, 0x0f, { 0x8, 0x4, 0x2, 0x1, 0xf, 0x0, 0x0 }, },
	/* EBL self/any default to any transitions */
	{ 2, utm_exclusive, 0x20, { 0x0, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0 }, },
	/* MMX PII events */
	{ 1, utm_mandatory, 0xf, { 0xf, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }, },
	{ 7, utm_bitmask, 0x3f, { 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x3f }, },
	{ 2, utm_exclusive, 0x0, { 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0 }, },
	{ 5, utm_bitmask, 0x0f, { 0x1, 0x2, 0x4, 0x8, 0xf, 0x0, 0x0 }, },
	/* KNI PIII events */
	{ 4, utm_exclusive, 0x0, { 0x0, 0x1, 0x2, 0x3, 0x0, 0x0, 0x0 }, },
	{ 2, utm_bitmask, 0x1, { 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0 }, },
	/* Athlon MOESI cache events */
	{ 6, utm_bitmask, 0x1f, { 0x10, 0x8, 0x4, 0x2, 0x1, 0x1f }, }
};

/* the following are just short cut for filling the table of event */
#define OP_RTC		(1 << CPU_RTC)
#define OP_ATHLON	(1 << CPU_ATHLON)
#define OP_PPRO		(1 << CPU_PPRO)
#define OP_PII		(1 << CPU_PII)
#define OP_PIII		(1 << CPU_PIII)
#define OP_PII_PIII	(OP_PII | OP_PIII)
#define OP_IA_ALL	(OP_PII_PIII | OP_PPRO)

#define CTR_0		(1 << 0)
#define CTR_1		(1 << 1)

/* ctr allowed, Event #, unit mask, name, minimum event value */
/* event name must be in one word */
struct op_event op_events[] = {
  /* Clocks */
  { CTR_ALL, OP_IA_ALL, 0x79, 0, "CPU_CLK_UNHALTED", 6000 },
  /* Data Cache Unit (DCU) */
  { CTR_ALL, OP_IA_ALL, 0x43, 0, "DATA_MEM_REFS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x45, 0, "DCU_LINES_IN", 500 },
  { CTR_ALL, OP_IA_ALL, 0x46, 0, "DCU_M_LINES_IN", 500 },
  { CTR_ALL, OP_IA_ALL, 0x47, 0, "DCU_M_LINES_OUT", 500},
  { CTR_ALL, OP_IA_ALL, 0x48, 0, "DCU_MISS_OUTSTANDING", 500 },
  /* Intruction Fetch Unit (IFU) */
  { CTR_ALL, OP_IA_ALL, 0x80, 0, "IFU_IFETCH", 500 },
  { CTR_ALL, OP_IA_ALL, 0x81, 0, "IFU_IFETCH_MISS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x85, 0, "ITLB_MISS", 500},
  { CTR_ALL, OP_IA_ALL, 0x86, 0, "IFU_MEM_STALL", 500 },
  { CTR_ALL, OP_IA_ALL, 0x87, 0, "ILD_STALL", 500 },
  /* L2 Cache */
  { CTR_ALL, OP_IA_ALL, 0x28, 1, "L2_IFETCH", 500 },
  { CTR_ALL, OP_IA_ALL, 0x29, 1, "L2_LD", 500 },
  { CTR_ALL, OP_IA_ALL, 0x2a, 1, "L2_ST", 500 },
  { CTR_ALL, OP_IA_ALL, 0x24, 0, "L2_LINES_IN", 500 },
  { CTR_ALL, OP_IA_ALL, 0x26, 0, "L2_LINES_OUT", 500 },
  { CTR_ALL, OP_IA_ALL, 0x25, 0, "L2_M_LINES_INM", 500 },
  { CTR_ALL, OP_IA_ALL, 0x27, 0, "L2_M_LINES_OUTM", 500 },
  { CTR_ALL, OP_IA_ALL, 0x2e, 1, "L2_RQSTS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x21, 0, "L2_ADS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x22, 0, "L2_DBUS_BUSY", 500 },
  { CTR_ALL, OP_IA_ALL, 0x23, 0, "L2_DMUS_BUSY_RD", 500 },
  /* External Bus Logic (EBL) */
  { CTR_ALL, OP_IA_ALL, 0x62, 2, "BUS_DRDY_CLOCKS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x63, 2, "BUS_LOCK_CLOCKS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x60, 0, "BUS_REQ_OUTSTANDING", 500 },
  { CTR_ALL, OP_IA_ALL, 0x65, 2, "BUS_TRAN_BRD", 500 },
  { CTR_ALL, OP_IA_ALL, 0x66, 2, "BUS_TRAN_RFO", 500 },
  { CTR_ALL, OP_IA_ALL, 0x67, 2, "BUS_TRANS_WB", 500 },
  { CTR_ALL, OP_IA_ALL, 0x68, 2, "BUS_TRAN_IFETCH", 500 },
  { CTR_ALL, OP_IA_ALL, 0x69, 2, "BUS_TRAN_INVAL", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6a, 2, "BUS_TRAN_PWR", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6b, 2, "BUS_TRANS_P", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6c, 2, "BUS_TRANS_IO", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6d, 2, "BUS_TRANS_DEF", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6e, 2, "BUS_TRAN_BURST", 500 },
  { CTR_ALL, OP_IA_ALL, 0x70, 2, "BUS_TRAN_ANY", 500 },
  { CTR_ALL, OP_IA_ALL, 0x6f, 2, "BUS_TRAN_MEM", 500 },
  { CTR_ALL, OP_IA_ALL, 0x64, 0, "BUS_DATA_RCV", 500 },
  { CTR_ALL, OP_IA_ALL, 0x61, 0, "BUS_BNR_DRV", 500 },
  { CTR_ALL, OP_IA_ALL, 0x7a, 0, "BUS_HIT_DRV", 500 },
  { CTR_ALL, OP_IA_ALL, 0x7b, 0, "BUS_HITM_DRV", 500 },
  { CTR_ALL, OP_IA_ALL, 0x7e, 0, "BUS_SNOOP_STALL", 500 },
  /* Floating Point Unit (FPU) */
  { CTR_0, OP_IA_ALL, 0xc1, 0, "COMP_FLOP_RET", 3000 },
  { CTR_0, OP_IA_ALL, 0x10, 0, "FLOPS", 3000 },
  { CTR_1, OP_IA_ALL, 0x11, 0, "FP_ASSIST", 500 },
  { CTR_1, OP_IA_ALL, 0x12, 0, "MUL", 1000 },
  { CTR_1, OP_IA_ALL, 0x13, 0, "DIV", 500 },
  { CTR_0, OP_IA_ALL, 0x14, 0, "CYCLES_DIV_BUSY", 1000 },
  /* Memory Ordering */
  { CTR_ALL, OP_IA_ALL, 0x03, 0, "LD_BLOCKS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x04, 0, "SB_DRAINS", 500 },
  { CTR_ALL, OP_IA_ALL, 0x05, 0, "MISALIGN_MEM_REF", 500 },
  /* PIII KNI */
  { CTR_ALL, OP_PIII, 0x07, 7, "EMON_KNI_PREF_DISPATCHED", 500 },
  { CTR_ALL, OP_PIII, 0x4b, 7, "EMON_KNI_PREF_MISS", 500 },
  /* Instruction Decoding and Retirement */
  { CTR_ALL, OP_IA_ALL, 0xc0, 0, "INST_RETIRED", 6000 },
  { CTR_ALL, OP_IA_ALL, 0xc2, 0, "UOPS_RETIRED", 6000 },
  { CTR_ALL, OP_IA_ALL, 0xd0, 0, "INST_DECODED", 6000 },
  /* PIII KNI */
  { CTR_ALL, OP_PIII, 0xd8, 8, "EMON_KNI_INST_RETIRED", 3000 },
  { CTR_ALL, OP_PIII, 0xd9, 8, "EMON_KNI_COMP_INST_RET", 3000 },
  /* Interrupts */
  { CTR_ALL, OP_IA_ALL, 0xc8, 0, "HW_INT_RX", 500 },
  { CTR_ALL, OP_IA_ALL, 0xc6, 0, "CYCLES_INT_MASKED", 500 },
  { CTR_ALL, OP_IA_ALL, 0xc7, 0, "CYCLES_INT_PENDING_AND_MASKED", 500 },
  /* Branches */
  { CTR_ALL, OP_IA_ALL, 0xc4, 0, "BR_INST_RETIRED", 500 },
  { CTR_ALL, OP_IA_ALL, 0xc5, 0, "BR_MISS_PRED_RETIRED", 500 },
  { CTR_ALL, OP_IA_ALL, 0xc9, 0, "BR_TAKEN_RETIRED", 500 },
  { CTR_ALL, OP_IA_ALL, 0xca, 0, "BR_MISS_PRED_TAKEN_RET", 500 },
  { CTR_ALL, OP_IA_ALL, 0xe0, 0, "BR_INST_DECODED", 500 },
  { CTR_ALL, OP_IA_ALL, 0xe2, 0, "BTB_MISSES", 500 },
  { CTR_ALL, OP_IA_ALL, 0xe4, 0, "BR_BOGUS", 500 },
  { CTR_ALL, OP_IA_ALL, 0xe6, 0, "BACLEARS", 500 },
  /* Stalls */
  { CTR_ALL, OP_IA_ALL, 0xa2, 0, "RESOURCE_STALLS", 500 },
  { CTR_ALL, OP_IA_ALL, 0xd2, 0, "PARTIAL_RAT_STALLS", 500 },
  /* Segment Register Loads */
  { CTR_ALL, OP_IA_ALL, 0x06, 0, "SEGMENT_REG_LOADS", 500 },
  /* MMX (Pentium II only) */
  { CTR_ALL, OP_PII, 0xb0, 0, "MMX_INSTR_EXEC", 3000 },
  { CTR_ALL, OP_PII_PIII, 0xb1, 0, "MMX_SAT_INSTR_EXEC", 3000 },
  { CTR_ALL, OP_PII_PIII, 0xb2, 3, "MMX_UOPS_EXEC", 3000 },
  { CTR_ALL, OP_PII_PIII, 0xb3, 4, "MMX_INSTR_TYPE_EXEC", 3000 },
  { CTR_ALL, OP_PII_PIII, 0xcc, 5, "FP_MMX_TRANS", 3000 },
  { CTR_ALL, OP_PII_PIII, 0xcd, 0, "MMX_ASSIST", 500 },
  { CTR_ALL, OP_PII_PIII, 0xce, 0, "MMX_INSTR_RET", 3000 },
  /* segment renaming (Pentium II only) */
  { CTR_ALL, OP_PII, 0xd4, 6, "SEG_RENAME_STALLS", 500 },
  { CTR_ALL, OP_PII, 0xd5, 6, "SEG_REG_RENAMES", 500 },
  { CTR_ALL, OP_PII, 0xd6, 0, "RET_SEG_RENAMES", 500 },

  /* athlon events */
  { CTR_ALL, OP_ATHLON, 0xc0, 0, "RETIRED_INSNS", 3000,},
  { CTR_ALL, OP_ATHLON, 0xc1, 0, "RETIRED_OPS", 500,},
  { CTR_ALL, OP_ATHLON, 0x80, 0, "ICACHE_FETCHES", 500,},
  { CTR_ALL, OP_ATHLON, 0x81, 0, "ICACHE_MISSES", 500,},
  { CTR_ALL, OP_ATHLON, 0x40, 0, "DATA_CACHE_ACCESSES", 500,},
  { CTR_ALL, OP_ATHLON, 0x41, 0, "DATA_CACHE_MISSES", 500,},
  { CTR_ALL, OP_ATHLON, 0x42, 9, "DATA_CACHE_REFILLS_FROM_L2", 500,},
  { CTR_ALL, OP_ATHLON, 0x43, 9, "DATA_CACHE_REFILLS_FROM_SYSTEM", 500,},
  { CTR_ALL, OP_ATHLON, 0x44, 9, "DATA_CACHE_WRITEBACKS", 500,},
  { CTR_ALL, OP_ATHLON, 0xc2, 0, "RETIRED_BRANCHES", 500,},
  { CTR_ALL, OP_ATHLON, 0xc3, 0, "RETIRED_BRANCHES_MISPREDICTED", 500,},
  { CTR_ALL, OP_ATHLON, 0xc4, 0, "RETIRED_TAKEN_BRANCHES", 500,},
  { CTR_ALL, OP_ATHLON, 0xc5, 0, "RETIRED_TAKEN_BRANCHES_MISPREDICTED", 500,},
  { CTR_ALL, OP_ATHLON, 0x45, 0, "L1_DTLB_MISSES_L2_DTLD_HITS", 500,},
  { CTR_ALL, OP_ATHLON, 0x46, 0, "L1_AND_L2_DTLB_MISSES", 500,},
  { CTR_ALL, OP_ATHLON, 0x47, 0, "MISALIGNED_DATA_REFS", 500,},
  { CTR_ALL, OP_ATHLON, 0x84, 0, "L1_ITLB_MISSES_L2_ITLB_HITS", 500,},
  { CTR_ALL, OP_ATHLON, 0x85, 0, "L1_AND_L2_ITLB_MISSES", 500,},
  { CTR_ALL, OP_ATHLON, 0xc6, 0, "RETIRED_FAR_CONTROL_TRANSFERS", 500,},
  { CTR_ALL, OP_ATHLON, 0xc7, 0, "RETIRED_RESYNC_BRANCHES", 500,},
  { CTR_ALL, OP_ATHLON, 0xcd, 0, "INTERRUPTS_MASKED", 500,},
  { CTR_ALL, OP_ATHLON, 0xce, 0, "INTERRUPTS_MASKED_PENDING", 500,},
  { CTR_ALL, OP_ATHLON, 0xcf, 0, "HARDWARE_INTERRUPTS", 10,},

  /* other CPUs */
  { CTR_0, OP_RTC, 0xff, 0, "RTC_Interrupts", 2,},
};

/* the total number of events for all processor type */
uint op_nr_events = (sizeof(op_events)/sizeof(op_events[0]));

/**
 * op_check_unit_mask - sanity check unit mask value
 * @allow: allowed unit mask array
 * @um: unit mask value to check
 *
 * Verify that a unit mask value is within the allowed array.
 *
 * The function returns:
 * -1  if the value is not allowed,
 * 0   if the value is allowed and represent multiple units,
 * > 0 otherwise, in this case allow->um[return value - 1] == um so the
 * caller can access to the description of the unit_mask.
 */
int op_check_unit_mask(struct op_unit_mask *allow, u8 um)
{
	uint i, mask;

	switch (allow->unit_type_mask) {
		case utm_exclusive:
		case utm_mandatory:
			for (i=0; i < allow->num; i++) {
				if (allow->um[i] == um)
					return i + 1;
			}
			break;

		case utm_bitmask:
			/* Must reject 0 bit mask because it can count nothing */
			if (um == 0)
				break;
 
			mask = 0;
			for (i=0; i < allow->num; i++) {
				if (allow->um[i] == um)
					/* it is an exact match so return the index + 1 */
					return i + 1;

				mask |= allow->um[i];
			}

			if ((mask & um) == um)
				return 0;
			break;
	}

	return -1;
}

/**
 * op_min_count - get the minimum count value.
 * @ctr_type: event value
 * @cpu_type: cpu type
 *
 * The function returns > 0 if the event is found
 * 0 otherwise
 */
int op_min_count(u8 ctr_type, op_cpu cpu_type)
{
	int ret = 0;
	uint i;
	int cpu_mask = 1 << cpu_type;

	for (i = 0; i < op_nr_events; i++) {
		if (op_events[i].val == ctr_type && (op_events[i].cpu_mask & cpu_mask)) {
			ret = op_events[i].min_count;
			break;
		}
	}

	return ret;
}

/**
 * op_check_events - sanity check event values
 * @ctr: counter number
 * @ctr_type: event value for counter 0
 * @ctr_um: unit mask for counter 0
 * @cpu_type: processor type
 *
 * Check that the counter event and unit mask values
 * are allowed. @cpu_type should be set as follows :
 *
 * 0 Pentium Pro
 *
 * 1 Pentium II
 *
 * 2 Pentium III
 *
 * 3 AMD Athlon
 *
 * Don't fail if ctr_type == 0.
 *
 * The function returns bitmask of failure cause
 * 0 otherwise
 */
int op_check_events(int ctr, u8 ctr_type, u8 ctr_um, op_cpu cpu_type)
{
	int ret = 0x0;
	uint i = 0;
	uint cpu_mask = 1 << cpu_type;
	uint ctr_mask = 1 << ctr;

	if (ctr_type != 0) {
		for ( ; i < op_nr_events; i++) {
			if (op_events[i].val == ctr_type && (op_events[i].cpu_mask & cpu_mask)) {
				if ((op_events[i].counter_mask & ctr_mask) == 0)
					ret |= OP_EVT_CTR_NOT_ALLOWED;

				if (op_events[i].unit && 
				    op_check_unit_mask(&op_unit_masks[op_events[i].unit], ctr_um) < 0)
					ret |= OP_EVT_NO_UM;
				break;
			}
		}
	}

	if (i == op_nr_events)
		ret |= OP_EVT_NOT_FOUND;

	return ret;
}

/**
 * op_get_cpu_type_str - get the cpu string.
 * @cpu_type: the cpu type identifier
 *
 * The function always return a valid const char*
 * the core cpu denomination or "invalid cpu type" if
 * @cpu_type is not valid.
 */
const char * op_get_cpu_type_str(op_cpu cpu_type)
{
	if (cpu_type < 0 || cpu_type > MAX_CPU_TYPE) {
		return "invalid cpu type";
	}

	return cpu_type_descrs[cpu_type].name;
}

/**
 * op_get_cpu_nr_counters - get the nr of counter
 * @cpu_type: the cpu type identifier
 *
 * The function return the number of counter available for this
 * cpu type. return (uint)-1 if the cpu type is nopt recognized
 */
uint op_get_cpu_nr_counters(op_cpu cpu_type)
{
	if (cpu_type < 0 || cpu_type > MAX_CPU_TYPE) {
		return (uint)-1;
	}

	return cpu_type_descrs[cpu_type].nr_counters;
}
