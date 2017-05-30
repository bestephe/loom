#!/bin/bash

BESS_DIR=/scratch/bes/git/loom-code/code/bess/
IFACE_ADDR="0000:08:00.0"

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
sudo modprobe uio_pci_generic
sudo ./bin/dpdk-devbind.py -b uio_pci_generic $IFACE_ADDR

# Configure huge pages
sudo sysctl vm.nr_hugepages=1024
