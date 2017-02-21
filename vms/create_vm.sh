#!/bin/bash

qemu-img create -f raw loom_vm.img 8G
sudo kvm -m 8G  -hda loom_vm.img -net nic -net user -boot d -cdrom /scratch/bes/ubuntu-16.04.2-server-amd64.iso
