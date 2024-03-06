#ifndef MEASURE_GENERIC_H
#define MEASURE_GENERIC_H

#include <linux/types.h>
#include <linux/percpu.h>

enum mode
{
	MODE_UNKNOWN,
	MODE_MEASURE,
	MODE_SIGNAL
};
extern enum mode operation_mode;

extern int duration;
extern enum entry_mechanism requested_entry_mechanism;
DECLARE_PER_CPU(enum entry_mechanism, cpu_entry_mechanism);

extern bool redo_measurement;
extern unsigned cpus_present;

DECLARE_PER_CPU(u64, wakeups);
DECLARE_PER_CPU(u64, wakeup_time);

bool is_leader(int cpu);
void leader_callback(void);
void all_cpus_callback(int this_cpu);

int prepare(void);
void cleanup(void);
int prepare_measurements(void);
void cleanup_measurements(void);
void preliminary_checks(void);
void cleanup_after_each_measurement(void);
void prepare_before_each_measurement(void);
void wakeup_other_cpus(void);
void commit_system_specific_results(unsigned number);
void set_global_final_values(void);
void set_cpu_final_values(int this_cpu);
void set_global_start_values(void);
void set_cpu_start_values(int this_cpu);
void setup_leader_wakeup(int this_cpu);
void setup_wakeup(int this_cpu);
void do_system_specific_sleep(int this_cpu);
void evaluate_global(void);
void evaluate_cpu(int this_cpu);
void disable_percpu_interrupts(void);
void enable_percpu_interrupts(void);
enum entry_mechanism get_signal_low_mechanism(void);

#endif