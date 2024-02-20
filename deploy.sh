#!/bin/bash

help() {
    echo "######################"
    echo "### Deploy script ###"
    echo "######################"
    echo
    echo "Syntax: deploy.sh <ip> [-h]"
    echo "Description:"
    echo "    <ip>: The IP address of the measurebox"
    echo
    echo "    -h: Print help, then quit"
}

while getopts ":h" option; do
    case $option in
        h)
            help
            exit;;
   esac
done

# preparation
pushd "$(dirname "$0")"

# remove any files from previous deployments
echo "rm -rf /root/mwait_deploy/*" | ssh root@$1 'bash -s'

# copy the necessary sources
rsync -r mwait_deploy root@$1:/root/

# compile
echo "cd mwait_deploy && make" | ssh root@$1 'bash -s'

popd
