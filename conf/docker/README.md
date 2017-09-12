## Miscellaneous testing and configuration guide for the Loom experiments that use docker containers

### Step 0: Install packages and libraries

Just use the BESS env scripts to setup the environment:
```
cd ${BESS_DIR} # code/bess
ansible-playbook -K -t package -i localhost, -c local env/bess.yml
#TODO: This does not install docker (env/docker.yml)
```

### Step 1: Build and run two docker images

Build a docker image for testing by hand with iperf3

```
sudo docker build -t loom/testimage -f Dockerfile.loomtest .
```

Then start two images to act as a client and server.
```
sudo ./start_containers_loom.sh
```
Note: this isn't necessary anymore?

### Step 2: Create and connect virtio-user interfaces

Start testpmd to create two virtio-user interfaces and bridge them

```
sudo ./build/app/testpmd -l 2-3 -n 4         --vdev=virtio_user0,path=/dev/vhost-net,queues=2,queue_size=1024 --vdev=virtio_user1,path=/dev/vhost-net,queues=2,queue_size=1024        -- --txqflags=0x0 --disable-hw-vlan --enable-lro         --enable-rx-cksum --txq=2 --rxq=2 --rxd=1024         --txd=1024
```

### Step 3: Attach the tap interfaces to the namespaces of the containers

```
sudo ./assign_interfaces.sh
```
