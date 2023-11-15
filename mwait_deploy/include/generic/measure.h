#ifndef MEASURE_GENERIC_H
#define MEASURE_GENERIC_H

#include <linux/types.h>

extern int measurement_duration;

extern bool redo_measurement;
extern unsigned cpus_present;

extern u64 energy_consumption;
extern u64 wakeup_time;

int prepare_measurement(void);
void preliminary_checks(void);
void cleanup_after_each_measurement(void);
void prepare_before_each_measurement(void);
void cleanup_after_measurements_done(void);
void leader_callback(void);
void all_cpus_callback(int this_cpu);
void wakeup_other_cpus(void);
void commit_system_specific_results(unsigned number);
void set_global_final_values(void);
void set_cpu_final_values(int this_cpu);
void set_global_start_values(void);
void set_cpu_start_values(int this_cpu);
void setup_wakeup(void);
void do_system_specific_sleep(int this_cpu);
void evaluate_global(void);
void evaluate_cpu(int this_cpu);
void publish_results_to_sysfs(void);
void cleanup_sysfs(void);

#endif