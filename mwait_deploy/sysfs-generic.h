#ifndef SYSFS_GENERIC_H
#define SYSFS_GENERIC_H

#ifndef SYSFS_H
#error Do not include directly, instead include architecture specific header!
#endif

extern struct pkg_stat
{
	struct kobject kobject;
	u64 energy_consumption[MAX_NUMBER_OF_MEASUREMENTS];
	u64 wakeup_time[MAX_NUMBER_OF_MEASUREMENTS];
	struct pkg_attributes attributes;
} pkg_stats;

extern struct cpu_stat
{
	struct kobject kobject;
	u64 wakeups[MAX_NUMBER_OF_MEASUREMENTS];
	struct cpu_attributes attributes;
} cpu_stats[MAX_CPUS];

#include "sysfs-helper.h"

#include <linux/sysfs.h>

ssize_t output_pkg_attributes(struct pkg_stat *stat, struct attribute *attr, char *buf);

static inline ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pkg_stat *stat = container_of(kobj, struct pkg_stat, kobject);
	if (strcmp(attr->name, "energy_consumption") == 0)
		return format_array_into_buffer(stat->energy_consumption, buf);
	if (strcmp(attr->name, "wakeup_time") == 0)
		return format_array_into_buffer(stat->wakeup_time, buf);
	return output_pkg_attributes(stat, attr, buf);
}

ssize_t output_cpu_attributes(struct cpu_stat *stat, struct attribute *attr, char *buf);

static inline ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpu_stat *stat = container_of(kobj, struct cpu_stat, kobject);
	if (strcmp(attr->name, "wakeups") == 0)
		return format_array_into_buffer(stat->wakeups, buf);
	return output_cpu_attributes(stat, attr, buf);
}

#define output_to_sysfs(attribute_name)                                                        \
	({                                                                                     \
		if (strcmp(attr->name, #attribute_name) == 0)                                  \
			return format_array_into_buffer(stat->attributes.attribute_name, buf); \
	})

#define create_attribute(prefix, attribute_name)                          \
	static struct attribute prefix##_##attribute_name##_attribute = { \
	    .name = #attribute_name,                                      \
	    .mode = 0444};

#endif