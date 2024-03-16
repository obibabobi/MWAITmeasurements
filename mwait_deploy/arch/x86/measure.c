#include "measure.h"
#include "sysfs.h"

#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/mwait.h>
#include <asm/apic.h>
#include <asm/nmi.h>
#include <asm/msr-index.h>

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
static int deactivate_pcstates = 0;
module_param(deactivate_pcstates, int, 0);
MODULE_PARM_DESC(deactivate_pcstates, "Deactivate Package C-states for the duration of the measurement. Default is '0' (PC-states enabled). '1' deactivates PC-states.");

// make sure that there is enough unused space around measurement_ongoing
// necessary because monitor surveils entire lines of memory
// the size of the monitored line varies from system to system
// to fit all systems, a relatively large padding was selected here, making sure no other variable can be in the same 512 byte line as measurement_ongoing
struct
{
	u64 padding1[63];
	volatile u64 measurement_ongoing;
	u64 padding2[63];
} padding;

static u32 calculated_mwait_hint;
static u16 calculated_io_port;
static u32 rapl_unit;
static u32 cpu_model;
static u32 cpu_family;

unsigned vendor;

static u32 msr_rapl_power_unit;
static u32 msr_pkg_energy_status;

DEFINE_PER_CPU(u64, start_cpu_rapl);
DEFINE_PER_CPU(u64, final_cpu_rapl);
DEFINE_PER_CPU(u64, cpu_energy_consumption);
DEFINE_PER_CPU(u64, start_unhalted);
DEFINE_PER_CPU(u64, final_unhalted);
DEFINE_PER_CPU(u64, start_c3);
DEFINE_PER_CPU(u64, final_c3);
DEFINE_PER_CPU(u64, start_c6);
DEFINE_PER_CPU(u64, final_c6);
DEFINE_PER_CPU(u64, start_c7);
DEFINE_PER_CPU(u64, final_c7);
DEFINE_PER_CPU(u64, wakeup_tsc);
static u64 wakeup_trigger_tsc;
volatile u64 end_tsc;
static u64 start_rapl, final_rapl, energy_consumption;
static u64 start_tsc, final_tsc;
static u64 start_pkg_c2, final_pkg_c2;
static u64 start_pkg_c3, final_pkg_c3;
static u64 start_pkg_c6, final_pkg_c6;
static u64 start_pkg_c7, final_pkg_c7;

static inline bool is_cpu_model(u32 family, u32 model)
{
	return cpu_family == family && cpu_model == model;
}

void wakeup_other_cpus(void)
{
	wakeup_trigger_tsc = rdtsc();
	padding.measurement_ongoing = false;
	if (requested_entry_mechanism == ENTRY_MECHANISM_IOPORT)
	{
		apic->send_IPI_allbutself(NMI_VECTOR);
	}
}

#define read_msr(msr, p)                           \
	({                                         \
		if (unlikely(rdmsrl_safe(msr, p))) \
			rdmsr_error(#msr, msr);    \
	})

static void rdmsr_error(char *reg, unsigned reg_nr)
{
	printk(KERN_WARNING "WARNING: Failed to read register %s (%u).\n", reg, reg_nr);
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
	else if (vendor == X86_VENDOR_AMD)
	{
		read_msr(MSR_AMD_CORE_ENERGY_STATUS, &per_cpu(start_cpu_rapl, this_cpu));
	}
}

#define APIC_LVT_TIMER_MODE_ONESHOT (0x0)
#define APIC_LVT_TIMER_MODE_PERIODIC (1 << 17)
#define APIC_LVT_TIMER_MODE_TSC_DEADLINE (1 << 18)

#define APIC_LVT_VECTOR_MASK (0xff)
#define APIC_LVT_TIMER_MODE_MASK (0x3 << 17)

void setup_leader_wakeup(int this_cpu)
{
	u32 value;
	u64 ticks;

	value = apic_read(APIC_LVTT);
	value &= APIC_LVT_VECTOR_MASK;
	value |= APIC_LVT_TIMER_MODE_TSC_DEADLINE;
	apic_write(APIC_LVTT, value);

	ticks = tsc_khz * duration;
	end_tsc = rdtsc() + ticks;
	if (unlikely(wrmsrl_safe(MSR_IA32_TSC_DEADLINE, end_tsc)))
		printk(KERN_ERR "Error when trying to set up wake-up interrupt!\n");
}

void setup_wakeup(int this_cpu)
{
}

void set_global_final_values(void)
{
	read_msr(msr_pkg_energy_status, &final_rapl);
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
	else if (vendor == X86_VENDOR_AMD)
	{
		read_msr(MSR_AMD_CORE_ENERGY_STATUS, &per_cpu(final_cpu_rapl, this_cpu));
	}
}

void do_system_specific_sleep(int this_cpu)
{
	u32 wake_up_on_interrupt = is_leader(this_cpu) ? 0x1 : 0;

	// handle POLL entry mechanism separately to minimize fluctuation
	if (per_cpu(cpu_entry_mechanism, this_cpu) == ENTRY_MECHANISM_POLL)
	{
		u64 tsc;

		while ((tsc = rdtsc()) < end_tsc)
		{
			per_cpu(wakeups, this_cpu) += 1;
		}

		per_cpu(wakeup_tsc, this_cpu) = tsc;
		if (is_leader(this_cpu))
			leader_callback();
		all_cpus_callback(this_cpu);
		return;
	}

	while (padding.measurement_ongoing)
	{
		switch (per_cpu(cpu_entry_mechanism, this_cpu))
		{
		case ENTRY_MECHANISM_MWAIT:
			asm volatile("monitor;" ::"a"(&padding.measurement_ongoing), "c"(0), "d"(0));

			// could get stuck if write occurs between while and monitor
			if (!padding.measurement_ongoing)
				break;

			asm volatile("mwait;" ::"a"(calculated_mwait_hint), "c"(wake_up_on_interrupt));

			per_cpu(wakeup_tsc, this_cpu) = rdtsc();
			break;

		case ENTRY_MECHANISM_IOPORT:
			inb(calculated_io_port);

			per_cpu(wakeup_tsc, this_cpu) = rdtsc();
			break;

		case ENTRY_MECHANISM_POLL:
		case ENTRY_MECHANISM_UNKNOWN:

			break;
		}

		per_cpu(wakeups, this_cpu) += 1;

		if (is_leader(this_cpu) && rdtsc() >= end_tsc)
		{
			leader_callback();
			break;
		}
	}

	all_cpus_callback(this_cpu);
}

void evaluate_global(void)
{
	final_rapl &= TOTAL_ENERGY_CONSUMED_MASK;

	// handle overflow
	if (final_rapl >= start_rapl)
	{
		final_rapl -= start_rapl;
	}
	else
	{
		final_rapl += (TOTAL_ENERGY_CONSUMED_MASK + 1) - start_rapl;

		printk(KERN_INFO "Overflow in Package RAPL register.\n");
	}

	energy_consumption = final_rapl * rapl_unit;

	final_tsc -= start_tsc;

	if (vendor == X86_VENDOR_INTEL)
	{
		final_pkg_c2 -= start_pkg_c2;
		final_pkg_c3 -= start_pkg_c3;
		final_pkg_c6 -= start_pkg_c6;
		final_pkg_c7 -= start_pkg_c7;
	}
}

void evaluate_cpu(int this_cpu)
{
	if (is_leader(this_cpu))
	{
		per_cpu(wakeup_time, this_cpu) = ((per_cpu(wakeup_tsc, 0) - end_tsc) * 1000000) / tsc_khz;
	}
	else
	{
		if (per_cpu(cpu_entry_mechanism, this_cpu) == ENTRY_MECHANISM_POLL)
		{
			per_cpu(wakeup_time, this_cpu) = ((per_cpu(wakeup_tsc, this_cpu) - end_tsc) * 1000000) / tsc_khz;
		}
		else
		{
			per_cpu(wakeup_time, this_cpu) = ((per_cpu(wakeup_tsc, this_cpu) - wakeup_trigger_tsc) * 1000000) / tsc_khz;
		}
	}

	if (vendor == X86_VENDOR_INTEL)
	{
		per_cpu(final_unhalted, this_cpu) -= per_cpu(start_unhalted, this_cpu);
		per_cpu(final_c3, this_cpu) -= per_cpu(start_c3, this_cpu);
		per_cpu(final_c6, this_cpu) -= per_cpu(start_c6, this_cpu);
		per_cpu(final_c7, this_cpu) -= per_cpu(start_c7, this_cpu);
	}
	else if (vendor == X86_VENDOR_AMD)
	{
		per_cpu(start_cpu_rapl, this_cpu) &= TOTAL_ENERGY_CONSUMED_MASK;
		per_cpu(final_cpu_rapl, this_cpu) &= TOTAL_ENERGY_CONSUMED_MASK;

		// handle overflow
		if (per_cpu(final_cpu_rapl, this_cpu) >= per_cpu(start_cpu_rapl, this_cpu))
		{
			per_cpu(final_cpu_rapl, this_cpu) -= per_cpu(start_cpu_rapl, this_cpu);
		}
		else
		{
			per_cpu(final_cpu_rapl, this_cpu) += (TOTAL_ENERGY_CONSUMED_MASK + 1) - per_cpu(start_cpu_rapl, this_cpu);

			printk(KERN_INFO "Overflow in Core RAPL register.\n");
		}

		per_cpu(cpu_energy_consumption, this_cpu) = per_cpu(final_cpu_rapl, this_cpu) * rapl_unit;
	}
}

void prepare_before_each_measurement(void)
{
	padding.measurement_ongoing = true;

	// prevents cpus in POLL from waking up immediately if they reach the
	// while condition before the leader has finished setting up the wake-up
	// interrupt
	end_tsc = U64_MAX;
}

void cleanup_after_each_measurement(void)
{
}

inline void commit_system_specific_results(unsigned number)
{
	pkg_stats.attributes.energy_consumption[number] = energy_consumption;
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
		else if (vendor == X86_VENDOR_AMD)
		{
			cpu_stats[i].attributes.energy_consumption[number] = per_cpu(cpu_energy_consumption, i);
		}
	}
}

DEFINE_SPINLOCK(pkg_cst_lock);
volatile bool first_at_pkg_cst = true;
static u64 max_pkg_cst_backup;

static void per_cpu_init(void *info)
{
	int err;

	get_cpu();

	if (vendor == X86_VENDOR_INTEL)
	{
		u64 pkg_cst_config_control;
		u64 ia32_fixed_ctr_ctrl;
		u64 ia32_perf_global_ctrl;

		err = 0;

		// As the MSR to control Package C-states is somehow a per-core MSR, handling it is a bit of a mess
		// The current implementation only saves the inital max PC-state value from one core and then restores it to all
		// Within one package this should not be a problem, as having different values for this field makes not much sense
		// This might need to be revisited once multiple sockets should be supported
		spin_lock(&pkg_cst_lock);

		err = rdmsrl_safe(MSR_PKG_CST_CONFIG_CONTROL, &pkg_cst_config_control);
		if (first_at_pkg_cst)
		{
			max_pkg_cst_backup = pkg_cst_config_control & 0b111;
			first_at_pkg_cst = false;
		}

		spin_unlock(&pkg_cst_lock);

		if (deactivate_pcstates)
		{
			pkg_cst_config_control &= ~0b111; // set last 3 bits to 0
		}
		else
		{
			pkg_cst_config_control |= 0b111; // set last 3 bits to 1
		}
		err |= wrmsrl_safe(MSR_PKG_CST_CONFIG_CONTROL, pkg_cst_config_control);

		if (err)
		{
			printk(KERN_WARNING "WARNING: Could not change Package C-state settings.\n");
		}

		err = 0;

		err = rdmsrl_safe(IA32_FIXED_CTR_CTRL, &ia32_fixed_ctr_ctrl);
		ia32_fixed_ctr_ctrl |= 0b11 << 8;
		err |= wrmsrl_safe(IA32_FIXED_CTR_CTRL, ia32_fixed_ctr_ctrl);

		err |= rdmsrl_safe(IA32_PERF_GLOBAL_CTRL, &ia32_perf_global_ctrl);
		ia32_perf_global_ctrl |= 1l << 34;
		err |= wrmsrl_safe(IA32_PERF_GLOBAL_CTRL, ia32_perf_global_ctrl);

		if (err)
		{
			printk(KERN_WARNING "WARNING: Could not enable 'unhalted' register.\n");
		}
	}

	put_cpu();
}

static void per_cpu_cleanup(void *info)
{
	int err;

	get_cpu();

	if (vendor == X86_VENDOR_INTEL)
	{
		u64 pkg_cst_config_control;

		err = 0;

		err = rdmsrl_safe(MSR_PKG_CST_CONFIG_CONTROL, &pkg_cst_config_control);
		pkg_cst_config_control &= ~0b111;
		pkg_cst_config_control |= max_pkg_cst_backup;
		err |= wrmsrl_safe(MSR_PKG_CST_CONFIG_CONTROL, pkg_cst_config_control);

		if (err)
		{
			printk(KERN_WARNING "WARNING: Could not restore Package C-state settings.\n");
		}
	}

	put_cpu();
}

static inline u32 get_cstate_hint(void)
{
	if (target_cstate == 0)
	{
		return 0xf;
	}

	if (target_cstate > 15)
	{
		printk(KERN_WARNING "WARNING: target_cstate of %i is invalid, using C1!", target_cstate);
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
		printk(KERN_WARNING "WARNING: Mwait not supported.\n");
	}
	set_cpu_info(a);
	printk(KERN_INFO "CPU FAMILY: 0x%x, CPU Model: 0x%x\n", cpu_family, cpu_model);

	a = 0x5;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (!(c & 1))
	{
		printk(KERN_WARNING "WARNING: Mwait Power Management not supported.\n");
	}
	if (!(c & (1 << 1)))
	{
		printk(KERN_WARNING "WARNING: Masked interrupts as break events not supported.\n");
	}

	a = 0x80000007;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (!(d & (1 << 8)))
	{
		printk(KERN_WARNING "WARNING: TSC not invariant, sleepstate statistics potentially meaningless.\n");
	}

	a = 0x0;
	asm("cpuid;"
	    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	    : "0"(a));
	if (b == 0x756E6547 && d == 0x49656E69 && c == 0x6C65746E) /* GenuineIntel in ASCII */
	{
		vendor = X86_VENDOR_INTEL;
		printk(KERN_INFO "INTEL CPU\n");
	}
	else if (b == 0x68747541 && d == 0x69746e65 && c == 0x444d4163) /* AuthenticAMD in ASCII */
	{
		vendor = X86_VENDOR_AMD;
		printk(KERN_INFO "AMD CPU\n");
	}
	else
	{
		vendor = X86_VENDOR_UNKNOWN;
		printk(KERN_INFO "VENDOR UNKNOWN\n");
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

#define APIC_LVT_ENTRY_COUNT (7)

u32 apic_lvt_entries[] = {
    APIC_LVTT,
    APIC_LVTTHMR,
    APIC_LVTPC,
    APIC_LVT0,
    APIC_LVT1,
    APIC_LVTERR,
    APIC_LVTCMCI};
DEFINE_PER_CPU(u32[APIC_LVT_ENTRY_COUNT], apic_lvt_backups);

void disable_percpu_interrupts(int this_cpu)
{
	u32 value;

	for (int i = 0; i < APIC_LVT_ENTRY_COUNT; ++i)
	{
		value = apic_read(apic_lvt_entries[i]);
		per_cpu(apic_lvt_backups[i], this_cpu) = value;
		value |= APIC_LVT_MASKED;
		apic_write(apic_lvt_entries[i], value);
	}
}

void enable_percpu_interrupts(int this_cpu)
{
	u32 value;

	for (int i = 0; i < APIC_LVT_ENTRY_COUNT; ++i)
	{
		apic_write(apic_lvt_entries[i], per_cpu(apic_lvt_backups[i], this_cpu));
	}

	// Restart the timer appropriately
	// Necessary so the system continues working properly
	value = apic_read(APIC_LVTT);
	if ((value & APIC_LVT_TIMER_MODE_MASK) == APIC_LVT_TIMER_MODE_ONESHOT)
	{
		apic_write(APIC_TMICT, 1);
	}
	else if ((value & APIC_LVT_TIMER_MODE_MASK) == APIC_LVT_TIMER_MODE_PERIODIC)
	{
		apic_write(APIC_TMICT, apic_read(APIC_TMICT));
	}
	else if ((value & APIC_LVT_TIMER_MODE_MASK) == APIC_LVT_TIMER_MODE_TSC_DEADLINE)
	{
		if (wrmsrl_safe(MSR_IA32_TSC_DEADLINE, rdtsc()))
			printk(KERN_WARNING "Error when trying to restart Local APIC Timer on CPU %i!\n", this_cpu);
	}
}

inline enum entry_mechanism get_signal_low_mechanism(void)
{
	return ENTRY_MECHANISM_MWAIT;
}

int prepare(void)
{
	for (int i = 0; i <= 255; ++i)
	{
		irq_set_status_flags(i, IRQ_DISABLE_UNLAZY);
		disable_irq(i);
	}

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
	on_each_cpu(per_cpu_cleanup, NULL, 1);
}

void cleanup(void)
{
	for (int i = 0; i <= 255; ++i)
	{
		enable_irq(i);
		irq_clear_status_flags(i, IRQ_DISABLE_UNLAZY);
	}
}
