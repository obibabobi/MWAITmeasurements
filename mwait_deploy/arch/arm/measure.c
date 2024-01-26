#include "measure.h"
#include "sysfs.h"

#include <linux/percpu.h>
#include <linux/interrupt.h>

DECLARE_PER_CPU(int, trigger);
DECLARE_PER_CPU(int, wakeups);

DEFINE_PER_CPU(u64, start_sc);
DEFINE_PER_CPU(u64, end_sc);
DEFINE_PER_CPU(u64, final_sc);

u32 sc_frequency;

static inline bool wakeup_handler(void)
{
	u64 ctl, sc;
	s32 tval;
	int this_cpu;

	sc = read_sysreg(CNTPCT_EL0);

	this_cpu = smp_processor_id();

	tval = read_sysreg(CNTP_TVAL_EL0);
	ctl = read_sysreg(CNTP_CTL_EL0);

	if (sc < per_cpu(end_sc, this_cpu))
	{
		per_cpu(wakeups, this_cpu) += 1;

		return 1;
	}

	printk("CPU %i: CNTP_TVAL_EL0: %i, CNTP_CTL_EL0: %llu\n", this_cpu, tval, ctl);

	if (!this_cpu)
	{
		leader_callback();
	}

	per_cpu(final_sc, this_cpu) = sc;

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

void setup_wakeup(int this_cpu)
{
	s32 ticks = (sc_frequency / 1000) * measurement_duration;
	per_cpu(start_sc, this_cpu) = read_sysreg(CNTPCT_EL0);
	per_cpu(end_sc, this_cpu) = per_cpu(start_sc, this_cpu) + ticks;
	write_sysreg(per_cpu(end_sc, this_cpu), CNTP_CVAL_EL0);
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
	per_cpu(wakeup_time, this_cpu) = (per_cpu(final_sc, this_cpu) - per_cpu(end_sc, this_cpu)) * 1000000000 / sc_frequency;
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
}

int prepare_measurement(void)
{
	sc_frequency = read_sysreg(CNTFRQ_EL0) & 0xffffffff;

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