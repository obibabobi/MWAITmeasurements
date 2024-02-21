#ifndef SYSFS_GENERIC_H
#define SYSFS_GENERIC_H

#ifndef SYSFS_H
#error Do not include directly, instead include architecture specific header!
#endif

#include "consts.h"

#include <linux/kobject.h>
#include <linux/types.h>
#include <linux/sysfs.h>

extern struct signal_stat
{
	struct kobject kobject;
	u64 signal_times[SIGNAL_EDGE_COUNT + 2];
} signal_stat;

void publish_signal_times(void);
void cleanup_signal_times(void);

extern struct pkg_stat
{
	struct kobject kobject;
	u64 energy_consumption[MAX_NUMBER_OF_MEASUREMENTS];
	u64 start_time[MAX_NUMBER_OF_MEASUREMENTS];
	u64 end_time[MAX_NUMBER_OF_MEASUREMENTS];
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
extern struct attribute start_time_attribute;
extern struct attribute end_time_attribute;

extern struct attribute cpu_wakeup_time_attribute;
extern struct attribute cpu_wakeups_attribute;

ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t ignore_write(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count);
void release(struct kobject *kobj);

extern int measurement_count;
ssize_t format_array_into_buffer(u64 *array, int len, char *buf);

#define output_to_sysfs(attribute_name, len)                                                        \
	({                                                                                          \
		if (strcmp(attr->name, #attribute_name) == 0)                                       \
			return format_array_into_buffer(stat->attributes.attribute_name, len, buf); \
	})

#define create_attribute(prefix, attribute_name)                          \
	static struct attribute prefix##_##attribute_name##_attribute = { \
	    .name = #attribute_name,                                      \
	    .mode = 0444};

void publish_measurement_results(void);
void cleanup_measurement_results(void);

#endif
