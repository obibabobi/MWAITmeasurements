#include "sysfs.h"

#include <linux/kernel.h>
#include <asm/page.h>

extern int measurement_count;

ssize_t format_array_into_buffer(u64 *array, char *buf)
{
	int bytes_written = 0;
	int i = 0;

	while (bytes_written < PAGE_SIZE && i < measurement_count)
	{
		bytes_written += scnprintf(buf + bytes_written, PAGE_SIZE - bytes_written, "%llu\n", array[i]);
		++i;
	}
	return bytes_written;
}

ssize_t output_pkg_attributes(struct pkg_stat *stat, struct attribute *attr, char *buf);
ssize_t output_cpu_attributes(struct cpu_stat *stat, struct attribute *attr, char *buf);

ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pkg_stat *stat = container_of(kobj, struct pkg_stat, kobject);
	if (strcmp(attr->name, "energy_consumption") == 0)
		return format_array_into_buffer(stat->energy_consumption, buf);
	if (strcmp(attr->name, "wakeup_time") == 0)
		return format_array_into_buffer(stat->wakeup_time, buf);
	return output_pkg_attributes(stat, attr, buf);
}

ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpu_stat *stat = container_of(kobj, struct cpu_stat, kobject);
	if (strcmp(attr->name, "wakeups") == 0)
		return format_array_into_buffer(stat->wakeups, buf);
	return output_cpu_attributes(stat, attr, buf);
}
