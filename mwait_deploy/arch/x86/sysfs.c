#include "sysfs.h"

struct pkg_stat pkg_stats;
struct cpu_stat cpu_stats[MAX_CPUS];

create_attribute(pkg, energy_consumption);
create_attribute(pkg, total_tsc);
create_attribute(pkg, wakeup_time);
create_attribute(pkg, c2);
create_attribute(pkg, c3);
create_attribute(pkg, c6);
create_attribute(pkg, c7);
static struct attribute *pkg_stats_attributes[] = {
    &pkg_energy_consumption_attribute,
    &pkg_total_tsc_attribute,
    &pkg_wakeup_time_attribute,
    &pkg_c2_attribute,
    &pkg_c3_attribute,
    &pkg_c6_attribute,
    &pkg_c7_attribute,
    NULL};
static struct attribute_group pkg_stats_group = {
    .attrs = pkg_stats_attributes};
static const struct attribute_group *pkg_stats_groups[] = {
    &pkg_stats_group,
    NULL};

create_attribute(cpu, wakeups);
create_attribute(cpu, unhalted);
create_attribute(cpu, c3);
create_attribute(cpu, c6);
create_attribute(cpu, c7);
static struct attribute *cpu_stats_attributes[] = {
    &cpu_wakeups_attribute,
    &cpu_unhalted_attribute,
    &cpu_c3_attribute,
    &cpu_c6_attribute,
    &cpu_c7_attribute,
    NULL};
static struct attribute_group cpu_stats_group = {
    .attrs = cpu_stats_attributes};
static const struct attribute_group *cpu_stats_groups[] = {
    &cpu_stats_group,
    NULL};

ssize_t show_pkg_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pkg_stat *stat = container_of(kobj, struct pkg_stat, kobject);
	output_to_sysfs(energy_consumption);
	output_to_sysfs(total_tsc);
	output_to_sysfs(wakeup_time);
	output_to_sysfs(c2);
	output_to_sysfs(c3);
	output_to_sysfs(c6);
	output_to_sysfs(c7);
	return 0;
}

ssize_t show_cpu_stats(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpu_stat *stat = container_of(kobj, struct cpu_stat, kobject);
	output_to_sysfs(wakeups);
	output_to_sysfs(unhalted);
	output_to_sysfs(c3);
	output_to_sysfs(c6);
	output_to_sysfs(c7);
	return 0;
}

ssize_t ignore_write(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	return count;
}
void release(struct kobject *kobj) {}

static const struct sysfs_ops pkg_sysfs_ops = {
    .show = show_pkg_stats,
    .store = ignore_write};
static const struct sysfs_ops cpu_sysfs_ops = {
    .show = show_cpu_stats,
    .store = ignore_write};
const struct kobj_type pkg_ktype = {
    .sysfs_ops = &pkg_sysfs_ops,
    .release = release,
    .default_groups = pkg_stats_groups};
const struct kobj_type cpu_ktype = {
    .sysfs_ops = &cpu_sysfs_ops,
    .release = release,
    .default_groups = cpu_stats_groups};
