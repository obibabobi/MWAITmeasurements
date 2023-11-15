#ifndef SYSFS_H
#define SYSFS_H

#include "consts.h"

#include <linux/types.h>

struct pkg_attributes
{
	u64 total_tsc[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c2[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c3[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c6[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c7[MAX_NUMBER_OF_MEASUREMENTS];
};

struct cpu_attributes
{
	u64 unhalted[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c3[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c6[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c7[MAX_NUMBER_OF_MEASUREMENTS];
};

#include "generic/sysfs.h"

#endif
