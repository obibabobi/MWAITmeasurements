#!/bin/bash

# preparation
pushd "$(dirname "$0")"

(echo "mwait_deploy/measure.sh $2" | ssh root@$1 'bash -s') &&
rm -rf output/* &&
rsync -r root@$1:/root/mwait_deploy/results/ output/results/

popd
