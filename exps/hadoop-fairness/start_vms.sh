#!/bin/bash

sudo mount /dev/sda4 /mnt

#sudo kvm -s --smp cpus=28 -m 64G -drive file=/mnt/loom_vm_base.img,index=0,media=disk,format=raw -net user,hostfwd=tcp::5555-:22 -net nic
#sudo kvm -s --smp cpus=28 -m 64G -drive file=/mnt/loom_vm_base.img,index=0,media=disk,format=raw -net user,net=10.10.1.100/24 -net nic
#sudo kvm -s --smp cpus=28 -m 64G -drive file=/mnt/loom_vm_base.img,index=0,media=disk,format=raw -netdev bridge,id=hn0 -device virtio-net-pci,netdev=hn0,id=nic1

sudo kvm -s --smp cpus=28 -m 96G -drive file=/mnt/loom_vm_base.img,index=0,media=disk,format=raw -net nic -net bridge,br=br0
