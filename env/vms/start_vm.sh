#!/bin/bash

#vmlinuz=/boot/vmlinuz-4.9.10-loom
vmlinuz=../../code/linux-4.9/arch/x86_64/boot/bzImage
initrd=/boot/initrd.img-4.9.10-loom
num_cpus=6

# Allow for unsafe interrupt assignment
echo 1 | sudo tee /sys/module/kvm/parameters/allow_unsafe_assigned_interrupts 

# Build FS mounts
loom="/scratch/bes/git/loom-code/"
loom_mnt="-fsdev local,security_model=passthrough,id=fsdev4,path=$loom -device virtio-9p-pci,id=fs4,fsdev=fsdev4,mount_tag=loom"
libmod="/lib/modules/4.9.10-loom/"
libmod_mnt="-fsdev local,security_model=passthrough,id=fsdev0,path=$libmod -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=libmod"

log_buf_len=1048576

sudo kvm -s -cpu host --smp cpus=$num_cpus -m 16G -hda ubuntu-16.04.raw -net nic -net user -redir tcp:5555::22 -kernel $vmlinuz -initrd $initrd -append "root=/dev/sda1 console=ttyS0 log_buf_len=$log_buf_len" -nographic $loom_mnt $libmod_mnt
