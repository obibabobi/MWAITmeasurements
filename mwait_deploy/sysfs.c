#include "sysfs.h"

#include <linux/kernel.h>
#include <asm/page.h>

struct signal_stat signal_stat;

struct attribute signal_times_attribute = {.name = "signal_times", .mode = 0444};

static struct attribute *signal_stat_attributes[] = {
    &signal_times_attribute,
    NULL};
static struct attribute_group signal_stat_group = {
    .attrs = signal_stat_attributes};
static const struct attribute_group *signal_stat_groups[] = {
    &signal_stat_group,
    NULL};

ssize_t show_signal_times(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct signal_stat *stat = container_of(kobj, struct signal_stat, kobject);
	if (strcmp(attr->name, "signal_times") == 0)
		return format_array_into_buffer(stat->signal_times, SIGNAL_EDGE_COUNT + 2, buf);
	return 0;
}

static const struct sysfs_ops signal_sysfs_ops = {
    .show = show_signal_times,
    .store = ignore_write};
static const struct kobj_type signal_ktype = {
    .sysfs_ops = &signal_sysfs_ops,
    .release = release,
    .default_groups = signal_stat_groups};

void publish_signal_times(void)
{
	int err = kobject_init_and_add(&(signal_stat.kobject), &signal_ktype, NULL, "mwait_measurements");
	if (err)
		printk(KERN_ERR "Could not properly initialize signal stat structure in the sysfs.");
}

void cleanup_signal_times(void)
{
	kobject_del(&(signal_stat.kobject));
}

struct attribute pkg_energy_consumption_attribute = {.name = "energy_consumption", .mode = 0444};
struct attribute start_time_attribute = {.name = "start_time", .mode = 0444};
struct attribute end_time_attribute = {.name = "end_time", .mode = 0444};

struct attribute cpu_wakeup_time_attribute = {.name = "wakeup_time", .mode = 0444};
struct attribute cpu_wakeups_attribute = {.name = "wakeups", .mode = 0444};

ssize_t format_array_into_buffer(u64 *array, int len, char *buf)
{
	int bytes_written = 0;
	int i = 0;

	while (bytes_written < PAGE_SIZE && i < len)
	{
		bytes_written += scnprintf(buf + bytes_written, PAGE_SIZE - bytes_written, "%llu\n", array[i]);
		++i;
	}
	return bytes_written;
}

ssize_t ignore_write(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	return count;
}
void release(struct kobject *kobj) {}

ssize_t output_pkg_attributes(struct pkg_stat *stat, struct attribute *attr, char *buf);
ssize_t output_cpu_attributes(struct cpu_stat *stat, struct attribute *attr, char *buf);

ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pkg_stat *stat = container_of(kobj, struct pkg_stat, kobject);
	if (strcmp(attr->name, "energy_consumption") == 0)
		return format_array_into_buffer(stat->energy_consumption, measurement_count, buf);
	if (strcmp(attr->name, "start_time") == 0)
		return format_array_into_buffer(stat->start_time, measurement_count, buf);
	if (strcmp(attr->name, "end_time") == 0)
		return format_array_into_buffer(stat->end_time, measurement_count, buf);
	return output_pkg_attributes(stat, attr, buf);
}

ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpu_stat *stat = container_of(kobj, struct cpu_stat, kobject);
	if (strcmp(attr->name, "wakeup_time") == 0)
		return format_array_into_buffer(stat->wakeup_time, measurement_count, buf);
	if (strcmp(attr->name, "wakeups") == 0)
		return format_array_into_buffer(stat->wakeups, measurement_count, buf);
	return output_cpu_attributes(stat, attr, buf);
}
