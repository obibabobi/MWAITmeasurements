#!/bin/bash

while getopts "s" option; do
    case $option in
    (s) SIGNAL_REQUESTED=true;;
    esac
done

# preparation
pushd "$(dirname "$0")"

RESULTS_DIR=results
if [[ ! -e $RESULTS_DIR ]]; then
    mkdir $RESULTS_DIR
else
    rm -r $RESULTS_DIR
    mkdir $RESULTS_DIR
fi

FREQ_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

if [[ -e /proc/sys/kernel/nmi_watchdog ]]; then
    NMI_WATCHDOG=$(cat /proc/sys/kernel/nmi_watchdog)
    echo 0 > /proc/sys/kernel/nmi_watchdog
fi

MEASURE_DURATION=$1

function measure {
    insmod mwait.ko $2 duration=$MEASURE_DURATION
    cp -r /sys/mwait_measurements $RESULTS_DIR/$3/$1
    rmmod mwait
}

# synchronization signal
if [ "$SIGNAL_REQUESTED" = true ]; then
    insmod mwait.ko mode=signal duration=$MEASURE_DURATION
    cp -r /sys/mwait_measurements/signal_times $RESULTS_DIR/
    rmmod mwait
fi

# measurements
if [[ -e /sys/devices/system/cpu/cpu0/cpuidle ]]; then
    MEASUREMENT_NAME=states
    mkdir $RESULTS_DIR/$MEASUREMENT_NAME
    for STATE in /sys/devices/system/cpu/cpu0/cpuidle/state*;
    do
        NAME=$(< "$STATE"/name);
        if [[ "$NAME" == 'POLL' ]]; then
            measure $NAME "entry_mechanism=POLL" $MEASUREMENT_NAME
            continue;
        fi
        DESC=$(< "$STATE"/desc);
        if [[ "${DESC%% *}" == 'ACPI' ]]; then
            DESC=${DESC#ACPI };
            if [[ "${DESC%% *}" == 'IOPORT' ]]; then
                IO_PORT=${DESC#IOPORT };
                measure $NAME "entry_mechanism=IOPORT io_port=$IO_PORT" $MEASUREMENT_NAME
            elif [[ "${DESC%% *}" == 'FFH' ]]; then
                DESC=${DESC#FFH };
                if [[ "${DESC%% *}" == 'MWAIT' ]]; then
                    MWAIT_HINT=${DESC#MWAIT };
                    measure $NAME "mwait_hint=$MWAIT_HINT" $MEASUREMENT_NAME
                fi
            fi
        elif [[ "${DESC%% *}" == 'MWAIT' ]]; then   # the Intel cpuidle driver does not prefix the description
            MWAIT_HINT=${DESC#MWAIT };
            measure $NAME "mwait_hint=$MWAIT_HINT" $MEASUREMENT_NAME
        fi
    done
fi

MEASUREMENT_NAME=cpus_sleep
mkdir $RESULTS_DIR/$MEASUREMENT_NAME
for ((i=0; i<=$(getconf _NPROCESSORS_ONLN); i++));
do
    measure $i "cpus_sleep=$i" $MEASUREMENT_NAME
done

# cleanup
if [[ -e /proc/sys/kernel/nmi_watchdog ]]; then
    echo $NMI_WATCHDOG > /proc/sys/kernel/nmi_watchdog
fi
echo $FREQ_GOVERNOR | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
popd