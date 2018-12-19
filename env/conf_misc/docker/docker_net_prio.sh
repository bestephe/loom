#!/bin/bash

#Note: This script was developed from the following guide:
# http://open-lldp.org/dcb_overview

cd /sys/fs/cgroup/net_prio
mkdir low_prio
cd low_prio
echo "loom1 1" > net_prio.ifpriomap

echo "Default:"
cat /sys/fs/cgroup/net_prio/net_prio.ifpriomap
echo
echo "Low Prio:"
cat /sys/fs/cgroup/net_prio/low_prio/net_prio.ifpriomap

