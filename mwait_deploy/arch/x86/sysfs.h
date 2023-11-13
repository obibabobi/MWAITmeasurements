#ifndef SYSFS_H
#define SYSFS_H

#include "../../consts.h"

#include <linux/kobject.h>

extern struct pkg_stat
{
	struct kobject kobject;
	u64 energy_consumption[MAX_NUMBER_OF_MEASUREMENTS];
	u64 total_tsc[MAX_NUMBER_OF_MEASUREMENTS];
	u64 wakeup_time[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c2[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c3[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c6[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c7[MAX_NUMBER_OF_MEASUREMENTS];
} pkg_stats;

extern struct cpu_stat
{
	struct kobject kobject;
	u64 wakeups[MAX_NUMBER_OF_MEASUREMENTS];
	u64 unhalted[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c3[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c6[MAX_NUMBER_OF_MEASUREMENTS];
	u64 c7[MAX_NUMBER_OF_MEASUREMENTS];
} cpu_stats[MAX_CPUS];

extern const struct kobj_type pkg_ktype;
extern const struct kobj_type cpu_ktype;

#include "../../sysfs-generic.h"

#endif