## This file is a place I'm using to keep track of bugs and issues I've encountered in BESS

### bessd crashes from incorrect configuration

- Incorrectly configuring gates on IPLookup causes a bessd crash.  For example, this snippet:
```
ipfwd = IPLookup()
ipfwd.add(prefix='10.10.101.1', prefix_len=32, gate=1)
```
    - This is caused by lpm\_ not being initialized properly

- Incorrectly setting traffic classes caused bessd to crash.  For example,
  trying to attach multiple tasks to a rate limit:
```
bess.add_tc('bit_limit',
            policy='rate_limit',
            resource='bit',
            limit={'bit': int(2.2e9)})
v_inc::PortInc(port=v.name)
for i in range(queue_count):
    v_inc.attach_task(parent='bit_limit_rr', module_taskid=i)
```

### VPort

- For some reason switching between two VPorts is flakey to where only one VPort works
    - This only happens when more than 8 queues are used?

### Scheduler problem

- weighted\_fair doesn't work with 'bits' as a resource
    - This is because the scheduler is not work conserving.  It stays
      perpetually blocked when a leaf does not consume any resources.


### Packet loss:

- The kmod interfaces with the kernel incorrectly and unnecessarily drops packets

- The TX PMD should exert backpressure

- Vhost Tap driver drops packets on buffer overflow

### Lack of TX Backpressure and BQL

- When TX queues reach a limit, they should block.
    - This could potentially cause problems if blocking goes all the way up to
      leaf nodes that *could* send to a port but could also switch packets to
      other ports.
    - If blocking does not pause all upstream leaf nodes, then what happens if
      a packet that needs to be switched to a block port.  Catch-22
        - The real solution to this is packet classification
            - Using an ideal input-queued switch would solve this problem

- The current scheduler lets 100us--1ms of packets build up.

- Priorities don't mean much when you don't limit HOL blocking

### Software switching

For something name a "software switch", BESS is surprisingly lacking in the
ability to behave as a L2 or L2/L3 switch appropriately

- ARPs are a problem

- It seems like switching on VLANs is the best possible solution
    - Unfortunately, this solution does not easily play well with CloudLab and
      switches that expect non-tagged packets and switches that do not support
      VLAN-in-VLAN tagging.

- One simple solution is that L2Forward should have a broadcast gate
    - More generally, L2Forward should have support for multicast

- Creating a VPort or VHost should have an option for conifguring a MAC address

