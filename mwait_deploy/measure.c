#include "measure.h"
#include "sysfs.h"

#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

int measurement_duration = 100;
module_param(measurement_duration, int, 0);
MODULE_PARM_DESC(measurement_duration, "Duration of each measurement in milliseconds.");
int measurement_count = 10;
module_param(measurement_count, int, 0);
MODULE_PARM_DESC(measurement_count, "How many measurements should be done.");
static int cpus_sleep = -1;
module_param(cpus_sleep, int, 0);
MODULE_PARM_DESC(cpus_sleep, "Number of CPUs that should use the requested entry_mechanism to sleep instead of polling during the measurement.\n"
			     "If 'POLL' was selected as entry_mechanism, this setting does nothing.\n"
			     "By default, none of the CPUs poll.");
static char *cpu_selection = "core";
module_param(cpu_selection, charp, 0);
MODULE_PARM_DESC(cpu_selection, "How the CPUs to poll instead should be selected. Supported are 'core' and 'cpu_nr'.");

u64 energy_consumption;
DEFINE_PER_CPU(u64, wakeup_time);
DEFINE_PER_CPU(u64, wakeups);

unsigned cpus_present;
bool redo_measurement;
DEFINE_PER_CPU(int, trigger);
enum entry_mechanism requested_entry_mechanism;
DEFINE_PER_CPU(enum entry_mechanism, cpu_entry_mechanism);

static atomic_t sync_var;

void leader_callback(void)
{
	wakeup_other_cpus();

	set_global_final_values();
}

void all_cpus_callback(int this_cpu)
{
	set_cpu_final_values(this_cpu);

	per_cpu(trigger, this_cpu) = 0;
}

static inline void sync(int this_cpu)
{
	atomic_inc(&sync_var);
	if (!this_cpu)
	{
		while (atomic_read(&sync_var) < cpus_present)
		{
		}
		set_global_start_values();
		atomic_inc(&sync_var);
		set_cpu_start_values(this_cpu);
		setup_leader_wakeup(this_cpu);
		atomic_inc(&sync_var);
	}
	else
	{
		while (atomic_read(&sync_var) < cpus_present + 1)
		{
		}
		set_cpu_start_values(this_cpu);

		setup_wakeup(this_cpu);

		while (atomic_read(&sync_var) < cpus_present + 2)
		{
		}
	}
}

static void per_cpu_func(void *info)
{
	unsigned long irq_flags;
	int this_cpu = get_cpu();
	local_irq_save(irq_flags);
	disable_percpu_interrupts();

	sync(this_cpu);
	do_system_specific_sleep(this_cpu);

	enable_percpu_interrupts();
	local_irq_restore(irq_flags);
	put_cpu();
}

static void commit_results(unsigned number)
{
	pkg_stats.energy_consumption[number] = energy_consumption;

	for (unsigned i = 0; i < cpus_present; ++i)
	{
		cpu_stats[i].wakeup_time[number] = per_cpu(wakeup_time, i);
		cpu_stats[i].wakeups[number] = per_cpu(wakeups, i);
	}

	commit_system_specific_results(number);
}

static void measure(unsigned number)
{
	do
	{
		redo_measurement = 0;
		for (unsigned i = 0; i < cpus_present; ++i)
		{
			per_cpu(wakeups, i) = 0;
			per_cpu(trigger, i) = 1;
		}
		atomic_set(&sync_var, 0);
		prepare_before_each_measurement();

		on_each_cpu(per_cpu_func, NULL, 1);

		evaluate_global();
		for (unsigned i = 0; i < cpus_present; ++i)
			evaluate_cpu(i);

		cleanup_after_each_measurement();
	} while (redo_measurement);

	commit_results(number);
}

static bool should_sleep(int cpu)
{
	if (strcmp(cpu_selection, "cpu_nr") == 0)
	{
		return cpu < cpus_sleep;
	}

	return (cpu < cpus_present / 2
		    ? 2 * cpu
		    : (cpu - (cpus_present / 2)) * 2 + 1) < cpus_sleep;
}

static int mwait_init(void)
{
	preliminary_checks();
	if (prepare_measurement())
		return 1;

	measurement_count = measurement_count < MAX_NUMBER_OF_MEASUREMENTS
				? measurement_count
				: MAX_NUMBER_OF_MEASUREMENTS;

	cpus_present = num_present_cpus();
	if (cpus_sleep == -1)
		cpus_sleep = cpus_present;

	for (unsigned i = 0; i < cpus_present; ++i)
		per_cpu(cpu_entry_mechanism, i) = should_sleep(i) ? requested_entry_mechanism : ENTRY_MECHANISM_POLL;

	for (unsigned i = 0; i < measurement_count; ++i)
	{
		measure(i);
	}

	cleanup_after_measurements_done();
	publish_results_to_sysfs();

	return 0;
}

static void mwait_exit(void)
{
	cleanup_sysfs();
}

module_init(mwait_init);
module_exit(mwait_exit);
