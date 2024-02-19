#include "measure.h"
#include "sysfs.h"

#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

static char *mode = "measure";
module_param(mode, charp, 0);
MODULE_PARM_DESC(mode, "The mode the module will operate in. Supported are 'measure' and 'signal', default is 'measure'.\n"
		       "In 'measure' mode, the usual measurements will be taken and published to the sysfs.\n"
		       "In 'signal' mode, a signature will be generated in the power consumption of the device "
		       "and only the timestamps of this signature will be published.");

int duration = 100;
module_param(duration, int, 0);
MODULE_PARM_DESC(duration, "In 'measure' mode, the duration of each measurement.\n"
			   "In 'signal' mode, how long the signal should stay at each level.\n"
			   "Unit is milliseconds. Default is 100.");

int measurement_count = 10;
module_param(measurement_count, int, 0);
MODULE_PARM_DESC(measurement_count, "How many measurements should be done. Default is 10.");
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
enum mode operation_mode;
enum entry_mechanism requested_entry_mechanism;
DEFINE_PER_CPU(enum entry_mechanism, cpu_entry_mechanism);

static atomic_t sync_var;

void leader_callback(void)
{
	wakeup_other_cpus();

	if (operation_mode == MODE_MEASURE)
		set_global_final_values();
}

void all_cpus_callback(int this_cpu)
{
	if (operation_mode == MODE_MEASURE)
		set_cpu_final_values(this_cpu);

	per_cpu(trigger, this_cpu) = 0;
}

static inline void sync(int this_cpu)
{
	atomic_inc(&sync_var);

	while (atomic_read(&sync_var) < cpus_present)
	{
	}

	if (!this_cpu)
	{
		if (operation_mode == MODE_MEASURE)
		{
			set_global_start_values();
			set_cpu_start_values(this_cpu);
		}
		setup_leader_wakeup(this_cpu);
	}
	else
	{
		if (operation_mode == MODE_MEASURE)
		{
			set_cpu_start_values(this_cpu);
		}
		setup_wakeup(this_cpu);
	}
}

static DEFINE_PER_CPU(unsigned long, irq_flags);

static int seize_core(void)
{
	int this_cpu = get_cpu();
	local_irq_save(per_cpu(irq_flags, this_cpu));
	disable_percpu_interrupts();

	return this_cpu;
}

static void release_core(int this_cpu)
{
	enable_percpu_interrupts();
	local_irq_restore(per_cpu(irq_flags, this_cpu));
	put_cpu();
}

static void per_cpu_measure(void *info)
{
	int this_cpu = seize_core();

	sync(this_cpu);
	do_system_specific_sleep(this_cpu);

	release_core(this_cpu);
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

		on_each_cpu(per_cpu_measure, NULL, 1);

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

static int measurement_init(void)
{
	if (prepare_measurements())
		return 1;

	measurement_count = measurement_count < MAX_NUMBER_OF_MEASUREMENTS
				? measurement_count
				: MAX_NUMBER_OF_MEASUREMENTS;

	if (cpus_sleep == -1)
		cpus_sleep = cpus_present;

	for (unsigned i = 0; i < cpus_present; ++i)
		per_cpu(cpu_entry_mechanism, i) = should_sleep(i) ? requested_entry_mechanism : ENTRY_MECHANISM_POLL;

	for (unsigned i = 0; i < measurement_count; ++i)
	{
		measure(i);
	}

	cleanup_measurements();
	publish_results_to_sysfs();

	return 0;
}

static void per_cpu_signal(void *info)
{
	int this_cpu = seize_core();

	per_cpu(cpu_entry_mechanism, this_cpu) = ENTRY_MECHANISM_POLL;

	sync(this_cpu);
	do_system_specific_sleep(this_cpu);

	per_cpu(cpu_entry_mechanism, this_cpu) = get_signal_low_mechanism();

	sync(this_cpu);
	do_system_specific_sleep(this_cpu);

	per_cpu(cpu_entry_mechanism, this_cpu) = ENTRY_MECHANISM_POLL;

	sync(this_cpu);
	do_system_specific_sleep(this_cpu);

	release_core(this_cpu);
}

static void signal_init(void)
{
	on_each_cpu(per_cpu_signal, NULL, 1);
}

static int mwait_init(void)
{
	preliminary_checks();
	prepare();

	cpus_present = num_present_cpus();

	if (strcmp(mode, "measure") == 0)
	{
		operation_mode = MODE_MEASURE;
		if (measurement_init())
			return 1;
	}
	else if (strcmp(mode, "signal") == 0)
	{
		operation_mode = MODE_SIGNAL;
		signal_init();
	}
	else
	{
		operation_mode = MODE_UNKNOWN;
		printk(KERN_ERR "Mode '%s' unknown, aborting!\n", mode);
		return 1;
	}

	cleanup();

	return 0;
}

static void mwait_exit(void)
{
	cleanup_sysfs();
}

module_init(mwait_init);
module_exit(mwait_exit);
