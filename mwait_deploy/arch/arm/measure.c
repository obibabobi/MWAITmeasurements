#include "measure.h"
#include "sysfs.h"

#include <linux/percpu.h>
#include <linux/interrupt.h>

DECLARE_PER_CPU(int, trigger);
DECLARE_PER_CPU(int, wakeups);

DEFINE_PER_CPU(u64, start_time);

static inline bool wakeup_handler(void)
{
	u64 time, ctl;
	s32 tval;

	int this_cpu = smp_processor_id();

	time = read_sysreg(CNTPCT_EL0);
	tval = read_sysreg(CNTP_TVAL_EL0);
	ctl = read_sysreg(CNTP_CTL_EL0);

	if (!this_cpu)
	{
		leader_callback();
	}

	all_cpus_callback(this_cpu);

	printk("CPU %i: CNTP_TVAL_EL0: %i, CNTP_CTL_EL0: %llu\n", this_cpu, tval, ctl);

	if (time < per_cpu(start_time, this_cpu) + 100000000)
	{
		printk(KERN_ERR "%i: Premature Wakeup\n", this_cpu);

		return 1;
	}
	else
	{
		return 0;
	}
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

void setup_wakeup(int this_cpu)
{
	per_cpu(start_time, this_cpu) = read_sysreg(CNTPCT_EL0);
	write_sysreg(100000000, CNTP_TVAL_EL0);
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
		asm volatile("wfi;");
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
	local_irq_disable();

	for (int i = 0; i <= 32; ++i)
	{
		if (i != 13)
		{
			disable_percpu_irq(i);
		}
	}
}

void enable_percpu_interrupts(void)
{
	for (int i = 0; i <= 32; ++i)
	{
		if (i != 13)
		{
			enable_percpu_irq(i, 0);
		}
	}

	local_irq_enable();
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
