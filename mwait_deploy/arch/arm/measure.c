#include "measure.h"
#include "sysfs.h"

#include <linux/percpu.h>
#include <linux/interrupt.h>

DECLARE_PER_CPU(int, trigger);
DECLARE_PER_CPU(int, wakeups);

static bool wakeup_handler(void)
{
	int this_cpu = smp_processor_id();

	printk(KERN_ERR "CPU %i woke up\n", this_cpu);

	if (!this_cpu)
	{
		leader_callback();
	}

	all_cpus_callback(this_cpu);

	return 0;
}

void wakeup_other_cpus(void)
{
}

void set_global_start_values(void)
{
}

void set_cpu_start_values(int this_cpu)
{
}

void setup_wakeup(void)
{
}

void set_global_final_values(void)
{
}

void set_cpu_final_values(int this_cpu)
{
}

void do_system_specific_sleep(int this_cpu)
{
	do
	{
		printk("%i: Before WFI\n", this_cpu);
		asm volatile("wfi;");
		printk("%i: After WFI\n", this_cpu);
	} while (wakeup_handler());
}

void evaluate_global(void)
{
}

void evaluate_cpu(int this_cpu)
{
}

void prepare_before_each_measurement(void)
{
}

void cleanup_after_each_measurement(void)
{
}

inline void commit_system_specific_results(unsigned number)
{
	for (unsigned i = 0; i < cpus_present; ++i)
	{
	}
}

void preliminary_checks(void)
{
}

void disable_percpu_interrupts(void)
{
	for (int i = 0; i <= 32; ++i)
	{
		irq_set_status_flags(i, IRQ_DISABLE_UNLAZY);
		disable_percpu_irq(i);
	}
}

void enable_percpu_interrupts(void)
{
	for (int i = 0; i <= 32; ++i)
	{
		enable_percpu_irq(i, 0);
		irq_clear_status_flags(i, IRQ_DISABLE_UNLAZY);
	}
}

int prepare_measurement(void)
{
	for (int i = 0; i <= 1000; ++i)
	{
		irq_set_status_flags(i, IRQ_DISABLE_UNLAZY);
		disable_irq(i);
	}

	return 0;
}

void cleanup_after_measurements_done(void)
{
	for (int i = 0; i <= 1000; ++i)
	{
		enable_irq(i);
		irq_clear_status_flags(i, IRQ_DISABLE_UNLAZY);
	}
}
