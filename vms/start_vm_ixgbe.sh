#!/bin/bash

iface=p2p1
vmlinuz=../code/linux-4.9/arch/x86_64/boot/bzImage 

num_cpus=8
#num_cpus=4
#num_cpus=1

if [ "$#" -eq 1 ]; then
    vmlinuz=$1
fi

# Get the right version of ixgbe installed and configured for SR-IOV
sudo rmmod ixgbe

# Get the pci interface of the VF
pci_addr=$(lspci | grep 82599 | head -n 1 | awk '{print $1}')
echo $pci_addr

# Allow for unsafe interrupt assignment
echo 1 | sudo tee /sys/module/kvm/parameters/allow_unsafe_assigned_interrupts 

# Shared directories
loom_code="/scratch/bes/git/loom-code/"

# Build FS mounts
loom_mnt="-fsdev local,security_model=passthrough,id=fsdev2,path=$loom_code -device virtio-9p-pci,id=fs2,fsdev=fsdev2,mount_tag=loom_code"

log_buf_len=1048576

sudo kvm -s --smp cpus=$num_cpus -m 16G -hda loom_vm.img -net nic -net user -redir tcp:5555::22 -kernel $vmlinuz -initrd /boot/initrd.img-4.9.10 -append "root=/dev/sda1 console=ttyS0 log_buf_len=$log_buf_len" -nographic -device pci-assign,host=$pci_addr $loom_mnt
