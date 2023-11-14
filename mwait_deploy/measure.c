#include "measure.h"
#include "consts.h"

#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

int measurement_duration = 100;
module_param(measurement_duration, int, 0);
MODULE_PARM_DESC(measurement_duration, "Duration of each measurement in milliseconds.");
int measurement_count = 10;
module_param(measurement_count, int, 0);
MODULE_PARM_DESC(measurement_count, "How many measurements should be done.");
static int cpus_mwait = -1;
module_param(cpus_mwait, int, 0);
MODULE_PARM_DESC(cpus_mwait, "Number of CPUs that should do mwait instead of a busy loop during the measurement.");
static char *cpu_selection = "core";
module_param(cpu_selection, charp, 0);
MODULE_PARM_DESC(cpu_selection, "How the cpus doing mwait should be selected. Supported are 'core' and 'cpu_nr'.");

DEFINE_PER_CPU(u64, wakeups);
DEFINE_PER_CPU(int, trigger);
static atomic_t sync_var;
bool redo_measurement;
static bool end_of_measurement;
unsigned cpus_present;

void leader_callback(void)
{
	end_of_measurement = 1;
	wakeup_other_cpus();

	set_global_final_values();
}

void all_cpus_callback(int this_cpu)
{
	set_cpu_final_values(this_cpu);

	if (!end_of_measurement)
	{
		printk(KERN_ERR "CPU %i was unexpectedly interrupted during measurement.\n", this_cpu);
		redo_measurement = 1;
	}

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
		setup_wakeup();
		atomic_inc(&sync_var);
	}
	else
	{
		while (atomic_read(&sync_var) < cpus_present + 1)
		{
		}
		set_cpu_start_values(this_cpu);
		while (atomic_read(&sync_var) < cpus_present + 2)
		{
		}
	}
}

static void do_idle_loop(int this_cpu)
{
	while (per_cpu(trigger, this_cpu))
	{
	}
}

static bool should_do_mwait(int this_cpu)
{
	if (strcmp(cpu_selection, "cpu_nr") == 0)
	{
		return this_cpu < cpus_mwait;
	}

	return (this_cpu < cpus_present / 2
		    ? 2 * this_cpu
		    : (this_cpu - (cpus_present / 2)) * 2 + 1) < cpus_mwait;
}

static void per_cpu_func(void *info)
{
	int this_cpu = get_cpu();
	local_irq_disable();

	per_cpu(trigger, this_cpu) = 1;

	sync(this_cpu);

	if (should_do_mwait(this_cpu))
	{
		do_system_specific_sleep(this_cpu);
	}
	else
	{
		do_idle_loop(this_cpu);
	}

	local_irq_enable();
	put_cpu();
}

static void measure(unsigned number)
{
	do
	{
		redo_measurement = 0;
		end_of_measurement = 0;
		prepare_before_each_measurement();
		for (unsigned i = 0; i < cpus_present; ++i)
			per_cpu(wakeups, i) = 0;
		atomic_set(&sync_var, 0);

		on_each_cpu(per_cpu_func, NULL, 1);

		evaluate_global();
		for (unsigned i = 0; i < cpus_present; ++i)
			evaluate_cpu(i);

		cleanup_after_each_measurement();
	} while (redo_measurement);

	commit_results(number);
}

static int mwait_init(void)
{
	preliminary_checks();
	prepare_measurement();

	measurement_count = measurement_count < MAX_NUMBER_OF_MEASUREMENTS
				? measurement_count
				: MAX_NUMBER_OF_MEASUREMENTS;

	cpus_present = num_present_cpus();
	if (cpus_mwait == -1)
		cpus_mwait = cpus_present;

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