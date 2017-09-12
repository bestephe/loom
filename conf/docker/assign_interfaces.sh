#!/bin/bash

C1="loom_test1"
C2="loom_test2"
CONTAINERS="$C1 $C2"
for c in loom_test1 loom_test2
do
  docker rm -f $c # This should probably go away.
  docker run --privileged -i -d -t --name=$c loom/testimage /bin/bash
  unlink /var/run/netns/$c
done

mkdir -p /var/run/netns

c1_pid="$(docker inspect --format '{{.State.Pid}}' $C1)"
c2_pid="$(docker inspect --format '{{.State.Pid}}' $C2)"
ln -s /proc/$c1_pid/ns/net /var/run/netns/$C1
ln -s /proc/$c2_pid/ns/net /var/run/netns/$C2

# Assign the tap interfaces to the contain namespaces
ip link set tap0 netns $C1
ip link set tap1 netns $C2

# Assign IPs
ip netns exec $C1 ifconfig tap0 10.10.1.1/24
ip netns exec $C2 ifconfig tap1 10.10.1.2/24
