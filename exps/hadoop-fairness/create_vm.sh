#!/bin/bash

sudo apt-get install -y kvm qemu-system qemu-utils
cd /mnt
#wget http://releases.ubuntu.com/16.04.2/ubuntu-16.04.2-server-amd64.iso
qemu-img create -f raw loom_vm_base.img 96G
sudo kvm -m 64G -drive file=loom_vm_base.img,index=0,media=disk,format=raw -net nic -net user -boot d -cdrom ubuntu-16.04.2-server-amd64.iso

# Use the following command to start the VM in a default (non-routed networking
# mode)
#sudo kvm -s --smp cpus=28 -m 64G -drive file=/mnt/loom_vm_base.img,index=0,media=disk,format=raw -net user,hostfwd=tcp::5555-:22 -net nic
