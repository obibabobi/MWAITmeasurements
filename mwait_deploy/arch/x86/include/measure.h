#ifndef MEASURE_H
#define MEASURE_H

#define TOTAL_ENERGY_CONSUMED_MASK (0xffffffff)
#define IA32_FIXED_CTR2 (0x30b)
#define IA32_FIXED_CTR_CTRL (0x38d)
#define IA32_PERF_GLOBAL_CTRL (0x38f)

enum entry_mechanism
{
	ENTRY_MECHANISM_UNKNOWN,
	ENTRY_MECHANISM_POLL,
	ENTRY_MECHANISM_MWAIT,
	ENTRY_MECHANISM_IOPORT
};

#include "generic/measure.h"

#endif