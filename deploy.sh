#!/bin/bash

# preparation
pushd "$(dirname "$0")"

echo "rm -rf /root/mwait_deploy/*" | ssh root@$1 'bash -s' &&
rsync -r mwait_deploy root@$1:/root/ &&
echo "cd mwait_deploy && make" | ssh root@$1 'bash -s'

popd
