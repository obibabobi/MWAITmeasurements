#ifndef SYSFS_GENERIC_H
#define SYSFS_GENERIC_H

#ifndef SYSFS_H
#error Do not include directly, instead include architecture specific header!
#endif

#include <linux/kobject.h>
#include <linux/types.h>
#include <linux/sysfs.h>

extern struct pkg_stat
{
	struct kobject kobject;
	u64 energy_consumption[MAX_NUMBER_OF_MEASUREMENTS];
	struct pkg_attributes attributes;
} pkg_stats;

extern struct cpu_stat
{
	struct kobject kobject;
	u64 wakeup_time[MAX_NUMBER_OF_MEASUREMENTS];
	u64 wakeups[MAX_NUMBER_OF_MEASUREMENTS];
	struct cpu_attributes attributes;
} cpu_stats[MAX_CPUS];

extern struct attribute pkg_energy_consumption_attribute;

extern struct attribute cpu_wakeup_time_attribute;
extern struct attribute cpu_wakeups_attribute;

ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t ignore_write(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count);
void release(struct kobject *kobj);

ssize_t format_array_into_buffer(u64 *array, char *buf);

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
