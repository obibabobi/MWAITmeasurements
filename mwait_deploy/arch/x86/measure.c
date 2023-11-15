#include "measure.h"
#include "sysfs.h"

#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <asm/mwait.h>
#include <asm/hpet.h>
#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/msr-index.h>
#include <asm/io_apic.h>

static char *mwait_hint = NULL;
module_param(mwait_hint, charp, 0);
MODULE_PARM_DESC(mwait_hint, "The hint mwait should use. If this is given, target_cstate and target_subcstate are ignored.");
static int target_cstate = 1;
module_param(target_cstate, int, 0);
MODULE_PARM_DESC(target_cstate, "The C-State that gets passed to mwait as a hint.");
static int target_subcstate = 0;
module_param(target_subcstate, int, 0);
MODULE_PARM_DESC(target_subcstate, "The sub C-State that gets passed to mwait as a hint.");

DECLARE_PER_CPU(int, trigger);
DECLARE_PER_CPU(int, wakeups);

static int dummy;
static u32 calculated_mwait_hint;
static u32 rapl_unit;
static int apic_id_of_cpu0;
static int hpet_pin;
static u32 hpet_period;
static u32 cpu_model;
static u32 cpu_family;
static int first;

DEFINE_PER_CPU(u64, start_unhalted);
DEFINE_PER_CPU(u64, final_unhalted);
DEFINE_PER_CPU(u64, start_c3);
DEFINE_PER_CPU(u64, final_c3);
DEFINE_PER_CPU(u64, start_c6);
DEFINE_PER_CPU(u64, final_c6);
DEFINE_PER_CPU(u64, start_c7);
DEFINE_PER_CPU(u64, final_c7);
static u64 start_time, final_time;
static u64 start_rapl, final_rapl;
static u64 start_tsc, final_tsc;
static u64 start_pkg_c2, final_pkg_c2;
static u64 start_pkg_c3, final_pkg_c3;
static u64 start_pkg_c6, final_pkg_c6;
static u64 start_pkg_c7, final_pkg_c7;
static u64 hpet_comparator, hpet_counter;

static inline bool is_cpu_model(u32 family, u32 model)
{
	return cpu_family == family && cpu_model == model;
}

static int measurement_callback(unsigned int val, struct pt_regs *regs)
{
	// this measurement is taken here to get the value as early as possible
	u64 hpet_counter_local = get_hpet_counter();

	int this_cpu = smp_processor_id();

	if (!this_cpu)
	{
		if (!first && is_cpu_model(0x6, 0x5e))
		{
			++first;
			return NMI_HANDLED;
		}

		// only commit the taken time to the global variable if this point is reached
		hpet_counter = hpet_counter_local;

		leader_callback();
	}

	all_cpus_callback(this_cpu);

	// Without some delay here, CPUs tend to get stuck on rare occasions
	// I don't know yet why exactly this happens, so this udelay should be seen as a (hopefully temporary) workaround
	udelay(1);

	return NMI_HANDLED;
}

void wakeup_other_cpus(void)
{
	apic->send_IPI_allbutself(NMI_VECTOR);
}

#define read_msr(msr, p)                           \
	({                                         \
		if (unlikely(rdmsrl_safe(msr, p))) \
			rdmsr_error(#msr, msr);    \
	})

static void rdmsr_error(char *reg, unsigned reg_nr)
{
	printk(KERN_ERR "WARNING: Failed to read register %s (%u).\n", reg, reg_nr);
}

void wait_for_rapl_update(void)
{
	u64 original_value;
	read_msr(MSR_PKG_ENERGY_STATUS, &original_value);
	original_value &= TOTAL_ENERGY_CONSUMED_MASK;
	do
	{
		read_msr(MSR_PKG_ENERGY_STATUS, &start_rapl);
		start_rapl &= TOTAL_ENERGY_CONSUMED_MASK;
	} while (original_value == start_rapl);
}

void set_global_start_values(void)
{
	wait_for_rapl_update();
	start_time = local_clock();
	start_tsc = rdtsc();
	read_msr(MSR_PKG_C2_RESIDENCY, &start_pkg_c2);
	read_msr(MSR_PKG_C3_RESIDENCY, &start_pkg_c3);
	read_msr(MSR_PKG_C6_RESIDENCY, &start_pkg_c6);
	read_msr(MSR_PKG_C7_RESIDENCY, &start_pkg_c7);
}

void set_cpu_start_values(int this_cpu)
{
	read_msr(IA32_FIXED_CTR2, &per_cpu(start_unhalted, this_cpu));
	read_msr(MSR_CORE_C3_RESIDENCY, &per_cpu(start_c3, this_cpu));
	read_msr(MSR_CORE_C6_RESIDENCY, &per_cpu(start_c6, this_cpu));
	read_msr(MSR_CORE_C7_RESIDENCY, &per_cpu(start_c7, this_cpu));
}

void setup_wakeup(void)
{
	hpet_comparator = setup_hpet_for_measurement(measurement_duration, hpet_pin);
}

void set_global_final_values(void)
{
	read_msr(MSR_PKG_ENERGY_STATUS, &final_rapl);
	final_time = local_clock();
	final_tsc = rdtsc();
	read_msr(MSR_PKG_C2_RESIDENCY, &final_pkg_c2);
	read_msr(MSR_PKG_C3_RESIDENCY, &final_pkg_c3);
	read_msr(MSR_PKG_C6_RESIDENCY, &final_pkg_c6);
	read_msr(MSR_PKG_C7_RESIDENCY, &final_pkg_c7);
}

void set_cpu_final_values(int this_cpu)
{
	read_msr(IA32_FIXED_CTR2, &per_cpu(final_unhalted, this_cpu));
	read_msr(MSR_CORE_C3_RESIDENCY, &per_cpu(final_c3, this_cpu));
	read_msr(MSR_CORE_C6_RESIDENCY, &per_cpu(final_c6, this_cpu));
	read_msr(MSR_CORE_C7_RESIDENCY, &per_cpu(final_c7, this_cpu));
}

void do_system_specific_sleep(int this_cpu)
{
	while (per_cpu(trigger, this_cpu))
	{
		asm volatile("monitor;" ::"a"(&dummy), "c"(0), "d"(0));
		asm volatile("mwait;" ::"a"(calculated_mwait_hint), "c"(0));
		per_cpu(wakeups, this_cpu) += 1;
	}
}

void evaluate_global(void)
{
	final_rapl &= TOTAL_ENERGY_CONSUMED_MASK;
	if (final_rapl <= start_rapl)
	{
		printk(KERN_ERR "Result would have been %llu.\n", final_rapl - start_rapl);
		redo_measurement = 1;
	}
	energy_consumption = (final_rapl - start_rapl) * rapl_unit;
	final_time -= start_time;
	if (final_time < measurement_duration * 1000000)
	{
		printk(KERN_ERR "Measurement lasted only %llu ns.\n", final_time);
		redo_measurement = 1;
	}
	wakeup_time = ((hpet_counter - hpet_comparator) * hpet_period) / 1000000;
	final_tsc -= start_tsc;
	final_pkg_c2 -= start_pkg_c2;
	final_pkg_c3 -= start_pkg_c3;
	final_pkg_c6 -= start_pkg_c6;
	final_pkg_c7 -= start_pkg_c7;

	if (redo_measurement)
	{
		printk(KERN_ERR "Redoing Measurement!\n");
	}
}

void evaluate_cpu(int this_cpu)
{
	per_cpu(final_unhalted, this_cpu) -= per_cpu(start_unhalted, this_cpu);
	per_cpu(final_c3, this_cpu) -= per_cpu(start_c3, this_cpu);
	per_cpu(final_c6, this_cpu) -= per_cpu(start_c6, this_cpu);
	per_cpu(final_c7, this_cpu) -= per_cpu(start_c7, this_cpu);
}

void prepare_before_each_measurement(void)
{
	first = 0;
}

void cleanup_after_each_measurement(void)
{
	restore_hpet_after_measurement();
}

inline void commit_system_specific_results(unsigned number)
{
	pkg_stats.attributes.total_tsc[number] = final_tsc;
	pkg_stats.attributes.c2[number] = final_pkg_c2;
	pkg_stats.attributes.c3[number] = final_pkg_c3;
	pkg_stats.attributes.c6[number] = final_pkg_c6;
	pkg_stats.attributes.c7[number] = final_pkg_c7;

	for (unsigned i = 0; i < cpus_present; ++i)
	{
		cpu_stats[i].attributes.unhalted[number] = per_cpu(final_unhalted, i);
		cpu_stats[i].attributes.c3[number] = per_cpu(final_c3, i);
		cpu_stats[i].attributes.c6[number] = per_cpu(final_c6, i);
		cpu_stats[i].attributes.c7[number] = per_cpu(final_c7, i);
	}
}

static void per_cpu_init(void *info)
{
	int err = 0;

	u64 ia32_fixed_ctr_ctrl;
	u64 ia32_perf_global_ctrl;

	err = rdmsrl_safe(IA32_FIXED_CTR_CTRL, &ia32_fixed_ctr_ctrl);
	ia32_fixed_ctr_ctrl |= 0b11 << 8;
	err |= wrmsrl_safe(IA32_FIXED_CTR_CTRL, ia32_fixed_ctr_ctrl);

	err |= rdmsrl_safe(IA32_PERF_GLOBAL_CTRL, &ia32_perf_global_ctrl);
	ia32_perf_global_ctrl |= 1l << 34;
	err |= wrmsrl_safe(IA32_PERF_GLOBAL_CTRL, ia32_perf_global_ctrl);

	if (err)
	{
		printk(KERN_ERR "WARNING: Could not enable 'unhalted' register.\n");
	}
}

static inline u32 get_cstate_hint(void)
{
	if (target_cstate == 0)
	{
		return 0xf;
	}

	if (target_cstate > 15)
	{
		printk(KERN_ERR "WARNING: target_cstate of %i is invalid, using C1!", target_cstate);
		return 0;
	}

	return target_cstate - 1;
}

// Model and Family calculation as specified in the Intel Software Developer's Manual
static void set_cpu_info(u32 a)
{
	u32 family_id = (a >> 8) & 0xf;
	u32 model_id = (a >> 4) & 0xf;

	cpu_family = family_id;
	if (family_id == 0xf)
	{
		cpu_family += (a >> 20) & 0xff;
	}

	cpu_model = model_id;
	if (family_id == 0x6 || family_id == 0xf)
	{
		cpu_model += ((a >> 16) & 0xf) << 4;
	}
}

void preliminary_checks(void)
{
	u32 a = 0x1, b, c, d;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (!(c & (1 << 3)))
	{
		printk(KERN_ERR "WARNING: Mwait not supported.\n");
	}
	set_cpu_info(a);
	printk(KERN_INFO "CPU FAMILY: 0x%x, CPU Model: 0x%x\n", cpu_family, cpu_model);

	a = 0x5;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (!(c & 1))
	{
		printk(KERN_ERR "WARNING: Mwait Power Management not supported.\n");
	}

	a = 0x80000007;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (!(d & (1 << 8)))
	{
		printk(KERN_ERR "WARNING: TSC not invariant, sleepstate statistics potentially meaningless.\n");
	}
}

// Get the unit of the PKG_ENERGY_STATUS MSR in 0.1 microJoule
static inline u32 get_rapl_unit(void)
{
	u64 val;
	read_msr(MSR_RAPL_POWER_UNIT, &val);
	val = (val >> 8) & 0b11111;
	return 10000000 / (1 << val);
}

int prepare_measurement(void)
{
	on_each_cpu(per_cpu_init, NULL, 1);
	if (mwait_hint == NULL)
	{
		calculated_mwait_hint = 0;
		calculated_mwait_hint += target_subcstate & MWAIT_SUBSTATE_MASK;
		calculated_mwait_hint += (get_cstate_hint() & MWAIT_CSTATE_MASK) << MWAIT_SUBSTATE_SIZE;
	}
	else
	{
		if (kstrtou32(mwait_hint, 0, &calculated_mwait_hint))
		{
			calculated_mwait_hint = 0;
			printk(KERN_ERR "Interpreting mwait_hint failed, falling back to hint 0x0!\n");
		}
	}
	printk(KERN_INFO "Using MWAIT hint 0x%x.", calculated_mwait_hint);

	rapl_unit = get_rapl_unit();
	printk(KERN_INFO "rapl_unit in 0.1 microJoule: %u\n", rapl_unit);

	register_nmi_handler(NMI_UNKNOWN, measurement_callback, NMI_FLAG_FIRST, "measurement_callback");

	apic_id_of_cpu0 = default_cpu_present_to_apicid(0);

	hpet_period = get_hpet_period();
	hpet_pin = select_hpet_pin();
	if (hpet_pin == -1)
	{
		printk(KERN_ERR "ERROR: No suitable pin found for HPET, aborting!\n");
		return 1;
	}
	printk(KERN_INFO "Using IOAPIC pin %i for HPET.\n", hpet_pin);

	setup_ioapic_for_measurement(apic_id_of_cpu0, hpet_pin);

	return 0;
}

void cleanup_after_measurements_done(void)
{
	restore_ioapic_after_measurement();
	unregister_nmi_handler(NMI_UNKNOWN, "measurement_callback");
}
