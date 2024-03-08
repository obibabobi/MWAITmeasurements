#include "sysfs.h"

struct pkg_stat pkg_stats;
struct cpu_stat cpu_stats[MAX_CPUS];

static struct attribute *pkg_stats_attributes[] = {
    &start_time_attribute,
    &end_time_attribute,
    &repetitions_attribute,
    NULL};
static struct attribute_group pkg_stats_group = {
    .attrs = pkg_stats_attributes};
static const struct attribute_group *pkg_stats_groups[] = {
    &pkg_stats_group,
    NULL};

static struct attribute *cpu_stats_attributes[] = {
    &cpu_wakeup_time_attribute,
    &cpu_wakeups_attribute,
    NULL};
static struct attribute_group cpu_stats_group = {
    .attrs = cpu_stats_attributes};
static const struct attribute_group *cpu_stats_groups[] = {
    &cpu_stats_group,
    NULL};

ssize_t output_pkg_attributes(struct pkg_stat *stat, struct attribute *attr, char *buf)
{
	return 0;
}

ssize_t output_cpu_attributes(struct cpu_stat *stat, struct attribute *attr, char *buf)
{
	return 0;
}

static const struct sysfs_ops pkg_sysfs_ops = {
    .show = show_pkg_stats,
    .store = ignore_write};
static const struct sysfs_ops cpu_sysfs_ops = {
    .show = show_cpu_stats,
    .store = ignore_write};
static const struct kobj_type pkg_ktype = {
    .sysfs_ops = &pkg_sysfs_ops,
    .release = release,
    .default_groups = pkg_stats_groups};
static const struct kobj_type cpu_ktype = {
    .sysfs_ops = &cpu_sysfs_ops,
    .release = release,
    .default_groups = cpu_stats_groups};

extern unsigned cpus_present;

void publish_measurement_results(void)
{
	int err = kobject_init_and_add(&(pkg_stats.kobject), &pkg_ktype, NULL, "mwait_measurements");
	for (unsigned i = 0; i < cpus_present; ++i)
	{
		err |= kobject_init_and_add(&(cpu_stats[i].kobject), &cpu_ktype, &(pkg_stats.kobject), "cpu%u", i);
	}
	if (err)
		printk(KERN_ERR "ERROR: Could not properly initialize CPU stat structure in the sysfs.\n");
}

void cleanup_measurement_results(void)
{
	for (unsigned i = 0; i < cpus_present; ++i)
	{
		kobject_del(&(cpu_stats[i].kobject));
	}
	kobject_del(&(pkg_stats.kobject));
}