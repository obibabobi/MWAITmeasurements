#include "measure.h"
#include "sysfs.h"

#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>

static char *entry_mechanism = "WFI";
module_param(entry_mechanism, charp, 0);
MODULE_PARM_DESC(entry_mechanism, "The mechanism used to enter the idle state. Supported are 'WFI' and 'POLL'. Default is 'WFI'.");

DEFINE_PER_CPU(u64, start_sc);
DEFINE_PER_CPU(u64, end_sc);
DEFINE_PER_CPU(u64, final_sc);

u32 sc_frequency;

static inline bool wakeup_handler(void)
{
	u64 sc;
	int this_cpu;

	sc = read_sysreg(CNTPCT_EL0);

	this_cpu = smp_processor_id();

	per_cpu(wakeups, this_cpu) += 1;

	if (sc < per_cpu(end_sc, this_cpu))
	{
		return true;
	}

	if (is_leader(this_cpu))
	{
		leader_callback();
	}

	per_cpu(final_sc, this_cpu) = sc;

	all_cpus_callback(this_cpu);

	return false;
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

void setup_leader_wakeup(int this_cpu)
{
	setup_wakeup(this_cpu);
}

inline void setup_wakeup(int this_cpu)
{
	s32 ticks = (sc_frequency / 1000) * duration;
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
	// handle POLL separately to make measured workload as simple as possible
	if (per_cpu(cpu_entry_mechanism, this_cpu) == ENTRY_MECHANISM_POLL)
	{
		do
		{
		} while (wakeup_handler());

		return;
	}

	do
	{
		switch (per_cpu(cpu_entry_mechanism, this_cpu))
		{
		case ENTRY_MECHANISM_WFI:
			asm volatile("wfi" ::: "memory");
			break;

		case ENTRY_MECHANISM_POLL:
		case ENTRY_MECHANISM_UNKNOWN:
			break;
		}
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
	for (int i = 0; i <= 31; ++i)
	{
		if (i != 13)
		{
			disable_percpu_irq(i);
		}
	}
}

void enable_percpu_interrupts(void)
{
	for (int i = 0; i <= 31; ++i)
	{
		if (i != 13)
		{
			enable_percpu_irq(i, IRQ_TYPE_NONE);
		}
	}
}

inline enum entry_mechanism get_signal_low_mechanism(void)
{
	return ENTRY_MECHANISM_WFI;
}

int prepare(void)
{
	sc_frequency = read_sysreg(CNTFRQ_EL0) & 0xffffffff;

	for (int i = 0; i <= 1024; ++i)
	{
		irq_set_status_flags(i, IRQ_DISABLE_UNLAZY);
		disable_irq(i);
	}

	return 0;
}

int prepare_measurements(void)
{
	printk(KERN_INFO "Using entry mechanism '%s'.", entry_mechanism);
	if (strcmp(entry_mechanism, "POLL") == 0)
	{
		requested_entry_mechanism = ENTRY_MECHANISM_POLL;
	}
	else if (strcmp(entry_mechanism, "WFI") == 0)
	{
		requested_entry_mechanism = ENTRY_MECHANISM_WFI;
	}
	else
	{
		requested_entry_mechanism = ENTRY_MECHANISM_UNKNOWN;
		printk(KERN_ERR "Entry mechanism '%s' unknown, aborting!\n", entry_mechanism);
		return 1;
	}

	return 0;
}

void cleanup_measurements(void)
{
}

void cleanup(void)
{
	for (int i = 0; i <= 1024; ++i)
	{
		enable_irq(i);
		irq_clear_status_flags(i, IRQ_DISABLE_UNLAZY);
	}
}
