#!/bin/bash

help() {
    echo "######################"
    echo "### Measure script ###"
    echo "######################"
    echo
    echo "Syntax: measure.sh [-e|h] <ip> <duration>"
    echo "Description:"
    echo "    <ip>: The IP address of the measurebox"
    echo "    <duration>: Duration of a single measurement in milliseconds."
    echo "                Should depend mainly on temporal resolution of power measurement method."
    echo
    echo "    -e: Run external power logging simultaneous to measurement"
    echo "    -h: Print help, then quit"
}

while getopts "eh" option; do
    case $option in
    (e) EXTERNAL_MEASUREMENT=true;;
    (h) help; exit;;
    esac
done

MEASUREBOX_OPTIONS=""

# preparation
pushd "$(dirname "$0")"

# for external measurements: start power logging
if [ "$EXTERNAL_MEASUREMENT" = true ]; then
    MEASUREBOX_OPTIONS="$MEASUREBOX_OPTIONS -s"

    su -c 'scripts/logPowerData.py & echo $!'
    LOGGER_PID=$?
fi

# start the measurement script on the measurebox
echo "mwait_deploy/measure.sh $MEASUREBOX_OPTIONS $2" | ssh root@$1 'bash -s'

# get rid of old results
rm -rf output/*

# copy the measurement results back to the controllbox
rsync -r root@$1:/root/mwait_deploy/results/ output/results/

# for external measurements: stop power logging
if [ "$EXTERNAL_MEASUREMENT" = true ]; then
    su -c "kill -SIGINT $LOGGER_PID"
    until [ -f "output/power_log.csv" ]
    do
        sleep 0.1
    done
fi

# post process
scripts/postProcess.py
scripts/plotMeasurements.py

popd
