#!/bin/bash

IP=$(/sbin/ifconfig eno2 | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}')
NETMASK=$(/sbin/ifconfig eno2 | grep 'inet addr:' | cut -d: -f4 | awk '{ print $1}')
#echo $IP
#echo $NETMASK
#
## Copy and setup the config for the bridge
#sudo cp bridge_interfaces /etc/network/interfaces
#sudo sed -i s/PRIV_IP/$IP/g /etc/network/interfaces
#sudo sed -i s/PRIV_NETMASK/$NETMASK/g /etc/network/interfaces
#
## Bring up the interface
#sudo ifconfig br0 up

## The "bridge-utils" approach.  Depricated?
#sudo brctl addbr br0
#sudo brctl addif br0 eno2
#sudo ip link set up dev br0
#sudo mkdir /etc/qemu
#echo "allow br0" | sudo tee /etc/qemu/bridge.conf

sudo iptables -I FORWARD -m physdev --physdev-is-bridged -j ACCEPT
sudo ip link add name br0 type bridge
sudo ip addr flush dev eno2
sudo ifconfig br0 $IP netmask $NETMASK
sudo ip link set br0 up
sudo ip link set eno2 up
sudo ip link set eno2 master br0
