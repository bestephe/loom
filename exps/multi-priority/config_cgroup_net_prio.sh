#!/bin/bash

#Note: This script was developed from the following guide:
# http://open-lldp.org/dcb_overview

cd /sys/fs/cgroup/net_prio
mkdir high_prio
cd high_prio
echo "eno2 3" > net_prio.ifpriomap

echo "Default:"
cat /sys/fs/cgroup/net_prio/net_prio.ifpriomap
echo
echo "High Prio:"
cat /sys/fs/cgroup/net_prio/high_prio/net_prio.ifpriomap
