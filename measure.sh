#!/bin/bash

help() {
    echo "######################"
    echo "### Measure script ###"
    echo "######################"
    echo
    echo "Syntax: measure.sh <ip> <duration> [-e|h]"
    echo "Description:"
    echo "    <ip>: The IP address of the measurebox"
    echo "    <duration>: Duration of a single measurement in milliseconds."
    echo "                Should depend mainly on temporal resolution of power measurement method."
    echo
    echo "    -e: Run external power logging simultaneous to measurement"
    echo "    -h: Print help, then quit"
}

while getopts ":eh" option; do
    case $option in
        e)
            EXTERNAL_MEASUREMENT=true;;
        h)
            help
            exit;;
   esac
done


# preparation
pushd "$(dirname "$0")"

# for external measurements: start power logging
if $EXTERNAL_MEASUREMENT
then
    su -c 'scripts/logPowerData.py & echo $!'
    LOGGER_PID=$?
fi

# get rid of old results
rm -rf output/*

# start the measurement script on the measurebox
echo "mwait_deploy/measure.sh $2" | ssh root@$1 'bash -s'

# copy the measurement results back to the controllbox
rsync -r root@$1:/root/mwait_deploy/results/ output/results/

# for external measurements: stop power logging
if $EXTERNAL_MEASUREMENT
then
    su -c "kill -SIGINT $LOGGER_PID"
fi

popd
