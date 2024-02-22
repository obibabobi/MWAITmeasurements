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

#define APIC_LVT_TIMER_MODE_MASK (0x3 << 17)

static char *entry_mechanism = "MWAIT";
module_param(entry_mechanism, charp, 0);
MODULE_PARM_DESC(entry_mechanism, "The mechanism used to enter the C-State. Supported are 'MWAIT', 'IOPORT' and 'POLL'. Default is 'MWAIT'.");
static char *mwait_hint = NULL;
module_param(mwait_hint, charp, 0);
MODULE_PARM_DESC(mwait_hint, "If entry_mechanism is 'MWAIT', this is the hint that mwait will use. If this is given, target_cstate and target_subcstate are ignored.");
static int target_cstate = 1;
module_param(target_cstate, int, 0);
MODULE_PARM_DESC(target_cstate, "If entry_mechanism is 'MWAIT', and mwait_hint is not given, the mwait hint to request this C-state is calculated automatically. Default is 1.");
static int target_subcstate = 0;
module_param(target_subcstate, int, 0);
MODULE_PARM_DESC(target_subcstate, "If entry_mechanism is 'MWAIT', and mwait_hint is not given, the mwait hint to request this sub-C-state is calculated automatically. Default is 0.");
static char *io_port = NULL;
module_param(io_port, charp, 0);
MODULE_PARM_DESC(io_port, "If entry_mechanism is 'IOPORT', this needs to contain the io port address that has to be read.");

DECLARE_PER_CPU(int, trigger);
DECLARE_PER_CPU(int, wakeups);

static int dummy;
static u32 calculated_mwait_hint;
static u16 calculated_io_port;
static u32 rapl_unit;
static int apic_id_of_cpu0;
static int hpet_pin;
static u32 hpet_period;
static u32 cpu_model;
static u32 cpu_family;
static int first;
static bool end_of_measurement;

unsigned vendor;

static u32 msr_rapl_power_unit;
static u32 msr_pkg_energy_status;

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

		end_of_measurement = 1;

		leader_callback();
	}

	all_cpus_callback(this_cpu);

	if (!end_of_measurement)
	{
		printk(KERN_ERR "CPU %i was unexpectedly interrupted during measurement.\n", this_cpu);
		redo_measurement = 1;
	}

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
	read_msr(msr_pkg_energy_status, &original_value);
	original_value &= TOTAL_ENERGY_CONSUMED_MASK;
	do
	{
		read_msr(msr_pkg_energy_status, &start_rapl);
		start_rapl &= TOTAL_ENERGY_CONSUMED_MASK;
	} while (original_value == start_rapl);
}

void set_global_start_values(void)
{
	wait_for_rapl_update();
	start_time = local_clock();
	start_tsc = rdtsc();

	if (vendor == X86_VENDOR_INTEL)
	{
		read_msr(MSR_PKG_C2_RESIDENCY, &start_pkg_c2);
		read_msr(MSR_PKG_C3_RESIDENCY, &start_pkg_c3);
		read_msr(MSR_PKG_C6_RESIDENCY, &start_pkg_c6);
		read_msr(MSR_PKG_C7_RESIDENCY, &start_pkg_c7);
	}
}

void set_cpu_start_values(int this_cpu)
{
	if (vendor == X86_VENDOR_INTEL)
	{
		read_msr(IA32_FIXED_CTR2, &per_cpu(start_unhalted, this_cpu));
		read_msr(MSR_CORE_C3_RESIDENCY, &per_cpu(start_c3, this_cpu));
		read_msr(MSR_CORE_C6_RESIDENCY, &per_cpu(start_c6, this_cpu));
		read_msr(MSR_CORE_C7_RESIDENCY, &per_cpu(start_c7, this_cpu));
	}
}

void setup_leader_wakeup(int this_cpu)
{
	hpet_comparator = setup_hpet_for_measurement(duration, hpet_pin);
}

void setup_wakeup(int this_cpu)
{
}

void set_global_final_values(void)
{
	read_msr(msr_pkg_energy_status, &final_rapl);
	final_time = local_clock();
	final_tsc = rdtsc();

	if (vendor == X86_VENDOR_INTEL)
	{
		read_msr(MSR_PKG_C2_RESIDENCY, &final_pkg_c2);
		read_msr(MSR_PKG_C3_RESIDENCY, &final_pkg_c3);
		read_msr(MSR_PKG_C6_RESIDENCY, &final_pkg_c6);
		read_msr(MSR_PKG_C7_RESIDENCY, &final_pkg_c7);
	}
}

void set_cpu_final_values(int this_cpu)
{
	if (vendor == X86_VENDOR_INTEL)
	{
		read_msr(IA32_FIXED_CTR2, &per_cpu(final_unhalted, this_cpu));
		read_msr(MSR_CORE_C3_RESIDENCY, &per_cpu(final_c3, this_cpu));
		read_msr(MSR_CORE_C6_RESIDENCY, &per_cpu(final_c6, this_cpu));
		read_msr(MSR_CORE_C7_RESIDENCY, &per_cpu(final_c7, this_cpu));
	}
}

void do_system_specific_sleep(int this_cpu)
{
	// handle POLL entry mechanism separately to minimize fluctuation
	if (per_cpu(cpu_entry_mechanism, this_cpu) == ENTRY_MECHANISM_POLL)
	{
		while (per_cpu(trigger, this_cpu))
		{
		}
		return;
	}

	while (per_cpu(trigger, this_cpu))
	{
		switch (per_cpu(cpu_entry_mechanism, this_cpu))
		{
		case ENTRY_MECHANISM_MWAIT:
			asm volatile("monitor;" ::"a"(&dummy), "c"(0), "d"(0));
			asm volatile("mwait;" ::"a"(calculated_mwait_hint), "c"(0));
			break;

		case ENTRY_MECHANISM_IOPORT:
			inb(calculated_io_port);
			break;

		case ENTRY_MECHANISM_POLL:
		case ENTRY_MECHANISM_UNKNOWN:
			break;
		}

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
	if (final_time < duration * 1000000)
	{
		printk(KERN_ERR "Measurement lasted only %llu ns.\n", final_time);
		redo_measurement = 1;
	}
	final_tsc -= start_tsc;

	if (vendor == X86_VENDOR_INTEL)
	{
		final_pkg_c2 -= start_pkg_c2;
		final_pkg_c3 -= start_pkg_c3;
		final_pkg_c6 -= start_pkg_c6;
		final_pkg_c7 -= start_pkg_c7;
	}

	if (redo_measurement)
	{
		printk(KERN_ERR "Redoing Measurement!\n");
	}
}

void evaluate_cpu(int this_cpu)
{
	if (!this_cpu)
	{
		per_cpu(wakeup_time, this_cpu) = ((hpet_counter - hpet_comparator) * hpet_period) / 1000000;
	}
	else
	{
		per_cpu(wakeup_time, this_cpu) = 0;
	}

	if (vendor == X86_VENDOR_INTEL)
	{
		per_cpu(final_unhalted, this_cpu) -= per_cpu(start_unhalted, this_cpu);
		per_cpu(final_c3, this_cpu) -= per_cpu(start_c3, this_cpu);
		per_cpu(final_c6, this_cpu) -= per_cpu(start_c6, this_cpu);
		per_cpu(final_c7, this_cpu) -= per_cpu(start_c7, this_cpu);
	}
}

void prepare_before_each_measurement(void)
{
	end_of_measurement = 0;
	first = 0;
}

void cleanup_after_each_measurement(void)
{
	restore_hpet_after_measurement();
}

inline void commit_system_specific_results(unsigned number)
{
	pkg_stats.attributes.total_tsc[number] = final_tsc;
	if (vendor == X86_VENDOR_INTEL)
	{
		pkg_stats.attributes.c2[number] = final_pkg_c2;
		pkg_stats.attributes.c3[number] = final_pkg_c3;
		pkg_stats.attributes.c6[number] = final_pkg_c6;
		pkg_stats.attributes.c7[number] = final_pkg_c7;
	}

	for (unsigned i = 0; i < cpus_present; ++i)
	{
		if (vendor == X86_VENDOR_INTEL)
		{
			cpu_stats[i].attributes.unhalted[number] = per_cpu(final_unhalted, i);
			cpu_stats[i].attributes.c3[number] = per_cpu(final_c3, i);
			cpu_stats[i].attributes.c6[number] = per_cpu(final_c6, i);
			cpu_stats[i].attributes.c7[number] = per_cpu(final_c7, i);
		}
	}
}

static void per_cpu_init(void *info)
{
	if (vendor == X86_VENDOR_INTEL)
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

	a = 0x0;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (b == 0x756E6547 && d == 0x49656E69 && c == 0x6C65746E) /* GenuineIntel in ASCII */
	{
		vendor = X86_VENDOR_INTEL;
		printk(KERN_ERR "INTEL CPU\n");
	}
	else if (b == 0x68747541 && d == 0x69746e65 && c == 0x444d4163) /* AuthenticAMD in ASCII */
	{
		vendor = X86_VENDOR_AMD;
		printk(KERN_ERR "AMD CPU\n");
	}
	else
	{
		vendor = X86_VENDOR_UNKNOWN;
		printk(KERN_ERR "VENDOR UNKNOWN\n");
	}
}

// Get the unit of the PKG_ENERGY_STATUS MSR in 0.1 microJoule
static inline u32 get_rapl_unit(void)
{
	u64 val;
	read_msr(msr_rapl_power_unit, &val);
	val = (val >> 8) & 0b11111;
	return 10000000 / (1 << val);
}

void disable_percpu_interrupts(void)
{
	u32 value;

	value = apic_read(APIC_LVTT);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVTT, value);

	value = apic_read(APIC_LVTTHMR);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVTTHMR, value);

	value = apic_read(APIC_LVTPC);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVTPC, value);

	value = apic_read(APIC_LVT0);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVT0, value);

	value = apic_read(APIC_LVT1);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVT1, value);

	value = apic_read(APIC_LVTERR);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVTERR, value);

	value = apic_read(APIC_LVTCMCI);
	value |= APIC_LVT_MASKED;
	apic_write(APIC_LVTCMCI, value);
}

void enable_percpu_interrupts(void)
{
	u32 value;

	value = apic_read(APIC_LVTCMCI);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVTCMCI, value);

	value = apic_read(APIC_LVTERR);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVTERR, value);

	value = apic_read(APIC_LVT1);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVT1, value);

	value = apic_read(APIC_LVT0);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVT0, value);

	value = apic_read(APIC_LVTPC);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVTPC, value);

	value = apic_read(APIC_LVTTHMR);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVTTHMR, value);

	value = apic_read(APIC_LVTT);
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVTT, value);

	// Restart the timer, if it is in oneshot mode
	// Necessary, so the system continues working properly
	if (!(value & APIC_LVT_TIMER_MODE_MASK))
		apic_write(APIC_TMICT, 1);
}

inline enum entry_mechanism get_signal_low_mechanism(void)
{
	return ENTRY_MECHANISM_MWAIT;
}

int prepare(void)
{
	register_nmi_handler(NMI_UNKNOWN, measurement_callback, NMI_FLAG_FIRST, "measurement_callback");

	apic_id_of_cpu0 = default_cpu_present_to_apicid(0);

	hpet_period = get_hpet_period();
	hpet_pin = select_hpet_pin();
	if (hpet_pin == -1)
	{
		printk(KERN_ERR "No suitable pin found for HPET, aborting!\n");
		return 1;
	}
	printk(KERN_INFO "Using IOAPIC pin %i for HPET.\n", hpet_pin);

	setup_ioapic_for_measurement(apic_id_of_cpu0, hpet_pin);

	return 0;
}

int prepare_measurements(void)
{
	on_each_cpu(per_cpu_init, NULL, 1);

	printk(KERN_INFO "Using C-State entry mechanism '%s'.", entry_mechanism);
	if (strcmp(entry_mechanism, "POLL") == 0)
	{
		requested_entry_mechanism = ENTRY_MECHANISM_POLL;
	}
	else if (strcmp(entry_mechanism, "MWAIT") == 0)
	{
		requested_entry_mechanism = ENTRY_MECHANISM_MWAIT;
		if (mwait_hint == NULL)
		{
			calculated_mwait_hint = 0x0;
			calculated_mwait_hint += target_subcstate & MWAIT_SUBSTATE_MASK;
			calculated_mwait_hint += (get_cstate_hint() & MWAIT_CSTATE_MASK) << MWAIT_SUBSTATE_SIZE;
		}
		else if (kstrtou32(mwait_hint, 0, &calculated_mwait_hint))
		{
			calculated_mwait_hint = 0x0;
			printk(KERN_WARNING "Interpreting mwait_hint failed, falling back to hint 0x0!\n");
		}

		printk(KERN_INFO "Using MWAIT hint 0x%x.", calculated_mwait_hint);
	}
	else if (strcmp(entry_mechanism, "IOPORT") == 0)
	{
		requested_entry_mechanism = ENTRY_MECHANISM_IOPORT;

		if (!io_port)
		{

			calculated_io_port = 0x0;
			printk(KERN_ERR "No IO port address given in io_port, despite using 'IOPORT' entry mechanism. Aborting!\n");
			return 1;
		}

		if (kstrtou16(io_port, 0, &calculated_io_port))
		{
			calculated_io_port = 0x0;
			printk(KERN_ERR "Interpreting io_port failed, aborting!\n");
			return 1;
		}
		printk(KERN_INFO "Using IO port 0x%x.\n", calculated_io_port);
	}
	else
	{
		requested_entry_mechanism = ENTRY_MECHANISM_UNKNOWN;
		printk(KERN_ERR "C-State entry mechanism '%s' unknown, aborting!\n", entry_mechanism);
		return 1;
	}

	if (vendor == X86_VENDOR_AMD)
	{
		msr_rapl_power_unit = MSR_AMD_RAPL_POWER_UNIT;
		msr_pkg_energy_status = MSR_AMD_PKG_ENERGY_STATUS;
	}
	else
	{
		msr_rapl_power_unit = MSR_RAPL_POWER_UNIT;
		msr_pkg_energy_status = MSR_PKG_ENERGY_STATUS;
	}

	rapl_unit = get_rapl_unit();
	printk(KERN_INFO "RAPL Unit in 0.1 microJoule: %u\n", rapl_unit);

	return 0;
}

void cleanup_measurements(void)
{
}

void cleanup(void)
{
	restore_ioapic_after_measurement();
	unregister_nmi_handler(NMI_UNKNOWN, "measurement_callback");
}
