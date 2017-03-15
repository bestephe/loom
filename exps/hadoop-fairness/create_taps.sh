#!/bin/bash

IFACE=eno2
IP=$(/sbin/ifconfig $IFACE | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}')
#echo $IP

for i in {1..2}
do
    TAP_NAME=${IFACE}t$i
    TAP_IP_PREFIX=$(echo $IP | cut -d. -f1,2,3)
    TAP_IP_SUFFIX=${i}0$(echo $IP | cut -d. -f4)
    TAP_IP=$TAP_IP_PREFIX.$TAP_IP_SUFFIX
    #echo $TAP_NAME $TAP_IP

    sudo ip tuntap add dev $TAP_NAME mode tap
    sudo ifconfig $TAP_NAME $TAP_IP/24
    sudo ifconfig $TAP_NAME down
    sudo ifconfig $TAP_NAME up
done
