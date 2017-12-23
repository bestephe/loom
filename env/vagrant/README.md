## Notes about setting up a vagrant VM that can boot a custom kernel image

### Setup vagrant
```
vagrant plugin install vagrant-libvirt
vagrant plugin install vagrant-mutate
```

### Download and setup a vagrant box


## Notes about setting up a vagrant VM that uses QEMU and virtio-user

### Various setup commands

Patch ansible to work around a plugin install bug
```
sudo patch --directory /usr/lib/ruby/vendor_ruby/vagrant < vagrant-plugin.patch
```

Install virtualbox (optional?)
- Added the following line to sources.list (/etc/apt/sources.list.d/virtualbox-ubuntu-virtualbox-xenial.list)
```
deb http://download.virtualbox.org/virtualbox/debian xenial contrib
```
- Add the key
```
wget -q https://www.virtualbox.org/download/oracle_vbox.asc -O- | sudo apt-key add -
wget -q https://www.virtualbox.org/download/oracle_vbox_2016.asc -O- | sudo apt-key add -
```

Commands for vagrant
```
sudo apt-get install vagrant
sudo vagrant plugin install vagrant-libvirt
sudo vagrant plugin install vagrant-mutate
# Skip for now because nosec
# sudo vagrant plugin install vagrant-rekey-ssh
```

Notes on configuring MQ vhost
- QEMU args:
```
-netdev type=vhost-user,id=mynet1,chardev=char0,vhostforce,queues=<queues_nr>
-device virtio-net-pci,netdev=mynet1,mac=52:54:00:02:d9:0<n>,mq=on,vectors=<2 + 2 * queues_nr>
```
    - From BESS:
    ```
qemu-system-x86_64 <other args...>
    -chardev socket,id=mychr,path=/tmp/my_vhost.sock \
    -netdev vhost-user,id=mydev,chardev=mychr,vhostforce,queues=1 \
    -device virtio-net-pci,netdev=mydev
    ```
- In VM:
```
ethtool -L eth0 combined <queues_nr>
```

### Links:

http://www.lucainvernizzi.net/blog/2014/12/03/vagrant-and-libvirt-kvm-qemu-setting-up-boxes-the-easy-way/

https://wiki.qemu.org/Documentation/vhost-user-ovs-dpdk

http://blog.vmsplice.net/2011/09/qemu-internals-vhost-architecture.html

It may be possible to force another backend tap?
https://libvirt.org/formatdomain.html#elementsNICSModel

https://www.linux-kvm.org/page/Multiqueue#Multiqueue_virtio-net
