#!/bin/bash

BESS_DIR=`realpath ../code/bess/`
IFACE="enp130s0"
IFACE_ADDR="82:00.0"

# Install the BESS kernel module
echo "BESS"
cd $BESS_DIR
cd core/kmod
sudo ./install

# Configure DPDK
echo
echo "DPDK"
cd $BESS_DIR
pwd
sudo ifconfig $IFACE 0.0.0.0
sudo ifconfig $IFACE 0.0.0.0
sleep 0.2
sudo modprobe uio_pci_generic
#sudo ./bin/dpdk-devbind.py -b uio_pci_generic $IFACE_ADDR

# Configure huge pages
#sudo sysctl vm.nr_hugepages=1024
echo 8192 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 8192 | sudo tee /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

# For Crail
sudo sysctl vm.max_map_count=1048576
#ulimit -n 1048576
